cmake_minimum_required(VERSION 3.25)

project(ticket-hub VERSION 0.2.0 LANGUAGES CXX)

option(TICKETHUB_BUILD_SERVER "Build the Crow HTTP server" ON)
option(TICKETHUB_BUILD_CLI "Build the Ticket Hub administration CLI" ON)
option(TICKETHUB_WITH_POSTGRES "Build the PostgreSQL database backend" ON)
option(TICKETHUB_WITH_SQLITE "Build the SQLite database backend" ON)
option(TICKETHUB_BUILD_TESTS "Build Ticket Hub tests" ON)

if(NOT TICKETHUB_WITH_POSTGRES AND NOT TICKETHUB_WITH_SQLITE)
    message(FATAL_ERROR "At least one database backend must be enabled")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Argon2 REQUIRED)

set(TICKETHUB_CORE_SOURCES
    src/application/AuthService.cpp
    src/application/TicketService.cpp
    src/common/FileUtil.cpp
    src/common/Hmac.cpp
    src/common/PasswordHash.cpp
    src/common/RandomToken.cpp
    src/common/Sha256.cpp
    src/common/Uuid.cpp
    src/config/Config.cpp
    src/domain/Validation.cpp
    src/infrastructure/database/DatabaseFactory.cpp
    src/infrastructure/database/Migration.cpp
    src/infrastructure/storage/LocalAttachmentStorage.cpp
)

if(TICKETHUB_WITH_SQLITE)
    find_package(SQLite3 REQUIRED)
    list(APPEND TICKETHUB_CORE_SOURCES src/infrastructure/database/SqliteDatabase.cpp)
endif()

if(TICKETHUB_WITH_POSTGRES)
    find_package(PostgreSQL REQUIRED)
    list(APPEND TICKETHUB_CORE_SOURCES src/infrastructure/database/PostgresDatabase.cpp)
endif()

add_library(ticket-hub-core STATIC ${TICKETHUB_CORE_SOURCES})
target_include_directories(ticket-hub-core PUBLIC src)
target_compile_definitions(ticket-hub-core PRIVATE
    TICKETHUB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(TICKETHUB_WITH_SQLITE)
    target_compile_definitions(ticket-hub-core PUBLIC TICKETHUB_WITH_SQLITE=1)
    target_link_libraries(ticket-hub-core PUBLIC SQLite::SQLite3)
endif()

if(TICKETHUB_WITH_POSTGRES)
    target_compile_definitions(ticket-hub-core PUBLIC TICKETHUB_WITH_POSTGRES=1)
    target_link_libraries(ticket-hub-core PUBLIC PostgreSQL::PostgreSQL)
endif()

target_link_libraries(ticket-hub-core PUBLIC Argon2::Argon2)

if(MSVC)
    target_compile_options(ticket-hub-core PRIVATE /W4 /permissive-)
else()
    target_compile_options(ticket-hub-core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()


if(TICKETHUB_BUILD_CLI)
    # libcurl powers `process-outbox` (D39/D41 webhooks over HTTP, D52 email
    # over SMTP -- see migrations/*/019_outbox_delivery.sql and
    # src/infrastructure/delivery/). Only the CLI needs it: the server
    # itself never makes an outbound network call, so ticket-hub-core and
    # the `ticket-hub` server target stay free of this dependency.
    find_package(CURL REQUIRED)

    add_executable(ticket-hub-cli
        src/cli/main.cpp
        src/infrastructure/delivery/SmtpEmailSender.cpp
        src/infrastructure/delivery/WebhookDeliveryClient.cpp
    )
    target_link_libraries(ticket-hub-cli PRIVATE ticket-hub-core CURL::libcurl)
    target_compile_definitions(ticket-hub-cli PRIVATE TICKETHUB_VERSION="${PROJECT_VERSION}")
    if(MSVC)
        target_compile_options(ticket-hub-cli PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ticket-hub-cli PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
    install(TARGETS ticket-hub-cli RUNTIME DESTINATION bin)
endif()

if(TICKETHUB_BUILD_SERVER)
    include(FetchContent)
    find_package(Crow CONFIG QUIET)
    if(NOT Crow_FOUND)
        FetchContent_Declare(
            Crow
            GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
            GIT_TAG v1.3.3
            GIT_SHALLOW TRUE
        )
        set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(Crow)
    endif()

    add_executable(ticket-hub
        src/main.cpp
        src/web/Api.cpp
        src/web/HttpServer.cpp
        src/web/JsonLogHandler.cpp
        src/web/RateLimiter.cpp
    )
    target_link_libraries(ticket-hub PRIVATE ticket-hub-core Crow::Crow)
    target_compile_definitions(ticket-hub PRIVATE
        TICKETHUB_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_options(ticket-hub PRIVATE /W4 /permissive-)
    else()
        target_compile_options(ticket-hub PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()

    install(TARGETS ticket-hub RUNTIME DESTINATION bin)
    install(DIRECTORY web/ DESTINATION share/ticket-hub/web)
endif()

if(TICKETHUB_BUILD_SERVER OR TICKETHUB_BUILD_CLI)
    install(DIRECTORY migrations/ DESTINATION share/ticket-hub/migrations)
endif()

if(TICKETHUB_BUILD_TESTS)
    enable_testing()

    add_executable(ticket-hub-domain-tests tests/domain_validation_tests.cpp)
    target_link_libraries(ticket-hub-domain-tests PRIVATE ticket-hub-core)
    add_test(NAME ticket-hub-domain-tests COMMAND ticket-hub-domain-tests)

    add_executable(ticket-hub-migration-tests tests/migration_tests.cpp)
    target_link_libraries(ticket-hub-migration-tests PRIVATE ticket-hub-core)
    add_test(NAME ticket-hub-migration-tests COMMAND ticket-hub-migration-tests)

    if(TICKETHUB_WITH_SQLITE)
        add_executable(ticket-hub-sqlite-integration-tests tests/sqlite_integration_tests.cpp)
        target_link_libraries(ticket-hub-sqlite-integration-tests PRIVATE ticket-hub-core)
        target_compile_definitions(ticket-hub-sqlite-integration-tests PRIVATE
            TICKETHUB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME ticket-hub-sqlite-integration-tests COMMAND ticket-hub-sqlite-integration-tests)

        add_executable(ticket-hub-identity-tests tests/identity_integration_tests.cpp)
        target_link_libraries(ticket-hub-identity-tests PRIVATE ticket-hub-core)
        target_compile_definitions(ticket-hub-identity-tests PRIVATE
            TICKETHUB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME ticket-hub-identity-tests COMMAND ticket-hub-identity-tests)

        add_executable(ticket-hub-authorization-tests tests/authorization_integration_tests.cpp)
        target_link_libraries(ticket-hub-authorization-tests PRIVATE ticket-hub-core)
        target_compile_definitions(ticket-hub-authorization-tests PRIVATE
            TICKETHUB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME ticket-hub-authorization-tests COMMAND ticket-hub-authorization-tests)

        add_executable(ticket-hub-workflow-tests tests/workflow_integration_tests.cpp)
        target_link_libraries(ticket-hub-workflow-tests PRIVATE ticket-hub-core)
        target_compile_definitions(ticket-hub-workflow-tests PRIVATE
            TICKETHUB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME ticket-hub-workflow-tests COMMAND ticket-hub-workflow-tests)
    endif()

    add_executable(ticket-hub-crypto-tests tests/crypto_tests.cpp)
    target_link_libraries(ticket-hub-crypto-tests PRIVATE ticket-hub-core)
    add_test(NAME ticket-hub-crypto-tests COMMAND ticket-hub-crypto-tests)

    # RateLimiter has no Crow dependency, so it is tested standalone (not
    # linked into ticket-hub-core, which the CLI target also uses) even when
    # TICKETHUB_BUILD_SERVER is off.
    add_executable(ticket-hub-ratelimiter-tests tests/rate_limiter_tests.cpp src/web/RateLimiter.cpp)
    target_include_directories(ticket-hub-ratelimiter-tests PRIVATE src)
    add_test(NAME ticket-hub-ratelimiter-tests COMMAND ticket-hub-ratelimiter-tests)
endif()
