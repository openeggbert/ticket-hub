#include "application/TicketService.h"

#include "common/Sha256.h"
#include "common/Uuid.h"
#include "domain/Errors.h"
#include "domain/Validation.h"

#include <algorithm>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace TicketHub::Application {

namespace {
std::string joinErrors(const std::vector<std::string>& errors) {
    std::ostringstream message;
    for (std::size_t index = 0; index < errors.size(); ++index) {
        if (index != 0) {
            message << "; ";
        }
        message << errors[index];
    }
    return message.str();
}

void normalizeLabels(std::vector<std::string>& labels) {
    for (auto& label : labels) {
        label = Domain::normalizeLabel(label);
    }
    std::sort(labels.begin(), labels.end());
    labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
}

void validateCommentBody(const std::string& body) {
    if (body.empty()) {
        throw std::invalid_argument("comment body is required");
    }
    if (body.size() > 100000) {
        throw std::invalid_argument("comment body must not exceed 100000 characters");
    }
}

// @mentions (D80): every distinct `@handle` token in the body, lowercased
// (handles are always stored lowercase, D56), deduplicated. Deliberately
// simple -- no escaping/code-fence awareness, since the comment body isn't
// rendered as Markdown anywhere yet either (D16 is not implemented).
std::set<std::string> extractMentionedHandles(const std::string& body) {
    static const std::regex pattern("@([a-zA-Z0-9_]{1,32})");
    std::set<std::string> handles;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), pattern); it != std::sregex_iterator(); ++it) {
        handles.insert(Domain::normalizeHandle((*it)[1].str()));
    }
    return handles;
}

// Used by the single-field bulk actions (assign, add label): editTicket is a
// full-replacement PUT, so a single-field bulk change still has to carry
// every other current field forward unchanged.
Domain::EditTicketRequest editRequestFrom(const Domain::Ticket& ticket) {
    Domain::EditTicketRequest request;
    request.summary = ticket.summary;
    request.description = ticket.description;
    request.priorityKey = ticket.priority.key;
    request.assigneeEmail = ticket.assignee ? std::optional<std::string>(ticket.assignee->email) : std::nullopt;
    request.labels = ticket.labels;
    request.componentName = ticket.component ? std::optional<std::string>(ticket.component->name) : std::nullopt;
    request.storyPoints = ticket.storyPoints;
    request.dueDate = ticket.dueDate;
    request.ticketTypeKey = ticket.type.key;
    request.parentTicketKey = ticket.parentTicketKey;
    return request;
}
} // namespace

TicketService::TicketService(std::shared_ptr<Infrastructure::Database::IDatabase> database, std::string attachmentsRoot,
                             const bool emailDeliveryEnabled)
    : database_(std::move(database)), attachmentStorage_(std::move(attachmentsRoot)),
      emailDeliveryEnabled_(emailDeliveryEnabled) {
    if (!database_) {
        throw std::invalid_argument("database must not be null");
    }
}

void TicketService::requireProjectRole(const Domain::Principal& actor,
                                       const std::string& projectKey,
                                       const int minimumRank) const {
    if (actor.isAdmin) {
        return;
    }
    const auto role = database_->findProjectRoleByKey(projectKey, actor.userId);
    const int rank = role ? Domain::projectRoleRank(*role) : -1;
    if (rank < minimumRank) {
        throw Domain::Forbidden("Actor lacks the required role on project " + projectKey);
    }
}

void TicketService::requireGlobalAdmin(const Domain::Principal& actor) const {
    if (!actor.isAdmin) {
        throw Domain::Forbidden("This action requires global administrator privileges");
    }
}

void TicketService::requireReadAccess(const std::optional<Domain::Principal>& actor) {
    if (actor) {
        return;
    }
    if (!isAnonymousReadEnabled()) {
        throw Domain::AuthenticationRequired("Anonymous read access is disabled on this installation");
    }
}

void TicketService::requireValidHierarchy(Domain::CreateTicketRequest& request) {
    validateHierarchyShape(request.ticketTypeKey, request.parentTicketKey, request.projectKey, std::nullopt);
}

void TicketService::validateHierarchyShape(const std::string& ticketTypeKey,
                                           std::optional<std::string>& parentTicketKey,
                                           const std::string& projectKey,
                                           const std::optional<std::string>& excludeSelfKey) {
    const int level = Domain::ticketTypeHierarchyLevel(ticketTypeKey);
    const bool hasParent = parentTicketKey.has_value() && !parentTicketKey->empty();

    if (level == 1 && hasParent) {
        throw std::invalid_argument("An Epic cannot have a parent ticket");
    }
    if (level == -1 && !hasParent) {
        throw std::invalid_argument("A sub-task must have a parent ticket");
    }
    if (!hasParent) {
        parentTicketKey = std::nullopt;
        return;
    }

    const std::string parentKey = Domain::normalizeTicketKey(*parentTicketKey);
    if (excludeSelfKey && parentKey == *excludeSelfKey) {
        throw std::invalid_argument("A ticket cannot be its own parent");
    }
    const auto parent = database_->findTicketByKey(parentKey);
    if (!parent) {
        throw std::invalid_argument("Unknown parent ticket: " + parentKey);
    }
    if (parent->projectKey != projectKey) {
        throw std::invalid_argument("A parent ticket must be in the same project");
    }
    const int parentLevel = Domain::ticketTypeHierarchyLevel(parent->type.key);
    if (level == -1 && parentLevel != 0) {
        throw std::invalid_argument("A sub-task's parent must be a Story, Task, or Bug");
    }
    if (level == 0 && parentLevel != 1) {
        throw std::invalid_argument("A Story/Task/Bug's parent must be an Epic");
    }
    parentTicketKey = parentKey;
}

std::string TicketService::backendName() const {
    return database_->backendName();
}

bool TicketService::isAnonymousReadEnabled() {
    return database_->getSetting("anonymous_read_access") == std::string("true");
}

void TicketService::setAnonymousReadEnabled(const bool enabled, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    database_->setSetting("anonymous_read_access", enabled ? "true" : "false");
    database_->recordAuditEvent("admin", "settings.anonymous_read_changed", actor.userId,
                                std::string("installation_settings"), std::string("anonymous_read_access"),
                                enabled ? std::string("enabled") : std::string("disabled"));
}

std::optional<std::string> TicketService::latestKnownVersion(const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->getSetting("latest_known_version");
}

void TicketService::setLatestKnownVersion(const std::string& version, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    if (version.empty()) {
        throw std::invalid_argument("version must not be empty");
    }
    database_->setSetting("latest_known_version", version);
    database_->recordAuditEvent("admin", "settings.latest_known_version_changed", actor.userId,
                                std::string("installation_settings"), std::string("latest_known_version"),
                                version);
}

std::vector<Domain::Project> TicketService::listProjects(const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listProjects();
}

std::vector<Domain::Ticket> TicketService::listTickets(const Domain::TicketFilter& filter,
                                                      const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    auto normalized = filter;
    if (normalized.projectKey) {
        normalized.projectKey = Domain::normalizeProjectKey(*normalized.projectKey);
    }
    return database_->listTickets(normalized);
}

Domain::Page<Domain::Ticket> TicketService::listTicketsPaged(const Domain::TicketFilter& filter,
                                                            int page,
                                                            int pageSize,
                                                            const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    auto normalized = filter;
    if (normalized.projectKey) {
        normalized.projectKey = Domain::normalizeProjectKey(*normalized.projectKey);
    }
    // Fixed constants (D125/D126), no admin exceptions: page is clamped to
    // at least 1 (a caller passing 0 or a negative page gets page 1 rather
    // than an error, since "no results yet" is a more useful response than
    // a 400 for a cosmetic off-by-one from the caller); pageSize is clamped
    // into [1, MaxPageSize].
    const int clampedPage = std::max(page, 1);
    const int clampedPageSize = std::clamp(pageSize, 1, Domain::MaxPageSize);
    const int offset = (clampedPage - 1) * clampedPageSize;

    Domain::Page<Domain::Ticket> result;
    result.page = clampedPage;
    result.pageSize = clampedPageSize;
    result.totalItems = database_->countTickets(normalized);
    result.items = database_->listTickets(normalized, clampedPageSize, offset);
    return result;
}

std::optional<Domain::Ticket> TicketService::findTicket(const std::string& ticketKey,
                                                       const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->findTicketByKey(Domain::normalizeTicketKey(ticketKey));
}

Domain::Ticket TicketService::createTicket(Domain::CreateTicketRequest request, const Domain::Principal& actor) {
    request.projectKey = Domain::normalizeProjectKey(request.projectKey);
    requireProjectRole(actor, request.projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    requireValidHierarchy(request);
    if (request.assigneeEmail) {
        request.assigneeEmail = Domain::normalizeEmail(*request.assigneeEmail);
    }
    normalizeLabels(request.labels);
    const auto errors = Domain::validateCreateTicket(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    requireCustomFieldsSatisfied(request.projectKey, request.customFieldValues);
    const auto created = database_->createTicket(request, actor.userId);
    dispatchAssignmentNotification(created, std::nullopt, actor);
    enqueueWebhookEvent(Domain::WebhookEventTicketCreated, created);
    return created;
}

bool TicketService::changeStatus(const std::string& ticketKey,
                                 const std::string& statusKey,
                                 const Domain::Principal& actor,
                                 const std::optional<std::string> resolution,
                                 const std::optional<std::int64_t> expectedVersion) {
    if (ticketKey.empty() || statusKey.empty()) {
        throw std::invalid_argument("ticketKey and statusKey are required");
    }
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        return false;
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    const bool changed = database_->changeTicketStatus(normalizedKey, statusKey, actor.userId, resolution, expectedVersion);
    if (changed) {
        // Re-fetched (rather than mutating `ticket` locally) so the webhook
        // payload reflects the actual persisted row -- e.g. the real
        // resolution/version changeTicketStatus applied, not a best-guess
        // reconstruction of what the request probably did.
        if (const auto updated = database_->findTicketByKey(normalizedKey)) {
            enqueueWebhookEvent(Domain::WebhookEventTicketStatusChanged, *updated);
        }
    }
    return changed;
}

std::optional<Domain::Ticket> TicketService::editTicket(const std::string& ticketKey,
                                                       Domain::EditTicketRequest request,
                                                       const Domain::Principal& actor,
                                                       const std::optional<std::int64_t> expectedVersion) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        return std::nullopt;
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    if (request.assigneeEmail) {
        request.assigneeEmail = Domain::normalizeEmail(*request.assigneeEmail);
    }
    normalizeLabels(request.labels);
    const auto errors = Domain::validateEditTicket(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    // Re-typing (ticketTypeKey) and re-parenting (parentTicketKey) a ticket
    // after creation (previously unimplemented -- see NEXT.md history).
    // Shape validation (Epic/Sub-task/same-project/parent-level) happens
    // here, same as at creation; whether the ticket currently has *children*
    // that a hierarchy-level change would orphan/invalidate depends on
    // concurrent database state and is therefore checked transactionally
    // inside IDatabase::editTicket, the same split changeTicketStatus and
    // moveTicket already use for their own database-state-dependent rules.
    validateHierarchyShape(request.ticketTypeKey, request.parentTicketKey, ticket->projectKey, ticket->key);
    requireCustomFieldsSatisfied(ticket->projectKey, request.customFieldValues);
    const auto assigneeBefore = ticket->assignee;
    const auto edited = database_->editTicket(normalizedKey, request, actor.userId, expectedVersion);
    if (edited) {
        dispatchAssignmentNotification(*edited, assigneeBefore, actor);
        enqueueWebhookEvent(Domain::WebhookEventTicketUpdated, *edited);
    }
    return edited;
}

std::vector<Domain::TicketHistoryEntry> TicketService::listTicketHistory(const std::string& ticketKey,
                                                                         const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listTicketHistory(Domain::normalizeTicketKey(ticketKey));
}

std::vector<Domain::Comment> TicketService::listComments(const std::string& ticketKey,
                                                          const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listComments(Domain::normalizeTicketKey(ticketKey));
}

Domain::Comment TicketService::addComment(const std::string& ticketKey, const std::string& body, const Domain::Principal& actor) {
    validateCommentBody(body);
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    const auto comment = database_->addComment(Domain::AddCommentRequest{normalizedKey, body}, actor.userId);
    dispatchCommentNotifications(*ticket, comment, actor);
    enqueueWebhookEvent(Domain::WebhookEventCommentAdded, *ticket);
    return comment;
}

std::optional<Domain::Comment> TicketService::editComment(const std::string& ticketKey,
                                                           const std::string& commentId,
                                                           const std::string& body,
                                                           const Domain::Principal& actor,
                                                           const std::optional<std::int64_t> expectedVersion) {
    validateCommentBody(body);
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    const auto comment = database_->findCommentById(commentId);
    if (!comment || comment->ticketId != ticket->id) {
        return std::nullopt;
    }
    if (comment->author.id != actor.userId) {
        requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    }
    return database_->editComment(commentId, body, actor.userId, expectedVersion);
}

bool TicketService::deleteComment(const std::string& ticketKey, const std::string& commentId, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    const auto comment = database_->findCommentById(commentId);
    if (!comment || comment->ticketId != ticket->id) {
        return false;
    }
    if (comment->author.id != actor.userId) {
        requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    }
    return database_->deleteComment(commentId, actor.userId);
}

bool TicketService::addCommentReaction(const std::string& ticketKey, const std::string& commentId,
                                       const std::string& reactionKey, const Domain::Principal& actor) {
    if (!Domain::isValidCommentReactionKey(reactionKey)) {
        throw std::invalid_argument("Unknown reaction key: " + reactionKey);
    }
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    if (!database_->findTicketByKey(normalizedKey)) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    if (!database_->findCommentById(commentId)) {
        throw std::invalid_argument("Unknown comment id: " + commentId);
    }
    return database_->addCommentReaction(commentId, actor.userId, reactionKey);
}

bool TicketService::removeCommentReaction(const std::string& ticketKey, const std::string& commentId,
                                          const std::string& reactionKey, const Domain::Principal& actor) {
    if (!Domain::isValidCommentReactionKey(reactionKey)) {
        throw std::invalid_argument("Unknown reaction key: " + reactionKey);
    }
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    if (!database_->findTicketByKey(normalizedKey)) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    if (!database_->findCommentById(commentId)) {
        throw std::invalid_argument("Unknown comment id: " + commentId);
    }
    return database_->removeCommentReaction(commentId, actor.userId, reactionKey);
}

std::vector<Domain::CommentReaction> TicketService::listCommentReactions(
    const std::string& ticketKey, const std::string& commentId, const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    if (!database_->findTicketByKey(normalizedKey)) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    return database_->listCommentReactions(commentId);
}

std::vector<Domain::Worklog> TicketService::listWorklogs(const std::string& ticketKey,
                                                          const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listWorklogs(Domain::normalizeTicketKey(ticketKey));
}

Domain::Worklog TicketService::addWorklog(const std::string& ticketKey,
                                          const std::string& workDate,
                                          const std::int64_t timeSpentSeconds,
                                          std::optional<std::string> comment,
                                          const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    Domain::AddWorklogRequest request{normalizedKey, workDate, timeSpentSeconds, std::move(comment)};
    const auto errors = Domain::validateAddWorklog(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->addWorklog(request, actor.userId);
}

std::optional<Domain::Worklog> TicketService::editWorklog(const std::string& ticketKey,
                                                           const std::string& worklogId,
                                                           const std::string& workDate,
                                                           const std::int64_t timeSpentSeconds,
                                                           std::optional<std::string> comment,
                                                           const Domain::Principal& actor,
                                                           const std::optional<std::int64_t> expectedVersion) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    const auto worklog = database_->findWorklogById(worklogId);
    if (!worklog || worklog->ticketId != ticket->id) {
        return std::nullopt;
    }
    Domain::EditWorklogRequest request{workDate, timeSpentSeconds, std::move(comment)};
    const auto errors = Domain::validateEditWorklog(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->editWorklog(worklogId, request, expectedVersion);
}

bool TicketService::deleteWorklog(const std::string& ticketKey, const std::string& worklogId, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    const auto worklog = database_->findWorklogById(worklogId);
    if (!worklog || worklog->ticketId != ticket->id) {
        return false;
    }
    return database_->deleteWorklog(worklogId, actor.userId);
}

std::vector<Domain::Attachment> TicketService::listAttachments(const std::string& ticketKey,
                                                                const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listAttachments(Domain::normalizeTicketKey(ticketKey));
}

// The attachment id doubles as its local filesystem storage key (see
// IDatabase::createAttachment) -- unlike every other create* use case, the
// id must be known and the file must already be written before the
// database row is inserted, so a row never describes a file that doesn't
// exist on disk. If the database insert fails after the file was written,
// the file is orphaned (unreachable, never listed or served) rather than
// leaving a broken database reference; D105 has no periodic audit to
// reconcile this, but it is a silent waste of disk space, not a
// user-visible correctness ticket.
Domain::Attachment TicketService::uploadAttachment(const std::string& ticketKey,
                                                    const std::string& fileName,
                                                    const std::string& contentType,
                                                    const std::string& bytes,
                                                    const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));

    const auto existingCount = static_cast<int>(database_->listAttachments(normalizedKey).size());
    const auto errors = Domain::validateAttachmentUpload(fileName, static_cast<std::int64_t>(bytes.size()), existingCount);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }

    const std::string id = Common::uuidV4();
    const std::string sha256 = Common::sha256Hex(bytes);
    attachmentStorage_.save(id, bytes);
    return database_->createAttachment(id, normalizedKey, actor.userId, fileName, contentType,
                                       static_cast<std::int64_t>(bytes.size()), sha256);
}

std::pair<Domain::Attachment, std::string> TicketService::downloadAttachment(
    const std::string& attachmentId, const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    const auto attachment = database_->findAttachmentById(attachmentId);
    if (!attachment) {
        throw std::invalid_argument("Unknown attachment: " + attachmentId);
    }
    return {*attachment, attachmentStorage_.read(attachment->id)};
}

bool TicketService::deleteAttachment(const std::string& ticketKey, const std::string& attachmentId,
                                     const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    const auto attachment = database_->findAttachmentById(attachmentId);
    if (!attachment || attachment->ticketId != ticket->id) {
        return false;
    }
    // The uploader may always delete their own attachment; otherwise the
    // actor needs project-Admin-or-above on the ticket's project (or global
    // admin) -- mirrors D83's comment edit/delete rule, the closest
    // existing precedent (no decision text addresses attachment deletion
    // directly).
    if (attachment->uploader.id != actor.userId) {
        requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    }
    return database_->softDeleteAttachment(attachmentId, actor.userId);
}

std::vector<Domain::Attachment> TicketService::listDeletedAttachments(const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    // Fixed 90-day on-demand retention (D102), implemented here rather than
    // inside the database adapter (unlike listDeletedTickets/
    // listDeletedProjects, which purge with a single DELETE entirely at the
    // SQL layer): purging an attachment also means deleting its file on
    // disk, which the SQL-only IDatabase layer cannot do. ISO 8601
    // timestamps compare correctly as plain strings, so no date-parsing
    // library is needed for the cutoff check.
    const std::string cutoff = Common::utcNowPlusSecondsIso8601(-90LL * 24 * 3600);
    auto deleted = database_->listDeletedAttachments();
    std::vector<Domain::Attachment> stillRetained;
    for (const auto& attachment : deleted) {
        if (attachment.deletedAt && *attachment.deletedAt <= cutoff) {
            attachmentStorage_.remove(attachment.id);
            database_->permanentlyDeleteAttachment(attachment.id);
            continue;
        }
        stillRetained.push_back(attachment);
    }
    return stillRetained;
}

bool TicketService::restoreAttachment(const std::string& attachmentId, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->restoreAttachment(attachmentId);
}

bool TicketService::permanentlyDeleteAttachment(const std::string& attachmentId, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    const auto attachment = database_->findAttachmentById(attachmentId);
    if (!attachment) {
        return false;
    }
    const bool deleted = database_->permanentlyDeleteAttachment(attachmentId);
    if (deleted) {
        attachmentStorage_.remove(attachment->id);
        database_->recordAuditEvent("admin", "attachment.permanently_deleted", actor.userId,
                                    std::string("attachment"), attachment->id, std::nullopt);
    }
    return deleted;
}

Domain::Ticket TicketService::cloneTicket(const std::string& ticketKey, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto source = database_->findTicketByKey(normalizedKey);
    if (!source) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    requireProjectRole(actor, source->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));

    Domain::CreateTicketRequest clone;
    clone.projectKey = source->projectKey;
    clone.summary = source->summary;
    clone.description = source->description;
    clone.ticketTypeKey = source->type.key;
    clone.priorityKey = source->priority.key;
    clone.labels = source->labels;
    clone.componentName = source->component ? std::optional<std::string>(source->component->name) : std::nullopt;
    if (source->type.key == Domain::TicketTypeSubTask && source->parentTicketKey) {
        clone.parentTicketKey = source->parentTicketKey;
    }

    const auto created = createTicket(clone, actor);
    database_->createTicketLink(created.key, source->key, Domain::LinkTypeClones);
    return created;
}

Domain::Ticket TicketService::reorderTicket(const std::string& ticketKey,
                                          std::optional<std::string> beforeTicketKey,
                                          const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    if (beforeTicketKey.has_value() && !beforeTicketKey->empty()) {
        beforeTicketKey = Domain::normalizeTicketKey(*beforeTicketKey);
    } else {
        beforeTicketKey = std::nullopt;
    }
    return database_->reorderTicket(normalizedKey, beforeTicketKey);
}

Domain::Ticket TicketService::moveTicket(const std::string& ticketKey,
                                       const std::string& targetProjectKey,
                                       const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const std::string normalizedTargetProjectKey = Domain::normalizeProjectKey(targetProjectKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedKey);
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    requireProjectRole(actor, normalizedTargetProjectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->moveTicket(normalizedKey, normalizedTargetProjectKey, actor.userId);
}

Domain::TicketLink TicketService::createTicketLink(const std::string& sourceTicketKey,
                                                  const std::string& targetTicketKey,
                                                  const std::string& linkType,
                                                  const Domain::Principal& actor) {
    if (!Domain::isValidLinkType(linkType)) {
        throw std::invalid_argument("Unknown link type: " + linkType);
    }
    const std::string normalizedSource = Domain::normalizeTicketKey(sourceTicketKey);
    const std::string normalizedTarget = Domain::normalizeTicketKey(targetTicketKey);
    if (normalizedSource == normalizedTarget) {
        throw std::invalid_argument("A ticket cannot be linked to itself");
    }
    const auto source = database_->findTicketByKey(normalizedSource);
    if (!source) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedSource);
    }
    const auto target = database_->findTicketByKey(normalizedTarget);
    if (!target) {
        throw std::invalid_argument("Unknown ticket key: " + normalizedTarget);
    }
    requireProjectRole(actor, source->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    requireProjectRole(actor, target->projectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->createTicketLink(normalizedSource, normalizedTarget, linkType);
}

std::vector<Domain::TicketLink> TicketService::listTicketLinks(const std::string& ticketKey,
                                                              const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listTicketLinks(Domain::normalizeTicketKey(ticketKey));
}

bool TicketService::deleteTicketLink(const std::string& linkId, const Domain::Principal& actor) {
    const auto link = database_->findTicketLinkById(linkId);
    if (!link) {
        return false;
    }
    requireProjectRole(actor, link->sourceProjectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    requireProjectRole(actor, link->targetProjectKey, Domain::projectRoleRank(Domain::ProjectRoleMember));
    return database_->deleteTicketLink(linkId);
}

bool TicketService::watchTicket(const std::string& ticketKey, const Domain::Principal& actor) {
    return database_->watchTicket(Domain::normalizeTicketKey(ticketKey), actor.userId);
}

bool TicketService::unwatchTicket(const std::string& ticketKey, const Domain::Principal& actor) {
    return database_->unwatchTicket(Domain::normalizeTicketKey(ticketKey), actor.userId);
}

std::vector<Domain::UserSummary> TicketService::listWatchers(const std::string& ticketKey,
                                                              const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listWatchers(Domain::normalizeTicketKey(ticketKey));
}

bool TicketService::voteTicket(const std::string& ticketKey, const Domain::Principal& actor) {
    return database_->voteTicket(Domain::normalizeTicketKey(ticketKey), actor.userId);
}

bool TicketService::unvoteTicket(const std::string& ticketKey, const Domain::Principal& actor) {
    return database_->unvoteTicket(Domain::normalizeTicketKey(ticketKey), actor.userId);
}

std::vector<Domain::UserSummary> TicketService::listVoters(const std::string& ticketKey,
                                                            const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listVoters(Domain::normalizeTicketKey(ticketKey));
}

bool TicketService::deleteTicket(const std::string& ticketKey, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    const auto ticket = database_->findTicketByKey(normalizedKey);
    if (!ticket) {
        return false;
    }
    requireProjectRole(actor, ticket->projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    return database_->softDeleteTicket(normalizedKey, actor.userId);
}

bool TicketService::restoreTicket(const std::string& ticketKey, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->restoreTicket(Domain::normalizeTicketKey(ticketKey));
}

std::vector<Domain::Ticket> TicketService::listDeletedTickets(const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->listDeletedTickets();
}

bool TicketService::permanentlyDeleteTicket(const std::string& ticketKey, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    const std::string normalizedKey = Domain::normalizeTicketKey(ticketKey);
    // Collected before the delete, regardless of the attachments' own
    // soft-delete state: ON DELETE CASCADE will hard-delete every
    // attachment row under this ticket, and there is no periodic orphan-file
    // audit (D105) to catch files left behind afterward.
    const auto storageKeys = database_->listAttachmentStorageKeysForTicket(normalizedKey);
    const bool deleted = database_->permanentlyDeleteTicket(normalizedKey);
    if (deleted) {
        for (const auto& storageKey : storageKeys) {
            attachmentStorage_.remove(storageKey);
        }
        database_->recordAuditEvent("admin", "ticket.permanently_deleted", actor.userId, std::string("ticket"),
                                    normalizedKey, std::nullopt);
    }
    return deleted;
}

Domain::BulkActionResult TicketService::bulkChangeStatus(const std::vector<std::string>& ticketKeys,
                                                          const std::string& statusKey,
                                                          const std::optional<std::string> resolution,
                                                          const Domain::Principal& actor) {
    Domain::BulkActionResult result;
    for (const auto& key : ticketKeys) {
        try {
            if (changeStatus(key, statusKey, actor, resolution)) {
                result.succeeded.push_back(key);
            } else {
                result.failed.push_back(key);
            }
        } catch (...) {
            result.failed.push_back(key);
        }
    }
    return result;
}

Domain::BulkActionResult TicketService::bulkAssign(const std::vector<std::string>& ticketKeys,
                                                    const std::optional<std::string> assigneeEmail,
                                                    const Domain::Principal& actor) {
    Domain::BulkActionResult result;
    for (const auto& key : ticketKeys) {
        try {
            const std::string normalizedKey = Domain::normalizeTicketKey(key);
            const auto ticket = database_->findTicketByKey(normalizedKey);
            if (!ticket) {
                result.failed.push_back(key);
                continue;
            }
            auto edit = editRequestFrom(*ticket);
            edit.assigneeEmail = assigneeEmail;
            if (editTicket(normalizedKey, edit, actor).has_value()) {
                result.succeeded.push_back(key);
            } else {
                result.failed.push_back(key);
            }
        } catch (...) {
            result.failed.push_back(key);
        }
    }
    return result;
}

Domain::BulkActionResult TicketService::bulkAddLabel(const std::vector<std::string>& ticketKeys,
                                                      const std::string& label,
                                                      const Domain::Principal& actor) {
    Domain::BulkActionResult result;
    const std::string normalizedLabel = Domain::normalizeLabel(label);
    for (const auto& key : ticketKeys) {
        try {
            const std::string normalizedKey = Domain::normalizeTicketKey(key);
            const auto ticket = database_->findTicketByKey(normalizedKey);
            if (!ticket) {
                result.failed.push_back(key);
                continue;
            }
            auto edit = editRequestFrom(*ticket);
            if (std::find(edit.labels.begin(), edit.labels.end(), normalizedLabel) == edit.labels.end()) {
                edit.labels.push_back(normalizedLabel);
            }
            if (editTicket(normalizedKey, edit, actor).has_value()) {
                result.succeeded.push_back(key);
            } else {
                result.failed.push_back(key);
            }
        } catch (...) {
            result.failed.push_back(key);
        }
    }
    return result;
}

Domain::BulkActionResult TicketService::bulkDelete(const std::vector<std::string>& ticketKeys, const Domain::Principal& actor) {
    Domain::BulkActionResult result;
    for (const auto& key : ticketKeys) {
        try {
            if (deleteTicket(key, actor)) {
                result.succeeded.push_back(key);
            } else {
                result.failed.push_back(key);
            }
        } catch (...) {
            result.failed.push_back(key);
        }
    }
    return result;
}

// Fixed personal dashboard (D24): the installation-wide widgets (counts,
// recent activity) come straight from dashboardStats(); the personal
// widgets (assigned to me, watched, upcoming deadlines) are only
// meaningful for an authenticated actor and stay empty for an anonymous
// viewer. `assignedToMe` reuses listTickets' existing assignee filter
// rather than a dedicated query; `upcomingDeadlines` is derived from that
// same result set (open tickets with a due date, soonest first) instead of
// a second database round trip.
Domain::DashboardStats TicketService::dashboard(const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    auto stats = database_->dashboardStats();
    if (!actor) {
        return stats;
    }

    Domain::TicketFilter assignedFilter;
    assignedFilter.assigneeEmail = actor->email;
    const auto assigned = database_->listTickets(assignedFilter);

    std::vector<Domain::Ticket> openAssigned;
    std::vector<Domain::Ticket> deadlines;
    for (const auto& ticket : assigned) {
        if (ticket.status.category == "done") {
            continue;
        }
        openAssigned.push_back(ticket);
        if (ticket.dueDate.has_value()) {
            deadlines.push_back(ticket);
        }
    }
    if (openAssigned.size() > 8) {
        openAssigned.resize(8);
    }
    std::sort(deadlines.begin(), deadlines.end(), [](const Domain::Ticket& a, const Domain::Ticket& b) {
        return *a.dueDate < *b.dueDate;
    });
    if (deadlines.size() > 8) {
        deadlines.resize(8);
    }

    stats.assignedToMe = std::move(openAssigned);
    stats.upcomingDeadlines = std::move(deadlines);
    stats.watchedTickets = database_->listWatchedTickets(actor->userId, 8);
    return stats;
}

std::vector<Domain::BoardColumn> TicketService::listBoardColumns(const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listBoardColumns();
}

void TicketService::setBoardColumnWipLimit(const std::string& statusKey, const std::optional<int> wipLimit,
                                           const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    if (!database_->setBoardColumnWipLimit(statusKey, wipLimit)) {
        throw std::invalid_argument("Unknown status key: " + statusKey);
    }
}

std::vector<Domain::ProjectComponent> TicketService::listComponents(const std::string& projectKey,
                                                                     const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listComponents(Domain::normalizeProjectKey(projectKey));
}

Domain::ProjectComponent TicketService::createComponent(Domain::CreateComponentRequest request, const Domain::Principal& actor) {
    request.projectKey = Domain::normalizeProjectKey(request.projectKey);
    requireProjectRole(actor, request.projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    if (request.leadEmail) {
        request.leadEmail = Domain::normalizeEmail(*request.leadEmail);
    }
    if (request.defaultAssigneeEmail) {
        request.defaultAssigneeEmail = Domain::normalizeEmail(*request.defaultAssigneeEmail);
    }
    const auto errors = Domain::validateCreateComponent(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->createComponent(request);
}

std::optional<Domain::ProjectComponent> TicketService::editComponent(const std::string& projectKey,
                                                                      const std::string& componentId,
                                                                      Domain::EditComponentRequest request,
                                                                      const Domain::Principal& actor) {
    const std::string normalizedProjectKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedProjectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    const auto existing = database_->findComponentById(componentId);
    if (!existing || existing->projectKey != normalizedProjectKey) {
        return std::nullopt;
    }
    if (request.leadEmail) {
        request.leadEmail = Domain::normalizeEmail(*request.leadEmail);
    }
    if (request.defaultAssigneeEmail) {
        request.defaultAssigneeEmail = Domain::normalizeEmail(*request.defaultAssigneeEmail);
    }
    const auto errors = Domain::validateEditComponent(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->editComponent(componentId, request);
}

bool TicketService::deleteComponent(const std::string& projectKey, const std::string& componentId, const Domain::Principal& actor) {
    const std::string normalizedProjectKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedProjectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    const auto existing = database_->findComponentById(componentId);
    if (!existing || existing->projectKey != normalizedProjectKey) {
        return false;
    }
    return database_->deleteComponent(componentId);
}

std::vector<Domain::CustomFieldDefinition> TicketService::listCustomFields(const std::string& projectKey,
                                                                            const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listCustomFields(Domain::normalizeProjectKey(projectKey));
}

Domain::CustomFieldDefinition TicketService::createCustomField(Domain::CreateCustomFieldRequest request,
                                                                 const Domain::Principal& actor) {
    request.projectKey = Domain::normalizeProjectKey(request.projectKey);
    requireProjectRole(actor, request.projectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    const auto errors = Domain::validateCreateCustomField(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->createCustomField(request);
}

std::optional<Domain::CustomFieldDefinition> TicketService::editCustomField(const std::string& projectKey,
                                                                             const std::string& fieldId,
                                                                             Domain::EditCustomFieldRequest request,
                                                                             const Domain::Principal& actor) {
    const std::string normalizedProjectKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedProjectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    const auto existing = database_->findCustomFieldById(fieldId);
    if (!existing || existing->projectKey != normalizedProjectKey) {
        return std::nullopt;
    }
    // fieldType is immutable (see EditCustomFieldRequest's doc comment) --
    // options only make sense for a select field, so an edit that supplies
    // any while the existing field isn't one of the select types is
    // rejected the same way create already rejects that combination.
    if (existing->fieldType != "single_select" && existing->fieldType != "multi_select" && !request.options.empty()) {
        throw std::invalid_argument("options is only meaningful for single_select/multi_select fields");
    }
    const auto errors = Domain::validateEditCustomField(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->editCustomField(fieldId, request);
}

bool TicketService::deleteCustomField(const std::string& projectKey, const std::string& fieldId, const Domain::Principal& actor) {
    const std::string normalizedProjectKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedProjectKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    const auto existing = database_->findCustomFieldById(fieldId);
    if (!existing || existing->projectKey != normalizedProjectKey) {
        return false;
    }
    return database_->deleteCustomField(fieldId);
}

std::vector<Domain::CustomFieldValue> TicketService::listTicketCustomFieldValues(const std::string& ticketKey,
                                                                                  const std::optional<Domain::Principal>& actor) {
    requireReadAccess(actor);
    return database_->listTicketCustomFieldValues(Domain::normalizeTicketKey(ticketKey));
}

void TicketService::requireCustomFieldsSatisfied(const std::string& projectKey,
                                                  const std::vector<Domain::CustomFieldValueInput>& values) {
    const auto fields = database_->listCustomFields(projectKey);
    if (fields.empty()) {
        return;
    }
    std::vector<std::string> errors;
    for (const auto& field : fields) {
        if (!field.required) {
            continue;
        }
        const auto supplied = std::find_if(values.begin(), values.end(),
                                           [&](const auto& input) { return input.fieldId == field.id; });
        if (supplied == values.end() || supplied->value.empty()) {
            errors.push_back("Custom field \"" + field.name + "\" is required");
        }
    }
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
}

Domain::Project TicketService::createProject(Domain::CreateProjectRequest request, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    request.key = Domain::normalizeProjectKey(request.key);
    const auto errors = Domain::validateCreateProject(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->createProject(request, actor.userId);
}

bool TicketService::setProjectArchived(const std::string& projectKey, const bool archived, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    return database_->setProjectArchived(normalizedKey, archived);
}

std::optional<Domain::Project> TicketService::changeProjectKey(const std::string& projectKey,
                                                                 const std::string& newKey,
                                                                 const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    const std::string normalizedNewKey = Domain::normalizeProjectKey(newKey);
    if (!Domain::isValidProjectKey(normalizedNewKey)) {
        throw std::invalid_argument("newKey must contain 2-12 uppercase letters or digits and start with a letter");
    }
    if (normalizedNewKey == normalizedKey) {
        throw std::invalid_argument("newKey must be different from the current key");
    }
    return database_->changeProjectKey(normalizedKey, normalizedNewKey);
}

bool TicketService::deleteProject(const std::string& projectKey, const Domain::Principal& actor) {
    const std::string normalizedKey = Domain::normalizeProjectKey(projectKey);
    requireProjectRole(actor, normalizedKey, Domain::projectRoleRank(Domain::ProjectRoleAdmin));
    return database_->softDeleteProject(normalizedKey, actor.userId);
}

bool TicketService::restoreProject(const std::string& projectKey, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->restoreProject(Domain::normalizeProjectKey(projectKey));
}

std::vector<Domain::Project> TicketService::listDeletedProjects(const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->listDeletedProjects();
}

bool TicketService::permanentlyDeleteProject(const std::string& projectKey, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    const std::string normalizedKey = Domain::normalizeProjectKey(projectKey);
    // Same reasoning as permanentlyDeleteTicket: collected before the delete
    // since ON DELETE CASCADE will hard-delete every ticket (and therefore
    // every attachment) under this project.
    const auto storageKeys = database_->listAttachmentStorageKeysForProject(normalizedKey);
    const bool deleted = database_->permanentlyDeleteProject(normalizedKey);
    if (deleted) {
        for (const auto& storageKey : storageKeys) {
            attachmentStorage_.remove(storageKey);
        }
        database_->recordAuditEvent("admin", "project.permanently_deleted", actor.userId, std::string("project"),
                                    normalizedKey, std::nullopt);
    }
    return deleted;
}

std::vector<Domain::User> TicketService::listUsers(const Domain::Principal& /*actor*/) {
    return database_->listUsers();
}

Domain::Principal TicketService::updatePreferences(const Domain::UpdatePreferencesRequest& request,
                                                     const Domain::Principal& actor) {
    const auto errors = Domain::validateUpdatePreferences(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    database_->updateUserPreferences(actor.userId, request);
    Domain::Principal updated = actor;
    updated.timeZone = request.timeZone;
    updated.clockFormat = request.clockFormat;
    return updated;
}

std::vector<Domain::Notification> TicketService::listNotifications(const Domain::Principal& actor, const bool unreadOnly) {
    return database_->listNotifications(actor.userId, unreadOnly);
}

Domain::Page<Domain::Notification> TicketService::listNotificationsPaged(const Domain::Principal& actor,
                                                                          const bool unreadOnly,
                                                                          const int page,
                                                                          const int pageSize) {
    const int clampedPage = std::max(page, 1);
    const int clampedPageSize = std::clamp(pageSize, 1, Domain::MaxPageSize);
    const int offset = (clampedPage - 1) * clampedPageSize;

    Domain::Page<Domain::Notification> result;
    result.page = clampedPage;
    result.pageSize = clampedPageSize;
    result.totalItems = database_->countNotifications(actor.userId, unreadOnly);
    result.items = database_->listNotifications(actor.userId, unreadOnly, clampedPageSize, offset);
    return result;
}

int TicketService::countUnreadNotifications(const Domain::Principal& actor) {
    return database_->countUnreadNotifications(actor.userId);
}

bool TicketService::markNotificationRead(const std::string& notificationId, const Domain::Principal& actor) {
    return database_->markNotificationRead(notificationId, actor.userId);
}

bool TicketService::markAllNotificationsRead(const Domain::Principal& actor) {
    return database_->markAllNotificationsRead(actor.userId);
}

std::vector<Domain::AuditEvent> TicketService::listAuditEvents(const Domain::Principal& actor, const int limit) {
    requireGlobalAdmin(actor);
    return database_->listAuditEvents(limit);
}

Domain::Page<Domain::AuditEvent> TicketService::listAuditEventsPaged(const Domain::Principal& actor,
                                                                      const int page,
                                                                      const int pageSize) {
    requireGlobalAdmin(actor);
    const int clampedPage = std::max(page, 1);
    const int clampedPageSize = std::clamp(pageSize, 1, Domain::MaxPageSize);
    const int offset = (clampedPage - 1) * clampedPageSize;

    Domain::Page<Domain::AuditEvent> result;
    result.page = clampedPage;
    result.pageSize = clampedPageSize;
    result.totalItems = database_->countAuditEvents();
    result.items = database_->listAuditEvents(clampedPageSize, offset);
    return result;
}

void TicketService::dispatchAssignmentNotification(const Domain::Ticket& ticketAfter,
                                                    const std::optional<Domain::UserSummary>& assigneeBefore,
                                                    const Domain::Principal& actor) {
    if (!ticketAfter.assignee) {
        return;
    }
    if (ticketAfter.assignee->id == actor.userId) {
        return; // Assigning to yourself needs no notification.
    }
    if (assigneeBefore && assigneeBefore->id == ticketAfter.assignee->id) {
        return; // Unchanged assignee (e.g. re-saving an edit) -- not a new assignment.
    }
    database_->createNotification(ticketAfter.assignee->id, Domain::NotificationTypeAssigned, ticketAfter.id);
    maybeEnqueueEmail(ticketAfter.assignee->id, Domain::NotificationTypeAssigned, ticketAfter);
}

void TicketService::dispatchCommentNotifications(const Domain::Ticket& ticket,
                                                  const Domain::Comment& comment,
                                                  const Domain::Principal& actor) {
    std::set<std::string> notified;

    for (const auto& handle : extractMentionedHandles(comment.body)) {
        const auto mentioned = database_->findUserByHandle(handle);
        if (mentioned && mentioned->id != actor.userId && notified.insert(mentioned->id).second) {
            database_->createNotification(mentioned->id, Domain::NotificationTypeMentioned, ticket.id);
            maybeEnqueueEmail(mentioned->id, Domain::NotificationTypeMentioned, ticket);
        }
    }

    for (const auto& watcher : database_->listWatchers(ticket.key)) {
        if (watcher.id != actor.userId && notified.insert(watcher.id).second) {
            database_->createNotification(watcher.id, Domain::NotificationTypeWatchedComment, ticket.id);
            maybeEnqueueEmail(watcher.id, Domain::NotificationTypeWatchedComment, ticket);
        }
    }
}

std::vector<Domain::WebhookSubscription> TicketService::listWebhookSubscriptions(const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->listWebhookSubscriptions();
}

Domain::WebhookSubscription TicketService::createWebhookSubscription(Domain::CreateWebhookSubscriptionRequest request,
                                                                       const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    if (request.projectKey) {
        request.projectKey = Domain::normalizeProjectKey(*request.projectKey);
    }
    const auto errors = Domain::validateCreateWebhookSubscription(request);
    if (!errors.empty()) {
        throw std::invalid_argument(joinErrors(errors));
    }
    return database_->createWebhookSubscription(request, actor.userId);
}

bool TicketService::deleteWebhookSubscription(const std::string& subscriptionId, const Domain::Principal& actor) {
    requireGlobalAdmin(actor);
    return database_->deleteWebhookSubscription(subscriptionId);
}

namespace {
// A minimal, self-contained JSON string builder -- TicketService has no
// crow::json dependency (that's the web layer's, src/web/Api.cpp), and the
// webhook payload only ever needs a handful of flat string fields, so a
// small private escaper is simpler than adding a JSON library dependency
// to the application layer just for this.
std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    escaped += ' ';
                } else {
                    escaped += c;
                }
        }
    }
    return escaped;
}

std::string buildWebhookPayload(const std::string& eventType, const Domain::Ticket& ticket) {
    // Deliberately minimal -- just enough for a receiver to know what
    // happened and look the ticket up via the public API for anything
    // else, not a full ticket serialization (that lives only in
    // src/web/Api.cpp's ticketJson, which the application layer must not
    // depend on).
    std::ostringstream json;
    json << "{"
         << "\"event\":\"" << jsonEscape(eventType) << "\","
         << "\"ticketKey\":\"" << jsonEscape(ticket.key) << "\","
         << "\"projectKey\":\"" << jsonEscape(ticket.projectKey) << "\","
         << "\"summary\":\"" << jsonEscape(ticket.summary) << "\","
         << "\"statusKey\":\"" << jsonEscape(ticket.status.key) << "\""
         << "}";
    return json.str();
}
} // namespace

void TicketService::enqueueWebhookEvent(const std::string& eventType, const Domain::Ticket& ticket) {
    const std::string payload = buildWebhookPayload(eventType, ticket);
    for (const auto& subscription : database_->listWebhookSubscriptions()) {
        if (!subscription.enabled) {
            continue;
        }
        if (subscription.projectKey && *subscription.projectKey != ticket.projectKey) {
            continue;
        }
        if (!subscription.eventTypes.empty() &&
            std::find(subscription.eventTypes.begin(), subscription.eventTypes.end(), eventType) ==
                subscription.eventTypes.end()) {
            continue;
        }
        database_->createWebhookDelivery(subscription.id, eventType, payload);
    }
}

void TicketService::maybeEnqueueEmail(const std::string& userId, const std::string& notificationType,
                                       const Domain::Ticket& ticket) {
    if (!emailDeliveryEnabled_) {
        return;
    }
    // Deliberately plain-text and short -- D52 is "get outbound delivery
    // working at all," not a Markdown/HTML email template system. Mirrors
    // the fixed three-type in-app notification set (D14) one-for-one, so
    // no new "what happened" text is invented here beyond what the
    // notification bell already shows.
    std::string subject;
    std::string body;
    if (notificationType == Domain::NotificationTypeAssigned) {
        subject = "[" + ticket.key + "] Assigned to you";
        body = "You were assigned to " + ticket.key + ": " + ticket.summary;
    } else if (notificationType == Domain::NotificationTypeMentioned) {
        subject = "[" + ticket.key + "] You were mentioned";
        body = "You were mentioned in a comment on " + ticket.key + ": " + ticket.summary;
    } else if (notificationType == Domain::NotificationTypeWatchedComment) {
        subject = "[" + ticket.key + "] New comment";
        body = "A new comment was posted on " + ticket.key + " (" + ticket.summary + "), which you're watching.";
    } else {
        return;
    }
    database_->createEmailDelivery(userId, subject, body);
}

} // namespace TicketHub::Application
