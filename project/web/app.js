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
function issueTypeHierarchyLevel(issueTypeKey) {
  if (issueTypeKey === 'epic') return 1;
  if (issueTypeKey === 'sub-task') return -1;
  return 0;
}

function initialState() {
  return {
    view: 'dashboard',
    projects: [],
    issues: [],
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
    filterDueBefore: '',
    currentIssue: null,
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
const issueDrawer = document.querySelector('#issue-drawer');
const drawerBackdrop = document.querySelector('#issue-drawer-backdrop');
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
        items.push(`<li>${renderMarkdownInline(lines[index].replace(/^[-*]\s+/, ''))}</li>`);
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
// renderMarkdown/renderMarkdownInline). Only wired up when `issueKey` is
// known -- the create-issue form has no issue yet to attach files to, so
// its description textarea gets the toolbar without this capability.
function attachMarkdownToolbar(textarea, issueKey = null) {
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
    ${issueKey ? '<button type="button" data-md="attach" title="Attach a file">📎</button>' : ''}
    <button type="button" class="markdown-preview-toggle" data-md="preview" title="Toggle preview">👁 Preview</button>`;
  textarea.insertAdjacentElement('beforebegin', toolbar);

  const insertAttachmentReference = async file => {
    try {
      const attachment = await uploadAttachmentFile(issueKey, file);
      const isImage = attachment.contentType.startsWith('image/');
      insertAtCursor(textarea, `${isImage ? '!' : ''}[${attachment.fileName}](attachment://${attachment.id})`);
    } catch (error) {
      showToast(`${file.name}: ${error.message}`);
    }
  };

  if (issueKey) {
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

function initials(name) {
  return String(name || '?').split(/\s+/).filter(Boolean).slice(0, 2).map(part => part[0]).join('').toUpperCase();
}

function formatDate(value) {
  if (!value) return '—';
  const date = new Date(value.replace(' ', 'T'));
  if (Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat(undefined, { month: 'short', day: 'numeric', year: 'numeric' }).format(date);
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
async function uploadAttachmentFile(issueKey, file) {
  const formData = new FormData();
  formData.append('file', file);
  const csrfToken = getCookie('th_csrf');
  const response = await fetch(`/api/v1/issues/${encodeURIComponent(issueKey)}/attachments`, {
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
  if (!response.ok) throw new Error(payload.error || `Request failed (${response.status})`);
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
  // global-administrator-only, like the issue/project recycle bins.
  document.querySelector('#nav-audit').classList.toggle('hidden', !principal.isAdmin);
  document.querySelector('#nav-attachment-bin').classList.toggle('hidden', !principal.isAdmin);
}

// Checks the existing session cookie (if any) without ever showing the
// generic "session expired" error -- this is the initial, silent probe.
async function checkExistingSession() {
  try {
    state.principal = await api('/api/v1/auth/me');
    return true;
  } catch {
    return false;
  }
}

function showError(error) {
  content.innerHTML = `<div class="error-banner"><strong>Ticket Hub could not load this view.</strong><br>${escapeHtml(error.message || error)}</div>`;
}

function statusChip(issue) {
  return `<span class="status-chip ${escapeHtml(issue.status.category)}">${escapeHtml(issue.status.name)}</span>`;
}

function priorityChip(issue) {
  return `<span class="priority-chip"><span style="color:${escapeHtml(issue.priority.color)}">▲</span>${escapeHtml(issue.priority.name)}</span>`;
}

function assigneeMarkup(issue) {
  if (!issue.assignee) return '<span class="assignee-cell">Unassigned</span>';
  return `<span class="assignee-cell"><span class="small-avatar">${escapeHtml(initials(issue.assignee.displayName))}</span>${escapeHtml(issue.assignee.displayName)}</span>`;
}

function labelsMarkup(labels = []) {
  return labels.slice(0, 3).map(label => `<span class="label-chip">${escapeHtml(label)}</span>`).join('');
}

// `orderable` adds an Order column with move-up/move-down buttons (D31);
// only meaningful when `issues` is a single project's full, rank-sorted
// list, since reorderIssue's `beforeIssueKey` anchor must be in the same
// project as the issue being moved. `selectable` adds a checkbox column for
// bulk actions (D36).
function issueRows(issues, { orderable = false, selectable = false } = {}) {
  const extraColumns = (orderable ? 1 : 0) + (selectable ? 1 : 0);
  if (!issues.length) {
    return `<tr><td colspan="${6 + extraColumns}"><div class="empty-state"><strong>No issues found</strong>Adjust the filters or create a new issue.</div></td></tr>`;
  }
  return issues.map((issue, index) => `
    <tr data-issue-key="${escapeHtml(issue.key)}">
      ${selectable ? `<td class="select-column"><input type="checkbox" class="issue-select" value="${escapeHtml(issue.key)}" aria-label="Select ${escapeHtml(issue.key)}"></td>` : ''}
      <td><span class="issue-type" title="${escapeHtml(issue.type.name)}"><span style="color:${escapeHtml(issue.type.color)}">${escapeHtml(issue.type.icon)}</span>${escapeHtml(issue.type.name)}</span></td>
      <td><span class="issue-key">${escapeHtml(issue.key)}</span></td>
      <td class="issue-summary">${escapeHtml(issue.summary)}</td>
      <td>${statusChip(issue)}</td>
      <td>${priorityChip(issue)}</td>
      <td>${assigneeMarkup(issue)}</td>
      ${orderable ? `<td class="order-cell">
        <button type="button" class="icon-button" data-move-up="${escapeHtml(issue.key)}" ${index === 0 ? 'disabled' : ''} aria-label="Move up">↑</button>
        <button type="button" class="icon-button" data-move-down="${escapeHtml(issue.key)}" ${index === issues.length - 1 ? 'disabled' : ''} aria-label="Move down">↓</button>
      </td>` : ''}
    </tr>`).join('');
}

function deletedIssueRows(issues) {
  if (!issues.length) {
    return `<tr><td colspan="7"><div class="empty-state"><strong>The recycle bin is empty</strong></div></td></tr>`;
  }
  return issues.map(issue => `
    <tr>
      <td><span class="issue-type" title="${escapeHtml(issue.type.name)}"><span style="color:${escapeHtml(issue.type.color)}">${escapeHtml(issue.type.icon)}</span>${escapeHtml(issue.type.name)}</span></td>
      <td><span class="issue-key">${escapeHtml(issue.key)}</span></td>
      <td class="issue-summary">${escapeHtml(issue.summary)}</td>
      <td>${statusChip(issue)}</td>
      <td>${priorityChip(issue)}</td>
      <td>${assigneeMarkup(issue)}</td>
      <td><div class="project-card-actions"><button type="button" class="secondary-button" data-restore-issue="${escapeHtml(issue.key)}">Restore</button><button type="button" class="secondary-button" data-permanent-issue="${escapeHtml(issue.key)}">Delete permanently</button></div></td>
    </tr>`).join('');
}

function tablePanel(issues, title = 'Issues') {
  return `
    <div class="panel">
      <div class="panel-header"><h2>${escapeHtml(title)}</h2><span class="eyebrow">${issues.length} shown</span></div>
      <div style="overflow-x:auto">
        <table class="issue-table">
          <thead><tr><th>Type</th><th>Key</th><th>Summary</th><th>Status</th><th>Priority</th><th>Assignee</th></tr></thead>
          <tbody>${issueRows(issues)}</tbody>
        </table>
      </div>
    </div>`;
}

// Dashboard-only deadlines widget (D24): same shape as tablePanel but with
// a Due date column, since that's the one thing this particular list is
// sorted and shown for.
function deadlinesPanel(issues, title = 'Upcoming deadlines') {
  const rows = issues.length ? issues.map(issue => `
    <tr data-issue-key="${escapeHtml(issue.key)}">
      <td><span class="issue-type" title="${escapeHtml(issue.type.name)}"><span style="color:${escapeHtml(issue.type.color)}">${escapeHtml(issue.type.icon)}</span>${escapeHtml(issue.type.name)}</span></td>
      <td><span class="issue-key">${escapeHtml(issue.key)}</span></td>
      <td class="issue-summary">${escapeHtml(issue.summary)}</td>
      <td>${statusChip(issue)}</td>
      <td>${escapeHtml(formatDate(issue.dueDate))}</td>
    </tr>`).join('') : '<tr><td colspan="5"><div class="empty-state">No upcoming deadlines.</div></td></tr>';
  return `
    <div class="panel">
      <div class="panel-header"><h2>${escapeHtml(title)}</h2><span class="eyebrow">${issues.length} shown</span></div>
      <div style="overflow-x:auto">
        <table class="issue-table">
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
  const issueRef = notification.issueKey
    ? `${notification.issueKey}${notification.issueSummary ? ` — ${notification.issueSummary}` : ''}`
    : 'an issue';
  if (notification.type === 'assigned') return `You were assigned ${issueRef}`;
  if (notification.type === 'mentioned') return `You were mentioned on ${issueRef}`;
  if (notification.type === 'watched_comment') return `New comment on ${issueRef}`;
  return issueRef;
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
        <button type="button" class="notification-item ${notification.readAt ? '' : 'unread'}" data-notification-id="${escapeHtml(notification.id)}" data-issue-key="${escapeHtml(notification.issueKey || '')}">
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
    if (item.dataset.issueKey) {
      await openIssue(item.dataset.issueKey);
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
      <div class="stat-card"><div class="stat-label">All issues</div><div class="stat-value">${stats.totalIssues}</div><div class="stat-meta">Across ${state.projects.length} projects</div></div>
      <div class="stat-card"><div class="stat-label">To do</div><div class="stat-value">${stats.todoIssues}</div><div class="stat-meta">Backlog and selected work</div></div>
      <div class="stat-card"><div class="stat-label">In progress</div><div class="stat-value">${stats.inProgressIssues}</div><div class="stat-meta">Active and in review</div></div>
      <div class="stat-card"><div class="stat-label">Done</div><div class="stat-value">${stats.doneIssues}</div><div class="stat-meta">Completed work</div></div>
    </div>
    ${state.principal ? tablePanel(stats.assignedToMe, 'Assigned to me') : ''}
    ${state.principal ? tablePanel(stats.watchedIssues, 'Issues I’m watching') : ''}
    ${state.principal ? deadlinesPanel(stats.upcomingDeadlines) : ''}
    ${tablePanel(stats.recentIssues, 'Recently active issues')}`;
  bindIssueLinks();
}

// Simple append-only admin/security audit log (D23): global-administrator-
// only, read-only, no filtering/export/pagination -- just the newest 200
// events the server already caps the response to.
async function renderAuditLog() {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const { items } = await api('/api/v1/admin/audit-events');
  content.innerHTML = `
    ${pageHeader('Audit log', 'Append-only record of admin and security events. Never purged or exported.', 'Administration')}
    <div class="panel">
      <div class="panel-header"><h2>Events</h2><span class="eyebrow">${items.length} shown</span></div>
      <div style="overflow-x:auto">
        <table class="issue-table">
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
    </div>`;
}

// Attachment recycle bin (D101/D102): global-administrator-only, fixed
// 90-day on-demand retention (checked server-side on every fetch, not a
// background job). Spans every issue, so each row shows the issue key
// (resolved server-side via a join) rather than requiring the admin to
// already be looking at a specific issue.
async function renderAttachmentRecycleBin() {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const { items } = await api('/api/v1/attachments/deleted');
  content.innerHTML = `
    ${pageHeader('Attachment recycle bin', 'Deleted attachments, retained for 90 days.', 'Administration')}
    <div class="panel">
      <div class="panel-header"><h2>Deleted attachments</h2><span class="eyebrow">${items.length} shown</span></div>
      <div style="overflow-x:auto">
        <table class="issue-table">
          <thead><tr><th>File</th><th>Issue</th><th>Uploader</th><th>Deleted</th><th>Actions</th></tr></thead>
          <tbody>${items.length ? items.map(attachment => `
            <tr>
              <td>${attachmentIcon(attachment.contentType)} ${escapeHtml(attachment.fileName)}</td>
              <td><span class="issue-key" data-issue-key="${escapeHtml(attachment.issueKey)}">${escapeHtml(attachment.issueKey)}</span></td>
              <td>${escapeHtml(attachment.uploader.displayName)}</td>
              <td>${escapeHtml(relativeDate(attachment.createdAt))}</td>
              <td><div class="project-card-actions"><button type="button" class="secondary-button" data-restore-attachment="${escapeHtml(attachment.id)}">Restore</button><button type="button" class="secondary-button" data-permanent-attachment="${escapeHtml(attachment.id)}">Delete permanently</button></div></td>
            </tr>`).join('') : '<tr><td colspan="5"><div class="empty-state">The attachment recycle bin is empty.</div></td></tr>'}
          </tbody>
        </table>
      </div>
    </div>`;
  bindIssueLinks();
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
    ${pageHeader('Account', 'Manage your personal access tokens and active sessions.', 'Account')}
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
        <table class="issue-table">
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
        <table class="issue-table">
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

function issueFilterParams() {
  const params = new URLSearchParams();
  if (state.selectedProject) params.set('project', state.selectedProject);
  if (state.status) params.set('status', state.status);
  if (state.filterType) params.set('type', state.filterType);
  if (state.filterPriority) params.set('priority', state.filterPriority);
  if (state.filterAssignee) params.set('assignee', state.filterAssignee);
  if (state.filterLabel) params.set('label', state.filterLabel);
  if (state.filterDueBefore) params.set('dueBefore', state.filterDueBefore);
  if (state.search) params.set('q', state.search);
  return params;
}

async function fetchIssues() {
  const result = await api(`/api/v1/issues?${issueFilterParams()}`);
  state.issues = result.items;
}

async function renderIssues() {
  await renderIssuesView(false);
}

// Mirrors renderProjectsView's active/recycle-bin toggle: `showingDeleted`
// swaps the filter bar and normal issue table for the recycle bin (D22,
// global-administrator-only to view/restore/purge, same split as projects).
// The active view also adds: manual reordering (D31, only meaningful and
// only enabled with a single project selected, since reorderIssue's anchor
// must be in the same project -- the table is sorted by rankOrder in that
// case so up/down visibly matches the stored order) and simple bulk actions
// (D36, always available since each key is authorized/processed
// independently regardless of project).
async function renderIssuesView(showingDeleted) {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  let issues;
  if (showingDeleted) {
    try {
      issues = (await api('/api/v1/issues/deleted')).items;
    } catch (error) {
      showError(error);
      return;
    }
  } else {
    await fetchIssues();
    issues = state.issues;
    if (state.selectedProject) {
      issues = [...issues].sort((a, b) => a.rankOrder - b.rankOrder);
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
        <h1>${showingDeleted ? 'Issue recycle bin' : 'Issues'}</h1>
        <p>${showingDeleted ? 'Issues moved to the recycle bin (90-day retention).' : 'Search, filter, and inspect the work items in a project.'}</p>
      </div>
      <div class="page-actions">
        ${showingDeleted ? '' : `<a class="secondary-button" id="export-issues-csv" href="/api/v1/issues/export.csv?${issueFilterParams()}" download="issues.csv">⬇ Export CSV</a>`}
        ${isAdmin ? `<button type="button" class="secondary-button" id="toggle-issue-recycle-bin">${showingDeleted ? '← Back to issues' : '🗑 Recycle bin'}</button>` : ''}
      </div>
    </div>
    <div class="panel">
      ${showingDeleted ? '' : `
      <div class="filter-bar">
        <select id="issue-project-filter"><option value="">All projects</option>${projectOptions}</select>
        <select id="issue-status-filter"><option value="">All statuses</option>${statusOptions}</select>
        <select id="issue-type-filter">
          <option value="">All types</option>
          <option value="epic" ${state.filterType === 'epic' ? 'selected' : ''}>Epic</option>
          <option value="story" ${state.filterType === 'story' ? 'selected' : ''}>Story</option>
          <option value="task" ${state.filterType === 'task' ? 'selected' : ''}>Task</option>
          <option value="bug" ${state.filterType === 'bug' ? 'selected' : ''}>Bug</option>
          <option value="sub-task" ${state.filterType === 'sub-task' ? 'selected' : ''}>Sub-task</option>
        </select>
        <select id="issue-priority-filter">
          <option value="">All priorities</option>
          <option value="highest" ${state.filterPriority === 'highest' ? 'selected' : ''}>Highest</option>
          <option value="high" ${state.filterPriority === 'high' ? 'selected' : ''}>High</option>
          <option value="medium" ${state.filterPriority === 'medium' ? 'selected' : ''}>Medium</option>
          <option value="low" ${state.filterPriority === 'low' ? 'selected' : ''}>Low</option>
          <option value="lowest" ${state.filterPriority === 'lowest' ? 'selected' : ''}>Lowest</option>
        </select>
        <select id="issue-assignee-filter">
          <option value="">Any assignee</option>
          <option value="demo@ticket-hub.local" ${state.filterAssignee === 'demo@ticket-hub.local' ? 'selected' : ''}>Demo User</option>
          <option value="alex@ticket-hub.local" ${state.filterAssignee === 'alex@ticket-hub.local' ? 'selected' : ''}>Alex Morgan</option>
          <option value="sam@ticket-hub.local" ${state.filterAssignee === 'sam@ticket-hub.local' ? 'selected' : ''}>Sam Lee</option>
        </select>
        <input id="issue-label-filter" value="${escapeHtml(state.filterLabel)}" placeholder="Label" style="width:110px">
        <input id="issue-due-filter" type="date" value="${escapeHtml(state.filterDueBefore)}" title="Due on or before">
        <input id="issue-search-filter" type="search" value="${escapeHtml(state.search)}" placeholder="Filter by key, summary, or description">
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
        <table class="issue-table">
          <thead><tr>
            ${showingDeleted ? '' : `<th class="select-column"><input type="checkbox" id="select-all-issues" aria-label="Select all issues" ${issues.length ? '' : 'disabled'}></th>`}
            <th>Type</th><th>Key</th><th>Summary</th><th>Status</th><th>Priority</th><th>Assignee</th>
            ${showingDeleted ? '<th>Actions</th>' : ''}
            ${orderable ? '<th>Order</th>' : ''}
          </tr></thead>
          <tbody>${showingDeleted ? deletedIssueRows(issues) : issueRows(issues, { orderable, selectable: true })}</tbody>
        </table>
      </div>
    </div>`;

  document.querySelector('#toggle-issue-recycle-bin')?.addEventListener('click', () => renderIssuesView(!showingDeleted));

  if (showingDeleted) {
    document.querySelectorAll('[data-restore-issue]').forEach(button => button.addEventListener('click', async () => {
      const key = button.dataset.restoreIssue;
      try {
        await api(`/api/v1/issues/${encodeURIComponent(key)}/restore`, { method: 'POST' });
        showToast(`${key} restored`);
        await renderIssuesView(true);
      } catch (error) { showToast(error.message); }
    }));
    document.querySelectorAll('[data-permanent-issue]').forEach(button => button.addEventListener('click', async () => {
      const key = button.dataset.permanentIssue;
      try {
        await api(`/api/v1/issues/${encodeURIComponent(key)}/permanent`, { method: 'DELETE' });
        showToast(`${key} permanently deleted`);
        await renderIssuesView(true);
      } catch (error) { showToast(error.message); }
    }));
    return;
  }

  document.querySelector('#issue-project-filter').addEventListener('change', event => {
    state.selectedProject = event.target.value || null;
    renderIssues().catch(showError);
  });
  document.querySelector('#issue-status-filter').addEventListener('change', event => {
    state.status = event.target.value;
    renderIssues().catch(showError);
  });
  document.querySelector('#issue-type-filter').addEventListener('change', event => {
    state.filterType = event.target.value;
    renderIssues().catch(showError);
  });
  document.querySelector('#issue-priority-filter').addEventListener('change', event => {
    state.filterPriority = event.target.value;
    renderIssues().catch(showError);
  });
  document.querySelector('#issue-assignee-filter').addEventListener('change', event => {
    state.filterAssignee = event.target.value;
    renderIssues().catch(showError);
  });
  document.querySelector('#issue-label-filter').addEventListener('input', debounce(event => {
    state.filterLabel = event.target.value.trim();
    renderIssues().catch(showError);
  }, 300));
  document.querySelector('#issue-due-filter').addEventListener('change', event => {
    state.filterDueBefore = event.target.value;
    renderIssues().catch(showError);
  });
  document.querySelector('#issue-search-filter').addEventListener('input', debounce(event => {
    state.search = event.target.value.trim();
    renderIssues().catch(showError);
  }, 300));
  document.querySelector('#clear-filters').addEventListener('click', () => {
    state.status = '';
    state.search = '';
    state.filterType = '';
    state.filterPriority = '';
    state.filterAssignee = '';
    state.filterLabel = '';
    state.filterDueBefore = '';
    state.selectedProject = null;
    renderIssues().catch(showError);
  });

  if (orderable) {
    document.querySelectorAll('[data-move-up]').forEach(button => button.addEventListener('click', async event => {
      event.stopPropagation();
      const key = button.dataset.moveUp;
      const index = issues.findIndex(candidate => candidate.key === key);
      if (index <= 0) return;
      try {
        await api(`/api/v1/issues/${encodeURIComponent(key)}/reorder`, { method: 'POST', body: JSON.stringify({ beforeIssueKey: issues[index - 1].key }) });
        await renderIssuesView(false);
      } catch (error) { showToast(error.message); }
    }));
    document.querySelectorAll('[data-move-down]').forEach(button => button.addEventListener('click', async event => {
      event.stopPropagation();
      const key = button.dataset.moveDown;
      const index = issues.findIndex(candidate => candidate.key === key);
      if (index === -1 || index >= issues.length - 1) return;
      const beforeIssueKey = index + 2 < issues.length ? issues[index + 2].key : null;
      try {
        await api(`/api/v1/issues/${encodeURIComponent(key)}/reorder`, { method: 'POST', body: JSON.stringify({ beforeIssueKey }) });
        await renderIssuesView(false);
      } catch (error) { showToast(error.message); }
    }));
  }

  const bulkBar = document.querySelector('#bulk-bar');
  const checkboxes = [...document.querySelectorAll('.issue-select')];
  const selectedKeys = () => checkboxes.filter(box => box.checked).map(box => box.value);
  const selectAllBox = document.querySelector('#select-all-issues');
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
    document.querySelectorAll('.issue-select:checked').forEach(box => { box.checked = false; });
    lastCheckedIndex = null;
    refreshBulkBar();
  });
  const runBulk = async (path, extraPayload, verb) => {
    const issueKeys = selectedKeys();
    if (!issueKeys.length) return;
    try {
      const result = await api(path, { method: 'POST', body: JSON.stringify({ issueKeys, ...extraPayload }) });
      showToast(`${verb}: ${result.succeeded.length} succeeded, ${result.failed.length} failed`);
      await renderIssuesView(false);
    } catch (error) { showToast(error.message); }
  };
  // Bulk transition to a Done-category status shares the same resolution
  // across every selected issue (D68-D70) -- the server already supported
  // this (bulkChangeStatus forwards one shared `resolution` to each issue's
  // own changeStatus call, same as the drawer/board's single-issue path),
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
    runBulk('/api/v1/issues/bulk/status', payload, 'Bulk status change');
  });
  document.querySelector('#bulk-assign-apply')?.addEventListener('click', () => runBulk('/api/v1/issues/bulk/assign', { assigneeEmail: document.querySelector('#bulk-assignee-select').value || null }, 'Bulk assign'));
  document.querySelector('#bulk-label-apply')?.addEventListener('click', () => {
    const label = document.querySelector('#bulk-label-input').value.trim();
    if (!label) return;
    runBulk('/api/v1/issues/bulk/label', { label }, 'Bulk add label');
  });
  document.querySelector('#bulk-delete-apply')?.addEventListener('click', () => runBulk('/api/v1/issues/bulk/delete', {}, 'Bulk delete'));

  bindIssueLinks();
}

async function renderBoard() {
  state.selectedProject ||= state.projects[0]?.key || null;
  state.search = '';
  state.status = '';
  state.filterType = '';
  state.filterPriority = '';
  state.filterAssignee = '';
  state.filterLabel = '';
  state.filterDueBefore = '';
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  const [, boardColumns] = await Promise.all([fetchIssues(), api('/api/v1/board-columns')]);
  const isAdmin = Boolean(state.principal?.isAdmin);
  const selected = state.projects.find(project => project.key === state.selectedProject);
  const columns = STATUSES.map(status => {
    const issues = state.issues.filter(issue => issue.status.key === status.key);
    // Kanban WIP limits (D32/D33): a single flat, installation-wide limit
    // per fixed workflow status -- soft and display-time-only, an
    // over-limit column is highlighted, never blocked from receiving more
    // issues.
    const column = boardColumns.items.find(candidate => candidate.statusKey === status.key);
    const wipLimit = column?.wipLimit ?? null;
    const overLimit = wipLimit !== null && issues.length > wipLimit;
    const countLabel = wipLimit !== null ? `${issues.length} / ${wipLimit}` : `${issues.length}`;
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
          ${issues.length ? issues.map(issue => `
            <article class="issue-card" draggable="true" data-issue-key="${escapeHtml(issue.key)}">
              <div class="issue-type"><span style="color:${escapeHtml(issue.type.color)}">${escapeHtml(issue.type.icon)}</span><span class="issue-key">${escapeHtml(issue.key)}</span></div>
              <div class="card-summary">${escapeHtml(issue.summary)}</div>
              <div class="issue-card-labels">${labelsMarkup(issue.labels)}</div>
              <div class="issue-card-footer">${priorityChip(issue)}${issue.assignee ? `<span class="small-avatar" title="${escapeHtml(issue.assignee.displayName)}">${escapeHtml(initials(issue.assignee.displayName))}</span>` : '<span></span>'}</div>
            </article>`).join('') : '<div class="empty-state">No issues</div>'}
        </div>
      </section>`;
  }).join('');

  content.innerHTML = `
    <div class="page-header">
      <div><span class="eyebrow">${escapeHtml(state.selectedProject || 'Project')}</span><h1>${escapeHtml(selected?.name || 'Board')}</h1><p>Simple status-based Kanban board.${isAdmin ? ' Soft WIP limits are installation-wide and apply to every project’s board.' : ''}</p></div>
      <div class="page-actions"><select id="board-project" class="status-select">${state.projects.map(project => `<option value="${escapeHtml(project.key)}" ${project.key === state.selectedProject ? 'selected' : ''}>${escapeHtml(project.key)} — ${escapeHtml(project.name)}</option>`).join('')}</select></div>
    </div>
    <div class="board">${columns}</div>`;
  document.querySelector('#board-project').addEventListener('change', event => {
    state.selectedProject = event.target.value;
    renderBoard().catch(showError);
  });
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
  bindIssueLinks();
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
  document.querySelectorAll('.issue-card[draggable]').forEach(card => {
    card.addEventListener('dragstart', event => {
      event.dataTransfer.setData('text/plain', card.dataset.issueKey);
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
      const issueKey = event.dataTransfer.getData('text/plain');
      handleBoardDrop(issueKey, list.dataset.statusKey);
    });
  });
}

function handleBoardDrop(issueKey, targetStatusKey) {
  const issue = state.issues.find(candidate => candidate.key === issueKey);
  if (!issue || issue.status.key === targetStatusKey) {
    return;
  }
  const targetStatus = STATUSES.find(status => status.key === targetStatusKey);
  if (targetStatus?.category === 'done' && !issue.resolution) {
    promptBoardResolution(issue, targetStatusKey);
    return;
  }
  applyBoardStatusChange(issue.key, targetStatusKey, null, issue.version);
}

// Same request as applyStatusChange (used by the drawer's status select),
// but stays on the board and re-renders it instead of opening the drawer --
// jumping into the detail view is the right follow-up after a deliberate
// dropdown change, but not after a quick drag-and-drop card move.
async function applyBoardStatusChange(issueKey, statusKey, resolution, expectedVersion) {
  try {
    const payload = { statusKey, expectedVersion };
    if (resolution) payload.resolution = resolution;
    await api(`/api/v1/issues/${encodeURIComponent(issueKey)}/status`, { method: 'PATCH', body: JSON.stringify(payload) });
    const statusName = STATUSES.find(status => status.key === statusKey)?.name || statusKey;
    showToast(`${issueKey} moved to ${statusName}`);
    await renderBoard();
  } catch (error) {
    showToast(error.message);
    await renderBoard();
  }
}

// A minimal dynamically-created dialog (reuses the existing .modal-backdrop/
// .modal styling, same as the create-issue/create-project modals, but not
// pre-declared in index.html since it only ever exists transiently) --
// mirrors the drawer's inline resolution picker for the one case drag-and-
// drop can't skip: a Done-category status requires a resolution.
function promptBoardResolution(issue, targetStatusKey) {
  const targetStatus = STATUSES.find(status => status.key === targetStatusKey);
  const backdrop = document.createElement('div');
  backdrop.className = 'modal-backdrop';
  backdrop.setAttribute('role', 'dialog');
  backdrop.setAttribute('aria-modal', 'true');
  backdrop.innerHTML = `
    <div class="modal" style="max-width:420px">
      <div class="modal-header">
        <div><span class="eyebrow">${escapeHtml(issue.key)}</span><h2>Resolve issue</h2></div>
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
    await applyBoardStatusChange(issue.key, targetStatusKey, resolution, issue.version);
  });
  backdrop.querySelector('#board-resolution-select').focus();
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
        <p>${showingDeleted ? 'Projects moved to the recycle bin (90-day retention).' : 'Choose a project to see its issues and board.'}</p>
      </div>
      <div class="page-actions">
        ${isAdmin ? `<button type="button" class="secondary-button" id="toggle-recycle-bin">${showingDeleted ? '← Back to projects' : '🗑 Recycle bin'}</button>` : ''}
        ${showingDeleted ? '' : '<button type="button" class="primary-button" id="new-project-button">＋ New project</button>'}
      </div>
    </div>
    <div class="project-grid">
      ${projects.length ? projects.map(project => `
        <article class="project-card" data-project-card="${escapeHtml(project.key)}">
          <div class="project-card-head"><span class="project-avatar">${escapeHtml(project.key.slice(0, 2))}</span><div><h3>${escapeHtml(project.name)}</h3><span class="issue-key">${escapeHtml(project.key)}</span></div></div>
          <p>${escapeHtml(project.description)}</p>
          <div class="project-card-stats"><div><strong>${project.issueCount}</strong><span>Total issues</span></div><div><strong>${project.openIssueCount}</strong><span>Open issues</span></div><div><strong>${escapeHtml(project.lead?.displayName || '—')}</strong><span>Lead</span></div></div>
          <div class="project-card-actions">${showingDeleted
            ? `<button type="button" class="secondary-button" data-restore-project="${escapeHtml(project.key)}">Restore</button><button type="button" class="secondary-button" data-permanent-project="${escapeHtml(project.key)}">Delete permanently</button>`
            : `<button type="button" class="secondary-button" data-archive-project="${escapeHtml(project.key)}" data-archived="${project.archived}">${project.archived ? 'Unarchive' : 'Archive'}</button><button type="button" class="secondary-button" data-delete-project="${escapeHtml(project.key)}">Delete</button>`}</div>
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
    else if (state.view === 'issues') await renderIssues();
    else if (state.view === 'account') await renderAccountView();
    else if (state.view === 'audit') await renderAuditLog();
    else if (state.view === 'attachment-bin') await renderAttachmentRecycleBin();
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

function bindIssueLinks() {
  document.querySelectorAll('[data-issue-key]').forEach(element => {
    element.addEventListener('click', () => openIssue(element.dataset.issueKey));
    makeKeyboardActivatable(element, () => openIssue(element.dataset.issueKey));
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
async function applyStatusChange(issueKey, statusKey, resolution, expectedVersion) {
  try {
    const payload = { statusKey, expectedVersion };
    if (resolution) payload.resolution = resolution;
    await api(`/api/v1/issues/${encodeURIComponent(issueKey)}/status`, { method: 'PATCH', body: JSON.stringify(payload) });
    showToast(`${issueKey} status updated`);
    await renderCurrentView();
    await openIssue(issueKey);
  } catch (error) {
    showToast(error.message);
  }
}

function editFieldsMarkup(issue) {
  const typeOptions = [['task', 'Task'], ['story', 'Story'], ['bug', 'Bug'], ['epic', 'Epic'], ['sub-task', 'Sub-task']];
  return `
    <label>Type<select id="edit-type">${typeOptions.map(([key, label]) => `<option value="${key}" ${key === issue.type.key ? 'selected' : ''}>${label}</option>`).join('')}</select></label>
    <label class="wide" id="edit-parent-label">
      <span id="edit-parent-label-text">Epic (optional)</span>
      <select id="edit-parent"><option value="">None</option></select>
    </label>
    <label class="wide">Summary<input id="edit-summary" value="${escapeHtml(issue.summary)}" maxlength="255" required></label>
    <label class="wide">Description<textarea id="edit-description" rows="6">${escapeHtml(issue.description)}</textarea></label>
    <label>Priority<select id="edit-priority">${['highest', 'high', 'medium', 'low', 'lowest'].map(key => `<option value="${key}" ${key === issue.priority.key ? 'selected' : ''}>${key[0].toUpperCase()}${key.slice(1)}</option>`).join('')}</select></label>
    <label>Assignee<select id="edit-assignee">
      <option value="">Unassigned</option>
      <option value="demo@ticket-hub.local" ${issue.assignee?.email === 'demo@ticket-hub.local' ? 'selected' : ''}>Demo User</option>
      <option value="alex@ticket-hub.local" ${issue.assignee?.email === 'alex@ticket-hub.local' ? 'selected' : ''}>Alex Morgan</option>
      <option value="sam@ticket-hub.local" ${issue.assignee?.email === 'sam@ticket-hub.local' ? 'selected' : ''}>Sam Lee</option>
    </select></label>
    <label>Story points<input id="edit-story-points" type="number" min="0" max="10000" step="0.5" value="${issue.storyPoints ?? ''}"></label>
    <label>Due date<input id="edit-due-date" type="date" value="${issue.dueDate ?? ''}"></label>
    <label class="wide">Labels<input id="edit-labels" value="${escapeHtml(issue.labels.join(', '))}"></label>`;
}

async function openIssue(issueKey) {
  issueDrawer.classList.remove('hidden');
  drawerBackdrop.classList.remove('hidden');
  issueDrawer.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  try {
    const [issue, comments, links, watchers, voters, worklogs, attachments] = await Promise.all([
      api(`/api/v1/issues/${encodeURIComponent(issueKey)}`),
      api(`/api/v1/issues/${encodeURIComponent(issueKey)}/comments`),
      api(`/api/v1/issues/${encodeURIComponent(issueKey)}/links`),
      api(`/api/v1/issues/${encodeURIComponent(issueKey)}/watchers`),
      api(`/api/v1/issues/${encodeURIComponent(issueKey)}/voters`),
      api(`/api/v1/issues/${encodeURIComponent(issueKey)}/worklogs`),
      api(`/api/v1/issues/${encodeURIComponent(issueKey)}/attachments`)
    ]);
    state.currentIssue = issue;
    let attachmentSort = 'date';

    // Fixed emoji reactions (D84): one reactions list per comment, fetched
    // alongside everything else -- fine at demo scale, mirrors the
    // watchers/voters fetch-once-per-open pattern above.
    const reactionLists = await Promise.all(comments.items.map(comment =>
      api(`/api/v1/issues/${encodeURIComponent(issueKey)}/comments/${encodeURIComponent(comment.id)}/reactions`)));
    const reactionsByComment = new Map(comments.items.map((comment, index) => [comment.id, reactionLists[index].items]));

    const isWatching = watchers.items.some(user => user.id === state.principal?.userId);
    const isVoting = voters.items.some(user => user.id === state.principal?.userId);

    function render(editing) {
      issueDrawer.innerHTML = `
        <div class="drawer-header"><span class="issue-key">${escapeHtml(issue.key)}</span><button class="icon-button" id="close-drawer" aria-label="Close">×</button></div>
        <div class="drawer-content">
          <span class="issue-type"><span style="color:${escapeHtml(issue.type.color)}">${escapeHtml(issue.type.icon)}</span>${escapeHtml(issue.type.name)} · ${escapeHtml(issue.projectName)}</span>
          <div class="drawer-actions">
            <button type="button" class="secondary-button" id="watch-toggle">${isWatching ? '★ Watching' : '☆ Watch'} (${watchers.items.length})</button>
            <button type="button" class="secondary-button" id="vote-toggle">${isVoting ? '▲ Voted' : '△ Vote'} (${voters.items.length})</button>
            <button type="button" class="secondary-button" id="clone-issue">⧉ Clone</button>
            ${editing ? '' : '<button type="button" class="secondary-button" id="edit-issue">✎ Edit</button>'}
            ${editing ? '' : '<button type="button" class="secondary-button" id="delete-issue">🗑 Delete</button>'}
          </div>
          ${editing
            ? `<div class="form-grid" id="edit-form">${editFieldsMarkup(issue)}</div>
               <div id="edit-error" class="form-error hidden"></div>
               <div class="modal-footer" style="padding:0 0 20px"><button type="button" class="secondary-button" id="edit-cancel">Cancel</button><button type="button" class="primary-button" id="edit-save">Save changes</button></div>`
            : `<h1>${escapeHtml(issue.summary)}</h1>`}
          <div class="drawer-layout">
            <div>
              ${editing ? '' : `<section class="drawer-section"><h3>Description</h3><div class="description markdown-body">${issue.description ? renderMarkdown(issue.description) : '<p class="markdown-empty">No description provided.</p>'}</div></section>`}
              <section class="drawer-section">
                <h3>Links</h3>
                <div class="link-list">${links.items.length ? links.items.map(link => `
                  <div class="link-row">
                    <span class="link-label">${escapeHtml(link.label)}</span>
                    <span class="issue-key" data-issue-key="${escapeHtml(link.otherIssueKey)}">${escapeHtml(link.otherIssueKey)}</span>
                    <span class="link-summary">${escapeHtml(link.otherIssueSummary)}</span>
                    <button type="button" class="icon-button" data-delete-link="${escapeHtml(link.id)}" aria-label="Remove link">×</button>
                  </div>`).join('') : '<div class="empty-state">No links yet.</div>'}</div>
                <form class="link-form" id="link-form">
                  <select name="linkType">
                    <option value="blocks">blocks</option>
                    <option value="relates_to">relates to</option>
                    <option value="duplicates">duplicates</option>
                  </select>
                  <input name="targetIssueKey" placeholder="Issue key, e.g. TH-3" required>
                  <button class="secondary-button" type="submit">Add link</button>
                </form>
              </section>
              <section class="drawer-section">
                <h3>Time tracking</h3>
                <div class="worklog-list">${worklogs.items.length ? worklogs.items.map(worklog => `
                  <div class="worklog-row" data-worklog-id="${escapeHtml(worklog.id)}">
                    <span class="small-avatar">${escapeHtml(initials(worklog.author.displayName))}</span>
                    <div class="worklog-details">
                      <span><strong>${escapeHtml(formatDuration(worklog.timeSpentSeconds))}</strong> by ${escapeHtml(worklog.author.displayName)} on ${escapeHtml(formatDate(worklog.workDate))}</span>
                      ${worklog.comment ? `<span class="worklog-comment">${escapeHtml(worklog.comment)}</span>` : ''}
                    </div>
                    <button type="button" class="icon-button" data-delete-worklog="${escapeHtml(worklog.id)}" aria-label="Delete worklog">×</button>
                  </div>`).join('') : '<div class="empty-state">No time logged yet.</div>'}</div>
                <form class="worklog-form" id="worklog-form">
                  <input name="workDate" type="date" required value="${new Date().toISOString().slice(0, 10)}">
                  <input name="duration" placeholder="e.g. 1h 30m" required pattern="^(\\d+h)?\\s*(\\d+m)?$">
                  <input name="comment" placeholder="What did you work on? (optional)">
                  <button class="secondary-button" type="submit">Log time</button>
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
                <div class="attachment-dropzone" id="attachment-dropzone">Drag and drop files here, or use "Attach files" above (max 25MB each, 20 per issue)</div>
              </section>
              <section class="drawer-section">
                <h3>Comments</h3>
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
                <form class="comment-form" id="comment-form"><textarea name="body" rows="3" required placeholder="Add a comment…"></textarea><button class="primary-button" type="submit">Comment</button></form>
              </section>
            </div>
            <aside class="meta-list">
              <div class="meta-row"><span>Status</span><select id="drawer-status" class="status-select">${STATUSES.map(status => `<option value="${status.key}" ${status.key === issue.status.key ? 'selected' : ''}>${status.name}</option>`).join('')}</select></div>
              <div class="meta-row" id="drawer-resolution-row" ${STATUSES.find(status => status.key === issue.status.key)?.category === 'done' ? '' : 'hidden'}>
                <span>Resolution</span>
                ${issue.resolution ? `<strong>${escapeHtml(resolutionLabel(issue.resolution))}</strong>` : `
                <select id="drawer-resolution">${RESOLUTIONS.map(resolution => `<option value="${resolution.key}">${resolution.name}</option>`).join('')}</select>
                <div class="resolution-actions">
                  <button type="button" class="secondary-button" id="resolution-cancel">Cancel</button>
                  <button type="button" class="primary-button" id="resolution-confirm">Confirm</button>
                </div>`}
              </div>
              ${editing ? '' : `
              <div class="meta-row"><span>Priority</span>${priorityChip(issue)}</div>
              <div class="meta-row"><span>Assignee</span>${assigneeMarkup(issue)}</div>`}
              <div class="meta-row"><span>Reporter</span>${escapeHtml(issue.reporter.displayName)}</div>
              ${issue.parentIssueKey ? `<div class="meta-row"><span>Parent</span><strong class="issue-key" id="drawer-parent-link" style="cursor:pointer">${escapeHtml(issue.parentIssueKey)}</strong></div>` : ''}
              ${editing || state.projects.filter(project => project.key !== issue.projectKey).length === 0 ? '' : `
              <div class="meta-row">
                <span>Move to project</span>
                <div style="display:flex;gap:6px">
                  <select id="move-target-project" class="status-select">${state.projects.filter(project => project.key !== issue.projectKey).map(project => `<option value="${escapeHtml(project.key)}">${escapeHtml(project.key)}</option>`).join('')}</select>
                  <button type="button" class="secondary-button" id="move-issue-button">Move</button>
                </div>
              </div>`}
              ${editing ? '' : `
              <div class="meta-row"><span>Story points</span><strong>${issue.storyPoints ?? '—'}</strong></div>
              <div class="meta-row"><span>Due date</span><strong>${escapeHtml(formatDate(issue.dueDate))}</strong></div>
              <div class="meta-row"><span>Labels</span><div>${labelsMarkup(issue.labels) || '—'}</div></div>`}
              <div class="meta-row"><span>Created</span><strong>${escapeHtml(formatDate(issue.createdAt))}</strong></div>
              <div class="meta-row"><span>Updated</span><strong>${escapeHtml(relativeDate(issue.updatedAt))}</strong></div>
            </aside>
          </div>
        </div>`;

      document.querySelector('#close-drawer').addEventListener('click', closeDrawer);
      if (issue.parentIssueKey) {
        document.querySelector('#drawer-parent-link').addEventListener('click', () => openIssue(issue.parentIssueKey));
      }
      document.querySelector('#move-issue-button')?.addEventListener('click', async () => {
        const targetProjectKey = document.querySelector('#move-target-project').value;
        try {
          const moved = await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/move`, { method: 'POST', body: JSON.stringify({ targetProjectKey }) });
          showToast(`${issue.key} moved to ${moved.key}`);
          await loadBaseData();
          await renderCurrentView();
          await openIssue(moved.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('.link-row [data-issue-key]').forEach(element => {
        element.addEventListener('click', () => openIssue(element.dataset.issueKey));
      });

      document.querySelector('#watch-toggle').addEventListener('click', async () => {
        try {
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/watch`, { method: isWatching ? 'DELETE' : 'POST' });
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#vote-toggle').addEventListener('click', async () => {
        try {
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/vote`, { method: isVoting ? 'DELETE' : 'POST' });
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#clone-issue').addEventListener('click', async () => {
        try {
          const cloned = await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/clone`, { method: 'POST' });
          showToast(`${issue.key} cloned as ${cloned.key}`);
          await renderCurrentView();
          await openIssue(cloned.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#delete-issue')?.addEventListener('click', async () => {
        try {
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}`, { method: 'DELETE' });
          showToast(`${issue.key} moved to the recycle bin`);
          closeDrawer();
          await renderCurrentView();
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#link-form').addEventListener('submit', async event => {
        event.preventDefault();
        const values = Object.fromEntries(new FormData(event.currentTarget).entries());
        try {
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/links`, {
            method: 'POST',
            body: JSON.stringify({ targetIssueKey: values.targetIssueKey.trim(), linkType: values.linkType })
          });
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('[data-delete-link]').forEach(button => {
        button.addEventListener('click', async () => {
          try {
            await api(`/api/v1/issue-links/${encodeURIComponent(button.dataset.deleteLink)}`, { method: 'DELETE' });
            await openIssue(issue.key);
          } catch (error) { showToast(error.message); }
        });
      });

      document.querySelector('#drawer-status').addEventListener('change', async event => {
        const newStatusKey = event.target.value;
        const newStatus = STATUSES.find(status => status.key === newStatusKey);
        if (newStatus?.category === 'done' && !issue.resolution) {
          document.querySelector('#drawer-resolution-row').hidden = false;
          return;
        }
        await applyStatusChange(issue.key, newStatusKey, null, issue.version);
      });
      document.querySelector('#resolution-confirm')?.addEventListener('click', async () => {
        const resolution = document.querySelector('#drawer-resolution').value;
        const statusKey = document.querySelector('#drawer-status').value;
        await applyStatusChange(issue.key, statusKey, resolution, issue.version);
      });
      document.querySelector('#resolution-cancel')?.addEventListener('click', () => {
        document.querySelector('#drawer-status').value = issue.status.key;
        document.querySelector('#drawer-resolution-row').hidden = true;
      });
      document.querySelector('#worklog-form').addEventListener('submit', async event => {
        event.preventDefault();
        const values = Object.fromEntries(new FormData(event.currentTarget).entries());
        const timeSpentSeconds = parseDurationToSeconds(values.duration);
        if (!timeSpentSeconds) {
          showToast('Duration must look like "1h 30m", "2h", or "45m"');
          return;
        }
        try {
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/worklogs`, {
            method: 'POST',
            body: JSON.stringify({ workDate: values.workDate, timeSpentSeconds, comment: values.comment || null })
          });
          showToast('Time logged');
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('[data-delete-worklog]').forEach(button => button.addEventListener('click', async () => {
        try {
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/worklogs/${encodeURIComponent(button.dataset.deleteWorklog)}`, { method: 'DELETE' });
          showToast('Worklog deleted');
          await openIssue(issue.key);
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
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/attachments/${encodeURIComponent(button.dataset.deleteAttachment)}`, { method: 'DELETE' });
          showToast('Attachment deleted');
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      }));
      const uploadFiles = async files => {
        for (const file of files) {
          try {
            await uploadAttachmentFile(issue.key, file);
          } catch (error) {
            showToast(`${file.name}: ${error.message}`);
          }
        }
        await openIssue(issue.key);
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

      attachMarkdownToolbar(document.querySelector('#comment-form textarea[name=body]'), issue.key);
      attachMentionAutocomplete(document.querySelector('#comment-form textarea[name=body]'));
      document.querySelector('#comment-form').addEventListener('submit', async event => {
        event.preventDefault();
        const body = new FormData(event.currentTarget).get('body').trim();
        if (!body) return;
        try {
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/comments`, { method: 'POST', body: JSON.stringify({ body }) });
          showToast('Comment added');
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('[data-delete-comment]').forEach(button => button.addEventListener('click', async () => {
        try {
          await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/comments/${encodeURIComponent(button.dataset.deleteComment)}`, { method: 'DELETE' });
          showToast('Comment deleted');
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      }));
      document.querySelectorAll('[data-toggle-reaction]').forEach(button => button.addEventListener('click', async () => {
        const commentId = button.dataset.toggleReaction;
        const reactionKey = button.dataset.reactionKey;
        const path = `/api/v1/issues/${encodeURIComponent(issue.key)}/comments/${encodeURIComponent(commentId)}/reactions/${encodeURIComponent(reactionKey)}`;
        try {
          await api(path, { method: button.classList.contains('reaction-button--active') ? 'DELETE' : 'POST' });
          await openIssue(issue.key);
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
        attachMarkdownToolbar(article.querySelector('.comment-edit-textarea'), issue.key);
        attachMentionAutocomplete(article.querySelector('.comment-edit-textarea'));
        article.querySelector('#comment-edit-cancel').addEventListener('click', () => openIssue(issue.key));
        article.querySelector('#comment-edit-save').addEventListener('click', async () => {
          const newBody = article.querySelector('.comment-edit-textarea').value.trim();
          if (!newBody) return;
          try {
            await api(`/api/v1/issues/${encodeURIComponent(issue.key)}/comments/${encodeURIComponent(commentId)}`, {
              method: 'PATCH',
              body: JSON.stringify({ body: newBody, expectedVersion: comment.version })
            });
            showToast('Comment updated');
            await openIssue(issue.key);
          } catch (error) { showToast(error.message); }
        });
      }));

      if (editing) {
        attachMarkdownToolbar(document.querySelector('#edit-description'), issue.key);
        refreshEditParentOptions(issue).catch(() => {});
        document.querySelector('#edit-type').addEventListener('change', () => refreshEditParentOptions(issue).catch(() => {}));
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
            issueTypeKey: document.querySelector('#edit-type').value,
            parentIssueKey: document.querySelector('#edit-parent').value || null,
            expectedVersion: issue.version
          };
          try {
            await api(`/api/v1/issues/${encodeURIComponent(issue.key)}`, { method: 'PATCH', body: JSON.stringify(payload) });
            showToast(`${issue.key} updated`);
            await renderCurrentView();
            await openIssue(issue.key);
          } catch (error) {
            errorElement.textContent = error.message;
            errorElement.classList.remove('hidden');
          }
        });
      } else {
        document.querySelector('#edit-issue').addEventListener('click', () => render(true));
      }
    }

    render(false);
  } catch (error) {
    issueDrawer.innerHTML = `<div class="drawer-header"><span>Issue</span><button class="icon-button" id="close-drawer">×</button></div><div class="drawer-content"><div class="error-banner">${escapeHtml(error.message)}</div></div>`;
    document.querySelector('#close-drawer').addEventListener('click', closeDrawer);
  }
}

function closeDrawer() {
  issueDrawer.classList.add('hidden');
  drawerBackdrop.classList.add('hidden');
  state.currentIssue = null;
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
  const issueTypeKey = document.querySelector('#create-issue-type').value;
  const label = document.querySelector('#create-parent-label');
  const select = document.querySelector('#create-parent');
  const level = issueTypeHierarchyLevel(issueTypeKey);

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
    const result = await api(`/api/v1/issues?project=${encodeURIComponent(projectKey)}`);
    if (requestId !== createParentRequestId) return; // a newer call already superseded this one
    const candidates = result.items.filter(candidate => issueTypeHierarchyLevel(candidate.type.key) === wantedLevel);
    select.innerHTML += candidates.map(candidate => `<option value="${escapeHtml(candidate.key)}">${escapeHtml(candidate.key)} — ${escapeHtml(candidate.summary)}</option>`).join('');
  } catch {
    // Leave just the "None" option if the project's issues can't be loaded;
    // the create submit itself will surface a clearer error if needed.
  }
}

// Same purpose as refreshCreateParentOptions, adapted for the issue drawer's
// edit form (re-typing/re-parenting an existing issue): the project is
// fixed (moving an issue between projects is a separate action, D37), and
// the issue itself is excluded from its own candidate-parent list. The
// server (TicketService::validateHierarchyShape / IDatabase::editIssue) is
// the actual source of truth, including the "no children" rule for
// retyping across hierarchy levels that this picker doesn't attempt to
// predict client-side.
let editParentRequestId = 0;
async function refreshEditParentOptions(issue) {
  const requestId = ++editParentRequestId;
  const issueTypeKey = document.querySelector('#edit-type').value;
  const label = document.querySelector('#edit-parent-label');
  const select = document.querySelector('#edit-parent');
  const level = issueTypeHierarchyLevel(issueTypeKey);

  if (level === 1) {
    label.classList.add('hidden');
    select.value = '';
    return;
  }
  label.classList.remove('hidden');
  document.querySelector('#edit-parent-label-text').textContent = level === -1 ? 'Parent (required)' : 'Epic (optional)';
  const preselect = level === issueTypeHierarchyLevel(issue.type.key) ? issue.parentIssueKey : null;
  select.innerHTML = '<option value="">None</option>';

  const wantedLevel = level === -1 ? 0 : 1;
  try {
    const result = await api(`/api/v1/issues?project=${encodeURIComponent(issue.projectKey)}`);
    if (requestId !== editParentRequestId) return; // a newer call already superseded this one
    const candidates = result.items.filter(candidate =>
      issueTypeHierarchyLevel(candidate.type.key) === wantedLevel && candidate.key !== issue.key);
    select.innerHTML += candidates.map(candidate =>
      `<option value="${escapeHtml(candidate.key)}" ${candidate.key === preselect ? 'selected' : ''}>${escapeHtml(candidate.key)} — ${escapeHtml(candidate.summary)}</option>`).join('');
  } catch {
    // Leave just the "None" option if the project's issues can't be loaded.
  }
}

function openCreateModal() {
  document.querySelector('#create-error').classList.add('hidden');
  if (state.selectedProject) document.querySelector('#create-project').value = state.selectedProject;
  createModal.classList.remove('hidden');
  refreshCreateParentOptions().catch(() => {});
  createModal.querySelector('input[name="summary"]').focus();
}

function closeCreateModal() {
  createModal.classList.add('hidden');
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
document.querySelector('#create-issue-type').addEventListener('change', () => refreshCreateParentOptions().catch(() => {}));
document.querySelectorAll('[data-close-modal]').forEach(button => button.addEventListener('click', () => {
  button.closest('.modal-backdrop')?.classList.add('hidden');
}));
createModal.addEventListener('click', event => { if (event.target === createModal) closeCreateModal(); });
projectModal.addEventListener('click', event => { if (event.target === projectModal) closeProjectModal(); });
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
    state.filterDueBefore = '';
    navigate('issues');
  }
}, 350));

document.querySelector('#create-form').addEventListener('submit', async event => {
  event.preventDefault();
  const form = event.currentTarget;
  const values = Object.fromEntries(new FormData(form).entries());
  const payload = {
    projectKey: values.projectKey,
    issueTypeKey: values.issueTypeKey,
    summary: values.summary.trim(),
    description: values.description.trim(),
    priorityKey: values.priorityKey,
    assigneeEmail: values.assigneeEmail || null,
    parentIssueKey: values.parentIssueKey || null,
    storyPoints: values.storyPoints ? Number(values.storyPoints) : null,
    dueDate: values.dueDate || null,
    labels: values.labels.split(',').map(value => value.trim()).filter(Boolean)
  };
  const errorElement = document.querySelector('#create-error');
  try {
    const created = await api('/api/v1/issues', { method: 'POST', body: JSON.stringify(payload) });
    state.selectedProject = created.projectKey;
    form.reset();
    closeCreateModal();
    showToast(`${created.key} created`);
    await renderCurrentView();
    await openIssue(created.key);
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

// The create-issue modal's description field is static markup (unlike the
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
    renderCurrentUser();
    showAppShell();
    await loadBaseData();
    await renderCurrentView();
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

document.addEventListener('keydown', event => {
  if (event.key === 'Escape') {
    closeCreateModal();
    closeProjectModal();
    closeDrawer();
    document.querySelector('#sidebar').classList.remove('open');
  }
  if (event.key.toLowerCase() === 'c' && !['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName)) {
    openCreateModal();
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
  } catch (error) {
    showError(error);
  }
})();
