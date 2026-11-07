/* Galpon UI — bridge to the native engine + small helpers.
   In the app, window.__JUCE__ exists and calls go to C++.
   In a normal browser (for working on the design) dev/mock.js fakes the engine. */
(function () {
  'use strict';
  const G = (window.G = window.G || {});
  const listeners = {};
  let promiseId = 0;
  const pending = new Map();

  G.native = typeof window.__JUCE__ !== 'undefined';

  if (G.native) {
    window.__JUCE__.backend.addEventListener('__juce__complete', (payload) => {
      const p = pending.get(payload.promiseId);
      if (p) { pending.delete(payload.promiseId); p(payload.result); }
    });
  }

  /** Calls the engine. Resolves with the result object ({ok, data, error}). */
  G.call = function (cmd, args) {
    args = args || {};
    if (!G.native) return G.mock ? Promise.resolve(G.mock.call(cmd, args)) : Promise.resolve({ ok: false, error: 'sin motor' });
    return new Promise((resolve) => {
      const id = promiseId++;
      pending.set(id, resolve);
      window.__JUCE__.backend.emitEvent('__juce__invoke', { name: 'galpon', params: [cmd, args], resultId: id });
    });
  };

  /** Like call, but shows a message on failure and returns data. */
  G.run = async function (cmd, args) {
    const r = await G.call(cmd, args);
    if (r && r.ok === false) { G.toast(r.error || 'Algo salió mal', true); return undefined; }
    return r ? r.data : undefined;
  };

  G.on = function (event, fn) {
    (listeners[event] = listeners[event] || []).push(fn);
    if (G.native) window.__JUCE__.backend.addEventListener(event, fn);
  };

  G.emitLocal = function (event, payload) {
    (listeners[event] || []).forEach((fn) => fn(payload));
  };

  // ---------------------------------------------------------------- DOM helpers
  G.$ = (sel, root) => (root || document).querySelector(sel);
  G.$$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));

  /** el('div.cls#id', {attrs/props/on...}, children...) */
  G.el = function (spec, props, ...children) {
    const m = spec.match(/^([a-z0-9-]+)?((?:[.#][\w-]+)*)$/i) || [];
    const node = document.createElement(m[1] || 'div');
    (m[2] || '').replace(/([.#])([\w-]+)/g, (_, kind, name) => {
      if (kind === '.') node.classList.add(name); else node.id = name;
    });
    if (props) {
      for (const [k, v] of Object.entries(props)) {
        if (v === undefined || v === null || v === false) continue;
        if (k.startsWith('on')) node.addEventListener(k.slice(2).toLowerCase(), v);
        else if (k === 'style' && typeof v === 'object') {
          for (const [prop, val] of Object.entries(v)) {
            if (prop.startsWith('--')) node.style.setProperty(prop, val);
            else node.style[prop] = val;
          }
        }
        else if (k === 'html') node.innerHTML = v;
        else if (k === 'text') node.textContent = v;
        else if (k in node && typeof v !== 'string') node[k] = v;
        else node.setAttribute(k, v === true ? '' : v);
      }
    }
    for (const c of children.flat()) {
      if (c === null || c === undefined || c === false) continue;
      node.appendChild(typeof c === 'string' || typeof c === 'number' ? document.createTextNode(String(c)) : c);
    }
    return node;
  };

  G.icon = function (name, size) {
    const span = document.createElement('span');
    span.className = 'ico';
    span.innerHTML = (G.icons[name] || '').replace('<svg ', `<svg width="${size || 16}" height="${size || 16}" aria-hidden="true" `);
    return span;
  };

  G.icons = {
    library: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 5h4v14H4zM10 5h4v14h-4zM16 6l3.5-1 3 13.5-3.5 1z"/></svg>',
    controls: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="7" cy="12" r="3"/><circle cx="17" cy="12" r="3"/><path d="M7 3v6M7 15v6M17 3v6M17 15v6"/></svg>',
    editor: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 6h7M3 12h12M3 18h5M14 6h7M18 18h3"/></svg>',
    start: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M6 5v14M19 5l-9 7 9 7z"/></svg>',
    play: '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M7 4l13 8-13 8z"/></svg>',
    stop: '<svg viewBox="0 0 24 24" fill="currentColor"><rect x="6" y="6" width="12" height="12" rx="1.5"/></svg>',
    cycle: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M17 2l3 3-3 3M4 11V9a4 4 0 0 1 4-4h12M7 22l-3-3 3-3M20 13v2a4 4 0 0 1-4 4H4"/></svg>',
    metronome: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M9 3h6l4 18H5zM12 15l5-8"/></svg>',
    countin: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 12h3M10 8v8M14 6v12M18 9v6"/></svg>',
    loops: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M12 4a8 8 0 1 0 8 8"/><path d="M20 4v8h-8"/></svg>',
    keyboard: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><rect x="2" y="6" width="20" height="12" rx="2"/><path d="M7 6v7M12 6v7M17 6v7"/></svg>',
    tuner: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 18a8 8 0 1 1 16 0"/><path d="M12 18l4-6"/></svg>',
    settings: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.6 1.6 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.6 1.6 0 0 0-1.8-.3 1.6 1.6 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.6 1.6 0 0 0-1-1.5 1.6 1.6 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.6 1.6 0 0 0 .3-1.8 1.6 1.6 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.6 1.6 0 0 0 1.5-1 1.6 1.6 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.6 1.6 0 0 0 1.8.3H9a1.6 1.6 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.6 1.6 0 0 0 1 1.5 1.6 1.6 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.6 1.6 0 0 0-.3 1.8V9a1.6 1.6 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.6 1.6 0 0 0-1.5 1z"/></svg>',
    plus: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round"><path d="M12 5v14M5 12h14"/></svg>',
    close: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><path d="M6 6l12 12M18 6L6 18"/></svg>',
    headphones: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 15v-3a8 8 0 0 1 16 0v3"/><rect x="3" y="14" width="4" height="7" rx="1.5"/><rect x="17" y="14" width="4" height="7" rx="1.5"/></svg>',
    export: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M12 3v12M7 8l5-5 5 5M5 15v4a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-4"/></svg>',
    power: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><path d="M12 3v8M6.3 7.3a8 8 0 1 0 11.4 0"/></svg>',
    window: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18"/></svg>',
    folder: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></svg>',
    home: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 11l9-7 9 7v9a1 1 0 0 1-1 1h-5v-6h-6v6H4a1 1 0 0 1-1-1z"/></svg>',
    mixer: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M5 3v18M12 3v18M19 3v18"/><rect x="3" y="13" width="4" height="3" rx="1" fill="currentColor"/><rect x="10" y="7" width="4" height="3" rx="1" fill="currentColor"/><rect x="17" y="15" width="4" height="3" rx="1" fill="currentColor"/></svg>',
    back: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 18l-6-6 6-6"/></svg>',
    pause: '<svg viewBox="0 0 24 24" fill="currentColor"><rect x="6" y="4" width="4" height="16" rx="1"/><rect x="14" y="4" width="4" height="16" rx="1"/></svg>',
    game: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 7h18M3 12h18M3 17h18"/><rect x="6" y="5" width="4" height="4" rx="1" fill="currentColor"/><rect x="13" y="10" width="4" height="4" rx="1" fill="currentColor"/><rect x="8" y="15" width="4" height="4" rx="1" fill="currentColor"/></svg>',
    undo: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M9 14L4 9l5-5"/><path d="M4 9h11a5 5 0 0 1 0 10h-3"/></svg>',
  };

  // ---------------------------------------------------------------- misc
  G.clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  G.dbToText = (db) => (db <= -79 ? '-∞' : (db > 0 ? '+' : '') + db.toFixed(1)) + ' dB';
  G.noteName = (p) => ['Do', 'Do#', 'Re', 'Re#', 'Mi', 'Fa', 'Fa#', 'Sol', 'Sol#', 'La', 'La#', 'Si'][((p % 12) + 12) % 12] + (Math.floor(p / 12) - 1);
  G.debounce = (fn, ms) => { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; };
  G.throttle = (fn, ms) => {
    let last = 0, timer = null, lastArgs;
    return (...a) => {
      lastArgs = a;
      const now = performance.now();
      if (now - last >= ms) { last = now; fn(...a); }
      else if (!timer) timer = setTimeout(() => { timer = null; last = performance.now(); fn(...lastArgs); }, ms - (now - last));
    };
  };

  let toastTimer;
  G.toast = function (message, isError) {
    const t = G.$('#toast');
    if (!t) return;
    t.textContent = message;
    t.classList.toggle('error', !!isError);
    t.classList.add('show');
    clearTimeout(toastTimer);
    // long messages stay longer, so there's time to read them
    toastTimer = setTimeout(() => t.classList.remove('show'), Math.max(isError ? 6000 : 2600, message.length * 65));
  };

  /** Simple context menu. items: [{label, action, danger, disabled}|'-'] */
  G.menu = function (x, y, items) {
    G.closeMenu();
    const m = G.el('div.menu', { role: 'menu' });
    for (const it of items) {
      if (it === '-') { m.appendChild(G.el('div.menu-sep')); continue; }
      if (it.heading) { m.appendChild(G.el('div.menu-heading', { text: it.heading })); continue; }
      m.appendChild(G.el('button.menu-item' + (it.danger ? '.danger' : ''), {
        role: 'menuitem', disabled: it.disabled,
        onclick: () => { G.closeMenu(); it.action && it.action(); },
      }, it.label, it.hint ? G.el('span.menu-hint', { text: it.hint }) : null));
    }
    document.body.appendChild(m);
    const r = m.getBoundingClientRect();
    m.style.left = Math.min(x, window.innerWidth - r.width - 8) + 'px';
    m.style.top = Math.min(y, window.innerHeight - r.height - 8) + 'px';
    setTimeout(() => document.addEventListener('pointerdown', G._menuAway, { once: true, capture: true }), 0);
  };
  G._menuAway = (e) => { if (!e.target.closest('.menu')) G.closeMenu(); else document.addEventListener('pointerdown', G._menuAway, { once: true, capture: true }); };
  G.closeMenu = () => G.$$('.menu').forEach((m) => m.remove());

  /** Modal dialog. Returns {close, body}. */
  G.modal = function (title, build, opts) {
    opts = opts || {};
    const back = G.el('div.modal-back');
    const box = G.el('div.modal' + (opts.wide ? '.wide' : ''), { role: 'dialog', 'aria-label': title });
    const close = () => { back.remove(); opts.onClose && opts.onClose(); };
    const body = G.el('div.modal-body');
    box.append(
      G.el('div.modal-head', null, G.el('h2', { text: title }),
        G.el('button.icon-btn', { 'aria-label': 'Cerrar', onclick: close }, G.icon('close'))),
      body);
    back.appendChild(box);
    back.addEventListener('pointerdown', (e) => { if (e.target === back && !opts.sticky) close(); });
    document.body.appendChild(back);
    build(body, close);
    return { close, body };
  };

  G.prompt = function (title, value) {
    return new Promise((resolve) => {
      let done = false;
      const dlg = G.modal(title, (body, close) => {
        const input = G.el('input.text', { value: value || '' });
        const finish = (v) => { if (!done) { done = true; resolve(v); close(); } };
        input.addEventListener('keydown', (e) => { if (e.key === 'Enter') finish(input.value); if (e.key === 'Escape') finish(null); });
        body.append(input, G.el('div.row.end', null,
          G.el('button.btn', { onclick: () => finish(null), text: 'Cancelar' }),
          G.el('button.btn.primary', { onclick: () => finish(input.value), text: 'Aceptar' })));
        setTimeout(() => { input.focus(); input.select(); }, 30);
      }, { onClose: () => { if (!done) { done = true; resolve(null); } } });
      return dlg;
    });
  };

  // In a plain browser, load the fake engine so the interface can be tried out.
  if (!G.native) {
    document.write('<script src="dev/mock.js"><\/script>');
  }
})();
