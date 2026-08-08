// Stateful interaction controller for the ticket drawer. It intentionally
// receives the DOM node and the tiny HTML escaper as dependencies, keeping it
// framework-free and making app.js responsible only for ticket data/rendering.
export function createTicketDrawerControls(ticketDrawer, escapeHtml) {
  const storageKey = 'ticket-hub.ticket-drawer-width';
  const defaultWidth = 720;
  const minimumWidth = 480;
  let fullscreen = false;

  function maximumWidth() {
    return Math.max(minimumWidth, Math.floor(window.innerWidth * 0.94));
  }

  function setWidth(width, persist = false) {
    const normalized = Math.max(minimumWidth, Math.min(maximumWidth(), Math.round(width)));
    document.documentElement.style.setProperty('--ticket-drawer-width', `${normalized}px`);
    if (persist) {
      try {
        localStorage.setItem(storageKey, String(normalized));
      } catch {
        // Resizing still works when browser storage is disabled; only the
        // next-session preference is unavailable.
      }
    }
    return normalized;
  }

  function restoreWidth() {
    try {
      const saved = Number(localStorage.getItem(storageKey));
      setWidth(Number.isFinite(saved) && saved > 0 ? saved : defaultWidth);
    } catch {
      setWidth(defaultWidth);
    }
  }

  function headerMarkup(title) {
    const expandLabel = fullscreen ? 'Exit full screen' : 'Expand to full screen';
    return `
      <div class="drawer-resize-handle" id="drawer-resize-handle" role="separator" aria-orientation="vertical" aria-controls="ticket-drawer" aria-label="Resize ticket detail" tabindex="0" title="Drag or use arrow keys to resize"></div>
      <div class="drawer-header">
        <span class="ticket-key">${escapeHtml(title)}</span>
        <div class="drawer-header-controls">
          <button type="button" class="icon-button" id="toggle-drawer-fullscreen" aria-label="${expandLabel}" title="${expandLabel}">${fullscreen ? '↙' : '⛶'}</button>
          <button type="button" class="icon-button" id="close-drawer" aria-label="Close">×</button>
        </div>
      </div>`;
  }

  function update() {
    ticketDrawer.classList.toggle('ticket-drawer--fullscreen', fullscreen);
    const resizeHandle = ticketDrawer.querySelector('#drawer-resize-handle');
    if (resizeHandle) {
      const canResize = !fullscreen && window.innerWidth > 760;
      const width = Math.round(ticketDrawer.getBoundingClientRect().width);
      resizeHandle.tabIndex = canResize ? 0 : -1;
      resizeHandle.setAttribute('aria-disabled', String(!canResize));
      resizeHandle.setAttribute('aria-valuemin', String(minimumWidth));
      resizeHandle.setAttribute('aria-valuemax', String(maximumWidth()));
      resizeHandle.setAttribute('aria-valuenow', String(width));
      resizeHandle.setAttribute('aria-valuetext', `${width} pixels wide`);
    }
    const fullscreenButton = ticketDrawer.querySelector('#toggle-drawer-fullscreen');
    if (fullscreenButton) {
      const label = fullscreen ? 'Exit full screen' : 'Expand to full screen';
      fullscreenButton.textContent = fullscreen ? '↙' : '⛶';
      fullscreenButton.setAttribute('aria-label', label);
      fullscreenButton.title = label;
    }
  }

  function setFullscreen(value) {
    fullscreen = value;
    update();
  }

  function beginResize(event) {
    if (event.button !== 0 || fullscreen || window.innerWidth <= 760) return;
    event.preventDefault();
    const resizeHandle = event.currentTarget;
    const pointerId = event.pointerId;
    const startX = event.clientX;
    const startWidth = ticketDrawer.getBoundingClientRect().width;
    document.body.classList.add('drawer-resizing');
    resizeHandle.setPointerCapture(pointerId);

    const onPointerMove = moveEvent => {
      if (moveEvent.pointerId !== pointerId) return;
      setWidth(startWidth + startX - moveEvent.clientX);
      update();
    };
    const finishResize = endEvent => {
      if (endEvent.pointerId !== pointerId) return;
      document.body.classList.remove('drawer-resizing');
      if (resizeHandle.hasPointerCapture(pointerId)) resizeHandle.releasePointerCapture(pointerId);
      resizeHandle.removeEventListener('pointermove', onPointerMove);
      resizeHandle.removeEventListener('pointerup', finishResize);
      resizeHandle.removeEventListener('pointercancel', finishResize);
      setWidth(ticketDrawer.getBoundingClientRect().width, true);
      update();
    };
    resizeHandle.addEventListener('pointermove', onPointerMove);
    resizeHandle.addEventListener('pointerup', finishResize);
    resizeHandle.addEventListener('pointercancel', finishResize);
  }

  function bind() {
    const resizeHandle = ticketDrawer.querySelector('#drawer-resize-handle');
    resizeHandle?.addEventListener('pointerdown', beginResize);
    resizeHandle?.addEventListener('keydown', event => {
      if (fullscreen || window.innerWidth <= 760) return;
      const step = event.shiftKey ? 80 : 32;
      const currentWidth = ticketDrawer.getBoundingClientRect().width;
      let nextWidth = null;
      if (event.key === 'ArrowLeft') nextWidth = currentWidth + step;
      if (event.key === 'ArrowRight') nextWidth = currentWidth - step;
      if (event.key === 'Home') nextWidth = minimumWidth;
      if (event.key === 'End') nextWidth = maximumWidth();
      if (nextWidth === null) return;
      event.preventDefault();
      setWidth(nextWidth, true);
      update();
    });
    ticketDrawer.querySelector('#toggle-drawer-fullscreen')?.addEventListener('click', () => {
      setFullscreen(!fullscreen);
    });
    update();
  }

  function install() {
    restoreWidth();
    window.addEventListener('resize', update);
  }

  return { headerMarkup, bind, setFullscreen, isFullscreen: () => fullscreen, install };
}
