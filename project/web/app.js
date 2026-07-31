'use strict';

const STATUSES = [
  { key: 'backlog', name: 'Backlog', category: 'todo' },
  { key: 'selected', name: 'Selected', category: 'todo' },
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

// Fixed Epic -> Story/Task/Bug -> Sub-task hierarchy (D5, D29, D64-D66):
// 1 = Epic (no parent allowed), -1 = Sub-task (parent required, must be a
// Story/Task/Bug), 0 = Story/Task/Bug (parent optional, must be an Epic).
function issueTypeHierarchyLevel(issueTypeKey) {
  if (issueTypeKey === 'epic') return 1;
  if (issueTypeKey === 'sub-task') return -1;
  return 0;
}

const state = {
  view: 'dashboard',
  projects: [],
  issues: [],
  dashboard: null,
  selectedProject: null,
  search: '',
  status: '',
  currentIssue: null,
  principal: null
};

const content = document.querySelector('#content');
const createModal = document.querySelector('#create-modal');
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
  if (response.status === 401 && path !== '/api/auth/login' && path !== '/api/auth/me') {
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

function showLoginScreen() {
  state.principal = null;
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
}

// Checks the existing session cookie (if any) without ever showing the
// generic "session expired" error -- this is the initial, silent probe.
async function checkExistingSession() {
  try {
    state.principal = await api('/api/auth/me');
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

function issueRows(issues) {
  if (!issues.length) {
    return `<tr><td colspan="6"><div class="empty-state"><strong>No issues found</strong>Adjust the filters or create a new issue.</div></td></tr>`;
  }
  return issues.map(issue => `
    <tr data-issue-key="${escapeHtml(issue.key)}">
      <td><span class="issue-type" title="${escapeHtml(issue.type.name)}"><span style="color:${escapeHtml(issue.type.color)}">${escapeHtml(issue.type.icon)}</span>${escapeHtml(issue.type.name)}</span></td>
      <td><span class="issue-key">${escapeHtml(issue.key)}</span></td>
      <td class="issue-summary">${escapeHtml(issue.summary)}</td>
      <td>${statusChip(issue)}</td>
      <td>${priorityChip(issue)}</td>
      <td>${assigneeMarkup(issue)}</td>
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

function pageHeader(title, subtitle, eyebrow = 'Ticket Hub') {
  return `<div class="page-header"><div><span class="eyebrow">${escapeHtml(eyebrow)}</span><h1>${escapeHtml(title)}</h1><p>${escapeHtml(subtitle)}</p></div></div>`;
}

async function loadBaseData() {
  const [health, projects] = await Promise.all([api('/api/health'), api('/api/projects')]);
  state.projects = projects.items;
  state.selectedProject ||= state.projects[0]?.key || null;
  document.querySelector('#backend-pill').textContent = health.database;
  renderProjectSelectors();
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
  state.dashboard = await api('/api/dashboard');
  const stats = state.dashboard;
  content.innerHTML = `
    ${pageHeader('Dashboard', 'A focused overview of work across all projects.', 'Workspace')}
    <div class="stats-grid">
      <div class="stat-card"><div class="stat-label">All issues</div><div class="stat-value">${stats.totalIssues}</div><div class="stat-meta">Across ${state.projects.length} projects</div></div>
      <div class="stat-card"><div class="stat-label">To do</div><div class="stat-value">${stats.todoIssues}</div><div class="stat-meta">Backlog and selected work</div></div>
      <div class="stat-card"><div class="stat-label">In progress</div><div class="stat-value">${stats.inProgressIssues}</div><div class="stat-meta">Active and in review</div></div>
      <div class="stat-card"><div class="stat-label">Done</div><div class="stat-value">${stats.doneIssues}</div><div class="stat-meta">Completed work</div></div>
    </div>
    ${tablePanel(stats.recentIssues, 'Recently active issues')}`;
  bindIssueLinks();
}

async function fetchIssues() {
  const params = new URLSearchParams();
  if (state.selectedProject) params.set('project', state.selectedProject);
  if (state.status) params.set('status', state.status);
  if (state.search) params.set('q', state.search);
  const result = await api(`/api/issues?${params}`);
  state.issues = result.items;
}

async function renderIssues() {
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  await fetchIssues();
  const projectOptions = state.projects.map(project => `<option value="${escapeHtml(project.key)}" ${project.key === state.selectedProject ? 'selected' : ''}>${escapeHtml(project.key)} — ${escapeHtml(project.name)}</option>`).join('');
  const statusOptions = STATUSES.map(status => `<option value="${status.key}" ${status.key === state.status ? 'selected' : ''}>${status.name}</option>`).join('');
  content.innerHTML = `
    ${pageHeader('Issues', 'Search, filter, and inspect the work items in a project.', state.selectedProject || 'All projects')}
    <div class="panel">
      <div class="filter-bar">
        <select id="issue-project-filter"><option value="">All projects</option>${projectOptions}</select>
        <select id="issue-status-filter"><option value="">All statuses</option>${statusOptions}</select>
        <input id="issue-search-filter" type="search" value="${escapeHtml(state.search)}" placeholder="Filter by key or summary">
        <button class="secondary-button" id="clear-filters">Clear</button>
      </div>
      <div style="overflow-x:auto">
        <table class="issue-table">
          <thead><tr><th>Type</th><th>Key</th><th>Summary</th><th>Status</th><th>Priority</th><th>Assignee</th></tr></thead>
          <tbody>${issueRows(state.issues)}</tbody>
        </table>
      </div>
    </div>`;

  document.querySelector('#issue-project-filter').addEventListener('change', event => {
    state.selectedProject = event.target.value || null;
    renderIssues().catch(showError);
  });
  document.querySelector('#issue-status-filter').addEventListener('change', event => {
    state.status = event.target.value;
    renderIssues().catch(showError);
  });
  document.querySelector('#issue-search-filter').addEventListener('input', debounce(event => {
    state.search = event.target.value.trim();
    renderIssues().catch(showError);
  }, 300));
  document.querySelector('#clear-filters').addEventListener('click', () => {
    state.status = '';
    state.search = '';
    state.selectedProject = null;
    renderIssues().catch(showError);
  });
  bindIssueLinks();
}

async function renderBoard() {
  state.selectedProject ||= state.projects[0]?.key || null;
  state.search = '';
  state.status = '';
  content.innerHTML = '<div class="loading-state"><div class="spinner"></div></div>';
  await fetchIssues();
  const selected = state.projects.find(project => project.key === state.selectedProject);
  const columns = STATUSES.map(status => {
    const issues = state.issues.filter(issue => issue.status.key === status.key);
    return `
      <section class="board-column">
        <div class="board-column-header"><strong>${escapeHtml(status.name)}</strong><span class="column-count">${issues.length}</span></div>
        <div class="board-list">
          ${issues.length ? issues.map(issue => `
            <article class="issue-card" data-issue-key="${escapeHtml(issue.key)}">
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
      <div><span class="eyebrow">${escapeHtml(state.selectedProject || 'Project')}</span><h1>${escapeHtml(selected?.name || 'Board')}</h1><p>Simple status-based Kanban board.</p></div>
      <div class="page-actions"><select id="board-project" class="status-select">${state.projects.map(project => `<option value="${escapeHtml(project.key)}" ${project.key === state.selectedProject ? 'selected' : ''}>${escapeHtml(project.key)} — ${escapeHtml(project.name)}</option>`).join('')}</select></div>
    </div>
    <div class="board">${columns}</div>`;
  document.querySelector('#board-project').addEventListener('change', event => {
    state.selectedProject = event.target.value;
    renderBoard().catch(showError);
  });
  bindIssueLinks();
}

function renderProjects() {
  content.innerHTML = `
    ${pageHeader('Projects', 'Choose a project to see its issues and board.', 'Workspace')}
    <div class="project-grid">
      ${state.projects.map(project => `
        <article class="project-card" data-project-card="${escapeHtml(project.key)}">
          <div class="project-card-head"><span class="project-avatar">${escapeHtml(project.key.slice(0, 2))}</span><div><h3>${escapeHtml(project.name)}</h3><span class="issue-key">${escapeHtml(project.key)}</span></div></div>
          <p>${escapeHtml(project.description)}</p>
          <div class="project-card-stats"><div><strong>${project.issueCount}</strong><span>Total issues</span></div><div><strong>${project.openIssueCount}</strong><span>Open issues</span></div><div><strong>${escapeHtml(project.lead?.displayName || '—')}</strong><span>Lead</span></div></div>
        </article>`).join('')}
    </div>`;
  document.querySelectorAll('[data-project-card]').forEach(card => card.addEventListener('click', () => {
    state.selectedProject = card.dataset.projectCard;
    navigate('board');
  }));
}

async function renderCurrentView() {
  try {
    if (state.view === 'dashboard') await renderDashboard();
    else if (state.view === 'board') await renderBoard();
    else if (state.view === 'issues') await renderIssues();
    else renderProjects();
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
  });
}

// Resolution is required exactly when moving to a Done-category status
// (D68-D70) -- omitted (null) for every other transition, which leaves it
// untouched, or lets the server clear it automatically when reopening.
async function applyStatusChange(issueKey, statusKey, resolution, expectedVersion) {
  try {
    const payload = { statusKey, expectedVersion };
    if (resolution) payload.resolution = resolution;
    await api(`/api/issues/${encodeURIComponent(issueKey)}/status`, { method: 'PATCH', body: JSON.stringify(payload) });
    showToast(`${issueKey} status updated`);
    await renderCurrentView();
    await openIssue(issueKey);
  } catch (error) {
    showToast(error.message);
  }
}

function editFieldsMarkup(issue) {
  return `
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
    const [issue, comments, links, watchers, voters] = await Promise.all([
      api(`/api/issues/${encodeURIComponent(issueKey)}`),
      api(`/api/issues/${encodeURIComponent(issueKey)}/comments`),
      api(`/api/issues/${encodeURIComponent(issueKey)}/links`),
      api(`/api/issues/${encodeURIComponent(issueKey)}/watchers`),
      api(`/api/issues/${encodeURIComponent(issueKey)}/voters`)
    ]);
    state.currentIssue = issue;

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
          </div>
          ${editing
            ? `<div class="form-grid" id="edit-form">${editFieldsMarkup(issue)}</div>
               <div id="edit-error" class="form-error hidden"></div>
               <div class="modal-footer" style="padding:0 0 20px"><button type="button" class="secondary-button" id="edit-cancel">Cancel</button><button type="button" class="primary-button" id="edit-save">Save changes</button></div>`
            : `<h1>${escapeHtml(issue.summary)}</h1>`}
          <div class="drawer-layout">
            <div>
              ${editing ? '' : `<section class="drawer-section"><h3>Description</h3><div class="description">${escapeHtml(issue.description || 'No description provided.')}</div></section>`}
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
                <h3>Comments</h3>
                <div class="comment-list">${comments.items.length ? comments.items.map(comment => `
                  <article class="comment"><span class="small-avatar">${escapeHtml(initials(comment.author.displayName))}</span><div class="comment-body"><header><strong>${escapeHtml(comment.author.displayName)}</strong><span>${escapeHtml(relativeDate(comment.createdAt))}</span></header><p>${escapeHtml(comment.body)}</p></div></article>`).join('') : '<div class="empty-state">No comments yet.</div>'}</div>
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
      document.querySelectorAll('.link-row [data-issue-key]').forEach(element => {
        element.addEventListener('click', () => openIssue(element.dataset.issueKey));
      });

      document.querySelector('#watch-toggle').addEventListener('click', async () => {
        try {
          await api(`/api/issues/${encodeURIComponent(issue.key)}/watch`, { method: isWatching ? 'DELETE' : 'POST' });
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#vote-toggle').addEventListener('click', async () => {
        try {
          await api(`/api/issues/${encodeURIComponent(issue.key)}/vote`, { method: isVoting ? 'DELETE' : 'POST' });
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#clone-issue').addEventListener('click', async () => {
        try {
          const cloned = await api(`/api/issues/${encodeURIComponent(issue.key)}/clone`, { method: 'POST' });
          showToast(`${issue.key} cloned as ${cloned.key}`);
          await renderCurrentView();
          await openIssue(cloned.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelector('#link-form').addEventListener('submit', async event => {
        event.preventDefault();
        const values = Object.fromEntries(new FormData(event.currentTarget).entries());
        try {
          await api(`/api/issues/${encodeURIComponent(issue.key)}/links`, {
            method: 'POST',
            body: JSON.stringify({ targetIssueKey: values.targetIssueKey.trim(), linkType: values.linkType })
          });
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });
      document.querySelectorAll('[data-delete-link]').forEach(button => {
        button.addEventListener('click', async () => {
          try {
            await api(`/api/issue-links/${encodeURIComponent(button.dataset.deleteLink)}`, { method: 'DELETE' });
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
      document.querySelector('#comment-form').addEventListener('submit', async event => {
        event.preventDefault();
        const body = new FormData(event.currentTarget).get('body').trim();
        if (!body) return;
        try {
          await api(`/api/issues/${encodeURIComponent(issue.key)}/comments`, { method: 'POST', body: JSON.stringify({ body }) });
          showToast('Comment added');
          await openIssue(issue.key);
        } catch (error) { showToast(error.message); }
      });

      if (editing) {
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
            expectedVersion: issue.version
          };
          try {
            await api(`/api/issues/${encodeURIComponent(issue.key)}`, { method: 'PATCH', body: JSON.stringify(payload) });
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
    const result = await api(`/api/issues?project=${encodeURIComponent(projectKey)}`);
    if (requestId !== createParentRequestId) return; // a newer call already superseded this one
    const candidates = result.items.filter(candidate => issueTypeHierarchyLevel(candidate.type.key) === wantedLevel);
    select.innerHTML += candidates.map(candidate => `<option value="${escapeHtml(candidate.key)}">${escapeHtml(candidate.key)} — ${escapeHtml(candidate.summary)}</option>`).join('');
  } catch {
    // Leave just the "None" option if the project's issues can't be loaded;
    // the create submit itself will surface a clearer error if needed.
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
document.querySelectorAll('[data-close-modal]').forEach(button => button.addEventListener('click', closeCreateModal));
createModal.addEventListener('click', event => { if (event.target === createModal) closeCreateModal(); });
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
    const created = await api('/api/issues', { method: 'POST', body: JSON.stringify(payload) });
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

loginForm.addEventListener('submit', async event => {
  event.preventDefault();
  loginError.classList.add('hidden');
  const values = Object.fromEntries(new FormData(loginForm).entries());
  try {
    await api('/api/auth/login', { method: 'POST', body: JSON.stringify({ email: values.email.trim(), password: values.password }) });
    loginForm.reset();
    state.principal = await api('/api/auth/me');
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
    await api('/api/auth/logout', { method: 'POST' });
  } catch {
    // Best-effort: show the login screen regardless of the response.
  }
  showLoginScreen();
});

document.addEventListener('keydown', event => {
  if (event.key === 'Escape') {
    closeCreateModal();
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
