#pragma once

#include "application/AuthService.h"
#include "application/TicketService.h"

#include <crow.h>
#include <memory>

namespace TicketHub::Web {

void registerApiRoutes(crow::SimpleApp& app,
                       const std::shared_ptr<Application::TicketService>& service,
                       const std::shared_ptr<Application::AuthService>& authService);

} // namespace TicketHub::Web
