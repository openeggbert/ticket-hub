#pragma once

#include "application/TicketService.h"

#include <crow.h>
#include <memory>

namespace TicketHub::Web {

void registerApiRoutes(crow::SimpleApp& app,
                       const std::shared_ptr<Application::TicketService>& service);

} // namespace TicketHub::Web
