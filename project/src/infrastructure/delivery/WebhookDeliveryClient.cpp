#include "infrastructure/delivery/WebhookDeliveryClient.h"

#include "common/Hmac.h"

#include <curl/curl.h>

namespace TicketHub::Infrastructure::Delivery {
namespace {
// libcurl insists on a write callback even when the response body itself
// is of no interest here -- this just discards it.
std::size_t discardResponseBody(char*, std::size_t size, std::size_t nmemb, void*) {
    return size * nmemb;
}
} // namespace

WebhookDeliveryResult sendWebhookDelivery(const std::string& targetUrl, const std::string& secret,
                                          const std::string& payload) {
    WebhookDeliveryResult result;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error = "Failed to initialize HTTP client";
        return result;
    }

    const std::string signatureHeader = "X-TicketHub-Signature: sha256=" + Common::hmacSha256Hex(secret, payload);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, signatureHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, targetUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // A slow or hanging admin-configured target must never block
    // process-outbox indefinitely -- this is a fixed, short ceiling, not
    // configurable in this batch.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardResponseBody);
    // Never follow a redirect automatically -- a malicious or compromised
    // target could otherwise redirect the signed payload to an arbitrary
    // third host that never agreed to receive it.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        result.error = curl_easy_strerror(code);
    } else {
        long statusCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
        if (statusCode >= 200 && statusCode < 300) {
            result.success = true;
        } else {
            result.error = "HTTP " + std::to_string(statusCode);
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

} // namespace TicketHub::Infrastructure::Delivery
