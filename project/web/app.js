'use strict';

const STATUSES = [
  { key: 'backlog', name: 'Backlog', category: 'todo' },
  { key: 'selected', name: 'Confirmed', category: 'todo' },
  { key: 'in-progress', name: 'In Progress', category: 'in_progress' },
  { key: 'review', name: 'In Review', category: 'in_progress' },
  { key: 'done', name: 'Done', category: 'done' }
];

const RESOLUTIONS = [
  { key: 'fixed', name: 'Fixed' },
  { key: 'done', name: 'Done' },
  { key: 'wont-fix', name: "Won't fix" },
  { key: 'duplicate', name: 'Duplicate' },
  { key: 'cannot-reproduce', name: 'Cannot reproduce' }
];

function resolutionLabel(resolutionKey) {
  return RESOLUTIONS.find(r => r.key === resolutionKey)?.name || resolutionKey;
}

// History tab: small display-name lookups for the two fixed catalogs that
// don't already have one centralized elsewhere in this file (every filter
// dropdown that lists priorities/types spells its options out inline
// instead) -- kept local to history formatting rather than refactoring
// those dropdowns, which is out of scope here.
const PRIORITY_LABELS = { highest: 'Highest', high: 'High', medium: 'Medium', low: 'Low', lowest: 'Lowest' };
const TICKET_TYPE_LABELS = { epic: 'Epic', story: 'Story', task: 'Task', bug: 'Bug', 'sub-task': 'Sub-task' };

// Maps a raw ticket_history.field_name into the label shown in the History
// tab (D129/D37's own field-change log, read-only).
const HISTORY_FIELD_LABELS = {
  status: 'status', summary: 'summary', description: 'description', priority: 'priority',
  assignee: 'assignee', story_points: 'story points', due_date: 'due date', labels: 'labels',
  ticket_type: 'type', parent: 'parent', component: 'component', project: 'project'
};

function historyFieldLabel(fieldName) {
  return HISTORY_FIELD_LABELS[fieldName] || fieldName;
}

// Renders a raw stored old_value/new_value into something readable: status/
// priority/ticket_type are stored as their fixed catalog *key* (e.g.
// "highest", not "Highest"), due_date is a bare "YYYY-MM-DD" (rendered via
// the same date-only path formatDate already uses everywhere else), and a
// long summary/description change is truncated so one history row stays
// one line. Returns null for an empty/absent value so the caller can tell
// "cleared" apart from "changed to an empty string".
function historyValueLabel(fieldName, value) {
  if (value === null || value === undefined || value === '') return null;
  if (fieldName === 'status') return STATUSES.find(status => status.key === value)?.name || value;
  if (fieldName === 'priority') return PRIORITY_LABELS[value] || value;
  if (fieldName === 'ticket_type') return TICKET_TYPE_LABELS[value] || value;
  if (fieldName === 'due_date') return formatDate(value);
  if ((fieldName === 'summary' || fieldName === 'description') && value.length > 80) {
    return value.slice(0, 80) + '…';
  }
  return value;
}

function historyChangeText(entry) {
  const field = historyFieldLabel(entry.fieldName);
  const oldLabel = historyValueLabel(entry.fieldName, entry.oldValue);
  const newLabel = historyValueLabel(entry.fieldName, entry.newValue);
  if (!oldLabel && newLabel) return `set ${field} to “${newLabel}”`;
  if (oldLabel && !newLabel) return `cleared ${field} (was “${oldLabel}”)`;
  if (!oldLabel && !newLabel) return `changed ${field}`;
  return `changed ${field} from “${oldLabel}” to “${newLabel}”`;
}

// Fixed emoji reaction catalog (D84), matching Domain::isValidCommentReactionKey.
const COMMENT_REACTIONS = [
  { key: 'thumbs_up', emoji: '👍' },
  { key: 'thumbs_down', emoji: '👎' },
  { key: 'laugh', emoji: '😄' },
  { key: 'hooray', emoji: '🎉' },
  { key: 'confused', emoji: '😕' },
  { key: 'heart', emoji: '❤️' },
  { key: 'rocket', emoji: '🚀' },
  { key: 'eyes', emoji: '👀' }
];

// Fixed Epic -> Story/Task/Bug -> Sub-task hierarchy (D5, D29, D64-D66):
// 1 = Epic (no parent allowed), -1 = Sub-task (parent required, must be a
// Story/Task/Bug), 0 = Story/Task/Bug (parent optional, must be an Epic).
function ticketTypeHierarchyLevel(ticketTypeKey) {
  if (ticketTypeKey === 'epic') return 1;
  if (ticketTypeKey === 'sub-task') return -1;
  return 0;
}

function initialState() {
  return {
    view: 'dashboard',
    projects: [],
    tickets: [],
    dashboard: null,
    selectedProject: null,
    search: '',
    status: '',
    // Ad-hoc in-UI filters only (D10): no saved/shared filters, no JQL, not
    // usable as a webhook/board source.
    filterType: '',
    filterPriority: '',
    filterAssignee: '',
    filterLabel: '',
    filterComponent: '',
    // D66: a client-side-only filter (not sent to the server, unlike the
    // filters above) -- "No Epic" means a Story/Task/Bug ticket with no
    // parent; there is no dedicated `TicketFilter` field for it since it's
    // derivable from data already on hand once tickets are fetched.
    filterEpic: '',
    filterDueBefore: '',
    // Backlog screen (page-based, D126): reset to 1 whenever the selected
    // project or a filter changes, left as-is across an internal re-render
    // (e.g. after a reorder) so the user's position in a large backlog
    // survives their own actions.
    backlogPage: 1,
    // Quick filters (Board/Backlog only): one-click toggles, independent of
    // the ad-hoc filter-bar state above so switching to Board/Backlog never
    // silently inherits a filter left set on the Tickets screen (Board in
    // particular already resets every filter-bar field on each render).
    quickFilterMine: false,
    quickFilterNoEpic: false,
    // Audit log (page-based, D126 extended): the log is append-only
    // forever, so unlike a single ticket's comment/worklog list it has no
    // natural upper bound.
    auditPage: 1,
    currentTicket: null,
    principal: null,
    // Cached user directory (D80) -- powers @mention autocomplete. Fetched
    // once per session in loadBaseData(); the demo app is small enough that
    // per-comment or per-keystroke fetching would be unnecessary overhead.
    users: []
  };
}

const state = initialState();

const content = document.querySelector('#content');
const createModal = document.querySelector('#create-modal');
const projectModal = document.querySelector('#project-modal');
const ticketDrawer = document.querySelector('#ticket-drawer');
const drawerBackdrop = document.querySelector('#ticket-drawer-backdrop');
const toast = document.querySelector('#toast');
const loginScreen = document.querySelector('#login-screen');
const appShell = document.querySelector('#app-shell');
const loginForm = document.querySelector('#login-form');
const loginError = document.querySelector('#login-error');

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

// --- Markdown rendering (D16) ---
// A deliberately small subset of Markdown (bold/italic/inline code/links/
// headings/lists/blockquotes/fenced code/hr), not a general-purpose engine
// -- kept because D16 calls for a visual toolbar and live preview, and this
// subset covers what the toolbar buttons below produce.
//
// Security: the raw input is HTML-escaped FIRST (escapeHtml), and every
// transform below only ever wraps the already-escaped text in a fixed set
// of hardcoded safe tags -- user input can never introduce a real HTML tag
// or attribute this way, so there is no separate sanitization pass to get
// wrong (unlike rendering a full Markdown engine's output, which would need
// one). Link URLs are restricted to http(s)/mailto; anything else is left
// as literal `[text](url)` text instead of becoming a clickable link.
// `attachment://<id>` (D100) resolves to a real download/preview URL only
// when the id looks like the UUID this app always generates -- anything
// else is left as literal text, the same "safe by construction" posture
// applied to javascript:-scheme links below.
function attachmentDownloadUrl(url) {
  const match = /^attachment:\/\/([a-zA-Z0-9-]+)$/.exec(url);
  return match ? `/api/v1/attachments/${match[1]}/download` : null;
}

function renderMarkdownInline(text) {
  // Bold/italic use only `**`/`*` (not `__`/`_`) -- underscore delimiters
  // are ambiguous with snake_case/dunder identifiers (e.g. `__init__`),
  // where a naive regex would treat adjacent underscores as emphasis
  // delimiters and mangle unrelated text spanning between them.
  return text
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/\*([^*]+)\*/g, '<em>$1</em>')
    .replace(/!\[([^\]]*)\]\(([^)\s]+)\)/g, (match, alt, url) => {
      const attachmentUrl = attachmentDownloadUrl(url);
      if (attachmentUrl) return `<img src="${attachmentUrl}" alt="${alt}" class="markdown-attachment-image">`;
      return /^https?:/i.test(url) ? `<img src="${url}" alt="${alt}" class="markdown-attachment-image">` : match;
    })
    .replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (match, label, url) => {
      const attachmentUrl = attachmentDownloadUrl(url);
      if (attachmentUrl) return `<a href="${attachmentUrl}" target="_blank" rel="noopener noreferrer">📎 ${label}</a>`;
      return /^(https?:|mailto:)/i.test(url) ? `<a href="${url}" target="_blank" rel="noopener noreferrer">${label}</a>` : match;
    });
}

function renderMarkdown(raw) {
  const lines = escapeHtml(String(raw ?? '')).split('\n');
  const blocks = [];
  let index = 0;
  const isBlockStart = line => /^(#{1,3})\s|^```|^[-*]\s|^\d+\.\s|^&gt;\s?|^(---|\*\*\*)$/.test(line);
  while (index < lines.length) {
    const line = lines[index];
    if (/^```/.test(line)) {
      const code = [];
      index++;
      while (index < lines.length && !/^```/.test(lines[index])) {
        code.push(lines[index]);
        index++;
      }
      index++; // skip the closing fence
      blocks.push(`<pre><code>${code.join('\n')}</code></pre>`);
      continue;
    }
    if (/^(---|\*\*\*)$/.test(line.trim())) {
      blocks.push('<hr>');
      index++;
      continue;
    }
    const heading = line.match(/^(#{1,3})\s+(.*)$/);
    if (heading) {
      const level = heading[1].length;
      blocks.push(`<h${level}>${renderMarkdownInline(heading[2])}</h${level}>`);
      index++;
      continue;
    }
    if (/^&gt;\s?/.test(line)) {
      const quoteLines = [];
      while (index < lines.length && /^&gt;\s?/.test(lines[index])) {
        quoteLines.push(renderMarkdownInline(lines[index].replace(/^&gt;\s?/, '')));
        index++;
      }
      blocks.push(`<blockquote><p>${quoteLines.join('<br>')}</p></blockquote>`);
      continue;
    }
    if (/^[-*]\s+/.test(line)) {
      const items = [];
      while (index < lines.length && /^[-*]\s+/.test(lines[index])) {
        const itemText = lines[index].replace(/^[-*]\s+/, '');
        // GitHub-style checklist syntax (D62): "- [ ] foo" / "- [x] foo"
        // renders as a disabled checkbox, not an editable one -- toggling
        // it would require rewriting the underlying Markdown source, which
        // is out of scope for "Markdown checklist syntax only."
        const checklist = itemText.match(/^\[([ xX])\]\s+(.*)$/);
        if (checklist) {
          const checked = checklist[1] !== ' ';
          items.push(`<li class="checklist-item"><input type="checkbox" disabled ${checked ? 'checked' : ''}> ${renderMarkdownInline(checklist[2])}</li>`);
        } else {
          items.push(`<li>${renderMarkdownInline(itemText)}</li>`);
        }
        index++;
      }
      blocks.push(`<ul>${items.join('')}</ul>`);
      continue;
    }
    if (/^\d+\.\s+/.test(line)) {
      const items = [];
      while (index < lines.length && /^\d+\.\s+/.test(lines[index])) {
        items.push(`<li>${renderMarkdownInline(lines[index].replace(/^\d+\.\s+/, ''))}</li>`);
        index++;
      }
      blocks.push(`<ol>${items.join('')}</ol>`);
      continue;
    }
    if (line.trim() === '') {
      index++;
      continue;
    }
    const paragraphLines = [];
    while (index < lines.length && lines[index].trim() !== '' && !isBlockStart(lines[index])) {
      paragraphLines.push(renderMarkdownInline(lines[index]));
      index++;
    }
    blocks.push(`<p>${paragraphLines.join('<br>')}</p>`);
  }
  return blocks.join('');
}

// --- Markdown toolbar (D16) ---
// Plain textarea manipulation (selectionStart/End), no execCommand/
// contenteditable -- keeps the field a real <textarea> so existing
// FormData-based submit handlers and the @mention autocomplete (which
// reads selectionStart directly) keep working unchanged.
function wrapSelection(textarea, before, after = before) {
  const start = textarea.selectionStart;
  const end = textarea.selectionEnd;
  const selected = textarea.value.slice(start, end);
  textarea.value = textarea.value.slice(0, start) + before + selected + after + textarea.value.slice(end);
  textarea.focus();
  textarea.selectionStart = start + before.length;
  textarea.selectionEnd = start + before.length + selected.length;
}

function prefixLines(textarea, prefix) {
  const start = textarea.selectionStart;
  const end = textarea.selectionEnd;
  const value = textarea.value;
  const lineStart = value.lastIndexOf('\n', start - 1) + 1;
  const lineEndSearch = value.indexOf('\n', end);
  const lineEnd = lineEndSearch === -1 ? value.length : lineEndSearch;
  const block = value.slice(lineStart, lineEnd);
  const prefixed = block.split('\n').map(line => prefix + line).join('\n');
  textarea.value = value.slice(0, lineStart) + prefixed + value.slice(lineEnd);
  textarea.focus();
}

function insertAtCursor(textarea, text) {
  const start = textarea.selectionStart;
  const end = textarea.selectionEnd;
  textarea.value = textarea.value.slice(0, start) + text + textarea.value.slice(end);
  const cursor = start + text.length;
  textarea.setSelectionRange(cursor, cursor);
  textarea.focus();
}

// Full upload + drag/drop + paste support in the Markdown editor (D100),
// referencing `attachment://<id>` (resolved at render time by
// renderMarkdown/renderMarkdownInline). Only wired up when `ticketKey` is
// known -- the create-ticket form has no ticket yet to attach files to, so
// its description textarea gets the toolbar without this capability.
function attachMarkdownToolbar(textarea, ticketKey = null) {
  const toolbar = document.createElement('div');
  toolbar.className = 'markdown-toolbar';
  toolbar.innerHTML = `
    <button type="button" data-md="bold" title="Bold"><strong>B</strong></button>
    <button type="button" data-md="italic" title="Italic"><em>I</em></button>
    <button type="button" data-md="code" title="Inline code">&lt;/&gt;</button>
    <button type="button" data-md="link" title="Link">🔗</button>
    <button type="button" data-md="ul" title="Bulleted list">•</button>
    <button type="button" data-md="ol" title="Numbered list">1.</button>
    <button type="button" data-md="quote" title="Quote">❝</button>
    ${ticketKey ? '<button type="button" data-md="attach" title="Attach a file">📎</button>' : ''}
    <button type="button" class="markdown-preview-toggle" data-md="preview" title="Toggle preview">👁 Preview</button>`;
  textarea.insertAdjacentElement('beforebegin', toolbar);

  const insertAttachmentReference = async file => {
    try {
      const attachment = await uploadAttachmentFile(ticketKey, file);
      const isImage = attachment.contentType.startsWith('image/');
      insertAtCursor(textarea, `${isImage ? '!' : ''}[${attachment.fileName}](attachment://${attachment.id})`);
    } catch (error) {
      showToast(`${file.name}: ${error.message}`);
    }
  };

  if (ticketKey) {
    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.hidden = true;
    textarea.insertAdjacentElement('afterend', fileInput);
    fileInput.addEventListener('change', () => {
      if (fileInput.files.length) insertAttachmentReference(fileInput.files[0]);
      fileInput.value = '';
    });
    toolbar.querySelector('[data-md="attach"]').addEventListener('click', () => fileInput.click());

    textarea.addEventListener('dragover', event => event.preventDefault());
    textarea.addEventListener('drop', event => {
      event.preventDefault();
      if (event.dataTransfer.files.length) insertAttachmentReference(event.dataTransfer.files[0]);
    });
    textarea.addEventListener('paste', event => {
      const file = [...(event.clipboardData?.files || [])][0];
      if (file) {
        event.preventDefault();
        insertAttachmentReference(file);
      }
    });
  }

  const previewPane = document.createElement('div');
  previewPane.className = 'markdown-preview hidden';
  textarea.insertAdjacentElement('afterend', previewPane);

  toolbar.querySelectorAll('button[data-md]').forEach(button => button.addEventListener('click', () => {
    const action = button.dataset.md;
    if (action === 'preview') {
      const showingPreview = !previewPane.classList.contains('hidden');
      if (showingPreview) {
        previewPane.classList.add('hidden');
        textarea.classList.remove('hidden');
      } else {
        previewPane.innerHTML = renderMarkdown(textarea.value) || '<p class="markdown-empty">Nothing to preview.</p>';
        previewPane.classList.remove('hidden');
        textarea.classList.add('hidden');
      }
      return;
    }
    if (action === 'bold') wrapSelection(textarea, '**');
    else if (action === 'italic') wrapSelection(textarea, '*');
    else if (action === 'code') wrapSelection(textarea, '`');
    else if (action === 'link') wrapSelection(textarea, '[', '](https://)');
    else if (action === 'ul') prefixLines(textarea, '- ');
    else if (action === 'ol') prefixLines(textarea, '1. ');
    else if (action === 'quote') prefixLines(textarea, '> ');
  }));
}

// D45: `Intl.supportedValuesOf` gives every IANA zone name the browser
// itself knows about, for free -- no curated/bundled zone list to keep in
// sync. Falls back to no suggestions (still a plain free-text input) on a
// browser old enough not to support it.
function timezoneDatalistOptions() {
  try {
    return Intl.supportedValuesOf('timeZone').map(zone => `<option value="${escapeHtml(zone)}">`).join('');
  } catch {
    return '';
  }
}

function initials(name) {
  return String(name || '?').split(/\s+/).filter(Boolean).slice(0, 2).map(part => part[0]).join('').toUpperCase();
}

// D45: a bare "YYYY-MM-DD" value (a ticket due date, a worklog work date)
// is a calendar date, not an instant in time -- it must never shift across
// a timezone boundary ("date-only stays date-only"), so it's always
// formatted in UTC regardless of the viewer's own timeZone preference.
// Anything else is a real timestamp (createdAt/updatedAt/expiresAt/...)
// and is rendered in the signed-in user's stored timeZone/clockFormat,
// defaulting to UTC/24h for an anonymous viewer or before login resolves.
function isDateOnly(value) {
  return /^\d{4}-\d{2}-\d{2}$/.test(value);
}

function formatDate(value) {
  if (!value) return '—';
  if (isDateOnly(value)) {
    const date = new Date(`${value}T00:00:00Z`);
    if (Number.isNaN(date.getTime())) return value;
    return new Intl.DateTimeFormat(undefined, { month: 'short', day: 'numeric', year: 'numeric', timeZone: 'UTC' }).format(date);
  }
  const date = new Date(value.replace(' ', 'T'));
  if (Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat(undefined, {
    month: 'short', day: 'numeric', year: 'numeric', hour: 'numeric', minute: '2-digit',
    hour12: (state.principal?.clockFormat || '24h') === '12h',
    timeZone: state.principal?.timeZone || 'UTC'
  }).format(date);
}

function relativeDate(value) {
  if (!value) return '';
  const date = new Date(value.replace(' ', 'T'));
  if (Number.isNaN(date.getTime())) return value;
  const diff = Date.now() - date.getTime();
  const hours = Math.floor(diff / 3_600_000);
  if (hours < 1) return 'just now';
  if (hours < 24) return `${hours}h ago`;
  const days = Math.floor(hours / 24);
  if (days < 14) return `${days}d ago`;
  return formatDate(value);
}

// Simplified worklogs (D12/D13): time is entered/displayed as "1h 30m",
// stored server-side as a plain integer of seconds (no duration type in
// JSON).
function formatDuration(totalSeconds) {
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.round((totalSeconds % 3600) / 60);
  if (hours && minutes) return `${hours}h ${minutes}m`;
  if (hours) return `${hours}h`;
  return `${minutes}m`;
}

function parseDurationToSeconds(text) {
  const match = String(text ?? '').trim().match(/^(?:(\d+)h)?\s*(?:(\d+)m)?$/i);
  if (!match || (!match[1] && !match[2])) return null;
  return (Number(match[1] || 0) * 3600) + (Number(match[2] || 0) * 60);
}

// Attachment previews (D99): only these four kinds get a native-element
// preview (img / iframe for pdf+text / audio / video); everything else is
// download-only.
function attachmentPreviewKind(contentType) {
  if (contentType.startsWith('image/')) return 'image';
  if (contentType === 'application/pdf') return 'pdf';
  if (contentType.startsWith('audio/')) return 'audio';
  if (contentType.startsWith('video/')) return 'video';
  if (contentType.startsWith('text/')) return 'text';
  return null;
}

function attachmentIcon(contentType) {
  const kind = attachmentPreviewKind(contentType);
  return { image: '🖼', pdf: '📄', audio: '🎵', video: '🎬', text: '📝' }[kind] || '📎';
}

function formatByteSize(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

// D101: "sortable list (name/size/date/author/type)" -- a client-side
// concern, not a server-side ordering option (the server always returns
// oldest-first).
function sortAttachments(attachments, sortBy) {
  const sorted = [...attachments];
  const comparators = {
    name: (a, b) => a.fileName.localeCompare(b.fileName),
    size: (a, b) => a.byteSize - b.byteSize,
    date: (a, b) => a.createdAt.localeCompare(b.createdAt),
    author: (a, b) => a.uploader.displayName.localeCompare(b.uploader.displayName),
    type: (a, b) => a.contentType.localeCompare(b.contentType)
  };
  sorted.sort(comparators[sortBy] || comparators.date);
  return sorted;
}

// Uploads via a real multipart/form-data POST, bypassing the shared api()
// helper (which always forces a JSON Content-Type) so the browser can set
// its own multipart boundary.
async function uploadAttachmentFile(ticketKey, file) {
  const formData = new FormData();
  formData.append('file', file);
  const csrfToken = getCookie('th_csrf');
  const response = await fetch(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/attachments`, {
    method: 'POST',
    headers: csrfToken ? { 'X-CSRF-Token': csrfToken } : {},
    body: formData
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(payload.error || `Upload failed (${response.status})`);
  return payload;
}

function getCookie(name) {
  const match = document.cookie.match(new RegExp(`(?:^|; )${name}=([^;]*)`));
  return match ? decodeURIComponent(match[1]) : null;
}

async function api(path, options = {}) {
  const method = (options.method || 'GET').toUpperCase();
  const headers = { 'Content-Type': 'application/json', ...(options.headers || {}) };
  if (method !== 'GET' && method !== 'HEAD') {
    const csrfToken = getCookie('th_csrf');
    if (csrfToken) headers['X-CSRF-Token'] = csrfToken;
  }
  const response = await fetch(path, { headers, ...options });
  if (response.status === 401 && path !== '/api/v1/auth/login' && path !== '/api/v1/auth/me') {
    showLoginScreen();
    throw new Error('Your session expired. Please sign in again.');
  }
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    const error = new Error(payload.error || `Request failed (${response.status})`);
    // D129: lets call sites special-case a 409 optimistic-lock conflict
    // (stale expectedVersion) with a reload/reapply dialog instead of the
    // generic error toast every other failure gets.
    error.status = response.status;
    throw error;
  }
  return payload;
}

function showToast(message) {
  toast.textContent = message;
  toast.classList.remove('hidden');
  window.setTimeout(() => toast.classList.add('hidden'), 2600);
}

// Resets all navigation/data state (view, selected project, filters,
// cached lists) back to defaults, not just the principal -- otherwise a
// second user logging into the same browser tab lands on whatever
// tab/project/filters the previous user last had open, which can reference
// a project the new user has no access to or that no longer exists.
function showLoginScreen() {
  Object.assign(state, initialState());
  document.querySelectorAll('.nav-item').forEach(item => item.classList.toggle('active', item.dataset.view === 'dashboard'));
  document.querySelector('#nav-audit').classList.add('hidden');
  document.querySelector('#nav-attachment-bin').classList.add('hidden');
  document.querySelector('#nav-webhooks').classList.add('hidden');
  appShell.classList.add('hidden');
  loginScreen.classList.remove('hidden');
  loginForm.querySelector('input[name="email"]').focus();
}

function showAppShell() {
  loginScreen.classList.add('hidden');
  appShell.classList.remove('hidden');
}

function renderCurrentUser() {
  const principal = state.principal;
  if (!principal) return;
  document.querySelector('#current-user-avatar').textContent = initials(principal.displayName);
  document.querySelector('#current-user-name').textContent = principal.displayName;
  document.querySelector('#current-user-email').textContent = principal.email;
  // The audit log (D23) and the attachment recycle bin (D101/D102) are both
  // global-administrator-only, like the ticket/project recycle bins.
  document.querySelector('#nav-audit').classList.toggle('hidden', !principal.isAdmin);
  document.querySelector('#nav-attachment-bin').classList.toggle('hidden', !principal.isAdmin);
  // Webhooks (D39/D41): global-admin-only, same installation-level
  // administration tier as the audit log/attachment recycle bin.
  document.querySelector('#nav-webhooks').classList.toggle('hidden', !principal.isAdmin);
}

// D45's "browser auto-detect" is a per-browser concern, not a per-account
// one: the server has no "this value was never explicitly chosen" sentinel
// (see the comment on Domain::User::timeZone), so this flag is tracked
// client-side instead, in this browser's localStorage. Once set (by saving
// the preferences form, even with the same values auto-detect would have
// picked), auto-detect never silently overwrites the account's timezone in
// this browser again -- a deliberate "I want UTC" choice is respected.
const PreferencesManualFlag = 'th-preferences-manual';

function markPreferencesManuallySet() {
  try {
    localStorage.setItem(PreferencesManualFlag, '1');
  } catch {
    // Private browsing / storage disabled: auto-detect may re-run on a
    // later visit, which is a harmless degrade, not a broken feature.
  }
}

async function autoDetectPreferencesIfNeeded() {
  if (!state.principal) return;
  try {
    if (localStorage.getItem(PreferencesManualFlag) === '1') return;
  } catch {
    // Treat inaccessible storage as "never manually set" and proceed.
  }
  let detected;
  try {
    detected = Intl.DateTimeFormat().resolvedOptions().timeZone;
  } catch {
    return;
  }
  if (!detected || detected === state.principal.timeZone) return;
  try {
    state.principal = await api('/api/v1/account/preferences', {
      method: 'PATCH',
      body: JSON.stringify({ timeZone: detected, clockFormat: state.principal.clockFormat })
    });
  } catch {
    // Best-effort: a failed auto-detect just leaves the account on
    // whatever it already had (UTC/24h for a brand-new account).
  }
}

// Checks the existing session cookie (if any) without ever showing the
// generic "session expired" error -- this is the initial, silent probe.
async function checkExistingSession() {
  try {
    state.principal = await api('/api/v1/auth/me');
    await autoDetectPreferencesIfNeeded();
    return true;
  } catch {
    return false;
  }
}

function showError(error) {
  content.innerHTML = `<div class="error-banner"><strong>Ticket Hub could not load this view.</strong><br>${escapeHtml(error.message || error)}</div>`;
}

function statusChip(ticket) {
  return `<span class="status-chip ${escapeHtml(ticket.status.category)}">${escapeHtml(ticket.status.name)}</span>`;
}

function priorityChip(ticket) {
  return `<span class="priority-chip"><span style="color:${escapeHtml(ticket.priority.color)}">▲</span>${escapeHtml(ticket.priority.name)}</span>`;
}

function assigneeMarkup(ticket) {
  if (!ticket.assignee) return '<span class="assignee-cell">Unassigned</span>';
  return `<span class="assignee-cell"><span class="small-avatar">${escapeHtml(initials(ticket.assignee.displayName))}</span>${escapeHtml(ticket.assignee.displayName)}</span>`;
}

function labelsMarkup(labels = []) {
  return labels.slice(0, 3).map(label => `<span class="label-chip">${escapeHtml(label)}</span>`).join('');
}

// `orderable` adds an Order column with move-up/move-down buttons (D31);
// only meaningful when `tickets` is a single project's full, rank-sorted
// list, since reorderTicket's `beforeTicketKey` anchor must be in the same
// project as the ticket being moved. `selectable` adds a checkbox column for
// bulk actions (D36).
function ticketRows(tickets, { orderable = false, selectable = false, showTimestamps = true } = {}) {
  const extraColumns = (orderable ? 1 : 0) + (selectable ? 1 : 0) + (showTimestamps ? 2 : 0);
  if (!tickets.length) {
    return `<tr><td colspan="${6 + extraColumns}"><div class="empty-state"><strong>No tickets found</strong>Adjust the filters or create a new ticket.</div></td></tr>`;
  }
  return tickets.map((ticket, index) => `
    <tr data-ticket-key="${escapeHtml(ticket.key)}">
      ${selectable ? `<td class="select-column"><input type="checkbox" class="ticket-select" value="${escapeHtml(ticket.key)}" aria-label="Select ${escapeHtml(ticket.key)}"></td>` : ''}
      <td><span class="ticket-type" title="${escapeHtml(ticket.type.name)}"><span style="color:${escapeHtml(ticket.type.color)}">${escapeHtml(ticket.type.icon)}</span>${escapeHtml(ticket.type.name)}</span></td>
      <td><span class="ticket-key">${escapeHtml(ticket.key)}</span></td>
      <td class="ticket-summary">${escapeHtml(ticket.summary)}</td>
      <td>${statusChip(ticket)}</td>
      <td>${priorityChip(ticket)}</td>
      <td>${assigneeMarkup(ticket)}</td>
      ${showTimestamps ? `<td class="ticket-timestamp">${escapeHtml(formatDate(ticket.createdAt))}</td>
      <td class="ticket-timestamp" title="${escapeHtml(ticket.updatedAt)}">${escapeHtml(relativeDate(ticket.updatedAt))}</td>` : ''}
      ${orderable ? `<td class="order-cell">
        <button type="button" class="icon-button" data-move-up="${escapeHtml(ticket.key)}" ${index === 0 ? 'disabled' : ''} aria-label="Move up">↑</button>
        <button type="button" class="icon-button" data-move-down="${escapeHtml(ticket.key)}" ${index === tickets.length - 1 ? 'disabled' : ''} aria-label="Move down">↓</button>
      </td>` : ''}
    </tr>`).join('');
}

function deletedTicketRows(tickets) {
  if (!tickets.length) {
    return `<tr><td colspan="7"><div class="empty-state"><strong>The recycle bin is empty</strong></div></td></tr>`;
  }
  return tickets.map(ticket => `
    <tr>
      <td><span class="ticket-type" title="${escapeHtml(ticket.type.name)}"><span style="color:${escapeHtml(ticket.type.color)}">${escapeHtml(ticket.type.icon)}</span>${escapeHtml(ticket.type.name)}</span></td>
      <td><span class="ticket-key">${escapeHtml(ticket.key)}</span></td>
      <td class="ticket-summary">${escapeHtml(ticket.summary)}</td>
      <td>${statusChip(ticket)}</td>
      <td>${priorityChip(ticket)}</td>
      <td>${assigneeMarkup(ticket)}</td>
      <td><div class="project-card-actions"><button type="button" class="secondary-button" data-restore-ticket="${escapeHtml(ticket.key)}">Restore</button><button type="button" class="secondary-button" data-permanent-ticket="${escapeHtml(ticket.key)}">Delete permanently</button></div></td>
    </tr>`).join('');
}

function tablePanel(tickets, title = 'Tickets') {
  return `
    <div class="panel">
      <div class="panel-header"><h2>${escapeHtml(title)}</h2><span class="eyebrow">${tickets.length} shown</span></div>
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr><th>Type</th><th>Key</th><th>Summary</th><th>Status</th><th>Priority</th><th>Assignee</th></tr></thead>
          <tbody>${ticketRows(tickets, { showTimestamps: false })}</tbody>
        </table>
      </div>
    </div>`;
}

// Dashboard-only deadlines widget (D24): same shape as tablePanel but with
// a Due date column, since that's the one thing this particular list is
// sorted and shown for.
function deadlinesPanel(tickets, title = 'Upcoming deadlines') {
  const rows = tickets.length ? tickets.map(ticket => `
    <tr data-ticket-key="${escapeHtml(ticket.key)}">
      <td><span class="ticket-type" title="${escapeHtml(ticket.type.name)}"><span style="color:${escapeHtml(ticket.type.color)}">${escapeHtml(ticket.type.icon)}</span>${escapeHtml(ticket.type.name)}</span></td>
      <td><span class="ticket-key">${escapeHtml(ticket.key)}</span></td>
      <td class="ticket-summary">${escapeHtml(ticket.summary)}</td>
      <td>${statusChip(ticket)}</td>
      <td>${escapeHtml(formatDate(ticket.dueDate))}</td>
    </tr>`).join('') : '<tr><td colspan="5"><div class="empty-state">No upcoming deadlines.</div></td></tr>';
  return `
    <div class="panel">
      <div class="panel-header"><h2>${escapeHtml(title)}</h2><span class="eyebrow">${tickets.length} shown</span></div>
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr><th>Type</th><th>Key</th><th>Summary</th><th>Status</th><th>Due date</th></tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </div>
    </div>`;
}

function pageHeader(title, subtitle, eyebrow = 'Ticket Hub') {
  return `<div class="page-header"><div><span class="eyebrow">${escapeHtml(eyebrow)}</span><h1>${escapeHtml(title)}</h1><p>${escapeHtml(subtitle)}</p></div></div>`;
}

async function loadBaseData() {
  const [health, projects, users] = await Promise.all([api('/api/health'), api('/api/v1/projects'), api('/api/v1/users')]);
  state.projects = projects.items;
  state.selectedProject ||= state.projects[0]?.key || null;
  state.users = users.items;
  document.querySelector('#backend-pill').textContent = health.database;
  renderProjectSelectors();
  await refreshNotificationBadge();
  await refreshUpdateBanner();
}

// In-app admin version banner (D112): "simple ... banner when a newer
// version is available; no email delivery." Global-administrator-only
// endpoint, so a non-admin's request 403s -- silently treated as "no
// banner" rather than surfaced as an error, since a non-admin has nothing
// to act on here anyway.
async function refreshUpdateBanner() {
  const banner = document.querySelector('#update-banner');
  if (!state.principal?.isAdmin) {
    banner.classList.add('hidden');
    return;
  }
  try {
    const status = await api('/api/v1/settings/latest-known-version');
    if (status.updateAvailable) {
      banner.textContent = `A newer Ticket Hub version is available: ${status.latestKnownVersion} (currently running ${status.currentVersion}). Run 'ticket-hub migrate' after upgrading.`;
      banner.classList.remove('hidden');
    } else {
      banner.classList.add('hidden');
    }
  } catch (error) {
    banner.classList.add('hidden');
  }
}

// --- Fixed in-app notifications (D14) ---

async function refreshNotificationBadge() {
  const { count } = await api('/api/v1/notifications/unread-count');
  const badge = document.querySelector('#notification-badge');
  badge.textContent = count > 99 ? '99+' : String(count);
  badge.classList.toggle('hidden', count === 0);
}

function notificationSummary(notification) {
  const ticketRef = notification.ticketKey
    ? `${notification.ticketKey}${notification.ticketSummary ? ` — ${notification.ticketSummary}` : ''}`
    : 'a ticket';
  if (notification.type === 'assigned') return `You were assigned ${ticketRef}`;
  if (notification.type === 'mentioned') return `You were mentioned on ${ticketRef}`;
  if (notification.type === 'watched_comment') return `New comment on ${ticketRef}`;
  return ticketRef;
}

async function renderNotificationPanel() {
  const panel = document.querySelector('#notification-panel');
  panel.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const { items } = await api('/api/v1/notifications');
  panel.innerHTML = `
    <div class="notification-panel-header">
      <strong>Notifications</strong>
      <button type="button" class="ghost-button" id="notification-mark-all-read">Mark all read</button>
    </div>
    ${items.length
      ? items.map(notification => `
        <button type="button" class="notification-item ${notification.readAt ? '' : 'unread'}" data-notification-id="${escapeHtml(notification.id)}" data-ticket-key="${escapeHtml(notification.ticketKey || '')}">
          <span class="notification-type">${escapeHtml(notification.type.replace('_', ' '))}</span>
          <span class="notification-summary">${escapeHtml(notificationSummary(notification))}</span>
          <span class="notification-time">${escapeHtml(relativeDate(notification.createdAt))}</span>
        </button>`).join('')
      : '<div class="empty-state">No notifications yet.</div>'}`;

  panel.querySelector('#notification-mark-all-read')?.addEventListener('click', async () => {
    await api('/api/v1/notifications/read-all', { method: 'POST' });
    await refreshNotificationBadge();
    await renderNotificationPanel();
  });
  panel.querySelectorAll('[data-notification-id]').forEach(item => item.addEventListener('click', async () => {
    await api(`/api/v1/notifications/${encodeURIComponent(item.dataset.notificationId)}/read`, { method: 'POST' });
    panel.classList.add('hidden');
    await refreshNotificationBadge();
    if (item.dataset.ticketKey) {
      await openTicket(item.dataset.ticketKey);
    }
  }));
}

document.querySelector('#notification-bell').addEventListener('click', async event => {
  event.stopPropagation();
  const panel = document.querySelector('#notification-panel');
  const opening = panel.classList.contains('hidden');
  panel.classList.toggle('hidden');
  if (opening) {
    await renderNotificationPanel();
  }
});
document.addEventListener('click', event => {
  const panel = document.querySelector('#notification-panel');
  if (!panel.classList.contains('hidden') && !panel.contains(event.target) && event.target.id !== 'notification-bell') {
    panel.classList.add('hidden');
  }
});

// --- @mention autocomplete (D80) ---
// Deliberately simple: a dropdown anchored below the textarea (not
// cursor-positioned) listing up to 5 handle matches for the "@partial"
// token immediately before the caret. Works against the already-cached
// state.users directory -- no per-keystroke network request.
function attachMentionAutocomplete(textarea) {
  const wrapper = document.createElement('div');
  wrapper.className = 'mention-autocomplete-list hidden';
  textarea.insertAdjacentElement('afterend', wrapper);

  function currentToken() {
    const caret = textarea.selectionStart;
    const before = textarea.value.slice(0, caret);
    const match = before.match(/@([a-zA-Z0-9_]{1,32})$/);
    return match ? match[1].toLowerCase() : null;
  }

  function renderMatches() {
    const token = currentToken();
    if (token === null) {
      wrapper.classList.add('hidden');
      wrapper.innerHTML = '';
      return;
    }
    const matches = state.users
      .filter(user => user.handle && user.handle.startsWith(token))
      .slice(0, 5);
    if (!matches.length) {
      wrapper.classList.add('hidden');
      wrapper.innerHTML = '';
      return;
    }
    wrapper.innerHTML = matches.map(user =>
      `<button type="button" class="mention-autocomplete-item" data-handle="${escapeHtml(user.handle)}">@${escapeHtml(user.handle)} — ${escapeHtml(user.displayName)}</button>`
    ).join('');
    wrapper.classList.remove('hidden');
    wrapper.querySelectorAll('[data-handle]').forEach(button => button.addEventListener('mousedown', event => {
      event.preventDefault(); // keep textarea focus/selection valid through the click
      const caret = textarea.selectionStart;
      const before = textarea.value.slice(0, caret).replace(/@([a-zA-Z0-9_]{1,32})$/, `@${button.dataset.handle} `);
      textarea.value = before + textarea.value.slice(caret);
      textarea.focus();
      wrapper.classList.add('hidden');
    }));
  }

  textarea.addEventListener('input', renderMatches);
  textarea.addEventListener('blur', () => window.setTimeout(() => wrapper.classList.add('hidden'), 150));
}

function renderProjectSelectors() {
  const shortcuts = document.querySelector('#sidebar-projects');
  shortcuts.innerHTML = state.projects.map(project => `
    <button class="project-shortcut" data-project="${escapeHtml(project.key)}">
      <span class="project-avatar">${escapeHtml(project.key.slice(0, 2))}</span>
      <span>${escapeHtml(project.name)}</span>
    </button>`).join('');

  const select = document.querySelector('#create-project');
  select.innerHTML = state.projects.map(project => `<option value="${escapeHtml(project.key)}">${escapeHtml(project.key)} — ${escapeHtml(project.name)}</option>`).join('');
  if (state.selectedProject) select.value = state.selectedProject;
}

async function renderDashboard() {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  state.dashboard = await api('/api/v1/dashboard');
  const stats = state.dashboard;
  content.innerHTML = `
    ${pageHeader('Dashboard', 'A focused overview of work across all projects.', 'Workspace')}
    <div class="stats-grid">
      <div class="stat-card"><div class="stat-label">All tickets</div><div class="stat-value">${stats.totalTickets}</div><div class="stat-meta">Across ${state.projects.length} projects</div></div>
      <div class="stat-card"><div class="stat-label">To do</div><div class="stat-value">${stats.todoTickets}</div><div class="stat-meta">Backlog and selected work</div></div>
      <div class="stat-card"><div class="stat-label">In progress</div><div class="stat-value">${stats.inProgressTickets}</div><div class="stat-meta">Active and in review</div></div>
      <div class="stat-card"><div class="stat-label">Done</div><div class="stat-value">${stats.doneTickets}</div><div class="stat-meta">Completed work</div></div>
    </div>
    ${state.principal ? tablePanel(stats.assignedToMe, 'Assigned to me') : ''}
    ${state.principal ? tablePanel(stats.watchedTickets, 'Tickets I’m watching') : ''}
    ${state.principal ? deadlinesPanel(stats.upcomingDeadlines) : ''}
    ${tablePanel(stats.recentTickets, 'Recently active tickets')}`;
  bindTicketLinks();
}

// Simple append-only admin/security audit log (D23): global-administrator-
// only, read-only, no filtering/export/pagination -- just the newest 200
// events the server already caps the response to.
const AuditPageSize = 50;

// Numbered/offset pagination (extends D126): the audit log is append-only
// forever and installation-wide, so it has no natural upper bound the way a
// single ticket's comment/worklog list does -- paged the same way the
// Backlog screen paginates a project's unbounded backlog.
async function renderAuditLog() {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const page = await api(`/api/v1/admin/audit-events?page=${state.auditPage}&pageSize=${AuditPageSize}`);
  const items = page.items;
  const totalPages = Math.max(page.totalPages, 1);
  content.innerHTML = `
    ${pageHeader('Audit log', 'Append-only record of admin and security events. Never purged or exported.', 'Administration')}
    <div class="panel">
      <div class="panel-header"><h2>Events</h2><span class="eyebrow">${page.totalItems} total</span></div>
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr><th>When</th><th>Category</th><th>Action</th><th>Actor</th><th>Target</th><th>Details</th></tr></thead>
          <tbody>${items.length ? items.map(event => `
            <tr>
              <td>${escapeHtml(relativeDate(event.createdAt))}</td>
              <td><span class="label-chip">${escapeHtml(event.category)}</span></td>
              <td>${escapeHtml(event.action)}</td>
              <td>${event.actor ? escapeHtml(event.actor.displayName) : '<span class="assignee-cell">System</span>'}</td>
              <td>${event.targetType ? `${escapeHtml(event.targetType)}${event.targetId ? `: ${escapeHtml(event.targetId)}` : ''}` : ''}</td>
              <td>${event.details ? escapeHtml(event.details) : ''}</td>
            </tr>`).join('') : '<tr><td colspan="6"><div class="empty-state">No audit events recorded yet.</div></td></tr>'}
          </tbody>
        </table>
      </div>
      <div class="pagination-bar">
        <span>${page.totalItems} event${page.totalItems === 1 ? '' : 's'} — page ${page.page} of ${totalPages}</span>
        <div>
          <button type="button" class="secondary-button" id="audit-prev" ${page.page <= 1 ? 'disabled' : ''}>← Previous</button>
          <button type="button" class="secondary-button" id="audit-next" ${page.page >= totalPages ? 'disabled' : ''}>Next →</button>
        </div>
      </div>
    </div>`;
  document.querySelector('#audit-prev').addEventListener('click', () => {
    state.auditPage = Math.max(1, state.auditPage - 1);
    renderAuditLog().catch(showError);
  });
  document.querySelector('#audit-next').addEventListener('click', () => {
    state.auditPage = state.auditPage + 1;
    renderAuditLog().catch(showError);
  });
}

const WEBHOOK_EVENT_TYPES = [
  { key: 'ticket.created', name: 'Ticket created' },
  { key: 'ticket.status_changed', name: 'Ticket status changed' },
  { key: 'ticket.updated', name: 'Ticket updated' },
  { key: 'comment.added', name: 'Comment added' },
];

// Outbound webhooks (D39/D41, deferred-after-V1, user-requested):
// global-administrator-only, like the audit log. The signing secret is
// shown exactly once, right after creation (matching how personal access
// tokens already work on the Account screen) -- never fetchable again
// afterward, so it is shown as a dismissible banner rather than a modal
// the admin might close before copying it.
async function renderWebhooks(revealSecret = null) {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const { items: subscriptions } = await api('/api/v1/webhooks');
  content.innerHTML = `
    ${pageHeader('Webhooks', 'Outbound event notifications to external systems. Signed with HMAC-SHA256; delivery is retried by a cron-scheduled ticket-hub-cli process-outbox, not the server itself.', 'Administration')}
    <div id="webhook-secret-banner">${revealSecret ? `
      <div class="panel" style="padding:16px;margin-bottom:16px;border-color:var(--primary)">
        <strong>Signing secret (shown once — copy it now):</strong>
        <div style="font-family:monospace;word-break:break-all;margin-top:6px;user-select:all">${escapeHtml(revealSecret)}</div>
      </div>` : ''}</div>
    <div class="panel">
      <div class="panel-header"><h2>Subscriptions</h2><span class="eyebrow">${subscriptions.length} configured</span></div>
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr><th>Target URL</th><th>Events</th><th>Project</th><th>Created</th><th>Actions</th></tr></thead>
          <tbody>${subscriptions.length ? subscriptions.map(subscription => `
            <tr>
              <td style="max-width:320px;overflow-wrap:break-word">${escapeHtml(subscription.targetUrl)}</td>
              <td>${subscription.eventTypes.length ? subscription.eventTypes.map(eventType => `<span class="label-chip">${escapeHtml(eventType)}</span>`).join(' ') : '<span class="assignee-cell">All events</span>'}</td>
              <td>${subscription.projectKey ? escapeHtml(subscription.projectKey) : '<span class="assignee-cell">All projects</span>'}</td>
              <td class="ticket-timestamp">${escapeHtml(relativeDate(subscription.createdAt))}</td>
              <td><button type="button" class="secondary-button" data-delete-webhook="${escapeHtml(subscription.id)}">Delete</button></td>
            </tr>`).join('') : '<tr><td colspan="5"><div class="empty-state">No webhook subscriptions yet.</div></td></tr>'}
          </tbody>
        </table>
      </div>
      <form class="form-grid" id="webhook-add-form" style="margin-top:16px">
        <label class="wide">Target URL<input name="targetUrl" type="url" required placeholder="https://example.com/hooks/ticket-hub"></label>
        <label>Project filter
          <select name="projectKey">
            <option value="">All projects</option>
            ${state.projects.map(project => `<option value="${escapeHtml(project.key)}">${escapeHtml(project.key)} — ${escapeHtml(project.name)}</option>`).join('')}
          </select>
        </label>
        <div class="wide">
          <span style="display:block;margin-bottom:6px;font-size:12px;color:var(--muted)">Events (none checked = every event)</span>
          ${WEBHOOK_EVENT_TYPES.map(eventType => `<label style="display:inline-flex;align-items:center;gap:6px;margin-right:16px;font-weight:400"><input type="checkbox" name="eventTypes" value="${eventType.key}" style="width:auto">${escapeHtml(eventType.name)}</label>`).join('')}
        </div>
      </form>
      <div id="webhook-error" class="form-error hidden"></div>
      <div class="modal-footer" style="padding:16px 0 0">
        <button type="submit" form="webhook-add-form" class="primary-button">Add webhook</button>
      </div>
    </div>`;

  document.querySelectorAll('[data-delete-webhook]').forEach(button => button.addEventListener('click', async () => {
    try {
      await api(`/api/v1/webhooks/${encodeURIComponent(button.dataset.deleteWebhook)}`, { method: 'DELETE' });
      showToast('Webhook deleted');
      await renderWebhooks();
    } catch (error) { showToast(error.message); }
  }));
  document.querySelector('#webhook-add-form').addEventListener('submit', async event => {
    event.preventDefault();
    const form = event.currentTarget;
    const formData = new FormData(form);
    const errorElement = document.querySelector('#webhook-error');
    try {
      const created = await api('/api/v1/webhooks', {
        method: 'POST',
        body: JSON.stringify({
          targetUrl: formData.get('targetUrl').trim(),
          projectKey: formData.get('projectKey') || null,
          eventTypes: formData.getAll('eventTypes'),
        }),
      });
      showToast('Webhook added');
      await renderWebhooks(created.secret);
    } catch (error) {
      errorElement.textContent = error.message;
      errorElement.classList.remove('hidden');
    }
  });
}

// Attachment recycle bin (D101/D102): global-administrator-only, fixed
// 90-day on-demand retention (checked server-side on every fetch, not a
// background job). Spans every ticket, so each row shows the ticket key
// (resolved server-side via a join) rather than requiring the admin to
// already be looking at a specific ticket.
async function renderAttachmentRecycleBin() {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const { items } = await api('/api/v1/attachments/deleted');
  content.innerHTML = `
    ${pageHeader('Attachment recycle bin', 'Deleted attachments, retained for 90 days.', 'Administration')}
    <div class="panel">
      <div class="panel-header"><h2>Deleted attachments</h2><span class="eyebrow">${items.length} shown</span></div>
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr><th>File</th><th>Ticket</th><th>Uploader</th><th>Deleted</th><th>Actions</th></tr></thead>
          <tbody>${items.length ? items.map(attachment => `
            <tr>
              <td>${attachmentIcon(attachment.contentType)} ${escapeHtml(attachment.fileName)}</td>
              <td><span class="ticket-key" data-ticket-key="${escapeHtml(attachment.ticketKey)}">${escapeHtml(attachment.ticketKey)}</span></td>
              <td>${escapeHtml(attachment.uploader.displayName)}</td>
              <td>${escapeHtml(relativeDate(attachment.createdAt))}</td>
              <td><div class="project-card-actions"><button type="button" class="secondary-button" data-restore-attachment="${escapeHtml(attachment.id)}">Restore</button><button type="button" class="secondary-button" data-permanent-attachment="${escapeHtml(attachment.id)}">Delete permanently</button></div></td>
            </tr>`).join('') : '<tr><td colspan="5"><div class="empty-state">The attachment recycle bin is empty.</div></td></tr>'}
          </tbody>
        </table>
      </div>
    </div>`;
  bindTicketLinks();
  document.querySelectorAll('[data-restore-attachment]').forEach(button => button.addEventListener('click', async () => {
    try {
      await api(`/api/v1/attachments/${encodeURIComponent(button.dataset.restoreAttachment)}/restore`, { method: 'POST' });
      showToast('Attachment restored');
      await renderAttachmentRecycleBin();
    } catch (error) { showToast(error.message); }
  }));
  document.querySelectorAll('[data-permanent-attachment]').forEach(button => button.addEventListener('click', async () => {
    try {
      await api(`/api/v1/attachments/${encodeURIComponent(button.dataset.permanentAttachment)}/permanent`, { method: 'DELETE' });
      showToast('Attachment permanently deleted');
      await renderAttachmentRecycleBin();
    } catch (error) { showToast(error.message); }
  }));
}

// Holds a just-created token's raw value across the single re-render that
// follows creating it -- the server only ever returns the raw value once
// (D40), so this is the one and only chance the UI has to show it. Cleared
// as soon as it's read, so refreshing or navigating away never re-shows it.
let revealedToken = null;

// Account settings (Phase 6, D39/D40/D54): personal access tokens and
// active sessions. Unlike the audit log/attachment recycle bin, this view
// is visible to every authenticated user, not just admins -- both
// resources are scoped to the caller's own account, not installation-wide.
async function renderAccountView() {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const [{ items: tokens }, { items: sessions }] = await Promise.all([
    api('/api/v1/tokens'),
    api('/api/v1/sessions')
  ]);
  const reveal = revealedToken;
  revealedToken = null;
  content.innerHTML = `
    ${pageHeader('Account', 'Manage your preferences, personal access tokens, and active sessions.', 'Account')}
    <div class="panel">
      <div class="panel-header"><h2>Preferences</h2></div>
      <form class="form-grid" id="preferences-form">
        <label>Time zone
          <input name="timeZone" value="${escapeHtml(state.principal?.timeZone || 'UTC')}" placeholder="e.g. Europe/Prague" list="timezone-options">
          <datalist id="timezone-options">${timezoneDatalistOptions()}</datalist>
        </label>
        <label>Clock format
          <select name="clockFormat">
            <option value="24h" ${state.principal?.clockFormat !== '12h' ? 'selected' : ''}>24-hour</option>
            <option value="12h" ${state.principal?.clockFormat === '12h' ? 'selected' : ''}>12-hour</option>
          </select>
        </label>
        <div class="wide modal-footer" style="padding:0">
          <button type="button" class="secondary-button" id="detect-timezone-button">Detect from browser</button>
          <button type="submit" class="primary-button">Save preferences</button>
        </div>
      </form>
      <div id="preferences-error" class="form-error hidden"></div>
    </div>
    ${reveal ? `
    <div class="panel token-reveal-panel">
      <strong>Copy your new token now -- it will not be shown again.</strong>
      <div class="token-reveal-value"><code id="revealed-token-value">${escapeHtml(reveal.token)}</code><button type="button" class="secondary-button" id="copy-token-button">Copy</button></div>
    </div>` : ''}
    <div class="panel">
      <div class="panel-header"><h2>Personal access tokens</h2><button type="button" class="primary-button" id="new-token-button">＋ New token</button></div>
      <form class="form-grid hidden" id="token-form">
        <label>Name<input name="name" required maxlength="255" placeholder="e.g. laptop CI script"></label>
        <label>Expires in (days)<input name="expiresInDays" type="number" min="1" max="365" required value="90"></label>
        <div class="wide modal-footer" style="padding:0"><button type="button" class="secondary-button" id="cancel-token-button">Cancel</button><button type="submit" class="primary-button">Create token</button></div>
      </form>
      <div id="token-error" class="form-error hidden"></div>
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr><th>Name</th><th>Created</th><th>Expires</th><th>Last used</th><th>Status</th><th></th></tr></thead>
          <tbody>${tokens.length ? tokens.map(token => `
            <tr>
              <td>${escapeHtml(token.name)}</td>
              <td>${escapeHtml(relativeDate(token.createdAt))}</td>
              <td>${escapeHtml(formatDate(token.expiresAt))}</td>
              <td>${token.lastUsedAt ? escapeHtml(relativeDate(token.lastUsedAt)) : 'Never used'}</td>
              <td>${token.revokedAt ? '<span class="status-chip todo">Revoked</span>' : '<span class="status-chip done">Active</span>'}</td>
              <td>${token.revokedAt ? '' : `<button type="button" class="secondary-button" data-revoke-token="${escapeHtml(token.id)}">Revoke</button>`}</td>
            </tr>`).join('') : '<tr><td colspan="6"><div class="empty-state">No personal access tokens yet.</div></td></tr>'}
          </tbody>
        </table>
      </div>
    </div>
    <div class="panel">
      <div class="panel-header"><h2>Active sessions</h2>${sessions.length > 1 ? '<button type="button" class="secondary-button" id="sign-out-others-button">Sign out everywhere else</button>' : ''}</div>
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr><th>Signed in</th><th>Expires</th><th></th></tr></thead>
          <tbody>${sessions.map(session => `
            <tr>
              <td>${escapeHtml(relativeDate(session.createdAt))}</td>
              <td>${escapeHtml(formatDate(session.expiresAt))}</td>
              <td>${session.isCurrent ? '<span class="status-chip done">This device</span>' : ''}</td>
            </tr>`).join('')}
          </tbody>
        </table>
      </div>
    </div>`;

  document.querySelector('#detect-timezone-button').addEventListener('click', () => {
    try {
      document.querySelector('#preferences-form input[name="timeZone"]').value = Intl.DateTimeFormat().resolvedOptions().timeZone;
    } catch {
      // No Intl timezone support in this browser -- leave the field as-is.
    }
  });
  document.querySelector('#preferences-form').addEventListener('submit', async event => {
    event.preventDefault();
    const form = event.target;
    const errorBox = document.querySelector('#preferences-error');
    errorBox.classList.add('hidden');
    try {
      state.principal = await api('/api/v1/account/preferences', {
        method: 'PATCH',
        body: JSON.stringify({
          timeZone: form.elements.timeZone.value.trim(),
          clockFormat: form.elements.clockFormat.value
        })
      });
      markPreferencesManuallySet();
      showToast('Preferences saved');
      await renderAccountView();
    } catch (error) {
      errorBox.textContent = error.message;
      errorBox.classList.remove('hidden');
    }
  });
  document.querySelector('#new-token-button').addEventListener('click', () => {
    document.querySelector('#token-form').classList.remove('hidden');
    document.querySelector('#token-form input[name="name"]').focus();
  });
  document.querySelector('#cancel-token-button').addEventListener('click', () => {
    document.querySelector('#token-form').classList.add('hidden');
  });
  document.querySelector('#token-form').addEventListener('submit', async event => {
    event.preventDefault();
    const form = event.target;
    const errorBox = document.querySelector('#token-error');
    errorBox.classList.add('hidden');
    try {
      const created = await api('/api/v1/tokens', {
        method: 'POST',
        body: JSON.stringify({
          name: form.elements.name.value,
          expiresInDays: Number(form.elements.expiresInDays.value)
        })
      });
      revealedToken = { token: created.token };
      showToast('Token created');
      await renderAccountView();
    } catch (error) {
      errorBox.textContent = error.message;
      errorBox.classList.remove('hidden');
    }
  });
  document.querySelectorAll('[data-revoke-token]').forEach(button => button.addEventListener('click', async () => {
    try {
      await api(`/api/v1/tokens/${encodeURIComponent(button.dataset.revokeToken)}`, { method: 'DELETE' });
      showToast('Token revoked');
      await renderAccountView();
    } catch (error) { showToast(error.message); }
  }));
  document.querySelector('#sign-out-others-button')?.addEventListener('click', async () => {
    try {
      const result = await api('/api/v1/sessions/sign-out-others', { method: 'POST' });
      showToast(`Signed out ${result.signedOutCount} other session(s)`);
      await renderAccountView();
    } catch (error) { showToast(error.message); }
  });
  if (reveal) {
    document.querySelector('#copy-token-button')?.addEventListener('click', async () => {
      try {
        await navigator.clipboard.writeText(reveal.token);
        showToast('Token copied to clipboard');
      } catch {
        showToast('Could not copy automatically -- select and copy manually');
      }
    });
  }
}

function ticketFilterParams() {
  const params = new URLSearchParams();
  if (state.selectedProject) params.set('project', state.selectedProject);
  if (state.status) params.set('status', state.status);
  if (state.filterType) params.set('type', state.filterType);
  if (state.filterPriority) params.set('priority', state.filterPriority);
  if (state.filterAssignee) params.set('assignee', state.filterAssignee);
  if (state.filterLabel) params.set('label', state.filterLabel);
  if (state.filterComponent) params.set('component', state.filterComponent);
  if (state.filterDueBefore) params.set('dueBefore', state.filterDueBefore);
  if (state.search) params.set('q', state.search);
  return params;
}

async function fetchTickets() {
  const result = await api(`/api/v1/tickets?${ticketFilterParams()}`);
  state.tickets = result.items;
}

async function renderTickets() {
  await renderTicketsView(false);
}

// Mirrors renderProjectsView's active/recycle-bin toggle: `showingDeleted`
// swaps the filter bar and normal ticket table for the recycle bin (D22,
// global-administrator-only to view/restore/purge, same split as projects).
// The active view also adds: manual reordering (D31, only meaningful and
// only enabled with a single project selected, since reorderTicket's anchor
// must be in the same project -- the table is sorted by rankOrder in that
// case so up/down visibly matches the stored order) and simple bulk actions
// (D36, always available since each key is authorized/processed
// independently regardless of project).
async function renderTicketsView(showingDeleted) {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  let tickets;
  if (showingDeleted) {
    try {
      tickets = (await api('/api/v1/tickets/deleted')).items;
    } catch (error) {
      showError(error);
      return;
    }
  } else {
    await fetchTickets();
    tickets = state.tickets;
    if (state.selectedProject) {
      tickets = [...tickets].sort((a, b) => a.rankOrder - b.rankOrder);
    }
    if (state.filterEpic === 'no-epic') {
      tickets = tickets.filter(ticket => ticketTypeHierarchyLevel(ticket.type.key) === 0 && !ticket.parentTicketKey);
    }
  }
  const orderable = !showingDeleted && Boolean(state.selectedProject);
  const isAdmin = Boolean(state.principal?.isAdmin);
  const projectOptions = state.projects.map(project => `<option value="${escapeHtml(project.key)}" ${project.key === state.selectedProject ? 'selected' : ''}>${escapeHtml(project.key)} — ${escapeHtml(project.name)}</option>`).join('');
  const statusOptions = STATUSES.map(status => `<option value="${status.key}" ${status.key === state.status ? 'selected' : ''}>${status.name}</option>`).join('');
  const bulkStatusOptions = STATUSES.map(status => `<option value="${status.key}">${status.name}</option>`).join('');
  const bulkResolutionOptions = RESOLUTIONS.map(resolution => `<option value="${resolution.key}">${resolution.name}</option>`).join('');
  content.innerHTML = `
    <div class="page-header">
      <div>
        <span class="eyebrow">${escapeHtml(state.selectedProject || 'All projects')}</span>
        <h1>${showingDeleted ? 'Ticket recycle bin' : 'Tickets'}</h1>
        <p>${showingDeleted ? 'Tickets moved to the recycle bin (90-day retention).' : 'Search, filter, and inspect the work items in a project.'}</p>
      </div>
      <div class="page-actions">
        ${showingDeleted ? '' : `<a class="secondary-button" id="export-tickets-csv" href="/api/v1/tickets/export.csv?${ticketFilterParams()}" download="tickets.csv">⬇ Export CSV</a>`}
        ${isAdmin ? `<button type="button" class="secondary-button" id="toggle-ticket-recycle-bin">${showingDeleted ? '← Back to tickets' : '🗑 Recycle bin'}</button>` : ''}
      </div>
    </div>
    <div class="panel">
      ${showingDeleted ? '' : `
      <div class="filter-bar">
        <select id="ticket-project-filter"><option value="">All projects</option>${projectOptions}</select>
        <select id="ticket-status-filter"><option value="">All statuses</option>${statusOptions}</select>
        <select id="ticket-type-filter">
          <option value="">All types</option>
          <option value="epic" ${state.filterType === 'epic' ? 'selected' : ''}>Epic</option>
          <option value="story" ${state.filterType === 'story' ? 'selected' : ''}>Story</option>
          <option value="task" ${state.filterType === 'task' ? 'selected' : ''}>Task</option>
          <option value="bug" ${state.filterType === 'bug' ? 'selected' : ''}>Bug</option>
          <option value="sub-task" ${state.filterType === 'sub-task' ? 'selected' : ''}>Sub-task</option>
        </select>
        <select id="ticket-priority-filter">
          <option value="">All priorities</option>
          <option value="highest" ${state.filterPriority === 'highest' ? 'selected' : ''}>Highest</option>
          <option value="high" ${state.filterPriority === 'high' ? 'selected' : ''}>High</option>
          <option value="medium" ${state.filterPriority === 'medium' ? 'selected' : ''}>Medium</option>
          <option value="low" ${state.filterPriority === 'low' ? 'selected' : ''}>Low</option>
          <option value="lowest" ${state.filterPriority === 'lowest' ? 'selected' : ''}>Lowest</option>
        </select>
        <select id="ticket-assignee-filter">
          <option value="">Any assignee</option>
          <option value="demo@ticket-hub.local" ${state.filterAssignee === 'demo@ticket-hub.local' ? 'selected' : ''}>Demo User</option>
          <option value="alex@ticket-hub.local" ${state.filterAssignee === 'alex@ticket-hub.local' ? 'selected' : ''}>Alex Morgan</option>
          <option value="sam@ticket-hub.local" ${state.filterAssignee === 'sam@ticket-hub.local' ? 'selected' : ''}>Sam Lee</option>
        </select>
        <input id="ticket-label-filter" value="${escapeHtml(state.filterLabel)}" placeholder="Label" style="width:110px">
        <input id="ticket-component-filter" value="${escapeHtml(state.filterComponent)}" placeholder="Component" style="width:110px">
        <select id="ticket-epic-filter">
          <option value="">Any hierarchy</option>
          <option value="no-epic" ${state.filterEpic === 'no-epic' ? 'selected' : ''}>No Epic</option>
        </select>
        <input id="ticket-due-filter" type="date" value="${escapeHtml(state.filterDueBefore)}" title="Due on or before">
        <input id="ticket-search-filter" type="search" value="${escapeHtml(state.search)}" placeholder="Filter by key, summary, or description">
        <button class="secondary-button" id="clear-filters">Clear</button>
      </div>
      <div class="bulk-bar hidden" id="bulk-bar">
        <span id="bulk-count">0 selected</span>
        <select id="bulk-status-select">${bulkStatusOptions}</select>
        <select id="bulk-resolution-select" class="hidden" title="Required to move to a Done-category status">${bulkResolutionOptions}</select>
        <button type="button" class="secondary-button" id="bulk-status-apply">Set status</button>
        <select id="bulk-assignee-select">
          <option value="">Unassigned</option>
          <option value="demo@ticket-hub.local">Demo User</option>
          <option value="alex@ticket-hub.local">Alex Morgan</option>
          <option value="sam@ticket-hub.local">Sam Lee</option>
        </select>
        <button type="button" class="secondary-button" id="bulk-assign-apply">Assign</button>
        <input id="bulk-label-input" placeholder="label" style="width:120px">
        <button type="button" class="secondary-button" id="bulk-label-apply">Add label</button>
        <button type="button" class="secondary-button" id="bulk-delete-apply">Delete</button>
        <button type="button" class="ghost-button" id="bulk-clear">Clear</button>
      </div>`}
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr>
            ${showingDeleted ? '' : `<th class="select-column"><input type="checkbox" id="select-all-tickets" aria-label="Select all tickets" ${tickets.length ? '' : 'disabled'}></th>`}
            <th>Type</th><th>Key</th><th>Summary</th><th>Status</th><th>Priority</th><th>Assignee</th>
            ${showingDeleted ? '' : '<th>Created</th><th>Updated</th>'}
            ${showingDeleted ? '<th>Actions</th>' : ''}
            ${orderable ? '<th>Order</th>' : ''}
          </tr></thead>
          <tbody>${showingDeleted ? deletedTicketRows(tickets) : ticketRows(tickets, { orderable, selectable: true })}</tbody>
        </table>
      </div>
    </div>`;

  document.querySelector('#toggle-ticket-recycle-bin')?.addEventListener('click', () => renderTicketsView(!showingDeleted));

  if (showingDeleted) {
    document.querySelectorAll('[data-restore-ticket]').forEach(button => button.addEventListener('click', async () => {
      const key = button.dataset.restoreTicket;
      try {
        await api(`/api/v1/tickets/${encodeURIComponent(key)}/restore`, { method: 'POST' });
        showToast(`${key} restored`);
        await renderTicketsView(true);
      } catch (error) { showToast(error.message); }
    }));
    document.querySelectorAll('[data-permanent-ticket]').forEach(button => button.addEventListener('click', async () => {
      const key = button.dataset.permanentTicket;
      try {
        await api(`/api/v1/tickets/${encodeURIComponent(key)}/permanent`, { method: 'DELETE' });
        showToast(`${key} permanently deleted`);
        await renderTicketsView(true);
      } catch (error) { showToast(error.message); }
    }));
    return;
  }

  document.querySelector('#ticket-project-filter').addEventListener('change', event => {
    state.selectedProject = event.target.value || null;
    renderTickets().catch(showError);
  });
  document.querySelector('#ticket-status-filter').addEventListener('change', event => {
    state.status = event.target.value;
    renderTickets().catch(showError);
  });
  document.querySelector('#ticket-type-filter').addEventListener('change', event => {
    state.filterType = event.target.value;
    renderTickets().catch(showError);
  });
  document.querySelector('#ticket-priority-filter').addEventListener('change', event => {
    state.filterPriority = event.target.value;
    renderTickets().catch(showError);
  });
  document.querySelector('#ticket-assignee-filter').addEventListener('change', event => {
    state.filterAssignee = event.target.value;
    renderTickets().catch(showError);
  });
  document.querySelector('#ticket-label-filter').addEventListener('input', debounce(event => {
    state.filterLabel = event.target.value.trim();
    renderTickets().catch(showError);
  }, 300));
  document.querySelector('#ticket-component-filter').addEventListener('input', debounce(event => {
    state.filterComponent = event.target.value.trim();
    renderTickets().catch(showError);
  }, 300));
  document.querySelector('#ticket-epic-filter').addEventListener('change', event => {
    state.filterEpic = event.target.value;
    renderTickets().catch(showError);
  });
  document.querySelector('#ticket-due-filter').addEventListener('change', event => {
    state.filterDueBefore = event.target.value;
    renderTickets().catch(showError);
  });
  document.querySelector('#ticket-search-filter').addEventListener('input', debounce(event => {
    state.search = event.target.value.trim();
    renderTickets().catch(showError);
  }, 300));
  document.querySelector('#clear-filters').addEventListener('click', () => {
    state.status = '';
    state.search = '';
    state.filterType = '';
    state.filterPriority = '';
    state.filterAssignee = '';
    state.filterLabel = '';
    state.filterComponent = '';
    state.filterEpic = '';
    state.filterDueBefore = '';
    state.selectedProject = null;
    renderTickets().catch(showError);
  });

  if (orderable) {
    document.querySelectorAll('[data-move-up]').forEach(button => button.addEventListener('click', async event => {
      event.stopPropagation();
      const key = button.dataset.moveUp;
      const index = tickets.findIndex(candidate => candidate.key === key);
      if (index <= 0) return;
      try {
        await api(`/api/v1/tickets/${encodeURIComponent(key)}/reorder`, { method: 'POST', body: JSON.stringify({ beforeTicketKey: tickets[index - 1].key }) });
        await renderTicketsView(false);
      } catch (error) { showToast(error.message); }
    }));
    document.querySelectorAll('[data-move-down]').forEach(button => button.addEventListener('click', async event => {
      event.stopPropagation();
      const key = button.dataset.moveDown;
      const index = tickets.findIndex(candidate => candidate.key === key);
      if (index === -1 || index >= tickets.length - 1) return;
      const beforeTicketKey = index + 2 < tickets.length ? tickets[index + 2].key : null;
      try {
        await api(`/api/v1/tickets/${encodeURIComponent(key)}/reorder`, { method: 'POST', body: JSON.stringify({ beforeTicketKey }) });
        await renderTicketsView(false);
      } catch (error) { showToast(error.message); }
    }));
  }

  const bulkBar = document.querySelector('#bulk-bar');
  const checkboxes = [...document.querySelectorAll('.ticket-select')];
  const selectedKeys = () => checkboxes.filter(box => box.checked).map(box => box.value);
  const selectAllBox = document.querySelector('#select-all-tickets');
  const refreshBulkBar = () => {
    const count = selectedKeys().length;
    document.querySelector('#bulk-count').textContent = `${count} selected`;
    bulkBar.classList.toggle('hidden', count === 0);
    if (selectAllBox) {
      selectAllBox.checked = checkboxes.length > 0 && count === checkboxes.length;
      selectAllBox.indeterminate = count > 0 && count < checkboxes.length;
    }
  };
  // Keyboard/shift-click multi-select: Shift+click a checkbox to check
  // every row between it and the last-clicked one (the common "range
  // select" convention); Shift+ArrowDown/ArrowUp does the same from the
  // keyboard alone, checking the next/previous row and moving focus there
  // so a range can be built without ever touching the mouse. The header
  // checkbox selects/clears every visible row (native, so it's already
  // keyboard-operable via Tab+Space).
  let lastCheckedIndex = null;
  checkboxes.forEach((box, index) => {
    box.addEventListener('click', event => {
      event.stopPropagation();
      if (event.shiftKey && lastCheckedIndex !== null) {
        const [start, end] = [lastCheckedIndex, index].sort((a, b) => a - b);
        for (let i = start; i <= end; i++) {
          checkboxes[i].checked = box.checked;
        }
      }
      lastCheckedIndex = index;
    });
    box.addEventListener('change', refreshBulkBar);
    box.addEventListener('keydown', event => {
      if (!event.shiftKey || (event.key !== 'ArrowDown' && event.key !== 'ArrowUp')) return;
      const nextIndex = event.key === 'ArrowDown' ? index + 1 : index - 1;
      const nextBox = checkboxes[nextIndex];
      if (!nextBox) return;
      event.preventDefault();
      nextBox.checked = true;
      nextBox.focus();
      lastCheckedIndex = nextIndex;
      refreshBulkBar();
    });
  });
  selectAllBox?.addEventListener('change', () => {
    checkboxes.forEach(box => { box.checked = selectAllBox.checked; });
    lastCheckedIndex = null;
    refreshBulkBar();
  });
  document.querySelector('#bulk-clear')?.addEventListener('click', () => {
    document.querySelectorAll('.ticket-select:checked').forEach(box => { box.checked = false; });
    lastCheckedIndex = null;
    refreshBulkBar();
  });
  const runBulk = async (path, extraPayload, verb) => {
    const ticketKeys = selectedKeys();
    if (!ticketKeys.length) return;
    try {
      const result = await api(path, { method: 'POST', body: JSON.stringify({ ticketKeys, ...extraPayload }) });
      showToast(`${verb}: ${result.succeeded.length} succeeded, ${result.failed.length} failed`);
      await renderTicketsView(false);
    } catch (error) { showToast(error.message); }
  };
  // Bulk transition to a Done-category status shares the same resolution
  // across every selected ticket (D68-D70) -- the server already supported
  // this (bulkChangeStatus forwards one shared `resolution` to each ticket's
  // own changeStatus call, same as the drawer/board's single-ticket path),
  // this only adds the picker so it's actually reachable from the bulk bar.
  const bulkStatusSelect = document.querySelector('#bulk-status-select');
  const bulkResolutionSelect = document.querySelector('#bulk-resolution-select');
  const refreshBulkResolutionVisibility = () => {
    const status = STATUSES.find(candidate => candidate.key === bulkStatusSelect.value);
    bulkResolutionSelect.classList.toggle('hidden', status?.category !== 'done');
  };
  bulkStatusSelect?.addEventListener('change', refreshBulkResolutionVisibility);
  refreshBulkResolutionVisibility();
  document.querySelector('#bulk-status-apply')?.addEventListener('click', () => {
    const statusKey = bulkStatusSelect.value;
    const status = STATUSES.find(candidate => candidate.key === statusKey);
    const payload = { statusKey };
    if (status?.category === 'done') payload.resolution = bulkResolutionSelect.value;
    runBulk('/api/v1/tickets/bulk/status', payload, 'Bulk status change');
  });
  document.querySelector('#bulk-assign-apply')?.addEventListener('click', () => runBulk('/api/v1/tickets/bulk/assign', { assigneeEmail: document.querySelector('#bulk-assignee-select').value || null }, 'Bulk assign'));
  document.querySelector('#bulk-label-apply')?.addEventListener('click', () => {
    const label = document.querySelector('#bulk-label-input').value.trim();
    if (!label) return;
    runBulk('/api/v1/tickets/bulk/label', { label }, 'Bulk add label');
  });
  document.querySelector('#bulk-delete-apply')?.addEventListener('click', () => runBulk('/api/v1/tickets/bulk/delete', {}, 'Bulk delete'));

  bindTicketLinks();
}

// Board columns exclude Backlog (product amendment, user-requested): a
// project's backlog can grow far past what a Kanban column can usefully
// display, so it gets its own paginated screen (renderBacklog) instead of
// competing for board space. Fetching per-status below (rather than one
// unfiltered fetchTickets() call) means the board's own fixed page-size
// budget is spent entirely on the statuses it actually renders, instead of
// possibly being consumed by backlog rows that would never appear on the
// board anyway.
const BOARD_STATUSES = STATUSES.filter(status => status.key !== 'backlog');

async function fetchBoardTickets() {
  const results = await Promise.all(BOARD_STATUSES.map(status => {
    const params = ticketFilterParams();
    params.set('status', status.key);
    return api(`/api/v1/tickets?${params}`);
  }));
  state.tickets = results.flatMap(result => result.items);
}

// Quick filters (Board/Backlog): one-click chip toggles, applied client-side
// after the normal fetch -- "Only my tickets" and "No Epic" reuse the exact
// same semantics as the Tickets screen's assignee dropdown and D66's "No
// Epic" filter, just as an always-visible toggle rather than a dropdown
// pick, matching Jira's own board quick-filter convention. Deliberately
// client-side-only (no query params) on both Board and Backlog so the same
// two functions cover both screens regardless of whether the underlying
// fetch is paginated (Backlog) or not (Board).
function applyQuickFilters(tickets) {
  let result = tickets;
  if (state.quickFilterMine) {
    const email = state.principal?.email;
    result = result.filter(ticket => ticket.assignee?.email === email);
  }
  if (state.quickFilterNoEpic) {
    result = result.filter(ticket => ticketTypeHierarchyLevel(ticket.type.key) === 0 && !ticket.parentTicketKey);
  }
  return result;
}

function quickFiltersBar() {
  return `
    <div class="quick-filters">
      <button type="button" class="quick-filter-chip ${state.quickFilterMine ? 'active' : ''}" id="quick-filter-mine">👤 Only my tickets</button>
      <button type="button" class="quick-filter-chip ${state.quickFilterNoEpic ? 'active' : ''}" id="quick-filter-no-epic">🚫 No Epic</button>
    </div>`;
}

function bindQuickFiltersBar(onChange) {
  document.querySelector('#quick-filter-mine')?.addEventListener('click', () => {
    state.quickFilterMine = !state.quickFilterMine;
    onChange();
  });
  document.querySelector('#quick-filter-no-epic')?.addEventListener('click', () => {
    state.quickFilterNoEpic = !state.quickFilterNoEpic;
    onChange();
  });
}

async function renderBoard() {
  state.selectedProject ||= state.projects[0]?.key || null;
  state.search = '';
  state.status = '';
  state.filterType = '';
  state.filterPriority = '';
  state.filterAssignee = '';
  state.filterLabel = '';
  state.filterComponent = '';
  state.filterEpic = '';
  state.filterDueBefore = '';
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const [, boardColumns] = await Promise.all([fetchBoardTickets(), api('/api/v1/board-columns')]);
  const isAdmin = Boolean(state.principal?.isAdmin);
  const selected = state.projects.find(project => project.key === state.selectedProject);
  const visibleTickets = applyQuickFilters(state.tickets);
  const columns = BOARD_STATUSES.map(status => {
    const tickets = visibleTickets.filter(ticket => ticket.status.key === status.key);
    // Kanban WIP limits (D32/D33): a single flat, installation-wide limit
    // per fixed workflow status -- soft and display-time-only, an
    // over-limit column is highlighted, never blocked from receiving more
    // tickets.
    const column = boardColumns.items.find(candidate => candidate.statusKey === status.key);
    const wipLimit = column?.wipLimit ?? null;
    const overLimit = wipLimit !== null && tickets.length > wipLimit;
    const countLabel = wipLimit !== null ? `${tickets.length} / ${wipLimit}` : `${tickets.length}`;
    const wipEditor = isAdmin ? `
      <div class="wip-limit-edit">
        <input type="number" min="0" placeholder="No limit" value="${wipLimit !== null ? wipLimit : ''}" data-wip-input="${escapeHtml(status.key)}" title="WIP limit for ${escapeHtml(status.name)}">
        <button type="button" class="icon-button" data-wip-save="${escapeHtml(status.key)}" aria-label="Save WIP limit">✓</button>
      </div>` : '';
    return `
      <section class="board-column">
        <div class="board-column-header">
          <strong>${escapeHtml(status.name)}</strong>
          <div class="board-column-header-actions">
            <span class="column-count ${overLimit ? 'over-limit' : ''}" title="${overLimit ? 'Over the soft WIP limit' : ''}">${countLabel}</span>
          </div>
        </div>
        ${wipEditor}
        <div class="board-list" data-status-key="${escapeHtml(status.key)}">
          ${tickets.length ? tickets.map(ticket => `
            <article class="ticket-card" draggable="true" data-ticket-key="${escapeHtml(ticket.key)}">
              <div class="ticket-type"><span style="color:${escapeHtml(ticket.type.color)}">${escapeHtml(ticket.type.icon)}</span><span class="ticket-key">${escapeHtml(ticket.key)}</span></div>
              <div class="card-summary">${escapeHtml(ticket.summary)}</div>
              <div class="ticket-card-labels">${labelsMarkup(ticket.labels)}</div>
              <div class="ticket-card-footer">${priorityChip(ticket)}${ticket.assignee ? `<span class="small-avatar" title="${escapeHtml(ticket.assignee.displayName)}">${escapeHtml(initials(ticket.assignee.displayName))}</span>` : '<span></span>'}</div>
            </article>`).join('') : '<div class="empty-state">No tickets</div>'}
        </div>
      </section>`;
  }).join('');

  content.innerHTML = `
    <div class="page-header">
      <div><span class="eyebrow">${escapeHtml(state.selectedProject || 'Project')}</span><h1>${escapeHtml(selected?.name || 'Board')}</h1><p>Simple status-based Kanban board. Backlog tickets live on their own screen.${isAdmin ? ' Soft WIP limits are installation-wide and apply to every project’s board.' : ''}</p></div>
      <div class="page-actions">
        <button type="button" class="secondary-button" id="board-view-backlog">☰ Backlog</button>
        <select id="board-project" class="status-select">${state.projects.map(project => `<option value="${escapeHtml(project.key)}" ${project.key === state.selectedProject ? 'selected' : ''}>${escapeHtml(project.key)} — ${escapeHtml(project.name)}</option>`).join('')}</select>
      </div>
    </div>
    ${quickFiltersBar()}
    <div class="board">${columns}</div>`;
  document.querySelector('#board-view-backlog').addEventListener('click', () => navigate('backlog'));
  document.querySelector('#board-project').addEventListener('change', event => {
    state.selectedProject = event.target.value;
    renderBoard().catch(showError);
  });
  bindQuickFiltersBar(() => renderBoard().catch(showError));
  document.querySelectorAll('[data-wip-save]').forEach(button => button.addEventListener('click', async () => {
    const statusKey = button.dataset.wipSave;
    const input = document.querySelector(`[data-wip-input="${CSS.escape(statusKey)}"]`);
    const raw = input.value.trim();
    const wipLimit = raw === '' ? null : Number(raw);
    try {
      await api(`/api/v1/board-columns/${encodeURIComponent(statusKey)}`, { method: 'PUT', body: JSON.stringify({ wipLimit }) });
      showToast('WIP limit updated');
      await renderBoard();
    } catch (error) { showToast(error.message); }
  }));
  bindBoardDragAndDrop();
  bindTicketLinks();
  bindBoardKeyboardNav();
}

const BacklogPageSize = 50;

// Dedicated Backlog screen (product amendment, user-requested): a project's
// backlog is where everything not yet actively worked ends up, so unlike
// every other list in this app it has no natural upper bound -- putting it
// in its own Kanban column (as D32's "one column per status" originally
// specified) breaks down once that column can hold thousands of tickets.
// This screen is real server-side pagination (page/pageSize, same
// contract as D126) rather than the fixed-200-row cap every other list in
// this app still relies on, ordered by the same manual rank (`sort=rank`)
// the reorder arrows already write to via `reorderTicket` -- so "page 1" is
// always the top of the backlog by priority, not an arbitrary recency-based
// cut. Always scoped to exactly one project (like the Board), since rank
// order and the reorder arrows are only meaningful within one project.
async function renderBacklog() {
  state.selectedProject ||= state.projects[0]?.key || null;
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const selected = state.projects.find(project => project.key === state.selectedProject);

  if (!state.selectedProject) {
    content.innerHTML = `
      <div class="page-header"><div><span class="eyebrow">Backlog</span><h1>Backlog</h1><p>Prioritize the work not yet ready for the board.</p></div></div>
      <div class="empty-state">Create a project first.</div>`;
    return;
  }

  const params = ticketFilterParams();
  params.set('status', 'backlog');
  params.set('sort', 'rank');
  params.set('page', String(state.backlogPage));
  params.set('pageSize', String(BacklogPageSize));
  let page;
  try {
    page = await api(`/api/v1/tickets?${params}`);
  } catch (error) {
    showError(error);
    return;
  }
  const tickets = applyQuickFilters(page.items);
  const totalPages = Math.max(page.totalPages, 1);

  content.innerHTML = `
    <div class="page-header">
      <div><span class="eyebrow">${escapeHtml(state.selectedProject)}</span><h1>${escapeHtml(selected?.name || 'Backlog')} backlog</h1><p>Prioritize the work not yet ready for the board. Use the arrows to set priority order.</p></div>
      <div class="page-actions">
        <button type="button" class="secondary-button" id="backlog-view-board">▦ Board</button>
        <select id="backlog-project" class="status-select">${state.projects.map(project => `<option value="${escapeHtml(project.key)}" ${project.key === state.selectedProject ? 'selected' : ''}>${escapeHtml(project.key)} — ${escapeHtml(project.name)}</option>`).join('')}</select>
      </div>
    </div>
    ${quickFiltersBar()}
    <div class="panel">
      <div class="filter-bar">
        <select id="backlog-type-filter">
          <option value="">All types</option>
          <option value="epic" ${state.filterType === 'epic' ? 'selected' : ''}>Epic</option>
          <option value="story" ${state.filterType === 'story' ? 'selected' : ''}>Story</option>
          <option value="task" ${state.filterType === 'task' ? 'selected' : ''}>Task</option>
          <option value="bug" ${state.filterType === 'bug' ? 'selected' : ''}>Bug</option>
          <option value="sub-task" ${state.filterType === 'sub-task' ? 'selected' : ''}>Sub-task</option>
        </select>
        <select id="backlog-priority-filter">
          <option value="">All priorities</option>
          <option value="highest" ${state.filterPriority === 'highest' ? 'selected' : ''}>Highest</option>
          <option value="high" ${state.filterPriority === 'high' ? 'selected' : ''}>High</option>
          <option value="medium" ${state.filterPriority === 'medium' ? 'selected' : ''}>Medium</option>
          <option value="low" ${state.filterPriority === 'low' ? 'selected' : ''}>Low</option>
          <option value="lowest" ${state.filterPriority === 'lowest' ? 'selected' : ''}>Lowest</option>
        </select>
        <select id="backlog-assignee-filter">
          <option value="">Any assignee</option>
          <option value="demo@ticket-hub.local" ${state.filterAssignee === 'demo@ticket-hub.local' ? 'selected' : ''}>Demo User</option>
          <option value="alex@ticket-hub.local" ${state.filterAssignee === 'alex@ticket-hub.local' ? 'selected' : ''}>Alex Morgan</option>
          <option value="sam@ticket-hub.local" ${state.filterAssignee === 'sam@ticket-hub.local' ? 'selected' : ''}>Sam Lee</option>
        </select>
        <input id="backlog-label-filter" value="${escapeHtml(state.filterLabel)}" placeholder="Label" style="width:110px">
        <input id="backlog-component-filter" value="${escapeHtml(state.filterComponent)}" placeholder="Component" style="width:110px">
        <input id="backlog-search-filter" type="search" value="${escapeHtml(state.search)}" placeholder="Filter by key, summary, or description">
        <button class="secondary-button" id="backlog-clear-filters">Clear</button>
      </div>
      <div style="overflow-x:auto">
        <table class="ticket-table">
          <thead><tr><th>Type</th><th>Key</th><th>Summary</th><th>Status</th><th>Priority</th><th>Assignee</th><th>Created</th><th>Updated</th><th>Order</th></tr></thead>
          <tbody>${ticketRows(tickets, { orderable: true, selectable: false })}</tbody>
        </table>
      </div>
      <div class="pagination-bar">
        <span>${page.totalItems} backlog ticket${page.totalItems === 1 ? '' : 's'} — page ${page.page} of ${totalPages}${(state.quickFilterMine || state.quickFilterNoEpic) ? ` (${tickets.length} match the active quick filter${state.quickFilterMine && state.quickFilterNoEpic ? 's' : ''} on this page)` : ''}</span>
        <div>
          <button type="button" class="secondary-button" id="backlog-prev" ${page.page <= 1 ? 'disabled' : ''}>← Previous</button>
          <button type="button" class="secondary-button" id="backlog-next" ${page.page >= totalPages ? 'disabled' : ''}>Next →</button>
        </div>
      </div>
    </div>`;

  document.querySelector('#backlog-view-board').addEventListener('click', () => navigate('board'));
  bindQuickFiltersBar(() => {
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  });
  document.querySelector('#backlog-project').addEventListener('change', event => {
    state.selectedProject = event.target.value;
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  });
  document.querySelector('#backlog-type-filter').addEventListener('change', event => {
    state.filterType = event.target.value;
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  });
  document.querySelector('#backlog-priority-filter').addEventListener('change', event => {
    state.filterPriority = event.target.value;
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  });
  document.querySelector('#backlog-assignee-filter').addEventListener('change', event => {
    state.filterAssignee = event.target.value;
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  });
  document.querySelector('#backlog-label-filter').addEventListener('input', debounce(event => {
    state.filterLabel = event.target.value.trim();
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  }, 300));
  document.querySelector('#backlog-component-filter').addEventListener('input', debounce(event => {
    state.filterComponent = event.target.value.trim();
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  }, 300));
  document.querySelector('#backlog-search-filter').addEventListener('input', debounce(event => {
    state.search = event.target.value.trim();
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  }, 300));
  document.querySelector('#backlog-clear-filters').addEventListener('click', () => {
    state.filterType = '';
    state.filterPriority = '';
    state.filterAssignee = '';
    state.filterLabel = '';
    state.filterComponent = '';
    state.search = '';
    state.backlogPage = 1;
    renderBacklog().catch(showError);
  });
  document.querySelector('#backlog-prev').addEventListener('click', () => {
    state.backlogPage = Math.max(1, state.backlogPage - 1);
    renderBacklog().catch(showError);
  });
  document.querySelector('#backlog-next').addEventListener('click', () => {
    state.backlogPage = state.backlogPage + 1;
    renderBacklog().catch(showError);
  });
  document.querySelectorAll('[data-move-up]').forEach(button => button.addEventListener('click', async event => {
    event.stopPropagation();
    const key = button.dataset.moveUp;
    const index = tickets.findIndex(candidate => candidate.key === key);
    if (index <= 0) return;
    try {
      await api(`/api/v1/tickets/${encodeURIComponent(key)}/reorder`, { method: 'POST', body: JSON.stringify({ beforeTicketKey: tickets[index - 1].key }) });
      await renderBacklog();
    } catch (error) { showToast(error.message); }
  }));
  document.querySelectorAll('[data-move-down]').forEach(button => button.addEventListener('click', async event => {
    event.stopPropagation();
    const key = button.dataset.moveDown;
    const index = tickets.findIndex(candidate => candidate.key === key);
    if (index === -1 || index >= tickets.length - 1) return;
    const beforeTicketKey = index + 2 < tickets.length ? tickets[index + 2].key : null;
    try {
      await api(`/api/v1/tickets/${encodeURIComponent(key)}/reorder`, { method: 'POST', body: JSON.stringify({ beforeTicketKey }) });
      await renderBacklog();
    } catch (error) { showToast(error.message); }
  }));
  bindTicketLinks();
}

// Drag-and-drop card movement between board columns (optional UX polish --
// not required by D32/D33, which only need a soft WIP-limit *display*; the
// board is already fully usable via the drawer's status dropdown, which
// remains the keyboard-operable path since native HTML5 drag-and-drop has
// no built-in keyboard equivalent). Dropping onto the same column a card is
// already in is a no-op; dropping onto a Done-category column without an
// existing resolution prompts for one first (D68-D70), exactly like the
// drawer's status-select already does for the same case.
function bindBoardDragAndDrop() {
  document.querySelectorAll('.ticket-card[draggable]').forEach(card => {
    card.addEventListener('dragstart', event => {
      event.dataTransfer.setData('text/plain', card.dataset.ticketKey);
      event.dataTransfer.effectAllowed = 'move';
      card.classList.add('dragging');
    });
    card.addEventListener('dragend', () => card.classList.remove('dragging'));
  });
  document.querySelectorAll('.board-list[data-status-key]').forEach(list => {
    list.addEventListener('dragover', event => {
      event.preventDefault();
      event.dataTransfer.dropEffect = 'move';
      list.classList.add('drag-over');
    });
    list.addEventListener('dragleave', () => list.classList.remove('drag-over'));
    list.addEventListener('drop', event => {
      event.preventDefault();
      list.classList.remove('drag-over');
      const ticketKey = event.dataTransfer.getData('text/plain');
      handleBoardDrop(ticketKey, list.dataset.statusKey);
    });
  });
}

function handleBoardDrop(ticketKey, targetStatusKey) {
  const ticket = state.tickets.find(candidate => candidate.key === ticketKey);
  if (!ticket || ticket.status.key === targetStatusKey) {
    return;
  }
  const targetStatus = STATUSES.find(status => status.key === targetStatusKey);
  if (targetStatus?.category === 'done' && !ticket.resolution) {
    promptBoardResolution(ticket, targetStatusKey);
    return;
  }
  applyBoardStatusChange(ticket.key, targetStatusKey, null, ticket.version);
}

// Same request as applyStatusChange (used by the drawer's status select),
// but stays on the board and re-renders it instead of opening the drawer --
// jumping into the detail view is the right follow-up after a deliberate
// dropdown change, but not after a quick drag-and-drop card move.
async function applyBoardStatusChange(ticketKey, statusKey, resolution, expectedVersion) {
  try {
    const payload = { statusKey, expectedVersion };
    if (resolution) payload.resolution = resolution;
    await api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/status`, { method: 'PATCH', body: JSON.stringify(payload) });
    const statusName = STATUSES.find(status => status.key === statusKey)?.name || statusKey;
    showToast(`${ticketKey} moved to ${statusName}`);
    await renderBoard();
  } catch (error) {
    showToast(error.message);
    await renderBoard();
  }
}

// A minimal dynamically-created dialog (reuses the existing .modal-backdrop/
// .modal styling, same as the create-ticket/create-project modals, but not
// pre-declared in index.html since it only ever exists transiently) --
// mirrors the drawer's inline resolution picker for the one case drag-and-
// drop can't skip: a Done-category status requires a resolution.
function promptBoardResolution(ticket, targetStatusKey) {
  const targetStatus = STATUSES.find(status => status.key === targetStatusKey);
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  backdrop.setAttribute('role', 'dialog');
  backdrop.setAttribute('aria-modal', 'true');
  backdrop.innerHTML = `
    <div class="modal" style="max-width:420px">
      <div class="modal-header">
        <div><span class="eyebrow">${escapeHtml(ticket.key)}</span><h2>Resolve ticket</h2></div>
        <button type="button" class="icon-button" id="board-resolution-close" aria-label="Close">×</button>
      </div>
      <div class="form-grid">
        <label class="wide">Moving to ${escapeHtml(targetStatus?.name || targetStatusKey)} requires a resolution
          <select id="board-resolution-select">${RESOLUTIONS.map(resolution => `<option value="${resolution.key}">${resolution.name}</option>`).join('')}</select>
        </label>
      </div>
      <div class="modal-footer">
        <button type="button" class="secondary-button" id="board-resolution-cancel">Cancel</button>
        <button type="button" class="primary-button" id="board-resolution-confirm">Confirm</button>
      </div>
    </div>`;
  document.body.appendChild(backdrop);
  const onKeydown = event => { if (event.key === 'Escape') close(); };
  const close = () => {
    backdrop.remove();
    document.removeEventListener('keydown', onKeydown);
  };
  document.addEventListener('keydown', onKeydown);
  backdrop.querySelector('#board-resolution-close').addEventListener('click', close);
  backdrop.querySelector('#board-resolution-cancel').addEventListener('click', close);
  backdrop.addEventListener('click', event => { if (event.target === backdrop) close(); });
  backdrop.querySelector('#board-resolution-confirm').addEventListener('click', async () => {
    const resolution = backdrop.querySelector('#board-resolution-select').value;
    close();
    await applyBoardStatusChange(ticket.key, targetStatusKey, resolution, ticket.version);
  });
  backdrop.querySelector('#board-resolution-select').focus();
}

// D129: a stale write (expectedVersion mismatch) is rejected with HTTP 409
// -- rather than a generic error toast, this makes the conflict explicit
// and offers a concrete next step. "Reapply" is deliberately not automatic
// (this app has no per-field diff/merge machinery, only full-replacement
// edits): reloading re-opens the edit form pre-filled with the ticket's
// current server state at its current version, so the user's own
// in-progress edit is visibly discarded rather than silently lost, and
// they can retype their intended change on top of the fresh data before
// saving again.
function showConflictDialog(ticketKey) {
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  backdrop.setAttribute('role', 'dialog');
  backdrop.setAttribute('aria-modal', 'true');
  backdrop.innerHTML = `
    <div class="modal" style="max-width:420px">
      <div class="modal-header">
        <div><span class="eyebrow">${escapeHtml(ticketKey)}</span><h2>Someone else changed this ticket</h2></div>
        <button type="button" class="icon-button" id="conflict-close" aria-label="Close">×</button>
      </div>
      <p style="padding:22px;margin:0;color:var(--text-secondary)">This ticket was updated by someone else while
      you were editing it, so your changes were not saved. Reload the current version and reapply your changes.</p>
      <div class="modal-footer">
        <button type="button" class="secondary-button" id="conflict-cancel">Cancel</button>
        <button type="button" class="primary-button" id="conflict-reload">Reload latest version</button>
      </div>
    </div>`;
  document.body.appendChild(backdrop);
  const onKeydown = event => { if (event.key === 'Escape') close(); };
  const close = () => {
    backdrop.remove();
    document.removeEventListener('keydown', onKeydown);
  };
  document.addEventListener('keydown', onKeydown);
  backdrop.querySelector('#conflict-close').addEventListener('click', close);
  backdrop.querySelector('#conflict-cancel').addEventListener('click', close);
  backdrop.addEventListener('click', event => { if (event.target === backdrop) close(); });
  backdrop.querySelector('#conflict-reload').addEventListener('click', async () => {
    close();
    await openTicket(ticketKey, true);
  });
  backdrop.querySelector('#conflict-reload').focus();
}

// Project components (D19, KEEP_FOR_V1): a dynamically-created dialog, same
// pattern as promptBoardResolution above -- listing, adding, and deleting a
// project's components. Create/edit/delete require project-Admin-or-above
// server-side (TicketService::createComponent/editComponent/
// deleteComponent); a non-admin's attempt just surfaces the resulting 403
// as a toast, the same convention every other write in this app follows.
// The demo user list mirrors every other lead/assignee picker in this file
// (there is no generic user-search picker anywhere in the app).
const DEMO_USERS = [
  ['demo@ticket-hub.local', 'Demo User'],
  ['alex@ticket-hub.local', 'Alex Morgan'],
  ['sam@ticket-hub.local', 'Sam Lee'],
];

function userSelectOptions(selectedEmail) {
  return '<option value="">None</option>' + DEMO_USERS.map(([email, name]) =>
    `<option value="${escapeHtml(email)}" ${email === selectedEmail ? 'selected' : ''}>${escapeHtml(name)}</option>`).join('');
}

async function openComponentsModal(projectKey) {
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  backdrop.setAttribute('role', 'dialog');
  backdrop.setAttribute('aria-modal', 'true');
  document.body.appendChild(backdrop);

  const onKeydown = event => { if (event.key === 'Escape') close(); };
  const close = () => {
    backdrop.remove();
    document.removeEventListener('keydown', onKeydown);
  };
  document.addEventListener('keydown', onKeydown);
  backdrop.addEventListener('click', event => { if (event.target === backdrop) close(); });

  async function render() {
    let components = [];
    let loadError = null;
    try {
      components = (await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/components`)).items;
    } catch (error) {
      loadError = error.message;
    }
    backdrop.innerHTML = `
      <div class="modal" style="max-width:520px">
        <div class="modal-header">
          <div><span class="eyebrow">${escapeHtml(projectKey)}</span><h2>Components</h2></div>
          <button type="button" class="icon-button" id="components-close" aria-label="Close">×</button>
        </div>
        ${loadError ? `<div class="form-error">${escapeHtml(loadError)}</div>` : `
        <div style="max-height:260px;overflow-y:auto">
          ${components.length ? components.map(component => `
            <div style="display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 0;border-bottom:1px solid var(--border)" data-component-row="${escapeHtml(component.id)}">
              <span>${escapeHtml(component.name)}${component.lead ? ` <span style="color:var(--muted)">(lead: ${escapeHtml(component.lead.displayName)})</span>` : ''}</span>
              <button type="button" class="secondary-button" data-delete-component="${escapeHtml(component.id)}">Delete</button>
            </div>`).join('') : '<div class="empty-state">No components yet.</div>'}
        </div>`}
        <form class="form-grid" id="component-add-form" style="margin-top:16px">
          <label class="wide">Name<input name="name" maxlength="160" required placeholder="e.g. Backend"></label>
          <label class="wide">Description<input name="description" placeholder="Optional"></label>
          <label>Lead<select name="leadEmail">${userSelectOptions('')}</select></label>
          <label>Default assignee<select name="defaultAssigneeEmail">${userSelectOptions('')}</select></label>
        </form>
        <div id="component-error" class="form-error hidden"></div>
        <div class="modal-footer">
          <button type="button" class="secondary-button" id="components-done">Close</button>
          <button type="submit" form="component-add-form" class="primary-button">Add component</button>
        </div>
      </div>`;

    backdrop.querySelector('#components-close').addEventListener('click', close);
    backdrop.querySelector('#components-done').addEventListener('click', close);
    backdrop.querySelectorAll('[data-delete-component]').forEach(button => button.addEventListener('click', async () => {
      try {
        await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/components/${encodeURIComponent(button.dataset.deleteComponent)}`,
          { method: 'DELETE' });
        showToast('Component deleted');
        await render();
      } catch (error) { showToast(error.message); }
    }));
    backdrop.querySelector('#component-add-form').addEventListener('submit', async event => {
      event.preventDefault();
      const values = Object.fromEntries(new FormData(event.currentTarget).entries());
      const errorElement = backdrop.querySelector('#component-error');
      try {
        await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/components`, {
          method: 'POST',
          body: JSON.stringify({
            name: values.name.trim(),
            description: values.description.trim(),
            leadEmail: values.leadEmail || null,
            defaultAssigneeEmail: values.defaultAssigneeEmail || null,
          }),
        });
        showToast('Component added');
        await render();
      } catch (error) {
        errorElement.textContent = error.message;
        errorElement.classList.remove('hidden');
      }
    });
  }

  await render();
}

const CUSTOM_FIELD_TYPES = [
  { key: 'text', name: 'Text' },
  { key: 'number', name: 'Number' },
  { key: 'date', name: 'Date' },
  { key: 'checkbox', name: 'Checkbox' },
  { key: 'single_select', name: 'Single select' },
  { key: 'multi_select', name: 'Multi select' },
];

function isCustomFieldSelectType(fieldType) {
  return fieldType === 'single_select' || fieldType === 'multi_select';
}

// Custom fields (D9, deferred-after-V1, user-requested): admin-defined
// fields scoped to one project. Mirrors openComponentsModal's structure
// (a dynamically-built modal, list + add-form, re-rendered after every
// mutation) since it's the closest existing analog -- a small per-project
// catalog managed by project-Admin-or-above.
async function openCustomFieldsModal(projectKey) {
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  backdrop.setAttribute('role', 'dialog');
  backdrop.setAttribute('aria-modal', 'true');
  document.body.appendChild(backdrop);

  const onKeydown = event => { if (event.key === 'Escape') close(); };
  const close = () => {
    backdrop.remove();
    document.removeEventListener('keydown', onKeydown);
  };
  document.addEventListener('keydown', onKeydown);
  backdrop.addEventListener('click', event => { if (event.target === backdrop) close(); });

  async function render() {
    let fields = [];
    let loadError = null;
    try {
      fields = (await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/custom-fields`)).items;
    } catch (error) {
      loadError = error.message;
    }
    backdrop.innerHTML = `
      <div class="modal" style="max-width:560px">
        <div class="modal-header">
          <div><span class="eyebrow">${escapeHtml(projectKey)}</span><h2>Custom fields</h2></div>
          <button type="button" class="icon-button" id="custom-fields-close" aria-label="Close">×</button>
        </div>
        ${loadError ? `<div class="form-error">${escapeHtml(loadError)}</div>` : `
        <div style="max-height:260px;overflow-y:auto">
          ${fields.length ? fields.map(field => `
            <div style="display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 0;border-bottom:1px solid var(--border)" data-custom-field-row="${escapeHtml(field.id)}">
              <span>${escapeHtml(field.name)} <span style="color:var(--muted)">(${escapeHtml(CUSTOM_FIELD_TYPES.find(t => t.key === field.fieldType)?.name || field.fieldType)}${field.required ? ', required' : ''})</span>${isCustomFieldSelectType(field.fieldType) ? `<br><span style="color:var(--muted);font-size:12px">${escapeHtml(field.options.join(', '))}</span>` : ''}</span>
              <button type="button" class="secondary-button" data-delete-custom-field="${escapeHtml(field.id)}">Delete</button>
            </div>`).join('') : '<div class="empty-state">No custom fields yet.</div>'}
        </div>`}
        <form class="form-grid" id="custom-field-add-form" style="margin-top:16px">
          <label class="wide">Name<input name="name" maxlength="160" required placeholder="e.g. Environment"></label>
          <label>Type
            <select name="fieldType" id="custom-field-type-select">
              ${CUSTOM_FIELD_TYPES.map(type => `<option value="${type.key}">${escapeHtml(type.name)}</option>`).join('')}
            </select>
          </label>
          <label style="align-self:end"><input type="checkbox" name="required" style="width:auto;margin-right:6px">Required</label>
          <label class="wide" id="custom-field-options-label" hidden>Options (comma-separated)
            <input name="options" placeholder="e.g. Low, Medium, High">
          </label>
        </form>
        <div id="custom-field-error" class="form-error hidden"></div>
        <div class="modal-footer">
          <button type="button" class="secondary-button" id="custom-fields-done">Close</button>
          <button type="submit" form="custom-field-add-form" class="primary-button">Add field</button>
        </div>
      </div>`;

    backdrop.querySelector('#custom-fields-close').addEventListener('click', close);
    backdrop.querySelector('#custom-fields-done').addEventListener('click', close);
    backdrop.querySelectorAll('[data-delete-custom-field]').forEach(button => button.addEventListener('click', async () => {
      try {
        await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/custom-fields/${encodeURIComponent(button.dataset.deleteCustomField)}`,
          { method: 'DELETE' });
        showToast('Custom field deleted');
        await render();
      } catch (error) { showToast(error.message); }
    }));
    const typeSelect = backdrop.querySelector('#custom-field-type-select');
    const optionsLabel = backdrop.querySelector('#custom-field-options-label');
    const refreshOptionsVisibility = () => { optionsLabel.hidden = !isCustomFieldSelectType(typeSelect.value); };
    typeSelect.addEventListener('change', refreshOptionsVisibility);
    refreshOptionsVisibility();
    backdrop.querySelector('#custom-field-add-form').addEventListener('submit', async event => {
      event.preventDefault();
      const values = Object.fromEntries(new FormData(event.currentTarget).entries());
      const errorElement = backdrop.querySelector('#custom-field-error');
      try {
        await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/custom-fields`, {
          method: 'POST',
          body: JSON.stringify({
            name: values.name.trim(),
            fieldType: values.fieldType,
            options: isCustomFieldSelectType(values.fieldType)
              ? (values.options || '').split(',').map(option => option.trim()).filter(Boolean)
              : [],
            required: values.required === 'on',
          }),
        });
        showToast('Custom field added');
        await render();
      } catch (error) {
        errorElement.textContent = error.message;
        errorElement.classList.remove('hidden');
      }
    });
  }

  await render();
}

// D91: permanently changing a project's key. This reuses the exact
// transactional alias pattern the ticket-move flow (D38) already
// established -- the project's old key keeps resolving forever via
// project_key_aliases, and every ticket in the project also gets a new key
// (project prefix changed) with its own ticket_key_aliases entry for the
// old ticket key, so old bookmarks/links never 404. Requires project-Admin-
// or-above server-side; a non-admin's attempt just surfaces the resulting
// 403 as a toast, same pattern as every other write in this app.
async function openRenameProjectKeyModal(projectKey) {
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  backdrop.setAttribute('role', 'dialog');
  backdrop.setAttribute('aria-modal', 'true');
  backdrop.innerHTML = `
    <div class="modal" style="max-width:460px">
      <div class="modal-header">
        <div><span class="eyebrow">${escapeHtml(projectKey)}</span><h2>Rename project key</h2></div>
        <button type="button" class="icon-button" id="rename-key-close" aria-label="Close">×</button>
      </div>
      <p style="padding:22px;margin:0;color:var(--text-secondary)">Changing the project key also changes
      every ticket key in this project (e.g. ${escapeHtml(projectKey)}-12 becomes NEWKEY-12). The old
      project key and every old ticket key keep working and redirect to the new ones.</p>
      <form class="form-grid" id="rename-key-form" style="padding:0 22px 22px">
        <label class="wide">New key
          <input name="newKey" maxlength="10" required placeholder="e.g. NEWKEY" autocomplete="off"
            style="text-transform:uppercase">
        </label>
      </form>
      <div id="rename-key-error" class="form-error hidden" style="margin:0 22px 16px"></div>
      <div class="modal-footer">
        <button type="button" class="secondary-button" id="rename-key-cancel">Cancel</button>
        <button type="submit" form="rename-key-form" class="primary-button">Rename</button>
      </div>
    </div>`;
  document.body.appendChild(backdrop);
  const onKeydown = event => { if (event.key === 'Escape') close(); };
  const close = () => {
    backdrop.remove();
    document.removeEventListener('keydown', onKeydown);
  };
  document.addEventListener('keydown', onKeydown);
  backdrop.querySelector('#rename-key-close').addEventListener('click', close);
  backdrop.querySelector('#rename-key-cancel').addEventListener('click', close);
  backdrop.addEventListener('click', event => { if (event.target === backdrop) close(); });
  backdrop.querySelector('#rename-key-form').addEventListener('submit', async event => {
    event.preventDefault();
    const newKey = new FormData(event.currentTarget).get('newKey').trim().toUpperCase();
    const errorElement = backdrop.querySelector('#rename-key-error');
    try {
      await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/key`, {
        method: 'PATCH',
        body: JSON.stringify({ newKey }),
      });
      close();
      showToast(`Project key changed to ${newKey}`);
      // The old key no longer resolves to a live project (D91), so a
      // Tickets/Board view still pointed at it would otherwise go silently
      // empty -- follow the rename if it was the selected project.
      if (state.selectedProject === projectKey) {
        state.selectedProject = newKey;
      }
      await loadBaseData();
      await renderProjectsView(false);
    } catch (error) {
      errorElement.textContent = error.message;
      errorElement.classList.remove('hidden');
    }
  });
  backdrop.querySelector('[name="newKey"]').focus();
}

async function renderProjects() {
  await renderProjectsView(false);
}

// `showingDeleted` toggles between the active project grid and the recycle
// bin (D88/D89: global-administrator-only to view/restore/purge). Archiving
// and moving to the recycle bin require project-admin-or-above; the server
// is the actual authorization check -- a non-admin's click just surfaces the
// resulting 403 as a toast, same pattern as every other write in this app.
async function renderProjectsView(showingDeleted) {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  let projects = state.projects;
  if (showingDeleted) {
    try {
      projects = (await api('/api/v1/projects/deleted')).items;
    } catch (error) {
      showError(error);
      return;
    }
  }
  const isAdmin = Boolean(state.principal?.isAdmin);
  content.innerHTML = `
    <div class="page-header">
      <div>
        <span class="eyebrow">Workspace</span>
        <h1>${showingDeleted ? 'Project recycle bin' : 'Projects'}</h1>
        <p>${showingDeleted ? 'Projects moved to the recycle bin (90-day retention).' : 'Choose a project to see its tickets and board.'}</p>
      </div>
      <div class="page-actions">
        ${isAdmin ? `<button type="button" class="secondary-button" id="toggle-recycle-bin">${showingDeleted ? '← Back to projects' : '🗑 Recycle bin'}</button>` : ''}
        ${showingDeleted ? '' : '<button type="button" class="primary-button" id="new-project-button">＋ New project</button>'}
      </div>
    </div>
    <div class="project-grid">
      ${projects.length ? projects.map(project => `
        <article class="project-card" data-project-card="${escapeHtml(project.key)}">
          <div class="project-card-head"><span class="project-avatar">${escapeHtml(project.key.slice(0, 2))}</span><div><h3>${escapeHtml(project.name)}</h3><span class="ticket-key">${escapeHtml(project.key)}</span></div></div>
          <p>${escapeHtml(project.description)}</p>
          <div class="project-card-stats"><div><strong>${project.ticketCount}</strong><span>Total tickets</span></div><div><strong>${project.openTicketCount}</strong><span>Open tickets</span></div><div><strong>${escapeHtml(project.lead?.displayName || '—')}</strong><span>Lead</span></div></div>
          <div class="project-card-actions">${showingDeleted
            ? `<button type="button" class="secondary-button" data-restore-project="${escapeHtml(project.key)}">Restore</button><button type="button" class="secondary-button" data-permanent-project="${escapeHtml(project.key)}">Delete permanently</button>`
            : `<button type="button" class="secondary-button" data-archive-project="${escapeHtml(project.key)}" data-archived="${project.archived}">${project.archived ? 'Unarchive' : 'Archive'}</button><button type="button" class="secondary-button" data-components-project="${escapeHtml(project.key)}">Components</button><button type="button" class="secondary-button" data-custom-fields-project="${escapeHtml(project.key)}">Custom fields</button><button type="button" class="secondary-button" data-rename-key-project="${escapeHtml(project.key)}">Rename key</button><button type="button" class="secondary-button" data-delete-project="${escapeHtml(project.key)}">Delete</button>`}</div>
        </article>`).join('') : `<div class="empty-state">${showingDeleted ? 'The recycle bin is empty.' : 'No projects yet.'}</div>`}
    </div>`;

  document.querySelectorAll('[data-project-card]').forEach(card => {
    const openProjectBoard = () => {
      state.selectedProject = card.dataset.projectCard;
      navigate('board');
    };
    card.addEventListener('click', openProjectBoard);
    makeKeyboardActivatable(card, openProjectBoard);
  });
  document.querySelector('#toggle-recycle-bin')?.addEventListener('click', () => renderProjectsView(!showingDeleted));
  document.querySelector('#new-project-button')?.addEventListener('click', openProjectModal);

  const stopAnd = handler => event => { event.stopPropagation(); return handler(event); };
  document.querySelectorAll('[data-components-project]').forEach(button => button.addEventListener('click', stopAnd(() => {
    openComponentsModal(button.dataset.componentsProject);
  })));
  document.querySelectorAll('[data-custom-fields-project]').forEach(button => button.addEventListener('click', stopAnd(() => {
    openCustomFieldsModal(button.dataset.customFieldsProject);
  })));
  document.querySelectorAll('[data-rename-key-project]').forEach(button => button.addEventListener('click', stopAnd(() => {
    openRenameProjectKeyModal(button.dataset.renameKeyProject);
  })));
  document.querySelectorAll('[data-archive-project]').forEach(button => button.addEventListener('click', stopAnd(async () => {
    const key = button.dataset.archiveProject;
    const archived = button.dataset.archived !== 'true';
    try {
      await api(`/api/v1/projects/${encodeURIComponent(key)}/archived`, { method: 'PATCH', body: JSON.stringify({ archived }) });
      showToast(`${key} ${archived ? 'archived' : 'unarchived'}`);
      await loadBaseData();
      await renderProjectsView(false);
    } catch (error) { showToast(error.message); }
  })));
  document.querySelectorAll('[data-delete-project]').forEach(button => button.addEventListener('click', stopAnd(async () => {
    const key = button.dataset.deleteProject;
    try {
      await api(`/api/v1/projects/${encodeURIComponent(key)}`, { method: 'DELETE' });
      showToast(`${key} moved to the recycle bin`);
      await loadBaseData();
      await renderProjectsView(false);
    } catch (error) { showToast(error.message); }
  })));
  document.querySelectorAll('[data-restore-project]').forEach(button => button.addEventListener('click', stopAnd(async () => {
    const key = button.dataset.restoreProject;
    try {
      await api(`/api/v1/projects/${encodeURIComponent(key)}/restore`, { method: 'POST' });
      showToast(`${key} restored`);
      await loadBaseData();
      await renderProjectsView(true);
    } catch (error) { showToast(error.message); }
  })));
  document.querySelectorAll('[data-permanent-project]').forEach(button => button.addEventListener('click', stopAnd(async () => {
    const key = button.dataset.permanentProject;
    try {
      await api(`/api/v1/projects/${encodeURIComponent(key)}/permanent`, { method: 'DELETE' });
      showToast(`${key} permanently deleted`);
      await renderProjectsView(true);
    } catch (error) { showToast(error.message); }
  })));
}

function openProjectModal() {
  document.querySelector('#project-error').classList.add('hidden');
  projectModal.classList.remove('hidden');
  projectModal.querySelector('input[name="key"]').focus();
}

function closeProjectModal() {
  projectModal.classList.add('hidden');
}

async function renderCurrentView() {
  try {
    if (state.view === 'dashboard') await renderDashboard();
    else if (state.view === 'board') await renderBoard();
    else if (state.view === 'backlog') await renderBacklog();
    else if (state.view === 'tickets') await renderTickets();
    else if (state.view === 'account') await renderAccountView();
    else if (state.view === 'audit') await renderAuditLog();
    else if (state.view === 'attachment-bin') await renderAttachmentRecycleBin();
    else if (state.view === 'webhooks') await renderWebhooks();
    else await renderProjects();
  } catch (error) {
    showError(error);
  }
}

function navigate(view) {
  state.view = view;
  document.querySelectorAll('.nav-item').forEach(item => item.classList.toggle('active', item.dataset.view === view));
  document.querySelector('#sidebar').classList.remove('open');
  renderCurrentView();
}

function bindTicketLinks() {
  const elements = [...document.querySelectorAll('[data-ticket-key]')];
  elements.forEach((element, index) => {
    element.addEventListener('click', () => openTicket(element.dataset.ticketKey));
    makeKeyboardActivatable(element, () => openTicket(element.dataset.ticketKey));
    // Roving Up/Down focus (Tab already reaches every row/card, but that
    // takes one Tab press per row -- Up/Down moves directly to the
    // previous/next ticket in reading order, matching Jira's own
    // table/board keyboard navigation). Left/Right on the board specifically
    // are handled separately in bindBoardKeyboardNav, since "next column" is
    // not "next element in DOM order".
    element.addEventListener('keydown', event => {
      if (event.target !== element) return;
      if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;
      const next = elements[event.key === 'ArrowDown' ? index + 1 : index - 1];
      if (!next) return;
      event.preventDefault();
      next.focus();
    });
  });
}

// Left/Right moves focus to the card at the same position in the
// previous/next board column (clamped to that column's last card if it's
// shorter) -- a plain "next element in DOM order" rule (as used for
// Up/Down in bindTicketLinks) would just walk down the current column
// instead, since board cards are grouped by column in the markup.
function bindBoardKeyboardNav() {
  const columns = [...document.querySelectorAll('.board-list[data-status-key]')].map(list => [...list.querySelectorAll('[data-ticket-key]')]);
  columns.forEach((cards, columnIndex) => {
    cards.forEach((card, rowIndex) => {
      card.addEventListener('keydown', event => {
        if (event.target !== card) return;
        if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
        const targetColumn = columns[event.key === 'ArrowRight' ? columnIndex + 1 : columnIndex - 1];
        if (!targetColumn || !targetColumn.length) return;
        event.preventDefault();
        targetColumn[Math.min(rowIndex, targetColumn.length - 1)].focus();
      });
    });
  });
}

// Table rows, board cards, and inline key references are clickable but are
// not natively focusable/keyboard-operable elements (D47) -- this makes them
// behave like a link for keyboard and assistive-technology users without
// changing their existing mouse-click behavior or markup structure.
function makeKeyboardActivatable(element, activate) {
  element.tabIndex = 0;
  if (!element.hasAttribute('role')) element.setAttribute('role', 'link');
  element.addEventListener('keydown', event => {
    if (event.key !== 'Enter' && event.key !== ' ') return;
    if (event.target !== element) return;
    event.preventDefault();
    activate();
  });
}

// Resolution is required exactly when moving to a Done-category status
// (D68-D70) -- omitted (null) for every other transition, which leaves it
// untouched, or lets the server clear it automatically when reopening.
async function applyStatusChange(ticketKey, statusKey, resolution, expectedVersion) {
  try {
    const payload = { statusKey, expectedVersion };
    if (resolution) payload.resolution = resolution;
    await api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/status`, { method: 'PATCH', body: JSON.stringify(payload) });
    showToast(`${ticketKey} status updated`);
    await renderCurrentView();
    await openTicket(ticketKey);
  } catch (error) {
    if (error.status === 409) {
      showConflictDialog(ticketKey);
    } else {
      showToast(error.message);
    }
  }
}

function editFieldsMarkup(ticket) {
  const typeOptions = [['task', 'Task'], ['story', 'Story'], ['bug', 'Bug'], ['epic', 'Epic'], ['sub-task', 'Sub-task']];
  return `
    <label>Type<select id="edit-type">${typeOptions.map(([key, label]) => `<option value="${key}" ${key === ticket.type.key ? 'selected' : ''}>${label}</option>`).join('')}</select></label>
    <label class="wide" id="edit-parent-label">
      <span id="edit-parent-label-text">Epic (optional)</span>
      <select id="edit-parent"><option value="">None</option></select>
    </label>
    <label class="wide">Summary<input id="edit-summary" value="${escapeHtml(ticket.summary)}" maxlength="255" required></label>
    <label class="wide">Description<textarea id="edit-description" rows="6">${escapeHtml(ticket.description)}</textarea></label>
    <label>Priority<select id="edit-priority">${['highest', 'high', 'medium', 'low', 'lowest'].map(key => `<option value="${key}" ${key === ticket.priority.key ? 'selected' : ''}>${key[0].toUpperCase()}${key.slice(1)}</option>`).join('')}</select></label>
    <label>Assignee<select id="edit-assignee">
      <option value="">Unassigned</option>
      <option value="demo@ticket-hub.local" ${ticket.assignee?.email === 'demo@ticket-hub.local' ? 'selected' : ''}>Demo User</option>
      <option value="alex@ticket-hub.local" ${ticket.assignee?.email === 'alex@ticket-hub.local' ? 'selected' : ''}>Alex Morgan</option>
      <option value="sam@ticket-hub.local" ${ticket.assignee?.email === 'sam@ticket-hub.local' ? 'selected' : ''}>Sam Lee</option>
    </select></label>
    <label>Story points<input id="edit-story-points" type="number" min="0" max="10000" step="0.5" value="${ticket.storyPoints ?? ''}"></label>
    <label>Due date<input id="edit-due-date" type="date" value="${ticket.dueDate ?? ''}"></label>
    <label class="wide">Labels<input id="edit-labels" value="${escapeHtml(ticket.labels.join(', '))}"></label>
    <label>Component<select id="edit-component"><option value="">None</option></select></label>`;
}

// Jira-style direct ticket links (/browse/TH-123), synced via history.
// pushState rather than a full page navigation. openTicket/closeDrawer only
// push a new history entry when the URL doesn't already reflect the target
// state -- this is what makes it safe to call openTicket(ticket.key) after
// every in-drawer mutation (edit, comment, watch/vote, worklog, ...) the
// way the rest of this file already does, without spamming the browser's
// back-button history with duplicate entries for the same ticket, and what
// makes the popstate handler below not re-push the URL it's reacting to.
function ticketUrlFor(ticketKey) {
  return `/browse/${encodeURIComponent(ticketKey)}`;
}

function ticketKeyFromLocation() {
  const match = window.location.pathname.match(/^\/browse\/([^/]+)$/);
  return match ? decodeURIComponent(match[1]) : null;
}

async function openTicketFromUrlIfAny() {
  const ticketKey = ticketKeyFromLocation();
  if (ticketKey) {
    await openTicket(ticketKey);
  }
}

// Jira-style Activity tabs (Comments / Work log) in the ticket drawer.
// Module-level (not scoped inside openTicket) so it survives a full
// drawer re-open -- e.g. logging time switches this to 'worklog' right
// before the drawer re-fetches, so the newly-logged entry is immediately
// visible instead of being hidden behind the default Comments tab.
let activeActivityTab = 'comments';

async function openTicket(ticketKey, editing = false) {
  if (ticketKeyFromLocation() !== ticketKey) {
    history.pushState({ ticketKey }, '', ticketUrlFor(ticketKey));
  }
  ticketDrawer.classList.remove('hidden');
  drawerBackdrop.classList.remove('hidden');
  ticketDrawer.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  try {
    const [ticket, comments, links, watchers, voters, worklogs, attachments, history, customFieldValues] = await Promise.all([
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}`),
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/comments`),
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/links`),
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/watchers`),
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/voters`),
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/worklogs`),
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/attachments`),
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/history`),
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/custom-fields`)
    ]);
    state.currentTicket = ticket;
    let attachmentSort = 'date';

    // Custom fields (D9): the ticket-scoped fetch above returns
    // {fieldId, name, fieldType, value} (enough to display), but not
    // `options`/`required` -- the edit form needs those, so a second,
    // project-scoped fetch gets the full field definitions and this merges
    // in each one's current value by fieldId. One extra request per drawer
    // open, only when the ticket's project actually has custom fields.
    let customFields = [];
    try {
      const definitions = (await api(`/api/v1/projects/${encodeURIComponent(ticket.projectKey)}/custom-fields`)).items;
      customFields = definitions.map(field => ({
        ...field,
        value: customFieldValues.items.find(entry => entry.fieldId === field.id)?.value ?? null
      }));
    } catch {
      // Leave the ticket drawer working even if custom fields can't be
      // loaded (e.g. transient network error) -- just show none.
    }

    // Fixed emoji reactions (D84): one reactions list per comment, fetched
    // alongside everything else -- fine at demo scale, mirrors the
    // watchers/voters fetch-once-per-open pattern above.
    const reactionLists = await Promise.all(comments.items.map(comment =>
      api(`/api/v1/tickets/${encodeURIComponent(ticketKey)}/comments/${encodeURIComponent(comment.id)}/reactions`)));
    const reactionsByComment = new Map(comments.items.map((comment, index) => [comment.id, reactionLists[index].items]));

    const isWatching = watchers.items.some(user => user.id === state.principal?.userId);
    const isVoting = voters.items.some(user => user.id === state.principal?.userId);

    function render(editing) {
      const statusCategory = STATUSES.find(status => status.key === ticket.status.key)?.category || 'todo';
      ticketDrawer.innerHTML = `
        <div class="drawer-header"><span class="ticket-key">${escapeHtml(ticket.key)}</span><button class="icon-button" id="close-drawer" aria-label="Close">×</button></div>
        <div class="drawer-content">
          <span class="ticket-type"><span style="color:${escapeHtml(ticket.type.color)}">${escapeHtml(ticket.type.icon)}</span>${escapeHtml(ticket.type.name)} · ${escapeHtml(ticket.projectName)}</span>
          ${editing
            ? `<div class="form-grid" id="edit-form">${editFieldsMarkup(ticket)}</div>
               <div id="edit-error" class="form-error hidden"></div>
               <div class="modal-footer" style="padding:0 0 20px"><button type="button" class="secondary-button" id="edit-cancel">Cancel</button><button type="button" class="primary-button" id="edit-save">Save changes</button></div>`
            : `<h1>${escapeHtml(ticket.summary)}</h1>`}
          <div class="ticket-toolbar">
            <div class="ticket-toolbar-status">
              <select id="drawer-status" class="status-pill status-pill--${escapeHtml(statusCategory)}">${STATUSES.map(status => `<option value="${status.key}" ${status.key === ticket.status.key ? 'selected' : ''}>${status.name}</option>`).join('')}</select>
              <div class="resolution-inline" id="drawer-resolution-row" ${statusCategory === 'done' ? '' : 'hidden'}>
                ${ticket.resolution ? `<span class="resolution-label">Resolution: <strong>${escapeHtml(resolutionLabel(ticket.resolution))}</strong></span>` : `
                <select id="drawer-resolution">${RESOLUTIONS.map(resolution => `<option value="${resolution.key}">${resolution.name}</option>`).join('')}</select>
                <button type="button" class="secondary-button" id="resolution-cancel">Cancel</button>
                <button type="button" class="primary-button" id="resolution-confirm">Confirm</button>`}
              </div>
            </div>
            <div class="drawer-actions">
              <button type="button" class="secondary-button" id="watch-toggle">${isWatching ? '★ Watching' : '☆ Watch'} (${watchers.items.length})</button>
              <button type="button" class="secondary-button" id="vote-toggle">${isVoting ? '▲ Voted' : '△ Vote'} (${voters.items.length})</button>
              <button type="button" class="secondary-button" id="clone-ticket">⧉ Clone</button>
              ${editing ? '' : '<button type="button" class="secondary-button" id="edit-ticket">✎ Edit</button>'}
              ${editing ? '' : '<button type="button" class="secondary-button" id="delete-ticket">🗑 Delete</button>'}
            </div>
          </div>
          <div class="drawer-layout">
            <div class="drawer-main">
              ${editing ? '' : `<section class="drawer-section"><h3>Description</h3><div class="description markdown-body">${ticket.description ? renderMarkdown(ticket.description) : '<p class="markdown-empty">No description provided.</p>'}</div></section>`}
              <section class="drawer-section">
                <h3>Linked issues</h3>
                <div class="link-list">${links.items.length ? links.items.map(link => `
                  <div class="link-row">
                    <span class="link-label">${escapeHtml(link.label)}</span>
                    <span class="ticket-key" data-ticket-key="${escapeHtml(link.otherTicketKey)}">${escapeHtml(link.otherTicketKey)}</span>
                    <span class="link-summary">${escapeHtml(link.otherTicketSummary)}</span>
                    <button type="button" class="icon-button" data-delete-link="${escapeHtml(link.id)}" aria-label="Remove link">×</button>
                  </div>`).join('') : '<div class="empty-state">No links yet.</div>'}</div>
                <form class="link-form" id="link-form">
                  <select name="linkType">
                    <option value="blocks">blocks</option>
                    <option value="relates_to">relates to</option>
                    <option value="duplicates">duplicates</option>
                  </select>
                  <input name="targetTicketKey" placeholder="Ticket key, e.g. TH-3" required>
                  <button class="secondary-button" type="submit">Add link</button>
                </form>
              </section>
              <section class="drawer-section">
                <h3>Attachments</h3>
                <div class="attachment-toolbar">
                  <span class="eyebrow">${attachments.items.length} file${attachments.items.length === 1 ? '' : 's'}</span>
                  <label>Sort by <select id="attachment-sort">
                    <option value="date" ${attachmentSort === 'date' ? 'selected' : ''}>Date</option>
                    <option value="name" ${attachmentSort === 'name' ? 'selected' : ''}>Name</option>
                    <option value="size" ${attachmentSort === 'size' ? 'selected' : ''}>Size</option>
                    <option value="author" ${attachmentSort === 'author' ? 'selected' : ''}>Uploader</option>
                    <option value="type" ${attachmentSort === 'type' ? 'selected' : ''}>Type</option>
                  </select></label>
                </div>
                <div class="attachment-list" id="attachment-list">${sortAttachments(attachments.items, attachmentSort).map(attachment => {
                  const canDelete = attachment.uploader.id === state.principal?.userId || state.principal?.isAdmin;
                  const previewKind = attachmentPreviewKind(attachment.contentType);
                  return `
                  <div>
                    <div class="attachment-row" data-attachment-id="${escapeHtml(attachment.id)}">
                      <span>${attachmentIcon(attachment.contentType)}</span>
                      <button type="button" class="attachment-name" data-preview-attachment="${escapeHtml(attachment.id)}" data-preview-kind="${previewKind || ''}" title="${previewKind ? 'Click to preview' : 'Click to download'}">${escapeHtml(attachment.fileName)}</button>
                      <span class="attachment-meta">${formatByteSize(attachment.byteSize)}</span>
                      <span class="attachment-meta">${escapeHtml(attachment.uploader.displayName)} · ${escapeHtml(relativeDate(attachment.createdAt))}</span>
                      ${canDelete ? `<button type="button" class="icon-button" data-delete-attachment="${escapeHtml(attachment.id)}" aria-label="Delete attachment">×</button>` : '<span></span>'}
                    </div>
                    <div class="attachment-preview hidden" id="attachment-preview-${escapeHtml(attachment.id)}"></div>
                  </div>`;
                }).join('') || '<div class="empty-state">No attachments yet.</div>'}</div>
                <input type="file" id="attachment-file-input" multiple hidden>
                <button type="button" class="secondary-button" id="attachment-upload-button">📎 Attach files</button>
                <div class="attachment-dropzone" id="attachment-dropzone">Drag and drop files here, or use "Attach files" above (max 25MB each, 20 per ticket)</div>
              </section>
              <section class="drawer-section drawer-activity">
                <div class="activity-tabs">
                  <button type="button" class="activity-tab ${activeActivityTab === 'comments' ? 'active' : ''}" data-activity-tab="comments">Comments (${comments.items.length})</button>
                  <button type="button" class="activity-tab ${activeActivityTab === 'worklog' ? 'active' : ''}" data-activity-tab="worklog">Work log (${worklogs.items.length})</button>
                  <button type="button" class="activity-tab ${activeActivityTab === 'history' ? 'active' : ''}" data-activity-tab="history">History (${history.items.length})</button>
                </div>
                <div class="activity-panel" data-activity-panel="comments" ${activeActivityTab === 'comments' ? '' : 'hidden'}>
                  <form class="comment-form" id="comment-form"><textarea name="body" rows="3" required placeholder="Add a comment…"></textarea><button class="primary-button" type="submit">Comment</button></form>
                  <div class="comment-list">${comments.items.length ? comments.items.map(comment => {
                    // Simplified permissions (D83): the author can always
                    // edit/delete their own comment. A project admin (not
                    // global admin) could too per the server, but the client
                    // never loads per-project role, so the buttons are shown
                    // only for the author or a global admin -- a conservative
                    // UI simplification, not a security boundary (the server
                    // enforces the real rule regardless).
                    const canModerate = comment.author.id === state.principal?.userId || state.principal?.isAdmin;
                    const reactions = reactionsByComment.get(comment.id) || [];
                    return `
                    <article class="comment" data-comment-id="${escapeHtml(comment.id)}">
                      <span class="small-avatar">${escapeHtml(initials(comment.author.displayName))}</span>
                      <div class="comment-body">
                        <header>
                          <strong>${escapeHtml(comment.author.displayName)}</strong>
                          <span>${escapeHtml(relativeDate(comment.createdAt))}${comment.editedAt ? ' (edited)' : ''}</span>
                        </header>
                        <div class="comment-body-text markdown-body">${renderMarkdown(comment.body)}</div>
                        <div class="comment-reactions">${COMMENT_REACTIONS.map(reaction => {
                          const reactedUsers = reactions.filter(entry => entry.reactionKey === reaction.key);
                          const mine = reactedUsers.some(entry => entry.user.id === state.principal?.userId);
                          return `<button type="button" class="reaction-button${mine ? ' reaction-button--active' : ''}"
                            data-toggle-reaction="${escapeHtml(comment.id)}" data-reaction-key="${reaction.key}"
                            title="${reaction.key}">${reaction.emoji}${reactedUsers.length ? ` ${reactedUsers.length}` : ''}</button>`;
                        }).join('')}</div>
                        ${canModerate ? `<div class="comment-actions">
                          <button type="button" class="ghost-button" data-edit-comment="${escapeHtml(comment.id)}">Edit</button>
                          <button type="button" class="ghost-button" data-delete-comment="${escapeHtml(comment.id)}">Delete</button>
                        </div>` : ''}
                      </div>
                    </article>`;
                  }).join('') : '<div class="empty-state">No comments yet.</div>'}</div>
                </div>
                <div class="activity-panel" data-activity-panel="worklog" ${activeActivityTab === 'worklog' ? '' : 'hidden'}>
                  <form class="worklog-form" id="worklog-form">
                    <input name="workDate" type="date" required value="${new Date().toISOString().slice(0, 10)}">
                    <input name="duration" placeholder="e.g. 1h 30m" required pattern="^(\\d+h)?\\s*(\\d+m)?$">
                    <textarea name="comment" rows="2" placeholder="What did you work on? (optional, Markdown supported)"></textarea>
                    <button class="secondary-button" type="submit">Log time</button>
                  </form>
                  <div class="worklog-list">${worklogs.items.length ? worklogs.items.map(worklog => `
                    <div class="worklog-row" data-worklog-id="${escapeHtml(worklog.id)}">
                      <span class="small-avatar">${escapeHtml(initials(worklog.author.displayName))}</span>
                      <div class="worklog-details">
                        <span><strong>${escapeHtml(formatDuration(worklog.timeSpentSeconds))}</strong> by ${escapeHtml(worklog.author.displayName)} on ${escapeHtml(formatDate(worklog.workDate))}</span>
                        ${worklog.comment ? `<div class="worklog-comment markdown-body">${renderMarkdown(worklog.comment)}</div>` : ''}
                      </div>
                      <button type="button" class="icon-button" data-delete-worklog="${escapeHtml(worklog.id)}" aria-label="Delete worklog">×</button>
                    </div>`).join('') : '<div class="empty-state">No time logged yet.</div>'}</div>
                </div>
                <div class="activity-panel" data-activity-panel="history" ${activeActivityTab === 'history' ? '' : 'hidden'}>
                  <div class="history-list">${history.items.length ? history.items.map(entry => `
                    <div class="history-row">
                      <span class="small-avatar">${entry.actor ? escapeHtml(initials(entry.actor.displayName)) : '—'}</span>
                      <div class="history-details">
                        <span><strong>${entry.actor ? escapeHtml(entry.actor.displayName) : 'System'}</strong> ${escapeHtml(historyChangeText(entry))}</span>
                        <span class="history-date">${escapeHtml(relativeDate(entry.createdAt))}</span>
                      </div>
                    </div>`).join('') : '<div class="empty-state">No history recorded yet.</div>'}</div>
                </div>
              </section>
            </div>
            <aside class="drawer-sidebar">
              <div class="sidebar-panel">
                <h4>Details</h4>
                ${editing ? '' : `<div class="meta-row"><span>Assignee</span>${assigneeMarkup(ticket)}</div>`}
                <div class="meta-row"><span>Reporter</span>${escapeHtml(ticket.reporter.displayName)}</div>
                ${editing ? '' : `<div class="meta-row"><span>Priority</span>${priorityChip(ticket)}</div>`}
                ${ticket.parentTicketKey ? `<div class="meta-row"><span>Parent</span><strong class="ticket-key" id="drawer-parent-link" style="cursor:pointer">${escapeHtml(ticket.parentTicketKey)}</strong></div>` : ''}
                ${editing ? '' : `
                <div class="meta-row"><span>Labels</span><div>${labelsMarkup(ticket.labels) || '—'}</div></div>
                <div class="meta-row"><span>Component</span><strong>${ticket.component ? escapeHtml(ticket.component.name) : '—'}</strong></div>
                <div class="meta-row"><span>Story points</span><strong>${ticket.storyPoints ?? '—'}</strong></div>
                <div class="meta-row"><span>Due date</span><strong>${escapeHtml(formatDate(ticket.dueDate))}</strong></div>`}
                ${editing || state.projects.filter(project => project.key !== ticket.projectKey).length === 0 ? '' : `
                <div class="meta-row">
                  <span>Move to project</span>
                  <div style="display:flex;gap:6px">
                    <select id="move-target-project" class="status-select">${state.projects.filter(project => project.key !== ticket.projectKey).map(project => `<option value="${escapeHtml(project.key)}">${escapeHtml(project.key)}</option>`).join('')}</select>
                    <button type="button" class="secondary-button" id="move-ticket-button">Move</button>
                  </div>
                </div>`}
              </div>
              ${customFields.length ? `
              <div class="sidebar-panel" id="custom-fields-panel">
                <h4>Custom fields</h4>
                ${editing
                  ? `<div class="form-grid" style="grid-template-columns:1fr">${customFields.map(field => customFieldInputMarkup(field, field.value)).join('')}</div>`
                  : customFields.map(field => `<div class="meta-row"><span>${escapeHtml(field.name)}</span><strong>${field.value ? escapeHtml(field.value) : '—'}</strong></div>`).join('')}
              </div>` : ''}
              <div class="sidebar-panel sidebar-panel--dates">
                <div class="meta-row-compact"><span>Created</span><strong>${escapeHtml(formatDate(ticket.createdAt))}</strong></div>
                <div class="meta-row-compact"><span>Updated</span><strong>${escapeHtml(relativeDate(ticket.updatedAt))}</strong></div>
              </div>
            </aside>
          </div>
        </div>`;

      document.querySelector('#close-drawer').addEventListener('click', closeDrawer);
      document.querySelectorAll('[data-activity-tab]').forEach(button => button.addEventListener('click', () => {
        activeActivityTab = button.dataset.activityTab;
        render(editing);
      }));
      if (ticket.parentTicketKey) {
        document.querySelector('#drawer-parent-link').addEventListener('click', () => openTicket(ticket.parentTicketKey));
      }
      document.querySelector('#move-ticket-button')?.addEventListener('click', async () => {
        const targetProjectKey = document.querySelector('#move-target-project').value;
        try {
          const moved = await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/move`, { method: 'POST', body: JSON.stringify({ targetProjectKey }) });
          showToast(`${ticket.key} moved to ${moved.key}`);
          await loadBaseData();
          await renderCurrentView();
          await openTicket(moved.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('.link-row [data-ticket-key]').forEach(element => {
        element.addEventListener('click', () => openTicket(element.dataset.ticketKey));
      });

      document.querySelector('#watch-toggle').addEventListener('click', async () => {
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/watch`, { method: isWatching ? 'DELETE' : 'POST' });
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#vote-toggle').addEventListener('click', async () => {
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/vote`, { method: isVoting ? 'DELETE' : 'POST' });
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#clone-ticket').addEventListener('click', async () => {
        try {
          const cloned = await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/clone`, { method: 'POST' });
          showToast(`${ticket.key} cloned as ${cloned.key}`);
          await renderCurrentView();
          await openTicket(cloned.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#delete-ticket')?.addEventListener('click', async () => {
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}`, { method: 'DELETE' });
          showToast(`${ticket.key} moved to the recycle bin`);
          closeDrawer();
          await renderCurrentView();
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#link-form').addEventListener('submit', async event => {
        event.preventDefault();
        const values = Object.fromEntries(new FormData(event.currentTarget).entries());
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/links`, {
            method: 'POST',
            body: JSON.stringify({ targetTicketKey: values.targetTicketKey.trim(), linkType: values.linkType })
          });
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('[data-delete-link]').forEach(button => {
        button.addEventListener('click', async () => {
          try {
            await api(`/api/v1/ticket-links/${encodeURIComponent(button.dataset.deleteLink)}`, { method: 'DELETE' });
            await openTicket(ticket.key);
          } catch (error) { showToast(error.message); }
        });
      });

      document.querySelector('#drawer-status').addEventListener('change', async event => {
        const newStatusKey = event.target.value;
        const newStatus = STATUSES.find(status => status.key === newStatusKey);
        if (newStatus?.category === 'done' && !ticket.resolution) {
          document.querySelector('#drawer-resolution-row').hidden = false;
          return;
        }
        await applyStatusChange(ticket.key, newStatusKey, null, ticket.version);
      });
      document.querySelector('#resolution-confirm')?.addEventListener('click', async () => {
        const resolution = document.querySelector('#drawer-resolution').value;
        const statusKey = document.querySelector('#drawer-status').value;
        await applyStatusChange(ticket.key, statusKey, resolution, ticket.version);
      });
      document.querySelector('#resolution-cancel')?.addEventListener('click', () => {
        document.querySelector('#drawer-status').value = ticket.status.key;
        document.querySelector('#drawer-resolution-row').hidden = true;
      });
      attachMarkdownToolbar(document.querySelector('#worklog-form textarea[name=comment]'), ticket.key);
      attachMentionAutocomplete(document.querySelector('#worklog-form textarea[name=comment]'));
      document.querySelector('#worklog-form').addEventListener('submit', async event => {
        event.preventDefault();
        const values = Object.fromEntries(new FormData(event.currentTarget).entries());
        const timeSpentSeconds = parseDurationToSeconds(values.duration);
        if (!timeSpentSeconds) {
          showToast('Duration must look like "1h 30m", "2h", or "45m"');
          return;
        }
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/worklogs`, {
            method: 'POST',
            body: JSON.stringify({ workDate: values.workDate, timeSpentSeconds, comment: values.comment.trim() || null })
          });
          showToast('Time logged');
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('[data-delete-worklog]').forEach(button => button.addEventListener('click', async () => {
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/worklogs/${encodeURIComponent(button.dataset.deleteWorklog)}`, { method: 'DELETE' });
          showToast('Worklog deleted');
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      }));

      document.querySelector('#attachment-sort').addEventListener('change', event => {
        attachmentSort = event.target.value;
        render(editing);
      });
      document.querySelectorAll('[data-preview-attachment]').forEach(button => button.addEventListener('click', () => {
        const id = button.dataset.previewAttachment;
        const kind = button.dataset.previewKind;
        const container = document.querySelector(`#attachment-preview-${CSS.escape(id)}`);
        if (!kind) {
          window.open(`/api/v1/attachments/${encodeURIComponent(id)}/download`, '_blank');
          return;
        }
        if (!container.classList.contains('hidden')) {
          container.classList.add('hidden');
          container.innerHTML = '';
          return;
        }
        const url = `/api/v1/attachments/${encodeURIComponent(id)}/download`;
        // `sandbox=""` (no flags) disables script execution, plugins, and
        // top-navigation inside the iframe -- an uploaded attachment's
        // declared Content-Type is caller-supplied (D98 deliberately has no
        // upload-time MIME allow-list) and is echoed back verbatim on
        // download, so a file uploaded with a spoofed `text/html`
        // Content-Type must never get a chance to execute script in this
        // preview, regardless of what the server serves it as.
        const markup = {
          image: `<img src="${url}" alt="">`,
          pdf: `<iframe src="${url}" title="PDF preview" sandbox=""></iframe>`,
          text: `<iframe src="${url}" title="Text preview" sandbox=""></iframe>`,
          audio: `<audio controls src="${url}"></audio>`,
          video: `<video controls src="${url}"></video>`
        }[kind];
        container.innerHTML = markup || '';
        container.classList.remove('hidden');
      }));
      document.querySelectorAll('[data-delete-attachment]').forEach(button => button.addEventListener('click', async () => {
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/attachments/${encodeURIComponent(button.dataset.deleteAttachment)}`, { method: 'DELETE' });
          showToast('Attachment deleted');
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      }));
      const uploadFiles = async files => {
        for (const file of files) {
          try {
            await uploadAttachmentFile(ticket.key, file);
          } catch (error) {
            showToast(`${file.name}: ${error.message}`);
          }
        }
        await openTicket(ticket.key);
      };
      document.querySelector('#attachment-upload-button').addEventListener('click', () => {
        document.querySelector('#attachment-file-input').click();
      });
      document.querySelector('#attachment-file-input').addEventListener('change', event => {
        if (event.target.files.length) uploadFiles([...event.target.files]);
      });
      const dropzone = document.querySelector('#attachment-dropzone');
      dropzone.addEventListener('dragover', event => { event.preventDefault(); dropzone.classList.add('drag-over'); });
      dropzone.addEventListener('dragleave', () => dropzone.classList.remove('drag-over'));
      dropzone.addEventListener('drop', event => {
        event.preventDefault();
        dropzone.classList.remove('drag-over');
        if (event.dataTransfer.files.length) uploadFiles([...event.dataTransfer.files]);
      });

      attachMarkdownToolbar(document.querySelector('#comment-form textarea[name=body]'), ticket.key);
      attachMentionAutocomplete(document.querySelector('#comment-form textarea[name=body]'));
      document.querySelector('#comment-form').addEventListener('submit', async event => {
        event.preventDefault();
        const body = new FormData(event.currentTarget).get('body').trim();
        if (!body) return;
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/comments`, { method: 'POST', body: JSON.stringify({ body }) });
          showToast('Comment added');
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('[data-delete-comment]').forEach(button => button.addEventListener('click', async () => {
        try {
          await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/comments/${encodeURIComponent(button.dataset.deleteComment)}`, { method: 'DELETE' });
          showToast('Comment deleted');
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      }));
      document.querySelectorAll('[data-toggle-reaction]').forEach(button => button.addEventListener('click', async () => {
        const commentId = button.dataset.toggleReaction;
        const reactionKey = button.dataset.reactionKey;
        const path = `/api/v1/tickets/${encodeURIComponent(ticket.key)}/comments/${encodeURIComponent(commentId)}/reactions/${encodeURIComponent(reactionKey)}`;
        try {
          await api(path, { method: button.classList.contains('reaction-button--active') ? 'DELETE' : 'POST' });
          await openTicket(ticket.key);
        } catch (error) { showToast(error.message); }
      }));
      document.querySelectorAll('[data-edit-comment]').forEach(button => button.addEventListener('click', () => {
        const commentId = button.dataset.editComment;
        const comment = comments.items.find(candidate => candidate.id === commentId);
        const article = document.querySelector(`[data-comment-id="${CSS.escape(commentId)}"]`);
        const bodyElement = article.querySelector('.comment-body-text');
        bodyElement.innerHTML = `
          <textarea class="comment-edit-textarea" rows="3">${escapeHtml(comment.body)}</textarea>
          <div class="comment-edit-actions">
            <button type="button" class="secondary-button" id="comment-edit-cancel">Cancel</button>
            <button type="button" class="primary-button" id="comment-edit-save">Save</button>
          </div>`;
        attachMarkdownToolbar(article.querySelector('.comment-edit-textarea'), ticket.key);
        attachMentionAutocomplete(article.querySelector('.comment-edit-textarea'));
        article.querySelector('#comment-edit-cancel').addEventListener('click', () => openTicket(ticket.key));
        article.querySelector('#comment-edit-save').addEventListener('click', async () => {
          const newBody = article.querySelector('.comment-edit-textarea').value.trim();
          if (!newBody) return;
          try {
            await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}/comments/${encodeURIComponent(commentId)}`, {
              method: 'PATCH',
              body: JSON.stringify({ body: newBody, expectedVersion: comment.version })
            });
            showToast('Comment updated');
            await openTicket(ticket.key);
          } catch (error) { showToast(error.message); }
        });
      }));

      if (editing) {
        attachMarkdownToolbar(document.querySelector('#edit-description'), ticket.key);
        refreshEditParentOptions(ticket).catch(() => {});
        refreshEditComponentOptions(ticket).catch(() => {});
        document.querySelector('#edit-type').addEventListener('change', () => refreshEditParentOptions(ticket).catch(() => {}));
        document.querySelector('#edit-cancel').addEventListener('click', () => render(false));
        document.querySelector('#edit-save').addEventListener('click', async () => {
          const errorElement = document.querySelector('#edit-error');
          const storyPointsValue = document.querySelector('#edit-story-points').value;
          const payload = {
            summary: document.querySelector('#edit-summary').value.trim(),
            description: document.querySelector('#edit-description').value.trim(),
            priorityKey: document.querySelector('#edit-priority').value,
            assigneeEmail: document.querySelector('#edit-assignee').value || null,
            storyPoints: storyPointsValue ? Number(storyPointsValue) : null,
            dueDate: document.querySelector('#edit-due-date').value || null,
            labels: document.querySelector('#edit-labels').value.split(',').map(value => value.trim()).filter(Boolean),
            ticketTypeKey: document.querySelector('#edit-type').value,
            parentTicketKey: document.querySelector('#edit-parent').value || null,
            componentName: document.querySelector('#edit-component').value || null,
            customFieldValues: collectCustomFieldValues(ticketDrawer, customFields),
            expectedVersion: ticket.version
          };
          try {
            await api(`/api/v1/tickets/${encodeURIComponent(ticket.key)}`, { method: 'PATCH', body: JSON.stringify(payload) });
            showToast(`${ticket.key} updated`);
            await renderCurrentView();
            await openTicket(ticket.key);
          } catch (error) {
            if (error.status === 409) {
              showConflictDialog(ticket.key);
            } else {
              errorElement.textContent = error.message;
              errorElement.classList.remove('hidden');
            }
          }
        });
      } else {
        document.querySelector('#edit-ticket').addEventListener('click', () => render(true));
      }
    }

    render(editing);
  } catch (error) {
    ticketDrawer.innerHTML = `<div class="drawer-header"><span>Ticket</span><button class="icon-button" id="close-drawer">×</button></div><div class="drawer-content"><div class="error-banner">${escapeHtml(error.message)}</div></div>`;
    document.querySelector('#close-drawer').addEventListener('click', closeDrawer);
  }
}

function closeDrawer() {
  ticketDrawer.classList.add('hidden');
  drawerBackdrop.classList.add('hidden');
  state.currentTicket = null;
  if (ticketKeyFromLocation()) {
    history.pushState(null, '', '/');
  }
}

// Populates the "Epic"/"Parent" picker to match the fixed hierarchy rules
// (D5, D29, D64-D66): an Epic may not have a parent at all; a Sub-task's
// parent must be a Story/Task/Bug in the same project; a Story/Task/Bug's
// optional parent must be an Epic in the same project. The server is the
// actual source of truth for this (see TicketService::requireValidHierarchy)
// -- this only narrows the picker's options so a valid choice is the
// common case, not a client-side substitute for that validation.
let createParentRequestId = 0;
async function refreshCreateParentOptions() {
  const requestId = ++createParentRequestId;
  const projectKey = document.querySelector('#create-project').value;
  const ticketTypeKey = document.querySelector('#create-ticket-type').value;
  const label = document.querySelector('#create-parent-label');
  const select = document.querySelector('#create-parent');
  const level = ticketTypeHierarchyLevel(ticketTypeKey);

  if (level === 1) {
    label.classList.add('hidden');
    select.value = '';
    return;
  }
  label.classList.remove('hidden');
  document.querySelector('#create-parent-label-text').textContent = level === -1 ? 'Parent (required)' : 'Epic (optional)';
  select.innerHTML = '<option value="">None</option>';
  if (!projectKey) return;

  const wantedLevel = level === -1 ? 0 : 1;
  try {
    const result = await api(`/api/v1/tickets?project=${encodeURIComponent(projectKey)}`);
    if (requestId !== createParentRequestId) return; // a newer call already superseded this one
    const candidates = result.items.filter(candidate => ticketTypeHierarchyLevel(candidate.type.key) === wantedLevel);
    select.innerHTML += candidates.map(candidate => `<option value="${escapeHtml(candidate.key)}">${escapeHtml(candidate.key)} — ${escapeHtml(candidate.summary)}</option>`).join('');
  } catch {
    // Leave just the "None" option if the project's tickets can't be loaded;
    // the create submit itself will surface a clearer error if needed.
  }
}

// Same purpose as refreshCreateParentOptions, adapted for the ticket drawer's
// edit form (re-typing/re-parenting an existing ticket): the project is
// fixed (moving a ticket between projects is a separate action, D37), and
// the ticket itself is excluded from its own candidate-parent list. The
// server (TicketService::validateHierarchyShape / IDatabase::editTicket) is
// the actual source of truth, including the "no children" rule for
// retyping across hierarchy levels that this picker doesn't attempt to
// predict client-side.
let editParentRequestId = 0;
async function refreshEditParentOptions(ticket) {
  const requestId = ++editParentRequestId;
  const ticketTypeKey = document.querySelector('#edit-type').value;
  const label = document.querySelector('#edit-parent-label');
  const select = document.querySelector('#edit-parent');
  const level = ticketTypeHierarchyLevel(ticketTypeKey);

  if (level === 1) {
    label.classList.add('hidden');
    select.value = '';
    return;
  }
  label.classList.remove('hidden');
  document.querySelector('#edit-parent-label-text').textContent = level === -1 ? 'Parent (required)' : 'Epic (optional)';
  const preselect = level === ticketTypeHierarchyLevel(ticket.type.key) ? ticket.parentTicketKey : null;
  select.innerHTML = '<option value="">None</option>';

  const wantedLevel = level === -1 ? 0 : 1;
  try {
    const result = await api(`/api/v1/tickets?project=${encodeURIComponent(ticket.projectKey)}`);
    if (requestId !== editParentRequestId) return; // a newer call already superseded this one
    const candidates = result.items.filter(candidate =>
      ticketTypeHierarchyLevel(candidate.type.key) === wantedLevel && candidate.key !== ticket.key);
    select.innerHTML += candidates.map(candidate =>
      `<option value="${escapeHtml(candidate.key)}" ${candidate.key === preselect ? 'selected' : ''}>${escapeHtml(candidate.key)} — ${escapeHtml(candidate.summary)}</option>`).join('');
  } catch {
    // Leave just the "None" option if the project's tickets can't be loaded.
  }
}

// Components are project-scoped (D19), so both pickers re-fetch whenever
// their project changes -- same request-id-guarded pattern as
// refreshCreateParentOptions/refreshEditParentOptions, to discard a stale
// response that resolves after a newer one already superseded it.
let createComponentRequestId = 0;
async function refreshCreateComponentOptions() {
  const requestId = ++createComponentRequestId;
  const projectKey = document.querySelector('#create-project').value;
  const select = document.querySelector('#create-component');
  select.innerHTML = '<option value="">None</option>';
  if (!projectKey) return;
  try {
    const result = await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/components`);
    if (requestId !== createComponentRequestId) return;
    select.innerHTML += result.items.map(component =>
      `<option value="${escapeHtml(component.name)}">${escapeHtml(component.name)}</option>`).join('');
  } catch {
    // Leave just the "None" option if the project's components can't be loaded.
  }
}

let editComponentRequestId = 0;
async function refreshEditComponentOptions(ticket) {
  const requestId = ++editComponentRequestId;
  const select = document.querySelector('#edit-component');
  select.innerHTML = '<option value="">None</option>';
  try {
    const result = await api(`/api/v1/projects/${encodeURIComponent(ticket.projectKey)}/components`);
    if (requestId !== editComponentRequestId) return;
    select.innerHTML += result.items.map(component =>
      `<option value="${escapeHtml(component.name)}" ${component.name === ticket.component?.name ? 'selected' : ''}>${escapeHtml(component.name)}</option>`).join('');
  } catch {
    // Leave just the "None" option if the project's components can't be loaded.
  }
}

// Custom fields (D9): shared between the create-ticket modal and the
// drawer's edit form. Every value is a single string on the wire (see
// migrations/*/018_custom_fields.sql), so multi_select is the one type
// that needs special handling on both write (join selected options with
// ", ") and read (split back into a Set for the <select multiple>).
function customFieldInputMarkup(field, currentValue) {
  const name = `cf_${field.id}`;
  const value = currentValue || '';
  const labelText = `${escapeHtml(field.name)}${field.required ? ' *' : ''}`;
  if (field.fieldType === 'checkbox') {
    return `<label><input type="checkbox" name="${name}" ${value === 'true' ? 'checked' : ''} style="width:auto;margin-right:6px">${labelText}</label>`;
  }
  if (field.fieldType === 'single_select') {
    return `<label>${labelText}
      <select name="${name}">
        <option value="">None</option>
        ${field.options.map(option => `<option value="${escapeHtml(option)}" ${option === value ? 'selected' : ''}>${escapeHtml(option)}</option>`).join('')}
      </select>
    </label>`;
  }
  if (field.fieldType === 'multi_select') {
    const selected = new Set(value.split(',').map(part => part.trim()).filter(Boolean));
    return `<label>${labelText}
      <select name="${name}" multiple size="${Math.max(2, Math.min(field.options.length, 4))}">
        ${field.options.map(option => `<option value="${escapeHtml(option)}" ${selected.has(option) ? 'selected' : ''}>${escapeHtml(option)}</option>`).join('')}
      </select>
    </label>`;
  }
  const inputType = field.fieldType === 'number' ? 'number' : field.fieldType === 'date' ? 'date' : 'text';
  return `<label>${labelText}
    <input type="${inputType}" name="${name}" value="${escapeHtml(value)}"${field.fieldType === 'number' ? ' step="any"' : ''}>
  </label>`;
}

function collectCustomFieldValues(form, fields) {
  const result = {};
  for (const field of fields) {
    const name = `cf_${field.id}`;
    const element = form.querySelector(`[name="${CSS.escape(name)}"]`);
    if (!element) continue;
    if (field.fieldType === 'checkbox') {
      result[field.id] = element.checked ? 'true' : '';
    } else if (field.fieldType === 'multi_select') {
      result[field.id] = [...element.selectedOptions].map(option => option.value).join(', ');
    } else {
      result[field.id] = element.value.trim();
    }
  }
  return result;
}

// The create modal's currently-selected project's fields, refreshed by
// refreshCreateCustomFieldOptions -- read back by the form's submit
// handler (a top-level listener registered once, not per-render) to know
// which cf_<id> inputs to collect and how.
let createModalCustomFields = [];

async function refreshCreateCustomFieldOptions() {
  const projectKey = document.querySelector('#create-project').value;
  const container = document.querySelector('#create-custom-fields');
  if (!projectKey) {
    container.innerHTML = '';
    createModalCustomFields = [];
    return;
  }
  try {
    createModalCustomFields = (await api(`/api/v1/projects/${encodeURIComponent(projectKey)}/custom-fields`)).items;
  } catch {
    createModalCustomFields = [];
  }
  container.innerHTML = createModalCustomFields.map(field => customFieldInputMarkup(field, '')).join('');
}

function openCreateModal() {
  document.querySelector('#create-error').classList.add('hidden');
  if (state.selectedProject) document.querySelector('#create-project').value = state.selectedProject;
  createModal.classList.remove('hidden');
  refreshCreateParentOptions().catch(() => {});
  refreshCreateComponentOptions().catch(() => {});
  refreshCreateCustomFieldOptions().catch(() => {});
  createModal.querySelector('input[name="summary"]').focus();
}

function closeCreateModal() {
  createModal.classList.add('hidden');
}

function openShortcutsModal() {
  document.querySelector('#shortcuts-modal').classList.remove('hidden');
}

function closeShortcutsModal() {
  document.querySelector('#shortcuts-modal').classList.add('hidden');
}

function debounce(fn, delay) {
  let timer;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), delay);
  };
}

document.querySelectorAll('.nav-item').forEach(item => item.addEventListener('click', () => navigate(item.dataset.view)));
document.querySelector('#create-button').addEventListener('click', openCreateModal);
document.querySelector('#create-project').addEventListener('change', () => refreshCreateParentOptions().catch(() => {}));
document.querySelector('#create-project').addEventListener('change', () => refreshCreateComponentOptions().catch(() => {}));
document.querySelector('#create-project').addEventListener('change', () => refreshCreateCustomFieldOptions().catch(() => {}));
document.querySelector('#create-ticket-type').addEventListener('change', () => refreshCreateParentOptions().catch(() => {}));
document.querySelectorAll('[data-close-modal]').forEach(button => button.addEventListener('click', () => {
  button.closest('.modal-backdrop')?.classList.add('hidden');
}));
createModal.addEventListener('click', event => { if (event.target === createModal) closeCreateModal(); });
projectModal.addEventListener('click', event => { if (event.target === projectModal) closeProjectModal(); });
const shortcutsModal = document.querySelector('#shortcuts-modal');
document.querySelector('#shortcuts-button').addEventListener('click', openShortcutsModal);
shortcutsModal.addEventListener('click', event => { if (event.target === shortcutsModal) closeShortcutsModal(); });
drawerBackdrop.addEventListener('click', closeDrawer);
document.querySelector('#menu-button').addEventListener('click', () => document.querySelector('#sidebar').classList.toggle('open'));

document.querySelector('#sidebar-projects').addEventListener('click', event => {
  const button = event.target.closest('[data-project]');
  if (!button) return;
  state.selectedProject = button.dataset.project;
  navigate('board');
});

document.querySelector('#global-search').addEventListener('input', debounce(event => {
  state.search = event.target.value.trim();
  if (state.search) {
    state.selectedProject = null;
    state.filterType = '';
    state.filterPriority = '';
    state.filterAssignee = '';
    state.filterLabel = '';
    state.filterComponent = '';
    state.filterEpic = '';
    state.filterDueBefore = '';
    navigate('tickets');
  }
}, 350));

document.querySelector('#create-form').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget;
  const values = Object.fromEntries(new FormData(form).entries());
  const payload = {
    projectKey: values.projectKey,
    ticketTypeKey: values.ticketTypeKey,
    summary: values.summary.trim(),
    description: values.description.trim(),
    priorityKey: values.priorityKey,
    assigneeEmail: values.assigneeEmail || null,
    parentTicketKey: values.parentTicketKey || null,
    componentName: values.componentName || null,
    storyPoints: values.storyPoints ? Number(values.storyPoints) : null,
    dueDate: values.dueDate || null,
    labels: values.labels.split(',').map(value => value.trim()).filter(Boolean),
    customFieldValues: collectCustomFieldValues(form, createModalCustomFields)
  };
  const errorElement = document.querySelector('#create-error');
  try {
    const created = await api('/api/v1/tickets', { method: 'POST', body: JSON.stringify(payload) });
    state.selectedProject = created.projectKey;
    form.reset();
    closeCreateModal();
    showToast(`${created.key} created`);
    await renderCurrentView();
    await openTicket(created.key);
  } catch (error) {
    errorElement.textContent = error.message;
    errorElement.classList.remove('hidden');
  }
});

document.querySelector('#project-form').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget;
  const values = Object.fromEntries(new FormData(form).entries());
  const payload = {
    key: values.key.trim().toUpperCase(),
    name: values.name.trim(),
    description: values.description.trim()
  };
  const errorElement = document.querySelector('#project-error');
  try {
    const created = await api('/api/v1/projects', { method: 'POST', body: JSON.stringify(payload) });
    form.reset();
    closeProjectModal();
    showToast(`${created.key} created`);
    await loadBaseData();
    await renderProjectsView(false);
  } catch (error) {
    errorElement.textContent = error.message;
    errorElement.classList.remove('hidden');
  }
});

// The create-ticket modal's description field is static markup (unlike the
// drawer's, which is rebuilt via innerHTML on every open) -- attach its
// toolbar once here rather than on every openCreateModal() call, which
// would otherwise stack a duplicate toolbar/preview pane each time.
attachMarkdownToolbar(document.querySelector('#create-form textarea[name=description]'));

loginForm.addEventListener('submit', async event => {
  event.preventDefault();
  loginError.classList.add('hidden');
  const values = Object.fromEntries(new FormData(loginForm).entries());
  try {
    await api('/api/v1/auth/login', { method: 'POST', body: JSON.stringify({ email: values.email.trim(), password: values.password }) });
    loginForm.reset();
    state.principal = await api('/api/v1/auth/me');
    await autoDetectPreferencesIfNeeded();
    renderCurrentUser();
    showAppShell();
    await loadBaseData();
    await renderCurrentView();
    await openTicketFromUrlIfAny();
  } catch (error) {
    loginError.textContent = error.message;
    loginError.classList.remove('hidden');
  }
});

document.querySelector('#logout-button').addEventListener('click', async () => {
  try {
    await api('/api/v1/auth/logout', { method: 'POST' });
  } catch {
    // Best-effort: show the login screen regardless of the response.
  }
  showLoginScreen();
});

// Back/forward button support for /browse/{key} links: react to the URL
// having already changed (the browser updates it before firing popstate)
// rather than pushing a new entry ourselves -- openTicket/closeDrawer's own
// "only push if the URL doesn't already match" guards make this safe.
window.addEventListener('popstate', () => {
  if (loginScreen.classList.contains('hidden')) {
    const ticketKey = ticketKeyFromLocation();
    if (ticketKey) {
      openTicket(ticketKey).catch(() => {});
    } else if (state.currentTicket) {
      closeDrawer();
    }
  }
});

document.addEventListener('keydown', event => {
  if (event.key === 'Escape') {
    closeCreateModal();
    closeProjectModal();
    closeShortcutsModal();
    closeDrawer();
    document.querySelector('#sidebar').classList.remove('open');
  }
  const inTextField = ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName);
  if (event.key.toLowerCase() === 'c' && !inTextField) {
    openCreateModal();
  }
  if (event.key === '?' && !inTextField) {
    event.preventDefault();
    openShortcutsModal();
  }
  if (event.key === '/' && !inTextField) {
    event.preventDefault();
    document.querySelector('#global-search').focus();
  }
});

(async function init() {
  const authenticated = await checkExistingSession();
  if (!authenticated) {
    showLoginScreen();
    return;
  }
  renderCurrentUser();
  showAppShell();
  try {
    await loadBaseData();
    await renderDashboard();
    await openTicketFromUrlIfAny();
  } catch (error) {
    showError(error);
  }
})();
