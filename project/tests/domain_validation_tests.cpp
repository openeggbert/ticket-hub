#include "domain/Validation.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace TicketHub::Domain;

    require(normalizeProjectKey(" ticket-hub ") == "TICKET-HUB", "project key preserves invalid punctuation for validation");
    require(normalizeProjectKey("dx12") == "DX12", "project key normalization");
    require(isValidProjectKey("DX12"), "letters and digits are valid");
    require(isValidProjectKey("ABCDEFGHIJKL"), "twelve-character project key is valid");
    require(!isValidProjectKey("TH-2"), "project key punctuation is rejected");
    require(!isValidProjectKey("1TH"), "project key must begin with a letter");
    require(normalizeTicketKey(" cna-123 ") == "CNA-123", "ticket key normalization");
    require(isValidTicketKey("DX12-145"), "ticket key validation");
    require(!isValidTicketKey("DX12-0"), "ticket number starts at one");
    require(normalizeLabel(" Graphics ") == "graphics", "label normalization");

    CreateTicketRequest valid;
    valid.projectKey = "TH";
    valid.summary = "Create a ticket";
    valid.labels = {"backend"};
    require(validateCreateTicket(valid).empty(), "valid ticket request");

    CreateTicketRequest invalid;
    invalid.projectKey = "1";
    invalid.summary = "";
    invalid.storyPoints = -1.0;
    const auto errors = validateCreateTicket(invalid);
    require(errors.size() == 3, "invalid request reports all expected errors");

    require(normalizeEmail(" Demo@Ticket-Hub.Local ") == "demo@ticket-hub.local", "email normalization lowercases and trims");
    require(isValidEmail("demo@ticket-hub.local"), "simple email is valid");
    require(isValidEmail("first.last+tag@sub.example.com"), "email with dots/plus/subdomain is valid");
    require(!isValidEmail("not-an-email"), "email without @ is rejected");
    require(!isValidEmail("missing-domain@"), "email without domain is rejected");
    require(!isValidEmail("@missing-local.com"), "email without local part is rejected");

    require(validatePassword("correct horse battery", "someone@example.com", "Someone").empty(),
           "sufficiently long unrelated password is valid");
    require(!validatePassword("short", "someone@example.com", "Someone").empty(),
           "short password is rejected");
    require(!validatePassword("someone@example.com", "someone@example.com", "Someone").empty(),
           "password identical to email is rejected");

    CreateComponentRequest validComponent;
    validComponent.projectKey = "TH";
    validComponent.name = "Backend";
    require(validateCreateComponent(validComponent).empty(), "valid component request");

    CreateComponentRequest invalidComponent;
    invalidComponent.projectKey = "1";
    invalidComponent.name = "";
    require(validateCreateComponent(invalidComponent).size() == 2, "invalid component request reports all expected errors");

    EditComponentRequest validEditComponent;
    validEditComponent.name = "Frontend";
    require(validateEditComponent(validEditComponent).empty(), "valid component edit request");

    EditComponentRequest invalidEditComponent;
    invalidEditComponent.name = "";
    require(validateEditComponent(invalidEditComponent).size() == 1, "invalid component edit request reports missing name");

    CreateUserRequest validUser;
    validUser.email = "new.user@ticket-hub.local";
    validUser.displayName = "New User";
    validUser.password = "correct horse battery staple";
    require(validateCreateUser(validUser).empty(), "valid create-user request");

    CreateUserRequest invalidUser;
    invalidUser.email = "not-an-email";
    invalidUser.displayName = "";
    invalidUser.password = "short";
    require(validateCreateUser(invalidUser).size() == 3, "invalid create-user request reports all expected errors");

    UpdatePreferencesRequest validPreferences;
    validPreferences.timeZone = "Europe/Prague";
    validPreferences.clockFormat = "24h";
    require(validateUpdatePreferences(validPreferences).empty(), "valid preferences request");

    UpdatePreferencesRequest validPreferences12h;
    validPreferences12h.timeZone = "UTC";
    validPreferences12h.clockFormat = "12h";
    require(validateUpdatePreferences(validPreferences12h).empty(), "12h clock format is valid");

    UpdatePreferencesRequest invalidPreferences;
    invalidPreferences.timeZone = "";
    invalidPreferences.clockFormat = "30h";
    require(validateUpdatePreferences(invalidPreferences).size() == 2, "invalid preferences request reports all expected errors");
    require(!isValidClockFormat("30h"), "clock format must be 12h or 24h");
    require(isValidClockFormat("12h") && isValidClockFormat("24h"), "12h and 24h are the only valid clock formats");

    std::cout << "All domain validation tests passed\n";
    return 0;
}
