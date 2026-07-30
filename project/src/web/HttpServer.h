#pragma once

#include "application/TicketService.h"
#include "config/Config.h"

#include <memory>

namespace TicketHub::Web {

void runHttpServer(const Config::AppConfig& config,
                   const std::shared_ptr<Application::TicketService>& service);

} // namespace TicketHub::Web
