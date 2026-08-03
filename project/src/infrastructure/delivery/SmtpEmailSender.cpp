#include "infrastructure/delivery/SmtpEmailSender.h"

#include <algorithm>
#include <cstring>
#include <curl/curl.h>
#include <sstream>

namespace TicketHub::Infrastructure::Delivery {
namespace {

// A ticket summary (the source of an email Subject line, see
// TicketService::maybeEnqueueEmail) is ordinary user input, not something
// this layer can trust to be free of CR/LF -- without stripping them, a
// summary containing "\r\nBcc: attacker@example.com" would inject an
// additional header into the outgoing message. Recipient addresses come
// from this installation's own `users` table (validated at account
// creation), not per-request user input, but are sanitized here too as
// cheap defense in depth.
std::string stripCrLf(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char c : value) {
        if (c != '\r' && c != '\n') {
            result += c;
        }
    }
    return result;
}

struct UploadState {
    const std::string* message;
    std::size_t offset;
};

std::size_t uploadRead(char* buffer, const std::size_t size, const std::size_t nmemb, void* userdata) {
    auto* state = static_cast<UploadState*>(userdata);
    const std::size_t room = size * nmemb;
    const std::size_t remaining = state->message->size() - state->offset;
    const std::size_t toCopy = std::min(room, remaining);
    if (toCopy > 0) {
        std::memcpy(buffer, state->message->data() + state->offset, toCopy);
        state->offset += toCopy;
    }
    return toCopy;
}

} // namespace

EmailSendResult sendEmail(const SmtpConfig& config, const std::string& toEmail, const std::string& subject,
                          const std::string& body) {
    EmailSendResult result;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error = "Failed to initialize SMTP client";
        return result;
    }

    const std::string sanitizedTo = stripCrLf(toEmail);
    const std::string sanitizedSubject = stripCrLf(subject);
    // The body is not itself header-injectable (it comes after the blank
    // line separating headers from content), so it is sent as-is.
    std::ostringstream message;
    message << "From: Ticket Hub <" << stripCrLf(config.fromAddress) << ">\r\n"
            << "To: <" << sanitizedTo << ">\r\n"
            << "Subject: " << sanitizedSubject << "\r\n"
            << "Content-Type: text/plain; charset=utf-8\r\n"
            << "\r\n"
            << body << "\r\n";
    const std::string messageText = message.str();
    UploadState state{&messageText, 0};

    const std::string url = "smtp://" + config.host + ":" + std::to_string(config.port);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (config.useTls) {
        // CURLUSESSL_ALL: require STARTTLS to succeed, never silently fall
        // back to a plaintext session (this may carry a password).
        curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
    }
    if (!config.username.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, config.username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, config.password.c_str());
    }
    const std::string mailFrom = "<" + stripCrLf(config.fromAddress) + ">";
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mailFrom.c_str());
    const std::string rcpt = "<" + sanitizedTo + ">";
    struct curl_slist* recipients = curl_slist_append(nullptr, rcpt.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, uploadRead);
    curl_easy_setopt(curl, CURLOPT_READDATA, &state);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        result.error = curl_easy_strerror(code);
    } else {
        result.success = true;
    }

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    return result;
}

} // namespace TicketHub::Infrastructure::Delivery
