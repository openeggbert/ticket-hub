#pragma once

#include "domain/Models.h"

#include <optional>
#include <string>
#include <vector>

namespace TicketHub::Infrastructure::Database {

class IDatabase {
public:
    virtual ~IDatabase() = default;

    virtual std::string backendName() const = 0;
    virtual void migrate() = 0;
    virtual void seedDemoData() = 0;

    // --- Identity (Phase 1) ---
    // `passwordHash` is an already-encoded Argon2id hash (see
    // common/PasswordHash.h); the database layer never sees a plaintext
    // password or performs hashing itself.
    virtual Domain::User createUser(const Domain::CreateUserRequest& request,
                                    const std::string& passwordHash) = 0;
    virtual std::optional<Domain::User> findUserByEmail(const std::string& email) = 0;
    virtual std::optional<Domain::User> findUserById(const std::string& userId) = 0;
    virtual std::vector<Domain::User> listUsers() = 0;
    // Returns nullopt if the user has no local credentials at all (should not
    // happen in V1 -- there is no OIDC -- but keeps the port honest).
    virtual std::optional<std::string> findPasswordHash(const std::string& userId) = 0;

    // Minimal login-attempt tracking (Phase 1). The full configurable
    // lockout policy rides along with REST rate limiting in Phase 6; see
    // docs/REDUCED_SCOPE_ROADMAP.md.
    static constexpr int MaxFailedLoginAttempts = 10;
    virtual void recordFailedLogin(const std::string& userId) = 0;
    virtual void resetFailedLogin(const std::string& userId) = 0;
    virtual bool isLoginLocked(const std::string& userId) = 0;

    // Sessions. `tokenHash` is a SHA-256 hex digest of the random session
    // token (see common/Sha256.h) -- the raw token itself is never stored,
    // only ever returned to the caller once, at creation.
    virtual Domain::Session createSession(const std::string& userId,
                                          const std::string& tokenHash,
                                          const std::string& expiresAtIso8601) = 0;
    virtual std::optional<Domain::Session> findSessionByTokenHash(const std::string& tokenHash) = 0;
    virtual void deleteSession(const std::string& sessionId) = 0;
    // Opportunistic housekeeping call (no background job exists in V1).
    virtual void deleteExpiredSessions() = 0;

    // --- Issue tracker (existing prototype surface, now principal-driven) ---
    virtual std::vector<Domain::Project> listProjects() = 0;
    virtual std::vector<Domain::Issue> listIssues(const Domain::IssueFilter& filter) = 0;
    virtual std::optional<Domain::Issue> findIssueByKey(const std::string& issueKey) = 0;
    virtual Domain::Issue createIssue(const Domain::CreateIssueRequest& request,
                                      const std::string& reporterUserId) = 0;
    virtual bool changeIssueStatus(const std::string& issueKey,
                                   const std::string& statusKey,
                                   const std::string& actorUserId,
                                   std::optional<std::int64_t> expectedVersion = std::nullopt) = 0;
    virtual std::vector<Domain::Comment> listComments(const std::string& issueKey) = 0;
    virtual Domain::Comment addComment(const Domain::AddCommentRequest& request,
                                       const std::string& authorUserId) = 0;
    virtual Domain::DashboardStats dashboardStats() = 0;
};

} // namespace TicketHub::Infrastructure::Database
