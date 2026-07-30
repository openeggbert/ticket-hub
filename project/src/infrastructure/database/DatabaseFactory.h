#pragma once

#include "config/Config.h"
#include "infrastructure/database/IDatabase.h"

#include <memory>

namespace TicketHub::Infrastructure::Database {

std::shared_ptr<IDatabase> createDatabase(const Config::AppConfig& config);

} // namespace TicketHub::Infrastructure::Database
