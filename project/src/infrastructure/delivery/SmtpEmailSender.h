#pragma once

#include <cstdint>
#include <string>

namespace TicketHub::Infrastructure::Delivery {

// Outbound email delivery (D52, deferred-after-V1): SMTP only ("prioritize
// SMTP/sendmail first" -- no sendmail/provider-API backend in this batch).
// Only ever called from ticket-hub-cli's `process-outbox` command; see
// WebhookDeliveryClient.h for why this lives outside ticket-hub-core.
// Deliberately a small, self-contained struct rather than passing the full
// Config::AppConfig through -- this module should not need to know about
// unrelated settings (database, attachments, ...).
struct SmtpConfig {
    std::string host;
    std::uint16_t port = 587;
    std::string username;
    std::string password;
    std::string fromAddress;
    bool useTls = true;
};

struct EmailSendResult {
    bool success = false;
    std::string error;
};

// Sends a single plain-text email. Never throws; a connection failure,
// authentication failure, or rejected recipient is reported as a failure
// with a short diagnostic string, same convention as
// WebhookDeliveryClient::sendWebhookDelivery.
EmailSendResult sendEmail(const SmtpConfig& config, const std::string& toEmail, const std::string& subject,
                          const std::string& body);

} // namespace TicketHub::Infrastructure::Delivery
