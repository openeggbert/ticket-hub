#pragma once

#include <string>

namespace TicketHub::Infrastructure::Delivery {

// Outbound webhook delivery (D39/D41, deferred-after-V1). Only ever called
// from ticket-hub-cli's `process-outbox` command -- the server itself never
// makes an outbound network call, see migrations/*/019_outbox_delivery.sql.
// Not part of ticket-hub-core: this is the one piece of the outbox feature
// that actually needs libcurl, so it (and SmtpEmailSender) are compiled
// directly into the CLI target instead of pulling a new dependency into the
// server binary or the shared core library.
struct WebhookDeliveryResult {
    bool success = false;
    std::string error;
};

// POSTs `payload` (already-built JSON text) to `targetUrl`, signed with an
// `X-TicketHub-Signature: sha256=<hmac>` header (Common::hmacSha256Hex over
// `secret` + `payload`) so a receiver can verify the delivery actually came
// from this installation. A 2xx response is the only success outcome;
// anything else (non-2xx status, connection failure, timeout) is reported
// as a failure with a short diagnostic string, never throws.
WebhookDeliveryResult sendWebhookDelivery(const std::string& targetUrl, const std::string& secret,
                                          const std::string& payload);

} // namespace TicketHub::Infrastructure::Delivery
