#pragma once

#include "application/AuthService.h"
#include "application/TicketService.h"
#include "config/Config.h"

#include <memory>

namespace TicketHub::Web {

void runHttpServer(const Config::AppConfig& config,
                   const std::shared_ptr<Application::TicketService>& service,
                   const std::shared_ptr<Application::AuthService>& authService);

} // namespace TicketHub::Web
