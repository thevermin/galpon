/* Galpon UI — modo Juego: biblioteca de canciones .feedpak y la autopista de notas.
   Galpon solo lee .feedpak (el formato abierto de feedBack). La detección de notas y los puntos vienen después. */
(function () {
  'use strict';
  const G = window.G;
  const { el, $ } = G;
  const Game = (G.Game = { active: false });

  // ------------------------------------------------------------------ settings (per viewer)
  const store = {
    get(k, d) { try { const v = localStorage.getItem('galpon.game.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem('galpon.game.' + k, JSON.stringify(v)); } catch (e) { /* storage unavailable */ } },
  };
  const opts = {
    palette: store.get('palette', 'classic'),     // classic | soft
    offsetMs: store.get('offsetMs', 0),           // calibration: + if the notes arrive late compared to what you hear
    zoom: store.get('zoom', 1),
    lyrics: store.get('lyrics', true),
    size: store.get('size', 1),                   // the whole highway and fretboard (− / +)
    invert: store.get('invert', false),           // true: low string on top, like Rocksmith
    view: store.get('view', '2d'),                // '2d' tablature highway, '3d' Rocksmith-like neck in perspective
    detect: store.get('detect', true),
    detectOffsetMs: store.get('detectOffsetMs', 0),
    autoTone: store.get('autoTone', false),
    t3assign: store.get('t3assign', {}),
    filter: 'all',
    query: '',
  };

  // string colours, from the lowest string (index 0) up
  const PALETTES = {
    classic: ['#ef4444', '#facc15', '#3b82f6', '#f59e0b', '#22c55e', '#a855f7', '#94a3b8', '#e2e8f0'],
    soft:    ['#e88a8a', '#e8d27a', '#7fa8e8', '#e8b07a', '#86d1a0', '#b99be8', '#a7b0bd', '#d8dde4'],
  };
  const colour = (s) => PALETTES[opts.palette][s] || '#ccc';
  G.stringColour = colour;   // the tuner uses the same colours

  // ------------------------------------------------------------------ tunings
  const NOTE = new Proxy([], { get: (_, i) => (/^\d+$/.test(String(i)) ? G.pcName(+i) : undefined) });   // C D E… or Do Re Mi…
  const LETTER = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const GUITAR = [40, 45, 50, 55, 59, 64, 69, 74];     // E2 A2 D2 G3 B3 E4 (+ extra strings)
  const BASS = [28, 33, 38, 43, 48, 53];               // E1 A1 D2 G2 (+ C3 F3)
  function openPitches(arr) {
    const tuning = (arr.tuning && arr.tuning.length) ? arr.tuning : [0, 0, 0, 0, 0, 0];
    const n = tuning.length;
    const bass = isBass(arr);
    let base;
    if (bass) base = n >= 5 && n !== 6 ? [23].concat(BASS).slice(0, n) : BASS.slice(0, n);   // 5 strings: low B
    else base = n === 7 ? [35].concat(GUITAR.slice(0, 6)) : n === 8 ? [30, 35].concat(GUITAR.slice(0, 6)) : GUITAR.slice(0, n);
    const capo = arr.capo || 0;
    return base.map((p, i) => p + (tuning[i] || 0) + capo);
  }
  function isBass(arr) {
    const t = String(arr.type || '').toLowerCase(), name = String(arr.name || arr.id || '').toLowerCase();
    return t === 'bass' || /bass|bajo/.test(name);
  }
  function tuningName(arr) {
    const t = (arr.tuning && arr.tuning.length) ? arr.tuning : [0, 0, 0, 0, 0, 0];
    const bass = isBass(arr);
    let name = (t.length === (bass ? 4 : 6) || t.length === 6) ? G.tuningName(t, bass) : 'Personalizada';
    if (name === 'Personalizada') name = openPitches(Object.assign({}, arr, { capo: 0 })).map((p) => NOTE[p % 12]).join(' ');
    if (arr.capo) name += ' · cejilla ' + arr.capo;
    if (play && play.arr === arr && play.virtualShift) name += ' · virtual';
    return name;
  }
  function kindName(a) {
    if (isBass(a)) return 'Bajo';
    const n = String(a.name || a.id || '').toLowerCase();
    if (/rhythm|ritmo/.test(n)) return 'Guitarra rítmica';
    if (/lead|solo|principal/.test(n)) return 'Guitarra principal';
    return 'Guitarra' + (a.name ? ' · ' + a.name : '');
  }
  const fmtTime = (s) => { s = Math.max(0, s || 0); return Math.floor(s / 60) + ':' + String(Math.floor(s % 60)).padStart(2, '0'); };

  // ------------------------------------------------------------------ root & mode switch
  let root, lib = { songs: [], folders: [] }, selected = null;
  function ensureRoot() {
    if (root) return root;
    root = el('div#game.hidden', { role: 'region', 'aria-label': 'Modo juego' });
    document.body.appendChild(root);
    return root;
  }

  Game.enter = async function () {
    ensureRoot();
    Game.active = true;
    document.body.classList.add('in-game');
    root.classList.remove('hidden');
    if (G.tick && G.tick.playing) G.call('stop');
    if (play) renderPlay(); else await showLibrary();
  };
  Game.leave = function () {
    if (play) G.call('gamePause');
    Game.active = false;
    document.body.classList.remove('in-game');
    if (root) root.classList.add('hidden');
  };

  // ------------------------------------------------------------------ library
  async function showLibrary() {
    stopDrawing();
    root.replaceChildren(el('div.g-loading', { text: 'Buscando canciones…' }));
    const r = await G.call('gameLibrary');
    if (r && r.ok) lib = r.data;
    renderLibrary();
  }

  function cover(song, size) {
    if (song.cover) return el('img.g-cover', { src: song.cover, alt: '', style: { width: size + 'px', height: size + 'px' } });
    // a colour from the title, so every song has its own face
    let h = 0;
    for (const c of String(song.title || '?')) h = (h * 31 + c.charCodeAt(0)) % 360;
    const initials = String(song.title || '?').split(/\s+/).slice(0, 2).map((w) => w[0] || '').join('').toUpperCase();
    return el('div.g-cover.fake', { style: { width: size + 'px', height: size + 'px', background: `linear-gradient(135deg, hsl(${h} 45% 32%), hsl(${(h + 50) % 360} 40% 18%))`, fontSize: Math.round(size / 3) + 'px' } }, initials);
  }

  function renderLibrary() {
    const q = opts.query.trim().toLowerCase();
    const songs = lib.songs.filter((s) => {
      if (q && !(String(s.title) + ' ' + String(s.artist) + ' ' + String(s.album || '')).toLowerCase().includes(q)) return false;
      if (opts.filter === 'bass') return (s.arrangements || []).some((a) => a.playable && isBass(a));
      if (opts.filter === 'guitar') return (s.arrangements || []).some((a) => a.playable && !isBass(a));
      return true;
    }).sort((a, b) => String(a.artist).localeCompare(String(b.artist)) || String(a.title).localeCompare(String(b.title)));
    if (selected && !lib.songs.find((s) => s.path === selected.path)) selected = null;

    const head = el('header.g-head', null,
      el('button.g-mode', { title: 'Volver al estudio', onclick: Game.leave }, G.icon('back', 16), 'Estudio'),
      el('h1', { text: 'Juego' }),
      el('input.text.g-search', { type: 'search', placeholder: 'Buscar canción o artista', value: opts.query, 'aria-label': 'Buscar',
        oninput: (e) => { opts.query = e.target.value; renderGrid(); } }),
      el('div.seg', { role: 'group', 'aria-label': 'Filtrar' },
        [['all', 'Todas'], ['guitar', 'Guitarra'], ['bass', 'Bajo']].map(([id, label]) =>
          el('button' + (opts.filter === id ? '.on' : ''), { 'aria-pressed': String(opts.filter === id), onclick: () => { opts.filter = id; renderLibrary(); } }, label))),
      el('div.spacer'),
      el('button.btn', { onclick: importSongs }, 'Importar canciones…'),
      el('button.btn', { title: 'Sumar una carpeta donde ya tienes canciones .feedpak', onclick: addFolder }, 'Agregar carpeta…'));

    const grid = el('div.g-grid');
    function renderGrid() {
      const qq = opts.query.trim().toLowerCase();
      grid.replaceChildren(...songs.filter((s) => !qq || (String(s.title) + ' ' + String(s.artist)).toLowerCase().includes(qq)).map((s) =>
        el('button.g-card' + (selected && selected.path === s.path ? '.on' : '') + (s.error ? '.broken' : ''), {
          onclick: () => { selected = s; renderLibrary(); },
          ondblclick: () => { const a = (s.arrangements || []).find((x) => x.playable); if (a && !s.error) openSong(s, a.id); },
        }, cover(s, 148),
        el('b', { text: s.title || 'Sin título' }),
        el('small', { text: s.error ? 'No se puede abrir' : (s.artist || '') }),
        el('div.g-tags', null, s.error ? null : (s.arrangements || []).filter((a) => a.playable).map((a) => el('span', { text: isBass(a) ? 'Bajo' : 'Guit.' }))))));
      if (!grid.children.length) grid.append(emptyLibrary(lib.songs.length > 0));
    }
    renderGrid();

    const body = el('div.g-libbody', null, el('div.g-gridwrap', null, grid), selected ? detail(selected) : null);
    root.replaceChildren(el('div.g-lib', null, head, body));
  }

  function emptyLibrary(filtered) {
    if (filtered) return el('div.g-empty', null, el('p', { text: 'Ninguna canción coincide.' }));
    const folder = (lib.folders && lib.folders[0]) || 'Música › Galpon › Juego';
    return el('div.g-empty', null,
      el('h2', { text: 'Todavía no hay canciones' }),
      el('p', { text: 'El juego lee canciones .feedpak (el formato abierto de feedBack). Impórtalas o cópialas en esta carpeta:' }),
      el('code', { text: folder }),
      el('div.row', null, el('button.btn.primary', { onclick: importSongs }, 'Importar canciones…'), el('button.btn', { onclick: addFolder }, 'Agregar carpeta…')),
      el('p.note', { text: 'Galpon no abre archivos .psarc de Rocksmith.' }));
  }

  function detail(s) {
    const box = el('aside.g-detail');
    box.append(cover(s, 220), el('h2', { text: s.title || 'Sin título' }), el('p.g-artist', { text: s.artist || '' }));
    const meta = [s.album, s.year, s.duration ? fmtTime(s.duration) : null].filter(Boolean).join(' · ');
    if (meta) box.append(el('p.note', { text: meta }));
    if (s.error) { box.append(el('p.g-error', { text: s.error })); return box; }
    box.append(el('h3', { text: 'Elige qué tocar' }));
    const list = el('div.g-arrs');
    for (const a of s.arrangements || []) {
      if (!a.playable) continue;
      list.append(el('button.g-arr', { onclick: () => openSong(s, a.id) },
        el('span.g-arr-ico', { style: { background: isBass(a) ? colour(0) : colour(4) } }),
        el('span', null, el('b', { text: kindName(a) }), el('small', { text: tuningName(a) })),
        el('span.g-arr-go', null, G.icon('play', 14), 'Tocar')));
    }
    if (!list.children.length) list.append(el('p.note', { text: 'Esta canción no tiene partes de guitarra ni de bajo.' }));
    box.append(list);
    if ((s.stems || []).length) box.append(el('p.note', { text: 'Audio: ' + s.stems.map(stemName).join(', ') }));
    return box;
  }

  async function importSongs() {
    const r = await G.call('choose', { mode: 'feedpak', title: 'Elige canciones .feedpak', patterns: '*.feedpak;*.zip' });
    if (!r || !r.ok) return;
    const paths = r.paths && r.paths.length ? r.paths : [r.path];
    const res = await G.run('gameImport', { paths });
    if (res) {
      G.toast(res.count === 1 ? 'Lista: 1 canción nueva' : `Listas: ${res.count} canciones nuevas` + (res.error ? '. ' + res.error : ''), !!res.error);
      showLibrary();
    }
  }
  async function addFolder() {
    const r = await G.call('choose', { mode: 'folder', title: 'Elige la carpeta con tus canciones .feedpak' });
    if (!r || !r.ok) return;
    if (await G.call('gameAddFolder', { path: r.path }).then((x) => x && x.ok)) showLibrary();
  }

  const STEM_NAMES = { full: 'Canción', guitar: 'Guitarra', bass: 'Bajo', drums: 'Batería', vocals: 'Voz', piano: 'Piano', keys: 'Teclados', other: 'Otros', click: 'Click' };
  const stemName = (s) => (typeof s === 'string' ? STEM_NAMES[s] || s : (STEM_NAMES[s.id] || s.name || s.id));

  // ------------------------------------------------------------------ opening a song
  let play = null;    // everything about the song being played

  async function openSong(song, arrId) {
    stopDrawing();
    root.replaceChildren(el('div.g-loading', null, el('b', { text: song.title }), el('span', { text: 'Preparando el audio…' })));
    await new Promise((res) => setTimeout(res, 30));   // let the message paint before the engine decodes
    const r = await G.call('gameOpen', { path: song.path });
    if (!r || !r.ok) { G.toast((r && r.error) || 'No se pudo abrir la canción', true); return showLibrary(); }
    const d = r.data;
    const arrangements = d.arrangements || {};
    const ids = Object.keys(arrangements);
    const id = arrangements[arrId] ? arrId : ids[0];
    play = { song, data: d, ids, length: d.length || song.duration || 0, stems: d.stems || [], pos: 0, playing: false, speed: 1,
      loop: null, markA: null, guitar: null, ended: false, master: 1, tone: null, toneName: '', listening: false };
    setArrangement(id);
    if (d.problems) G.toast('Parte del audio no se pudo leer: ' + d.problems, true);
    const g = await G.call('gameGuitar', {});
    if (g && g.ok) play.guitar = g.data;
    renderPlay();
    sendNotes();
    if (opts.autoTone && play.guitar) setAutoTone(true, true);
    applyTheirGain();
    if (!store.get('seenHelp', false)) { store.set('seenHelp', true); showHelp(); }
  }

  /** The song's own guitar (or bass) at the level you left it last time. */
  function applyDuck() { const db = store.get('duckDb', DUCK_MAX); G.call('gameDuck', { db: db >= DUCK_MAX ? 60 : db }); }
  function applyTheirGain() {
    applyDuck();
    const g = store.get('theirGain', 1);
    if (!play || g === 1) return;
    const mine = isBass(play.arr) ? /bass|bajo/i : /guitar|guitarra|gtr/i;
    const theirs = play.stems.find((s) => !s.full && (mine.test(s.id) || mine.test(s.name || '')));
    if (!theirs) return;
    theirs.gain = g;
    G.call('gameStemVolume', { id: theirs.id, gain: g });
  }

  function setArrangement(id) {
    const a = play.data.arrangements[id];
    play.arrId = id;
    play.arr = a;
    play.strings = (a.tuning && a.tuning.length) || (isBass(a) ? 4 : 6);
    play.pitches = openPitches(a);
    play.chordName = (cid) => { const t = (a.templates || [])[cid]; return t ? G.chordLabel(t.name || t.displayName || '') : ''; };
    // a flat, time-sorted list of everything to draw; chord notes carry the chord's time
    const items = [];
    for (const n of a.notes || []) items.push(Object.assign({ chord: -1 }, n));
    (a.chords || []).forEach((c, ci) => {
      for (const n of c.notes || []) items.push(Object.assign({}, n, { t: c.t, chord: ci }));
    });
    items.sort((x, y) => x.t - y.t || x.s - y.s);
    play.items = items;
    play.chords = (a.chords || []).slice().sort((x, y) => x.t - y.t);
    play.anchors = (a.anchors || []).slice().sort((x, y) => x.time - y.time);
    play.beats = (play.data.beats || []).slice().sort((x, y) => x.time - y.time);
    play.sections = (play.data.sections || []).slice().sort((x, y) => x.time - y.time);
    play.lyrics = buildLyrics(play.data.lyrics || []);
    play.maxFret = Math.max(12, ...items.map((n) => (n.f || 0) + 2), ...items.map((n) => (n.slu || n.sl || 0) + 1));
    buildEvents();
    resetScore();
  }

  /** What the detector listens for: notes that start together are one event (a chord). */
  function buildEvents() {
    const p = play, evs = [];
    let cur = null;
    p.items.forEach((n, i) => {
      if (!cur || n.t - cur.t > 0.012) { cur = { t: n.t, items: [], p: [], sus: 0, l: true, m: true }; evs.push(cur); }
      cur.items.push(i);
      n.ev = evs.length - 1;
      const pitch = p.pitches[n.s | 0] + (n.f | 0);
      if (!cur.p.includes(pitch)) cur.p.push(pitch);
      cur.sus = Math.max(cur.sus, n.sus || 0);
      const legato = !!(n.ho || n.po || n.tp);
      cur.l = cur.l && legato;
      cur.m = cur.m && !!(n.mt || n.fhm);
    });
    // a note that a slide arrives at isn't picked again
    for (let i = 0; i < p.items.length; i++) {
      const n = p.items[i];
      if (!(n.sl > 0 || n.slu > 0)) continue;
      for (let k = i + 1; k < p.items.length && p.items[k].t <= n.t + (n.sus || 0) + 0.05; k++) {
        const m = p.items[k];
        if (m.s === n.s && m.t >= n.t + (n.sus || 0) - 0.05 && evs[m.ev].items.length === 1) { evs[m.ev].l = true; break; }
      }
    }
    p.events = evs;
  }

  function sendNotes() {
    if (!play) return;
    const a = play.arr;
    G.call('gameNotes', { events: play.events.map((e) => ({ t: e.t, p: e.p, sus: e.sus, l: e.l, m: e.m })), cents: (a.centOffset || 0) - (play.virtualShift || 0) * 100, offsetMs: opts.detectOffsetMs })
      .then((r) => { if (r && r.ok) { play.listening = r.data.listening; updateScoreHud(); } });
  }

  // ------------------------------------------------------------------ score
  function resetScore() {
    play.score = { points: 0, streak: 0, best: 0, hits: 0, judged: 0, timings: [], sections: {}, pass: -1 };
    play.fx = []; play.banner = null;
    play.marks = new Map();       // item index → {hit, at}
    play.feedback = null;
  }
  const mult = () => Math.min(4, 1 + Math.floor(play.score.streak / 8));
  // ---- what you played, on screen (like Rocksmith): a burst where each note you hit lands, sparks
  // while you hold a long note well, the note going grey when you miss it, and a banner for streaks
  const STREAKS = [10, 25, 50, 75, 100, 150, 200];
  function spawnFx(ev, hit) {
    const p = play, at = performance.now();
    p.fx = (p.fx || []).filter((f) => at - f.at < 1500);
    for (const i of ev.items) p.fx.push({ i, hit, at, chord: ev.items.length > 1 });
  }
  function banner(text, streak, small) {
    play.banner = { text, streak, small: !!small, at: performance.now() };
  }
  /** Draws the bursts. pos(item) gives [x, y, size] where that note lands on screen. */
  function drawFx(ctx, pos, nowSong) {
    const p = play;
    if (!p.fx || !p.fx.length) return;
    const t = performance.now();
    ctx.save();
    for (const f of p.fx) {
      const age = (t - f.at) / 1000;
      const nt = p.items[f.i];
      if (!nt || age > 0.9) continue;
      const q = pos(nt);
      if (!q) continue;
      const [x, y, g] = q;
      const c = colour(nt.s | 0);
      if (f.hit) {
        const life = Math.min(1, age / 0.7);
        if (life >= 1) continue;
        const a = 1 - life * life;
        // white flash, then a ring in the string's colour
        if (age < 0.18) {
          ctx.globalAlpha = 1 - age / 0.18;
          const fl = ctx.createRadialGradient(x, y, 0, x, y, g * 0.9);
          fl.addColorStop(0, 'rgba(255,255,255,0.95)'); fl.addColorStop(0.4, c); fl.addColorStop(1, 'rgba(0,0,0,0)');
          ctx.fillStyle = fl; ctx.beginPath(); ctx.arc(x, y, g * 0.9, 0, 7); ctx.fill();
        }
        ctx.globalAlpha = a;
        ctx.strokeStyle = c; ctx.lineWidth = Math.max(1.5, g * 0.08 * a);
        ctx.shadowColor = c; ctx.shadowBlur = 16;
        ctx.beginPath(); ctx.arc(x, y, g * (0.35 + life * (f.chord ? 1.1 : 0.9)), 0, 7); ctx.stroke();
        // sparks flying out (always the same pattern for a note, so it doesn't flicker)
        ctx.fillStyle = c;
        const nSp = f.chord ? 8 : 12;
        for (let k = 0; k < nSp; k++) {
          const ang = (k / nSp) * Math.PI * 2 + (f.i % 7) * 0.4;
          const sp = g * (0.5 + ((k * 37 + f.i * 11) % 10) / 14);
          const d = sp * life * 1.6;
          const sx = x + Math.cos(ang) * d, sy = y + Math.sin(ang) * d * 0.7 + life * life * g * 0.6;
          const r = Math.max(1, g * 0.07 * a);
          ctx.beginPath(); ctx.arc(sx, sy, r, 0, 7); ctx.fill();
        }
        ctx.shadowBlur = 0;
      } else {
        // a miss: a dull grey puff and a small ✕
        const life = Math.min(1, age / 0.5);
        if (life >= 1) continue;
        ctx.globalAlpha = (1 - life) * 0.8;
        ctx.strokeStyle = '#f87171'; ctx.lineWidth = Math.max(1.5, g * 0.07);
        const r = g * 0.22;
        ctx.beginPath(); ctx.moveTo(x - r, y - r - life * g * 0.3); ctx.lineTo(x + r, y + r - life * g * 0.3);
        ctx.moveTo(x + r, y - r - life * g * 0.3); ctx.lineTo(x - r, y + r - life * g * 0.3); ctx.stroke();
      }
    }
    // sparks while you hold a long note you hit
    for (const [i, mk] of p.marks) {
      if (!mk.hit) continue;
      const nt = p.items[i];
      if (!nt || !(nt.sus >= 0.08) || nowSong < nt.t || nowSong > nt.t + nt.sus) continue;
      if (mk.held !== undefined && mk.held < 0.5) continue;
      const q = pos(nt);
      if (!q) continue;
      const [x, y, g] = q, c = colour(nt.s | 0);
      ctx.fillStyle = c; ctx.shadowColor = c; ctx.shadowBlur = 10;
      for (let k = 0; k < 5; k++) {
        const ph = ((t / 380) + k / 5 + i * 0.13) % 1;
        const ang = -Math.PI / 2 + Math.sin(k * 2.3 + i) * 1.1;
        ctx.globalAlpha = (1 - ph) * 0.9;
        ctx.beginPath(); ctx.arc(x + Math.cos(ang) * ph * g * 0.9, y + Math.sin(ang) * ph * g * 0.9, Math.max(1, g * 0.06 * (1 - ph)), 0, 7); ctx.fill();
      }
      ctx.shadowBlur = 0;
      ctx.globalAlpha = 0.5 + 0.3 * Math.sin(t / 60);
      ctx.strokeStyle = c; ctx.lineWidth = Math.max(1.5, g * 0.05);
      ctx.beginPath(); ctx.arc(x, y, g * 0.45, 0, 7); ctx.stroke();
    }
    ctx.restore();
  }
  /** The banner in the middle: "¡25 seguidas!", "x3", or the streak you lost */
  function drawBanner(ctx, w, h, flat) {
    const b = play.banner;
    if (!b) return;
    const age = (performance.now() - b.at) / 1000;
    const dur = b.small ? 0.9 : 1.6;
    if (age > dur) { play.banner = null; return; }
    const pop = age < 0.15 ? 0.6 + 0.4 * (age / 0.15) * 1.15 : age < 0.25 ? 1.1 - 0.1 * ((age - 0.15) / 0.1) : 1;
    const a = age > dur - 0.4 ? (dur - age) / 0.4 : 1;
    ctx.save();
    ctx.globalAlpha = Math.max(0, a);
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    const size = Math.round((b.small ? 34 : b.streak < 0 ? 22 : 44) * pop * (flat ? 0.6 : 1));
    ctx.font = '800 ' + size + 'px "Space Grotesk", Inter, sans-serif';
    const y = flat ? size * 0.9 : h * 0.3 - age * 14;
    if (b.streak < 0) ctx.fillStyle = '#fca5a5';
    else {
      const gr = ctx.createLinearGradient(0, y - size / 2, 0, y + size / 2);
      gr.addColorStop(0, '#fff3c4'); gr.addColorStop(1, b.small ? '#7dd3fc' : '#f5a524');
      ctx.fillStyle = gr;
      ctx.shadowColor = b.small ? '#38bdf8' : '#f59e0b'; ctx.shadowBlur = 24;
    }
    ctx.textAlign = 'right';
    ctx.fillText(b.text.toUpperCase(), w - 28, y);
    ctx.restore();
  }

  function onJudgments(list) {
    const p = play, sc = p.score;
    for (const [index, pass, kind, hit, ms, value] of list) {
      const ev = p.events[index];
      if (!ev) continue;
      if (pass !== sc.pass) {
        // the song jumped (loop, seek): forget the marks ahead
        sc.pass = pass;
        const from = clock.display - 0.3;
        for (const [i] of p.marks) if (p.items[i].t >= from) p.marks.delete(i);
      }
      if (kind === 1) {
        if (hit) sc.points += Math.round(60 * value) * mult();
        for (const i of ev.items) { const mk = p.marks.get(i); if (mk) mk.held = value; }
        if (!hit) p.feedback = { text: 'Soltaste antes', tone: 'warn', at: performance.now() };
        continue;
      }
      sc.judged++;
      spawnFx(ev, hit);
      const si = sectionAt(ev.t);
      const sec = sc.sections[si] || (sc.sections[si] = { hits: 0, total: 0 });
      sec.total++;
      if (hit) {
        sc.hits++; sec.hits++; sc.streak++; sc.best = Math.max(sc.best, sc.streak);
        const up = [10, 20, 30].includes(sc.streak) ? ' · x' + mult() : '';
        if (STREAKS.includes(sc.streak) || (sc.streak > 200 && sc.streak % 100 === 0)) banner('¡' + sc.streak + ' seguidas!' + up, sc.streak);
        else if (up) banner('x' + mult(), 0, true);
        sc.points += 100 * mult();
        if (!ev.l && !ev.m) sc.timings.push(ms);
        const abs = Math.abs(ms);
        p.feedback = ev.l || ev.m || abs <= 35 ? { text: '¡Justo!', tone: 'good', at: performance.now() }
          : { text: (ms < 0 ? 'Temprano' : 'Tarde') + ' · ' + Math.round(abs) + ' ms', tone: 'ok', at: performance.now() };
      } else {
        if (sc.streak >= 10) banner('Racha cortada: ' + sc.streak, -1);
        sc.streak = 0;
        p.feedback = { text: ev.p.length > 1 ? 'Acorde: sonaron ' + Math.round(value * ev.p.length) + ' de ' + ev.p.length : 'Errada', tone: 'bad', at: performance.now() };
      }
      for (const i of ev.items) p.marks.set(i, { hit, at: performance.now() });
    }
    updateScoreHud();
  }
  function updateScoreHud() {
    if (!ui.hud) return;
    const sc = play.score;
    const acc = sc.judged ? Math.round(100 * sc.hits / sc.judged) + ' %' : '—';
    ui.hud.replaceChildren(
      hudCell('Puntos', sc.points.toLocaleString('es-AR')),
      hudCell('Racha', sc.streak + (mult() > 1 ? ' · x' + mult() : ''), mult() > 1),
      hudCell('Precisión', acc));
    ui.hud.classList.toggle('off', !play.listening || !opts.detect);
    ui.hud.title = !opts.detect ? 'El puntaje está apagado (Opciones)' : play.listening ? '' : 'Para darte puntos tengo que escucharte: elige tu guitarra en «Tu sonido».';
  }
  const hudCell = (label, value, hot) => el('div.g-hudcell' + (hot ? '.hot' : ''), null, el('small', { text: label }), el('b', { text: value }));

  // ------------------------------------------------------------------ lyrics
  /** words → lines: a trailing "-" joins with the next word, "+" ends the line. */
  function buildLyrics(words) {
    const lines = [];
    let cur = null;
    for (const w0 of words) {
      if (!w0 || typeof w0.w !== 'string') continue;
      let w = w0.w, join = false, end = false;
      if (w.endsWith('+')) { end = true; w = w.slice(0, -1); }
      if (w.endsWith('-')) { join = true; w = w.slice(0, -1); }
      if (!cur) { cur = { t: w0.t, end: w0.t, words: [] }; lines.push(cur); }
      cur.words.push({ t: w0.t, d: w0.d || 0, w, join });
      cur.end = w0.t + (w0.d || 0);
      if (end) cur = null;
    }
    return lines;
  }

  const SECTION_ES = { intro: 'Intro', verse: 'Estrofa', chorus: 'Estribillo', prechorus: 'Pre-estribillo', bridge: 'Puente', solo: 'Solo', outro: 'Final',
    riff: 'Riff', breakdown: 'Breakdown', interlude: 'Interludio', hook: 'Gancho', transition: 'Transición', ambient: 'Ambiente', buildup: 'Subida', noguitar: 'Sin guitarra', silence: 'Silencio', fadein: 'Entrada', fadeout: 'Salida', tapping: 'Tapping', vamp: 'Vamp', variation: 'Variación', melody: 'Melodía', postchorus: 'Post-estribillo', head: 'Tema' };
  const sectionName = (s) => { const k = String(s.name || '').toLowerCase().replace(/[^a-z]/g, ''); return (SECTION_ES[k] || s.name || 'Parte') + (s.number > 1 ? ' ' + s.number : ''); };
  function sectionAt(t) {
    let i = -1;
    for (let k = 0; k < play.sections.length; k++) if (play.sections[k].time <= t + 1e-6) i = k;
    return i;
  }
  const sectionEnd = (i) => (i + 1 < play.sections.length ? play.sections[i + 1].time : play.length);

  // ------------------------------------------------------------------ sizes
  /** Everything scales with one "tamaño" (the − / + on the highway). */
  function dims() {
    const n = play.strings, s = opts.size;
    const laneH = Math.round(34 * s);
    return { n, laneH, box: Math.round(laneH * 0.84), top: 26, bottom: 24, hwH: n * laneH + 26 + 24, fbH: Math.round(n * 12 * s + 34), pps: 210 * s * opts.zoom };
  }
  function applySize() {
    if (!play || !hw) return;
    const d = dims();
    const v = opts.view;   // '2d', '3d' or 'both'
    const avail = (ui.stage ? ui.stage.clientHeight : 600) - 46;
    // the tab when it's shown together with the 3D: a bit smaller, under it
    const tabH = d.hwH;
    const h3 = v === '3d' ? Math.max(260, avail) : v === 'both' ? Math.max(220, avail - tabH - 6) : 0;
    ui.hwwrap.style.height = (v === '2d' ? d.hwH : v === '3d' ? h3 : h3 + 6 + tabH) + 'px';
    hw3.style.display = v === '2d' ? 'none' : '';
    hw3.style.height = h3 + 'px';
    hw.style.display = v === '3d' ? 'none' : '';
    hw.style.top = v === 'both' ? (h3 + 6) + 'px' : '0px';
    hw.style.height = tabH + 'px';
    ui.fbrow.style.height = d.fbH + 'px';
    ui.fbrow.style.display = v === '2d' ? '' : 'none';
    if (ui.viewBtns) ui.viewBtns.forEach((b) => b.classList.toggle('on', b.dataset.v === opts.view));
    ui.lyrics.style.fontSize = Math.round(24 * Math.max(0.85, Math.min(1.3, opts.size))) + 'px';
  }
  function setSize(v) {
    opts.size = G.clamp(Math.round(v * 20) / 20, 0.6, 1.8);
    store.set('size', opts.size);
    applySize();
  }
  /** Row of string s, top to bottom. Tablature: high string on top; Rocksmith style: low string on top. */
  const rowOf = (s, n) => (opts.invert ? s : n - 1 - s);

  // ------------------------------------------------------------------ play screen
  let hw, hw3, fb, ui = {};
  function renderPlay() {
    const p = play, a = p.arr;
    stopDrawing();
    const arrSelect = el('select.g-select', { 'aria-label': 'Parte', onchange: (e) => { setArrangement(e.target.value); renderPlay(); sendNotes(); applyTheirGain(); if (p.tone) setAutoTone(true); } },
      p.ids.map((id) => el('option', { value: id, selected: id === p.arrId, text: kindName(p.data.arrangements[id]) })));

    ui.sections = el('div.g-sections', { role: 'group', 'aria-label': 'Partes de la canción' });
    const total = Math.max(1, p.length);
    p.sections.forEach((s, i) => {
      const len = sectionEnd(i) - s.time;
      ui.sections.append(el('button.g-sec', {
        style: { flex: String(Math.max(0.01, len / total)) }, title: sectionName(s) + ' · clic: ir ahí · ⇧clic: repetir esta parte',
        onclick: (e) => { if (e.shiftKey) loopSection(i); else seek(s.time); },
      }, sectionName(s)));
    });
    if (!p.sections.length) ui.sections.append(el('div.g-sec.none', { text: 'La canción no marca sus partes' }));
    ui.progress = el('i.g-progress');
    ui.sections.append(ui.progress);
    ui.hud = el('div.g-hud', { 'aria-live': 'off' });

    hw = el('canvas.g-highway', { 'aria-label': 'Autopista de notas: las notas llegan de derecha a izquierda hasta la línea blanca' });
    hw3 = el('canvas.g-highway.g-hw3', { 'aria-label': 'Mástil en 3D: las notas vienen del fondo hacia su cuerda y su traste' });
    const onWheel = (e) => {
      e.preventDefault();
      if (e.ctrlKey || e.metaKey) { setSize(opts.size * (e.deltaY < 0 ? 1.05 : 0.95)); return; }
      if (!p.playing) seek(Math.max(0, displayPos() + e.deltaY / 400 + e.deltaX / 400));
    };
    hw.addEventListener('wheel', onWheel, { passive: false });
    hw3.addEventListener('wheel', onWheel, { passive: false });
    fb = el('canvas.g-fretboard', { 'aria-label': 'Diapasón: dónde van los dedos ahora' });
    ui.next = el('div.g-next');
    ui.feedback = el('div.g-feedback');
    ui.lyrics = el('div.g-lyrics', { 'aria-live': 'off' });
    ui.toneNow = el('div.g-tonenow');

    const VIEW = { '2d': ['2D', 'Tablatura (V)'], '3d': ['3D', 'Mástil en 3D, como Rocksmith (V)'], both: ['2D+3D', 'Las dos a la vez: el 3D arriba y la tablatura abajo (V)'] };
    ui.viewBtns = ['2d', '3d', 'both'].map((v) => el('button.g-view' + (opts.view === v ? '.on' : ''), { 'data-v': v, title: VIEW[v][1], onclick: () => setView(v) }, VIEW[v][0]));
    const sizeCtl = el('div.g-size', { role: 'group', 'aria-label': 'Vista y tamaño' },
      ...ui.viewBtns, el('i.g-sizesep'),
      el('button', { 'aria-label': 'Achicar', title: 'Achicar (−)', onclick: () => setSize(opts.size - 0.1) }, '−'),
      el('span', { text: 'Tamaño' }),
      el('button', { 'aria-label': 'Agrandar', title: 'Agrandar (+)', onclick: () => setSize(opts.size + 0.1) }, '+'));

    ui.playBtn = el('button.g-play', { 'aria-label': 'Tocar (Espacio)', onclick: toggle });
    ui.time = el('span.g-time');
    ui.seek = el('input.g-seek', { type: 'range', min: 0, max: Math.max(1, p.length), step: 0.05, value: 0, 'aria-label': 'Posición' });
    ui.seek.addEventListener('input', () => { G.busy = 1; seek(+ui.seek.value); });
    const speeds = [1, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3];
    ui.speed = el('select.g-select', { 'aria-label': 'Velocidad', title: 'Más despacio sin cambiar el tono',
      onchange: (e) => { p.speed = +e.target.value; G.call('gameSpeed', { speed: p.speed }); } },
      speeds.map((s) => el('option', { value: s, selected: Math.abs(s - p.speed) < 1e-3, text: Math.round(s * 100) + ' %' })));
    ui.loopBtn = el('button.g-btn', { onclick: abButton, title: 'Repetir un pedazo: toca una vez para marcar el principio (A) y otra para el final (B)' });
    ui.level = el('i.g-level');
    ui.volBtn = el('button.g-btn', { 'aria-expanded': 'false', onclick: () => togglePanel('vol') }, G.icon('mixer', 15), 'Volumen');
    ui.soundBtn = el('button.g-btn', { 'aria-expanded': 'false', onclick: () => togglePanel('sound') }, el('span.g-levelwrap', { title: 'Lo que entra de tu guitarra' }, ui.level), 'Tu sonido');
    ui.panel = el('div.g-panel.hidden', { role: 'region' });

    const head = el('header.g-playhead', null,
      el('button.icon-btn.g-back', { 'aria-label': 'Volver a la biblioteca', title: 'Volver a la biblioteca', onclick: closeSong }, G.icon('back', 18)),
      el('div.g-title', null, el('b', { text: p.song.title }), el('div', null, p.ids.length > 1 ? arrSelect : el('span', { text: kindName(a) }), el('small.g-tuning', { text: ' · ' + tuningName(a) }))),
      ui.sections,
      ui.hud,
      el('button.icon-btn', { 'aria-label': 'Cómo se lee', title: 'Cómo se lee', onclick: showHelp }, el('b.g-q', { text: '?' })),
      el('button.icon-btn', { 'aria-label': 'Opciones', title: 'Opciones', onclick: showOptions }, G.icon('settings', 18)));
    const foot = el('footer.g-foot', null,
      el('div.g-group', null, ui.playBtn, ui.time, ui.seek),
      el('div.g-group', null, el('label.g-lab', null, 'Velocidad', ui.speed), ui.loopBtn),
      el('span.spacer'),
      el('div.g-group', null, ui.volBtn, ui.soundBtn, el('button.g-btn', { onclick: tune }, G.icon('tuner', 15), 'Afinar')));

    ui.hwwrap = el('div.g-hwwrap', null, hw3, hw, ui.lyrics, sizeCtl, ui.feedback, ui.toneNow);   // the lyrics float over the top of the game
    ui.fbrow = el('div.g-fbrow', null, ui.next, el('div.g-fbwrap', null, fb));
    root.replaceChildren(el('div.g-play-screen', null, head,
      ui.stage = el('div.g-stage', null, ui.hwwrap, ui.fbrow, ui.panel),
      foot));
    applySize();
    updateControls();
    updateScoreHud();
    lyricKey = null;
    startDrawing();
  }

  // ------------------------------------------------------------------ inline panels (no windows)
  let panel = null;
  function togglePanel(which) {
    panel = panel === which ? null : which;
    ui.volBtn.classList.toggle('on', panel === 'vol');
    ui.soundBtn.classList.toggle('on', panel === 'sound');
    ui.volBtn.setAttribute('aria-expanded', String(panel === 'vol'));
    ui.soundBtn.setAttribute('aria-expanded', String(panel === 'sound'));
    ui.panel.classList.toggle('hidden', !panel);
    if (panel === 'vol') renderVolume();
    if (panel === 'sound') renderSound();
  }

  const pct = (g) => Math.round(g * 100) + ' %';
  function slider(value, max, oninput) {
    const s = el('input.g-slider', { type: 'range', min: 0, max, step: 0.01, value });
    s.addEventListener('input', () => oninput(+s.value));
    return s;
  }

  const DUCK_MAX = 18;   // dB: the slider's top is "as much as it needs", like before
  const duckText = (db) => (db <= 0 ? 'Nada' : db >= DUCK_MAX ? 'Lo necesario' : 'Hasta ' + db + ' dB');
  function renderVolume() {
    const p = play;
    const rows = [];
    const masterTxt = el('span.g-val', { text: pct(p.master) });
    rows.push(el('div.g-vrow.master', null, el('b', { text: 'Master' }),
      slider(p.master, 1.5, (v) => { p.master = v; masterTxt.textContent = pct(v); G.call('gameVolume', { gain: v }); }), masterTxt, el('span')));
    const instruments = p.stems.filter((s) => !s.full);
    const shown = instruments.length ? instruments : p.stems;
    // the part you're playing, from the recording: turn it down to hear yourself
    const mine = isBass(p.arr) ? /bass|bajo/i : /guitar|guitarra|gtr/i;
    const theirs = instruments.find((s) => mine.test(s.id) || mine.test(s.name || ''));
    const what = isBass(p.arr) ? 'el bajo' : 'la guitarra';
    if (theirs) {
      const setTheirs = (g) => {
        theirs.gain = g;
        if (!theirs.enabled) { theirs.enabled = true; G.call('gameStem', { id: theirs.id, on: true }); }
        G.call('gameStemVolume', { id: theirs.id, gain: g });
        store.set('theirGain', g);
        renderVolume();
      };
      const cur = theirs.enabled ? (theirs.gain == null ? 1 : theirs.gain) : 0;
      rows.push(el('div.g-quick', null,
        el('b', { text: (isBass(p.arr) ? 'Bajo' : 'Guitarra') + ' de la canción' }),
        el('div.seg', null, [[1, 'Normal'], [0.35, 'Más baja'], [0, 'Sin ' + (isBass(p.arr) ? 'bajo' : 'guitarra')]].map(([g, label]) =>
          el('button' + (Math.abs(cur - g) < 0.02 ? '.on' : ''), { onclick: () => setTheirs(g) }, label))),
        el('span.note', { text: 'Para escucharte mejor a ti.' })));
    } else {
      rows.push(el('div.g-quick', null, el('span.note', { text: 'Esta canción no trae ' + what + ' por separado, así que no se puede bajar sola: baja el Master o sube tu ' + (isBass(p.arr) ? 'bajo' : 'guitarra') + '.' })));
    }
    for (const s of shown) {
      const txt = el('span.g-val', { text: s.enabled ? pct(s.gain == null ? 1 : s.gain) : 'apagado' });
      const mute = el('button.g-mute' + (s.enabled ? '' : '.on'), { 'aria-pressed': String(!s.enabled), title: s.enabled ? 'Silenciar' : 'Que suene',
        onclick: async () => {
          const r = await G.run('gameStem', { id: s.id, on: !s.enabled });
          if (r) { p.stems = r; renderVolume(); }
        } }, s.enabled ? 'Silenciar' : 'Silenciado');
      rows.push(el('div.g-vrow' + (s.enabled ? '' : '.muted'), null, el('span', { text: instruments.length ? stemName(s) : 'La canción' }),
        slider(s.gain == null ? 1 : s.gain, 1.5, (v) => {
          s.gain = v; txt.textContent = pct(v);
          if (!s.enabled) { s.enabled = true; G.call('gameStem', { id: s.id, on: true }); }
          G.call('gameStemVolume', { id: s.id, gain: v });
        }), txt, mute));
    }
    rows.push(el('div.g-vsep'));
    const g = p.guitar;
    if (g) {
      const db = g.volumeDb || 0;
      const txt = el('span.g-val', { text: (db > 0 ? '+' : '') + db.toFixed(0) + ' dB' });
      const target = () => (p.tone && p.tone.trackId) || g.trackId;
      rows.push(el('div.g-vrow.mine', null, el('b', { text: 'Tu ' + (isBass(p.arr) ? 'bajo' : 'guitarra') }),
        slider(Math.pow(10, db / 40), 1.4, (v) => {
          const d = v <= 0.01 ? -80 : 40 * Math.log10(v);
          g.volumeDb = d; txt.textContent = d <= -79 ? '-∞' : (d > 0 ? '+' : '') + d.toFixed(0) + ' dB';
          G.call('setTrack', { trackId: target(), volumeDb: d });
        }), txt, el('span')));
    } else rows.push(el('div.g-vrow', null, el('span.note', { text: 'Tu guitarra todavía no suena acá.' }), el('button.btn', { onclick: () => togglePanel('sound') }, 'Elegirla')));
    // When you play loud on top of a loud song, the master's limiter lowers everything to keep it from
    // clipping. This sets how far the song may go down (0 = it doesn't; it clips softly instead).
    const duck = store.get('duckDb', DUCK_MAX);
    const duckTxt = el('span.g-val', { text: duckText(duck) });
    rows.push(el('div.g-vsep'));
    rows.push(el('div.g-vrow', { title: 'Cuando tu guitarra y la canción juntas son muy fuertes, Galpon baja todo para que no distorsione. Elige cuánto puede bajar la canción.' },
      el('span', { text: 'Cuánto baja la canción cuando tocas fuerte' }),
      slider(duck / DUCK_MAX, 1, (v) => {
        const db = Math.round(v * DUCK_MAX);
        store.set('duckDb', db); duckTxt.textContent = duckText(db);
        G.call('gameDuck', { db: db >= DUCK_MAX ? 60 : db });
      }), duckTxt, el('span')));
    ui.panel.replaceChildren(el('div.g-panelhead', null, el('h3', { text: 'Volumen' }), el('button.icon-btn', { 'aria-label': 'Cerrar', onclick: () => togglePanel(null) }, G.icon('close', 14))),
      el('div.g-vrows', null, rows),
      instruments.length ? el('p.note', { text: 'Con todos los instrumentos al mismo volumen suena la grabación original; si cambias uno, se mezclan las pistas separadas.' }) : null);
  }

  // -------- sound: your input, the song's tones, TONE3000
  let t3 = null, t3Timer = 0, t3Asked = false;
  async function refreshT3(loop) {
    t3Asked = true;
    const r = await G.call('t3Status');
    if (r && r.ok && r.data) t3 = r.data;
    if (panel === 'sound') renderSound();
    clearTimeout(t3Timer);
    if (loop && t3 && (t3.busy || t3.login || Object.values(t3.downloading || {}).some((v) => v === ''))) t3Timer = setTimeout(() => refreshT3(true), 1500);
  }

  function renderSound() {
    const p = play, g = p.guitar;
    const box = [];
    // 1. where your guitar comes in
    if (!g) {
      box.push(el('div.g-srow', null, el('div', null, el('b', { text: 'Tu guitarra' }), el('p.note', { text: 'Para escucharte y darte puntos, Galpon arma una pista con amplificador por la entrada de tu interfaz.' })),
        el('button.btn.primary', { onclick: async () => {
          const r = await G.run('gameGuitar', { create: true });
          if (r) { p.guitar = r; sendNotes(); if (opts.autoTone) setAutoTone(true, true); renderSound(); }
        } }, 'Escuchar mi guitarra')));
    } else {
      box.push(el('div.g-srow', null, el('div', null, el('b', { text: 'Entra por: ' + g.name + (g.input >= 0 ? ' · entrada ' + (g.input + 1) : '') }),
        el('p.note', { text: p.listening ? 'Te estoy escuchando para el puntaje. La barrita de «Tu sonido» se mueve cuando tocas.' : 'Todavía no te escucho.' }))));
    }
    // 2. the song's sound, switching by itself
    const auto = el('input', { type: 'checkbox', checked: !!p.tone, disabled: !g, onchange: (e) => setAutoTone(e.target.checked) });
    box.push(el('label.g-srow.g-toggle', null, auto, el('div', null, el('b', { text: 'Usar el sonido de la canción' }),
      el('p.note', { text: 'Arma los efectos que pide cada parte (limpio, distorsión, solo…) y los cambia solo mientras tocas. Tu pista y sus efectos no se tocan.' }))));
    if (p.tone) {
      const list = el('div.g-tones');
      for (const tn of p.tone.tones || []) {
        const a = opts.t3assign[tn.key] || {};
        const choice = a.preset ? 'p:' + a.preset : a.toneId ? String(a.toneId) : '';
        const t3ok = t3 && t3.connected && (t3.tones || []).length;
        const sel = el('select.g-select', { 'aria-label': 'Sonido para ' + tn.name, onchange: (e) => assignT3(tn, e.target.value) },
          el('option', { value: '', text: 'Automático (lo que pide la canción)' }),
          ...tonePresetGroups(choice),
          t3ok ? el('optgroup', { label: 'Mis tonos de TONE3000' },
            el('option', { value: 'auto', selected: choice === 'auto', text: 'El que mejor vaya, entre mis tonos' }),
            ...(t3.tones || []).map((t) => el('option', { value: t.id, selected: choice === String(t.id), text: (t.favorite ? '♥ ' : '') + t.title }))) : null);
        const dl = t3 && choice && choice !== 'auto' && t3.downloading ? t3.downloading[choice] : undefined;
        list.append(el('div.g-tone' + (p.toneName === tn.name ? '.on' : ''), null,
          el('div', null, el('b', { text: tn.name }), el('small', { text: dl === '' ? 'Bajando la captura…' : dl ? dl : tn.chain + (tn.heard ? ` — en la canción suena ${tn.heard}` : '') })), sel));
      }
      box.push(list);
    }
    // 3. TONE3000
    box.push(el('div.g-vsep'));
    if (!t3) { if (!t3Asked) refreshT3(false); box.push(el('p.note', { text: t3Asked ? 'No pude ver tu cuenta de TONE3000.' : 'Buscando tu cuenta de TONE3000…' })); }
    else if (t3.login) {
      box.push(el('div.g-t3', null, el('b', { text: 'Aprueba a Galpon en TONE3000' }),
        el('p', null, 'Se abrió la página en tu navegador. Si te pide un código, es este: ', el('code.g-code', { text: t3.login.code })),
        el('button.btn', { onclick: () => G.call('t3Disconnect').then(() => refreshT3(false)) }, 'Cancelar')));
    } else if (!t3.connected) {
      const key = el('input.text', { placeholder: 't3k_pub_…', 'aria-label': 'Clave pública de TONE3000', value: '' });
      box.push(el('div.g-t3', null, el('b', { text: 'Tus capturas de TONE3000' }),
        el('p.note', { text: 'Conecta tu cuenta y Galpon usa los tonos que marcaste con ♥ o bajaste en TONE3000: elige el que mejor va con cada parte. (TONE3000 no deja a las apps buscar en todo el sitio, solo en tus tonos.)' }),
        t3.hasKey ? null : el('p.note', { text: 'Una sola vez: en tone3000.com entra a Settings › API Keys, crea una clave «publishable» y pégala acá (empieza con t3k_pub_).' }),
        el('div.row', null, t3.hasKey ? null : key, el('button.btn.primary', { onclick: async () => {
          const r = await G.call('t3Connect', { key: key.value });
          if (!r || !r.ok) G.toast((r && r.error) || 'No se pudo conectar', true);
          refreshT3(true);
        } }, 'Conectar TONE3000')),
        t3.error ? el('p.g-error', { text: t3.error }) : null));
    } else {
      box.push(el('div.g-t3', null,
        el('div.row', null, el('b', { text: 'TONE3000: ' + (t3.user || 'conectado') }), el('span.note', { text: (t3.tones || []).length + ' tonos tuyos' + (t3.busy ? ' · actualizando…' : '') }),
          el('span.spacer'),
          el('button.btn', { onclick: () => G.call('t3Refresh').then(() => refreshT3(true)) }, 'Actualizar'),
          el('button.btn', { onclick: () => G.call('t3Disconnect').then(() => refreshT3(false)) }, 'Desconectar')),
        (t3.tones || []).length ? null : el('p.note', { text: 'No tienes tonos marcados. Pon ♥ en las capturas que te gusten en tone3000.com y toca Actualizar.' }),
        t3.error ? el('p.g-error', { text: t3.error }) : null));
    }
    ui.panel.replaceChildren(el('div.g-panelhead', null, el('h3', { text: 'Tu sonido' }), el('button.icon-btn', { 'aria-label': 'Cerrar', onclick: () => togglePanel(null) }, G.icon('close', 14))),
      el('div.g-sound', null, box));
  }

  /** Galpon's sounds to pick by hand for a part of the song (built from the usual amp settings). */
  /** Galpon's sounds, grouped as in the engine's library ("Limpios", "Crunch", "Distorsión"…) */
  function tonePresetGroups(choice) {
    const list = (G.lib && G.lib.tonePresets) || [];
    const groups = [];
    for (const tp of list) {
      if (!!tp.bass !== isBass(play.arr)) continue;
      let g = groups.find((x) => x.name === tp.group);
      if (!g) groups.push(g = { name: tp.group, items: [] });
      g.items.push(tp);
    }
    return groups.map((g) => el('optgroup', { label: 'Galpon · ' + g.name },
      ...g.items.map((tp) => el('option', { value: 'p:' + tp.id, selected: choice === 'p:' + tp.id, text: tp.name, title: tp.description }))));
  }

  /** Words that tell amps apart, to match a song's tone with one of your TONE3000 tones. */
  const FAMILY = {
    british: ['marshall', 'jcm', 'plexi', 'vox', 'ac30', 'orange', 'british', 'jtm', 'super lead', 'bluesbreaker'],
    american: ['fender', 'twin', 'deluxe', 'bassman', 'princeton', 'mesa', 'boogie', 'american', 'dumble', 'rectifier'],
    clean: ['clean', 'limpio', 'twin', 'jc120', 'jazz chorus', 'deluxe', 'ac30', 'princeton'],
    crunch: ['crunch', 'plexi', 'ac30', 'jcm800', 'drive', 'edge of breakup'],
    lead: ['lead', 'solo', 'slo', 'soldano', 'jcm800', '2203', 'friedman', 'bogner'],
    high: ['high gain', 'hi-gain', '5150', 'evh', 'rectifier', 'recto', 'metal', 'peavey', '6505', 'diezel', 'engl', 'uberschall', 'djent'],
    bass: ['bass', 'ampeg', 'svt', 'darkglass', 'b7k', 'sansamp', 'gallien'],
  };
  function matchT3(tn) {
    const tones = (t3 && t3.tones) || [];
    const words = (tn.name + ' ' + (tn.key || '')).toLowerCase();
    const wanted = new Set();
    if (isBass(play.arr)) FAMILY.bass.forEach((w) => wanted.add(w));
    for (const [fam, list] of Object.entries(FAMILY)) {
      const famHit = words.includes(fam) || (fam === 'high' && /metal|heavy|dist|gain/.test(words)) || (fam === 'clean' && /clean|limpi|acoustic/.test(words));
      if (famHit) list.forEach((w) => wanted.add(w));
    }
    let best = null, bestScore = 0;
    for (const t of tones) {
      const hay = (t.title + ' ' + (t.makes || '') + ' ' + (t.tags || '')).toLowerCase();
      if (!isBass(play.arr) && /\bbass\b|bajo/.test(hay)) continue;
      if (isBass(play.arr) && !/bass|bajo|ampeg|svt/.test(hay)) continue;
      let score = t.favorite ? 0.5 : 0;
      for (const w of wanted) if (hay.includes(w)) score += 1;
      for (const w of words.split(/[^a-z0-9]+/)) if (w.length > 3 && hay.includes(w)) score += 1.5;
      if (score > bestScore) { bestScore = score; best = t; }
    }
    return bestScore >= 1 ? best : null;
  }

  async function assignT3(tn, value) {
    if (!value) delete opts.t3assign[tn.key];
    else if (value.startsWith('p:')) opts.t3assign[tn.key] = { preset: value.slice(2) };
    else opts.t3assign[tn.key] = { toneId: value };
    store.set('t3assign', opts.t3assign);
    await resolveAssignments();
    setAutoTone(true);
  }

  /** Turns the choices into local files (downloading what's missing), then the engine builds the sound. */
  async function resolveAssignments() {
    const out = {};
    if (!play.tone) return out;
    // Galpon's own presets, chosen by hand
    for (const tn of play.tone.tones || []) { const a = opts.t3assign[tn.key]; if (a && a.preset) out[tn.key] = { preset: a.preset }; }
    if (!t3 || !t3.connected) return out;
    const need = [];
    for (const tn of play.tone.tones || []) {
      let a = opts.t3assign[tn.key];
      if (a && a.preset) continue;
      let id = a && a.toneId;
      if (id === 'auto') { const m = matchT3(tn); id = m ? m.id : null; }
      if (!id) continue;
      const file = t3.files && t3.files[id];
      const tone = (t3.tones || []).find((t) => String(t.id) === String(id));
      if (file) out[tn.key] = { file, gear: tone ? String(tone.gear || '') : '' };
      else need.push(id);
    }
    for (const id of need) G.call('t3Download', { toneId: id });
    if (need.length) {
      refreshT3(true);
      // when they arrive, build again
      const started = Date.now();
      const poll = async () => {
        const r = await G.call('t3Status');
        if (!r || !r.ok || !play) return;
        t3 = r.data;
        const pending = need.filter((id) => !(t3.files && t3.files[id]) && t3.downloading && t3.downloading[id] === '');
        if (pending.length && Date.now() - started < 120000) { setTimeout(poll, 1500); return; }
        if (need.some((id) => t3.files && t3.files[id])) setAutoTone(true);
        if (panel === 'sound') renderSound();
      };
      setTimeout(poll, 1500);
    }
    return out;
  }

  async function setAutoTone(on, quiet) {
    const p = play;
    opts.autoTone = on;
    store.set('autoTone', on);
    if (!on) {
      await G.call('gameTone', { on: false });
      p.tone = null; p.toneName = '';
      if (panel === 'sound') renderSound();
      return;
    }
    if (!t3) { const r = await G.call('t3Status'); if (r && r.ok) t3 = r.data; }
    const assign = p.tone ? await resolveAssignments() : {};
    const r = await G.call('gameTone', { on: true, arrangement: p.arrId, assign });
    if (!r || !r.ok) { if (!quiet) G.toast((r && r.error) || 'No se pudo armar el sonido', true); p.tone = null; }
    else {
      p.tone = r.data;
      // the first time we only knew the tones' names: now match them with your TONE3000 tones
      if (!Object.keys(assign).length && t3 && t3.connected) {
        const again = await resolveAssignments();
        if (Object.keys(again).length) { const r2 = await G.call('gameTone', { on: true, arrangement: p.arrId, assign: again }); if (r2 && r2.ok) p.tone = r2.data; }
      }
      if (!quiet) G.toast('Listo: el sonido cambia solo en cada parte.');
    }
    if (panel === 'sound') renderSound();
  }

  function tune() {
    const g = play.guitar;
    if (!g) { if (panel !== 'sound') togglePanel('sound'); G.toast('Primero elige por dónde entra tu guitarra.'); return; }
    G.sel.trackId = g.trackId; G.call('selectTrack', { id: g.trackId });
    if (play.playing) toggle();
    const a = play.arr;
    const bass = isBass(a);
    const offsets = ((a.tuning && a.tuning.length) ? a.tuning : [0, 0, 0, 0, 0, 0]).slice(0, bass ? 4 : 6);
    G.App.showTuner({
      offsets, bass, capo: a.capo || 0, trackId: g.trackId,
      title: play.song.title + ' · ' + kindName(a),
      virtualShift: play.virtualShift || 0,
      note: 'La afinación de esta canción. Toca una cuerda al aire por vez.',
      onVirtual: (shift) => setVirtual(shift),
    });
  }

  /** "Afinación virtual": your guitar sounds `shift` semitones away, so you can play without retuning */
  async function setVirtual(shift) {
    const r = await G.call('gameRetune', { semitones: shift });
    if (!r || !r.ok) { G.toast((r && r.error) || 'No se pudo', true); return; }
    play.virtualShift = shift;
    sendNotes();   // the notes you play now sound (and are heard) shifted: so is what to listen for
    const tl = ui.hwwrap && ui.hwwrap.closest ? document.querySelector('.g-tuning') : null;
    if (tl) tl.textContent = ' · ' + tuningName(play.arr);
    G.toast(shift ? 'Afinación virtual: tu guitarra suena ' + Math.abs(shift) + (Math.abs(shift) === 1 ? ' semitono ' : ' semitonos ') + (shift < 0 ? 'más grave' : 'más aguda') : 'Afinación virtual apagada');
  }

  // ------------------------------------------------------------------ help & options
  function showHelp() {
    if (!ui.hwwrap) return;
    const old = ui.hwwrap.querySelector('.g-help');
    if (old) { old.remove(); return; }
    const sw = (c) => el('i.g-sw', { style: { background: c } });
    const help = el('div.g-help', { role: 'dialog', 'aria-label': 'Cómo se lee' },
      el('h3', { text: 'Cómo se lee' }),
      el('ul', null,
        el('li', null, el('b', { text: 'Las notas vienen de derecha a izquierda.' }), ' Tócalas cuando llegan a la línea blanca.'),
        el('li', null, el('b', { text: 'Cada renglón es una cuerda' }), ' con su color: ', sw(colour(0)), sw(colour(1)), sw(colour(2)), sw(colour(3)), sw(colour(4)), sw(colour(5)),
          opts.invert ? ' (la más grave arriba).' : ' (la más aguda arriba, como en una tablatura).'),
        el('li', null, el('b', { text: 'El número es el traste.' }), ' 0 es la cuerda al aire.'),
        el('li', null, el('b', { text: 'Una barra larga detrás de la nota: mantenla' }), ' hasta que la barra termine de pasar la línea.'),
        el('li', null, el('b', { text: 'Recuadro dorado con nombre: acorde.' })),
        el('li', null, el('b', { text: 'Tipos de nota:' }), ' hueca = ligado (hammer-on o pull-off: no la púas, la haces sonar con la mano izquierda); rombo = armónico; franja oscura = palm mute (apoya el canto de la mano derecha sobre las cuerdas, cerca del puente, para que suene apagado y seco); ✕ = nota muteada; flecha hacia arriba = bend (½ o 1 tono); cinta que cruza a otro traste = slide; cinta ondulada = vibrato.'),
        el('li', null, el('b', { text: 'Abajo, el diapasón' }), ' te muestra dónde poner la mano (zona dorada) y qué trastes vienen.'),
        el('li', null, el('b', { text: 'Vista 3D (botón 3D o tecla V):' }), ' el mástil en perspectiva, como Rocksmith, con la cuerda más grave arriba. Cada nota viene desde el fondo a la altura de su cuerda y por el carril de su traste; el carril se ilumina y el número de abajo te dice qué traste es. La zona dorada es donde va la mano. Con 2D+3D ves las dos cosas a la vez.')),
      el('div.row.end', null, el('button.btn.primary', { onclick: () => help.remove() }, 'Entendido')));
    ui.hwwrap.append(help);
  }

  function showOptions() {
    G.modal('Opciones del juego', (body) => {
      const seg = (items, cur, set) => el('div.seg', null, items.map(([id, label]) =>
        el('button' + (cur === id ? '.on' : ''), { onclick: (e) => { set(id); e.target.parentNode.querySelectorAll('button').forEach((b) => b.classList.toggle('on', b === e.target)); } }, label)));
      const off = el('input', { type: 'range', min: -300, max: 300, step: 5, value: opts.offsetMs });
      const offTxt = el('span.note');
      const showOff = () => { offTxt.textContent = (opts.offsetMs > 0 ? '+' : '') + opts.offsetMs + ' ms'; };
      off.addEventListener('input', () => { opts.offsetMs = +off.value; store.set('offsetMs', opts.offsetMs); showOff(); });
      showOff();
      const det = el('input', { type: 'range', min: -150, max: 250, step: 5, value: opts.detectOffsetMs });
      const detTxt = el('span.note');
      const showDet = () => { detTxt.textContent = (opts.detectOffsetMs > 0 ? '+' : '') + opts.detectOffsetMs + ' ms'; };
      det.addEventListener('input', () => { opts.detectOffsetMs = +det.value; store.set('detectOffsetMs', opts.detectOffsetMs); showDet(); G.call('gameDetectOffset', { offsetMs: opts.detectOffsetMs }); });
      showDet();
      const zoom = el('input', { type: 'range', min: 0.5, max: 2.5, step: 0.05, value: opts.zoom });
      zoom.addEventListener('input', () => { opts.zoom = +zoom.value; store.set('zoom', opts.zoom); });
      const check = (label, key, after) => el('label.g-optrow', null, el('input', { type: 'checkbox', checked: opts[key], onchange: (e) => { opts[key] = e.target.checked; store.set(key, opts[key]); after && after(); } }), ' ' + label);
      body.append(
        el('h4', { text: 'Cómo se ve' }),
        el('div.g-optrow', null, el('label', { text: 'Vista' }), seg([['2d', 'Tablatura (2D)'], ['3d', 'Mástil en 3D, como Rocksmith'], ['both', 'Las dos a la vez']], opts.view, (v) => setView(v))),
        el('div.g-optrow', null, el('label', { text: 'Notas y acordes' }), seg([['en', 'C D E (inglés)'], ['es', 'Do Re Mi']], G.namesEs() ? 'es' : 'en', (v) => { G.setNames(v); lyricKey = null; })),
        el('div.g-optrow', null, el('label', { text: 'Orden de las cuerdas' }),
          seg([['tab', 'Como tablatura (aguda arriba)'], ['rs', 'Como Rocksmith (grave arriba)']], opts.invert ? 'rs' : 'tab', (v) => { opts.invert = v === 'rs'; store.set('invert', opts.invert); })),
        el('div.g-optrow', null, el('label', { text: 'Colores de las cuerdas' }), seg([['classic', 'Clásicos'], ['soft', 'Suaves']], opts.palette, (v) => { opts.palette = v; store.set('palette', v); })),
        el('div.g-optrow', null, el('label', { text: 'Separación de las notas' }), zoom),
        el('p.note', { text: 'El tamaño de todo se cambia con − y + sobre las notas (o ⌘ y la rueda del mouse).' }),
        check('Mostrar la letra', 'lyrics', () => { lyricKey = null; }),
        el('h4', { text: 'Puntaje' }),
        check('Escucharme y darme puntos', 'detect', () => { if (opts.detect) sendNotes(); else G.call('gameNotes', { events: [] }); updateScoreHud(); }),
        el('div.g-optrow', null, el('label', { text: 'Demora al escucharte' }), det, detTxt),
        el('p.note', { text: 'Si tocas a tiempo y el juego dice «Tarde», súbelo; si dice «Temprano», bájalo. Al terminar una canción te lo propongo yo.' }),
        el('h4', { text: 'Sincronía de la pantalla' }),
        el('div.g-optrow', null, el('label', { text: 'Ajuste visual' }), off, offTxt),
        el('p.note', { text: 'Si las notas pasan la línea antes o después de oírlas, muévelo hasta que coincidan.' }));
    });
  }

  // ------------------------------------------------------------------ transport
  function toggle() {
    if (!play) return;
    if (play.countdown) { clearTimeout(play.countdown.timer); play.countdown = null; updateControls(); return; }   // cancel the count
    if (play.ended) { play.ended = false; hideEnd(); resetScore(); updateScoreHud(); }
    if (!play.playing) {
      const pos = displayPos();
      const first = play.items.length ? play.items[0].t : 0;
      // No waiting through a long intro: start 3 s before the first note (the count shows 3, 2, 1)
      if (pos < first - 3.2) { seek(Math.max(0, first - 3)); startNow(); return; }
      // Coming back from a pause: back up a little and count 3, 2, 1 so you can get ready
      if (pos > 0.5 && pos < play.length - 0.5) {
        seek(Math.max(0, pos - 1.5));
        const until = performance.now() + 2400;
        play.countdown = { until, timer: setTimeout(() => { if (play && play.countdown) { play.countdown = null; startNow(); } }, 2400) };
        updateControls();
        return;
      }
    }
    startNow();
  }
  function startNow() {
    G.call('gameToggle');
    play.playing = !play.playing;   // the tick confirms
    clock.anchorAt = performance.now();
    updateControls();
  }
  /** The 3-2-1 before playing again, over the game. */
  function drawCountdown(ctx, w, h) {
    if (!play || !play.countdown) return;
    const left = (play.countdown.until - performance.now()) / 1000;
    if (left <= 0) return;
    const n = Math.ceil(left / 0.8), frac = (left % 0.8) / 0.8;
    ctx.save();
    ctx.fillStyle = 'rgba(0,0,0,0.35)'; ctx.fillRect(0, 0, w, h);
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.globalAlpha = 0.5 + 0.5 * frac;
    ctx.fillStyle = '#ffffff'; ctx.font = '800 ' + Math.round(90 + 40 * frac) + 'px "Space Grotesk", Inter, sans-serif';
    ctx.shadowColor = 'rgba(90,200,255,0.8)'; ctx.shadowBlur = 30;
    ctx.fillText(String(n), w / 2, h / 2);
    ctx.restore();
  }
  function seek(t) {
    if (!play) return;
    t = G.clamp(t, 0, play.length);
    G.call('gameSeek', { time: t });
    clock.pos = t; clock.display = t; clock.anchorAt = performance.now(); clock.hold = performance.now() + 250;
    for (const [i] of play.marks) if (play.items[i].t >= t - 0.3) play.marks.delete(i);
    if (play.ended) { play.ended = false; hideEnd(); }
  }
  function loopSection(i) {
    const s = play.sections[i];
    setLoop(s.time, sectionEnd(i));
    seek(Math.max(0, s.time - 1));
    G.toast('Repitiendo ' + sectionName(s) + '. Toca «Repitiendo» para salir.');
  }
  function setLoop(a, b) {
    if (a === null) { play.loop = null; play.markA = null; G.call('gameLoop', { on: false }); }
    else { play.loop = { a: Math.min(a, b), b: Math.max(a, b) }; play.markA = null; G.call('gameLoop', { on: true, a: play.loop.a, b: play.loop.b }); }
    updateControls();
  }
  function abButton() {
    const t = displayPos();
    if (play.loop) return setLoop(null);
    if (play.markA === null) { play.markA = t; updateControls(); return; }
    if (Math.abs(t - play.markA) < 0.5) { G.toast('El pedazo es muy corto: marca el final un poco más adelante.'); return; }
    setLoop(play.markA, t);
  }
  function updateControls() {
    if (!play || !ui.playBtn) return;
    ui.playBtn.replaceChildren(G.icon(play.playing || play.countdown ? 'pause' : 'play', 20));
    ui.playBtn.setAttribute('aria-label', play.playing ? 'Pausa (Espacio)' : 'Tocar (Espacio)');
    ui.loopBtn.classList.toggle('on', !!play.loop || play.markA !== null);
    ui.loopBtn.replaceChildren(G.icon('cycle', 15), play.loop ? `Repitiendo ${fmtTime(play.loop.a)}–${fmtTime(play.loop.b)} ✕` : play.markA !== null ? `A en ${fmtTime(play.markA)} · marca B` : 'Repetir A–B');
  }
  async function closeSong() {
    if (play && play.countdown) { clearTimeout(play.countdown.timer); play.countdown = null; }
    stopDrawing();
    hideEnd();
    clearTimeout(t3Timer);
    panel = null;
    await G.call('gameClose');
    play = null;
    showLibrary();
  }

  // ------------------------------------------------------------------ clock: smooth position between ticks
  const clock = { pos: 0, display: 0, anchorAt: 0, latency: 0, hold: 0 };
  Game.onTick = function (t) {
    if (!play || !t.game) return;
    const g = t.game;
    const now = performance.now();
    if (g.judgments && opts.detect) onJudgments(g.judgments);
    play.inputLevel = g.level || 0;
    if (g.listening !== undefined && g.listening !== play.listening) { play.listening = g.listening; updateScoreHud(); }
    if ((g.tone || '') !== play.toneName) {
      play.toneName = g.tone || '';
      ui.toneNow.textContent = play.toneName ? 'Sonido: ' + play.toneName : '';
      if (panel === 'sound') renderSound();
    }
    if (now < clock.hold) return;    // a seek just went out; ignore older positions
    clock.pos = g.pos; clock.anchorAt = now; clock.latency = g.latency || 0;
    if (g.playing !== play.playing) { play.playing = g.playing; updateControls(); }
    play.speed = g.speed;
    if (g.ended && !play.ended) { play.ended = true; play.playing = false; updateControls(); setTimeout(showEnd, 400); }
  };
  /** Song time as you hear it now. */
  function displayPos() {
    if (!play) return 0;
    const now = performance.now();
    const target = clock.pos + (play.playing ? (now - clock.anchorAt) / 1000 * play.speed : 0);
    const dt = Math.min(0.1, (now - (clock.last || now)) / 1000);
    clock.last = now;
    let d = clock.display + (play.playing ? dt * play.speed : 0);
    const err = target - d;
    if (Math.abs(err) > 0.12 || !play.playing) d = target; else d += err * 0.12;    // glide, no jumps
    clock.display = d;
    return Math.max(0, d - clock.latency + opts.offsetMs / 1000);
  }

  // ------------------------------------------------------------------ drawing
  let raf = 0;
  function startDrawing() { stopDrawing(); const loop = () => { raf = requestAnimationFrame(loop); draw(); }; raf = requestAnimationFrame(loop); }
  function stopDrawing() { if (raf) cancelAnimationFrame(raf); raf = 0; }

  function fit(canvas) {
    const r = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx, w: r.width, h: r.height };
  }
  function roundRect(ctx, x, y, w, h, r) {
    r = Math.max(0, Math.min(r, w / 2, h / 2));
    ctx.beginPath();
    ctx.moveTo(x + r, y); ctx.arcTo(x + w, y, x + w, y + h, r); ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath();
  }
  /** first index with arr[i][key] >= t (sorted) */
  function lowerBound(arr, t, key) {
    let lo = 0, hi = arr.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (arr[m][key] < t) lo = m + 1; else hi = m; }
    return lo;
  }

  function draw() {
    if (!play || !hw || !hw.isConnected) return;
    const now = displayPos();
    if (opts.view !== '2d') drawHighway3D(now);
    if (opts.view !== '3d') drawHighway(now);
    if (play.countdown) { const c = opts.view === '2d' ? hw : hw3; const cx = c.getContext('2d'); cx.setTransform(1, 0, 0, 1, 0, 0); drawCountdown(cx, c.width, c.height); }
    if (opts.view === '2d') drawFretboard(now);
    drawLyrics(now);
    const sec = sectionAt(now);
    ui.time.textContent = fmtTime(now) + ' / ' + fmtTime(play.length);
    if (G.busy !== 1 || document.activeElement !== ui.seek) ui.seek.value = now;
    ui.progress.style.left = (100 * now / Math.max(1, play.length)) + '%';
    Array.from(ui.sections.querySelectorAll('.g-sec')).forEach((b, i) => b.classList.toggle('on', i === sec));
    // feedback for the last judgment, fading
    const fbk = play.feedback;
    const age = fbk ? (performance.now() - fbk.at) / 1000 : 9;
    if (age < 1.2) {
      if (ui.feedback._t !== fbk) { ui.feedback._t = fbk; ui.feedback.textContent = fbk.text; ui.feedback.className = 'g-feedback ' + fbk.tone; }
      ui.feedback.style.opacity = String(Math.max(0, 1 - Math.max(0, age - 0.6) / 0.6));
    } else ui.feedback.style.opacity = '0';
    const lv = Math.max(0, Math.min(1, (20 * Math.log10(Math.max(play.inputLevel || 0, 1e-5)) + 60) / 54));
    ui.level.style.height = Math.round(lv * 100) + '%';
  }

  function drawHighway(now) {
    const { ctx, w, h } = fit(hw);
    const p = play, d = dims(), n = d.n;
    const hitX = Math.round(Math.min(220, w * 0.17));
    const pps = d.pps;
    const top = d.top, laneH = d.laneH, box = d.box;
    const rowY = (s) => top + (rowOf(s, n) + 0.5) * laneH;
    const xOf = (t) => hitX + (t - now) * pps;
    const t0 = now - hitX / pps, t1 = now + (w - hitX) / pps;
    const bandBottom = top + n * laneH;

    ctx.fillStyle = '#141417'; ctx.fillRect(0, 0, w, h);
    ctx.fillStyle = '#18181b'; ctx.fillRect(0, 0, hitX, h);

    if (p.loop) {
      const xa = Math.max(0, xOf(p.loop.a)), xb = Math.min(w, xOf(p.loop.b));
      if (xb > xa) { ctx.fillStyle = 'rgba(230,168,74,0.07)'; ctx.fillRect(xa, top - 4, xb - xa, n * laneH + 8); }
    }

    // beats and bars
    ctx.font = '600 10px Inter, system-ui, sans-serif';
    ctx.textBaseline = 'top'; ctx.textAlign = 'left';
    for (let i = lowerBound(p.beats, t0 - 0.1, 'time'); i < p.beats.length && p.beats[i].time <= t1; i++) {
      const b = p.beats[i], x = Math.round(xOf(b.time)) + 0.5;
      const bar = b.measure >= 0;
      ctx.fillStyle = bar ? 'rgba(255,255,255,0.2)' : 'rgba(255,255,255,0.06)';
      ctx.fillRect(x, top - 2, bar ? 2 : 1, n * laneH + 4);
      if (bar && x > hitX + 10) { ctx.fillStyle = '#5f5f68'; ctx.fillText(String(b.measure), x + 4, 4); }
    }
    ctx.font = '700 11px Inter, system-ui, sans-serif';
    for (const s of p.sections) {
      const x = xOf(s.time);
      if (x < hitX - 40 || x > w) continue;
      ctx.fillStyle = '#e6a84a'; ctx.fillRect(x, 2, 2, 14);
      ctx.fillText(sectionName(s), x + 6, 3);
    }

    // strings
    ctx.textBaseline = 'middle'; ctx.textAlign = 'center';
    for (let s = 0; s < n; s++) {
      const y = rowY(s), thick = 1 + s * -0.0 + (n - 1 - s) * 0.3 + (isBass(p.arr) ? 1 : 0);
      ctx.globalAlpha = 0.7; ctx.fillStyle = colour(s);
      ctx.fillRect(34, y - thick / 2, w - 34, thick);
      ctx.globalAlpha = 1;
      ctx.font = '700 ' + Math.max(10, Math.round(laneH * 0.4)) + 'px Inter, system-ui, sans-serif';
      ctx.fillText(NOTE[p.pitches[s] % 12].replace('#', '♯'), 16, y);
    }

    // chord frames behind the notes
    for (let i = lowerBound(p.chords, t0 - 1, 't'); i < p.chords.length && p.chords[i].t <= t1 + 0.1; i++) {
      const c = p.chords[i], x = xOf(c.t);
      const prev = p.chords[i - 1];
      const repeat = prev && prev.id === c.id && c.t - prev.t < 2.5;
      ctx.globalAlpha = x < hitX - box / 2 ? 0.3 : 1;
      ctx.strokeStyle = 'rgba(243,199,126,' + (repeat ? 0.35 : 0.8) + ')'; ctx.lineWidth = 2;
      roundRect(ctx, x - box / 2 - 5, top - 2, box + 10, n * laneH + 4, 8);
      ctx.fillStyle = 'rgba(230,168,74,0.06)'; ctx.fill(); ctx.stroke();
      const name = p.chordName(c.id);
      if (name && !repeat) {
        ctx.font = '700 14px "Space Grotesk", Inter, sans-serif'; ctx.fillStyle = '#f3c77e';
        ctx.fillText(name, x, bandBottom + 13);
      }
      ctx.globalAlpha = 1;
    }

    // notes: first every sustain (so the boxes go on top), then the boxes
    const first = lowerBound(p.items, t0 - 15, 't');
    const visible = [];
    for (let i = first; i < p.items.length && p.items[i].t <= t1 + 0.2; i++) {
      const nt = p.items[i];
      const s = nt.s | 0;
      if (s < 0 || s >= n) continue;
      const x = xOf(nt.t), sus = nt.sus || 0;
      if (x + sus * pps < 0) continue;
      visible.push(i);
      if (sus < 0.08) continue;
      const y = rowY(s), c = colour(s);
      const xe = x + sus * pps;
      const holding = now >= nt.t && now <= nt.t + sus;
      const xs = Math.max(x, hitX);
      if (xe <= xs) continue;
      const th = Math.max(10, Math.round(box * 0.56));
      const mk = p.marks.get(i);
      const missed = mk && !mk.hit;
      ctx.globalAlpha = missed ? 0.25 : holding ? 0.95 : 0.6;
      ctx.fillStyle = c;
      roundRect(ctx, xs, y - th / 2, xe - xs, th, th / 2); ctx.fill();
      // a bright edge and stripes: "this one you hold"
      ctx.globalAlpha = missed ? 0.2 : 0.9;
      ctx.strokeStyle = 'rgba(255,255,255,0.55)'; ctx.lineWidth = 1.5; ctx.stroke();
      ctx.save();
      roundRect(ctx, xs, y - th / 2, xe - xs, th, th / 2); ctx.clip();
      ctx.strokeStyle = 'rgba(0,0,0,0.18)'; ctx.lineWidth = 3;
      const phase = (now * pps) % 14;
      for (let xx = xs - th - phase; xx < xe; xx += 14) { ctx.beginPath(); ctx.moveTo(xx, y + th / 2); ctx.lineTo(xx + th, y - th / 2); ctx.stroke(); }
      ctx.restore();
      if (nt.vb || nt.tr) { ctx.globalAlpha = 0.9; ctx.strokeStyle = c; ctx.lineWidth = 2; ctx.beginPath(); for (let xx = xs; xx < xe; xx += 3) ctx.lineTo(xx, y - th / 2 - 4 + Math.sin(xx / 4) * 3); ctx.stroke(); }
      if (xe - Math.max(xs, x + box / 2) > 80 && !holding) {
        ctx.globalAlpha = 0.95; ctx.fillStyle = 'rgba(0,0,0,0.75)'; ctx.font = '700 ' + Math.max(9, Math.round(th * 0.62)) + 'px Inter, system-ui, sans-serif'; ctx.textAlign = 'left';
        ctx.fillText('mantén', Math.max(xs, x + box / 2) + 8, y + 0.5);
        ctx.textAlign = 'center';
      }
      if (holding) {
        // the part already held, and how much is left
        const done = (now - nt.t) / sus;
        ctx.globalAlpha = 1; ctx.fillStyle = c;
        ctx.beginPath(); ctx.arc(hitX, y, box * 0.62, -Math.PI / 2, -Math.PI / 2 + done * Math.PI * 2); ctx.lineWidth = 4; ctx.strokeStyle = c; ctx.stroke();
      }
      ctx.globalAlpha = 1;
    }
    for (const i of visible) {
      const nt = p.items[i];
      const s = nt.s | 0, c = colour(s), sus = nt.sus || 0;
      let x = xOf(nt.t);
      const y = rowY(s);
      const holding = sus >= 0.08 && now >= nt.t && now <= nt.t + sus;
      if (holding) x = hitX;                                   // a held note waits at the line until it ends
      if (x < hitX - box * 1.2 && !holding) {
        // gone: hit notes leave a short glow, missed ones fade out grey
        const mk = p.marks.get(i);
        if (!mk || x < hitX - box * 4) continue;
        const age = (performance.now() - mk.at) / 1000;
        if (age > 0.8) continue;
        ctx.globalAlpha = 1 - age / 0.8;
        ctx.fillStyle = mk.hit ? c : '#55555c';
        roundRect(ctx, x - box / 2, y - box / 2, box, box, box * 0.25); ctx.fill();
        ctx.globalAlpha = 1;
        continue;
      }
      const mk = p.marks.get(i);
      const atLine = Math.abs(nt.t - now) < 0.06 || holding;
      ctx.globalAlpha = mk && !mk.hit ? 0.45 : 1;
      if (mk && mk.hit) { ctx.shadowColor = c; ctx.shadowBlur = 18; }
      const hollow2 = (nt.ho || nt.po || nt.tp) && !mk && !atLine;
      ctx.fillStyle = mk ? (mk.hit ? c : '#2a1d1d') : atLine ? c : hollow2 ? 'rgba(10,10,12,0.4)' : '#1d1d21';
      ctx.strokeStyle = mk && !mk.hit ? '#f87171' : c; ctx.lineWidth = hollow2 ? 4 : 3;
      if (nt.hm || nt.hp) {
        ctx.beginPath(); ctx.moveTo(x, y - box * 0.62); ctx.lineTo(x + box * 0.62, y); ctx.lineTo(x, y + box * 0.62); ctx.lineTo(x - box * 0.62, y); ctx.closePath();
      } else roundRect(ctx, x - box / 2, y - box / 2, box, box, box * 0.25);
      ctx.fill(); ctx.stroke();
      ctx.shadowBlur = 0;
      if (nt.pm) { ctx.fillStyle = 'rgba(0,0,0,0.55)'; ctx.fillRect(x - box / 2 + 2, y + box * 0.22, box - 4, box * 0.16); }
      if (nt.ac) { ctx.lineWidth = 1.5; roundRect(ctx, x - box / 2 - 4, y - box / 2 - 4, box + 8, box + 8, box * 0.3); ctx.stroke(); }
      ctx.fillStyle = (mk && mk.hit) || (!mk && atLine) ? '#111' : '#f2f2f3';
      ctx.font = '700 ' + Math.round(box * 0.5) + 'px "Space Grotesk", Inter, sans-serif';
      ctx.fillText(nt.mt ? '✕' : String(nt.f | 0), x, y + 1);
      const tag = techTag(nt);
      if (tag && x > hitX - box) {
        ctx.font = '700 ' + Math.max(9, Math.round(box * 0.3)) + 'px Inter, system-ui, sans-serif';
        const tw = ctx.measureText(tag).width + 8, th = Math.max(12, Math.round(box * 0.36));
        const ty = opts.invert ? y + box / 2 + 2 : y - box / 2 - th - 2;
        ctx.fillStyle = '#2a2a2f'; roundRect(ctx, x - tw / 2, ty, tw, th, 4); ctx.fill();
        ctx.fillStyle = '#ededef'; ctx.fillText(tag, x, ty + th / 2 + 0.5);
      }
      ctx.globalAlpha = 1;
    }

    // the line
    ctx.fillStyle = '#f3f3f5'; roundRect(ctx, hitX - 2, top - 8, 4, n * laneH + 16, 2); ctx.fill();
    drawFx(ctx, (nt) => {
      const s2 = nt.s | 0;
      return s2 < 0 || s2 >= n ? null : [hitX, rowY(s2), box];
    }, now);
    if (opts.view === '2d') drawBanner(ctx, w, h, true);

    // count-in before the first note
    if (p.items.length && now < p.items[0].t && p.items[0].t - now < 4 && p.playing) {
      ctx.textAlign = 'center'; ctx.fillStyle = 'rgba(243,199,126,0.9)'; ctx.font = '700 28px "Space Grotesk", Inter, sans-serif';
      ctx.fillText(String(Math.ceil(p.items[0].t - now)), hitX / 2, top + n * laneH / 2);
    }
    ctx.textAlign = 'left';
  }

  function setView(v) {
    opts.view = v;
    store.set('view', v);
    applySize();
  }

  // ------------------------------------------------------------------ 3D: the neck in perspective (like Rocksmith)
  /** A real camera, a little above and to the left of the neck, looking along it: frets go
      across, time goes into the distance, and each note flies down its fret's lane at its
      string's height. The notes carry no numbers: the lane lights up under them and the fret
      number is on the neck. The camera drifts slowly to where the hand goes. */
  const cam = { x: 4, span: 8, t: 0 };
  /** Where fret `f` sits along the neck (frets can be fractional): each fret is 2^(1/12) narrower
      than the one before, like on a real guitar. Fret 1 is about one unit wide. */
  const FX = (f) => 18 * (1 - Math.pow(2, -Math.max(-1, f) / 12));
  const BEND_W = 0.6;   // how far sideways a one-step bend pushes the tail (in frets)
  /** How bent the note is (in steps) `dt` seconds after it starts: from the song's bend curve,
      or for a plain bend, rising over the first third of the note. */
  function bendAt(nt, dt) {
    const pts = nt.bnv;
    if (pts && pts.length) {
      if (dt <= pts[0].t) return pts[0].v || 0;
      for (let i = 1; i < pts.length; i++)
        if (dt <= pts[i].t) { const a = pts[i - 1], b = pts[i]; return (a.v || 0) + ((b.v || 0) - (a.v || 0)) * (dt - a.t) / Math.max(1e-6, b.t - a.t); }
      return pts[pts.length - 1].v || 0;
    }
    if (!nt.bn) return 0;
    const len = Math.max(0.2, nt.sus || 0.35);
    return nt.bn * Math.min(1, dt / (len * 0.35));
  }
  /** 0..1: how much a string still rings from a note you just hit (it glows) */
  function stringGlow(str) {
    if (!play.fx) return 0;
    const t = performance.now();
    let g = 0;
    for (const f of play.fx) {
      if (!f.hit) continue;
      const nt = play.items[f.i];
      if (!nt || (nt.s | 0) !== str) continue;
      g = Math.max(g, 1 - (t - f.at) / 350);
    }
    return Math.max(0, g);
  }
  function drawHighway3D(now) {
    const { ctx, w, h } = fit(hw3);
    const p = play, n = p.strings, s = opts.size;

    // ---- where the hand goes: look a little ahead and glide there (time-based, not per frame)
    const wall = performance.now() / 1000;
    const dt = cam.t ? Math.min(0.1, Math.max(0, wall - cam.t)) : 0;
    cam.t = wall;
    let anchor = null, nextAnchor = null;
    for (const a of p.anchors) { if (a.time <= now + 0.15) anchor = a; else { nextAnchor = a; break; } }
    if (!anchor && nextAnchor) { anchor = nextAnchor; nextAnchor = p.anchors[1] || null; }   // before the first note: start where the song starts
    const aFret = anchor ? anchor.fret : 1, aWidth = anchor ? (anchor.width || 4) : 4;
    // start drifting towards the next position a second before it comes
    let goal = anchor || { fret: 1, width: 4 };
    if (nextAnchor && nextAnchor.time - now < 1.0) goal = nextAnchor;
    // low on the neck the nut sits near the left edge (nothing to show before it)
    const targetX = Math.max(goal.fret - 1 + (goal.width || 4) / 2, Math.max((goal.width || 4) + 7, 11) * 0.5);
    const targetSpan = Math.max((goal.width || 4) + 7, 11);   // about a dozen frets across, like Rocksmith
    if (!dt || Math.abs(targetX - cam.x) > 14) { cam.x = targetX; cam.span = targetSpan; }   // first frame or a jump in the song
    else {
      const ease = 1 - Math.exp(-dt / 0.9);              // ~1 s to settle
      cam.x += Math.max(-dt * 5, Math.min(dt * 5, (targetX - cam.x) * ease));   // never faster than 5 frets a second
      cam.span += (targetSpan - cam.span) * (1 - Math.exp(-dt / 1.4));
    }

    // ---- the camera
    // Frets get narrower up the neck, like on a real guitar: positions go through FX (in "frets"
    // in, distance along the neck out); everything else is written in frets.
    const span = cam.span;
    const camW = FX(cam.x);
    const spanW = FX(cam.x + span / 2) - FX(Math.max(0, cam.x - span / 2)) || span;
    const ahead = 4.0 / opts.zoom;                       // seconds visible into the distance
    const depth = (spanW * 4.2) / ahead;                 // world units per second: a long road
    const strGap = Math.min(0.32, 1.7 / n) * Math.min(1.25, Math.max(0.8, s)) * spanW / span;
    // Like Rocksmith: the thickest string on top (as you see your own guitar looking down)
    const hOf = (str) => (n - str) * strGap;            // height above the floor: string 0 (the lowest) highest
    // The camera sits above and behind, off to the right of the hand; low on the neck it looks
    // across more, higher up it comes round towards the middle (the neck turns as you move)
    // Behind the neck and a little above, almost straight down the road (like Rocksmith); a slight
    // turn that follows the hand: low on the neck it looks a bit to the right, higher up straight on
    // the camera looks from the right of the hand, so the road runs off to the left (like Rocksmith);
    // higher up the neck it comes round more behind the hand
    const along = Math.min(1, Math.max(0, (cam.x - 3) / 14));
    const side = spanW * (0.16 - 0.08 * along);
    const C = { x: camW + side, y: spanW * 0.46, z: -spanW * 1.25 };
    const T = { x: camW - side * 1.6, y: 0, z: spanW * 2.4 };
    let F = { x: T.x - C.x, y: T.y - C.y, z: T.z - C.z };
    const fl = Math.hypot(F.x, F.y, F.z); F = { x: F.x / fl, y: F.y / fl, z: F.z / fl };
    let R = { x: F.z, y: 0, z: -F.x };
    const rl = Math.hypot(R.x, R.z); R = { x: R.x / rl, y: 0, z: R.z / rl };
    const U = { x: F.y * R.z - F.z * R.y, y: F.z * R.x - F.x * R.z, z: F.x * R.y - F.y * R.x };
    const focal = Math.min(w * 0.9, h * 1.8) * Math.min(1.2, Math.max(0.85, s));
    const ox = w * 0.47, oy = h * 0.34;
    /** world (fret X, height Y, seconds ahead) -> screen [x, y, scale] */
    const P = (X, Y, sec) => {
      const dx = FX(X) - C.x, dy = Y - C.y, dz = Math.max(-0.3, sec) * depth - C.z;
      const cz = Math.max(0.2, dx * F.x + dy * F.y + dz * F.z);
      const cx = dx * R.x + dy * R.y + dz * R.z, cy = dx * U.x + dy * U.y + dz * U.z;
      return [ox + focal * cx / cz, oy - focal * cy / cz, focal / cz];
    };
    const poly = (pts, fill, stroke, lw) => {
      ctx.beginPath(); pts.forEach((q, i) => (i ? ctx.lineTo(q[0], q[1]) : ctx.moveTo(q[0], q[1]))); ctx.closePath();
      if (fill) { ctx.fillStyle = fill; ctx.fill(); }
      if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = lw || 1; ctx.stroke(); }
    };
    const floorQuad = (x0, x1, z0, z1, fill) => poly([P(x0, 0, z0), P(x1, 0, z0), P(x1, 0, z1), P(x0, 0, z1)], fill);
    const zFar = ahead;
    const fMin = Math.max(0, Math.floor(cam.x - span * 0.75)), fMax = Math.min(24, Math.ceil(cam.x + span * 0.75));

    // ---- background, floor, the hand's zone
    // a dark stage: almost black, with a faint glow where the notes come from
    ctx.fillStyle = '#050c14'; ctx.fillRect(0, 0, w, h);   // a dark blue stage, like Rocksmith's
    const far = P(cam.x, 0, zFar);
    const glow = ctx.createRadialGradient(far[0], far[1], 0, far[0], far[1], Math.max(w, h) * 0.7);
    glow.addColorStop(0, 'rgba(40,110,150,0.35)'); glow.addColorStop(0.5, 'rgba(14,40,60,0.25)'); glow.addColorStop(1, 'rgba(0,0,0,0)');
    ctx.fillStyle = glow; ctx.fillRect(0, 0, w, h);
    // Some personality without getting in the way: soft lights drifting slowly in the dark, and the
    // glow at the end of the road breathes with the beat (a bit more on the first beat of the bar)
    {
      let pulse = 0;
      const bi = lowerBound(p.beats, now + 0.001, 'time') - 1;
      if (bi >= 0 && p.playing) {
        const since = now - p.beats[bi].time;
        pulse = Math.max(0, 1 - since / 0.35) * (p.beats[bi].measure >= 0 ? 1 : 0.5);
      }
      if (pulse > 0) {
        const pg = ctx.createRadialGradient(far[0], far[1], 0, far[0], far[1], Math.max(w, h) * 0.45);
        pg.addColorStop(0, `rgba(90,200,255,${0.12 * pulse})`); pg.addColorStop(1, 'rgba(0,0,0,0)');
        ctx.fillStyle = pg; ctx.fillRect(0, 0, w, h);
      }
      for (let k = 0; k < 14; k++) {
        const r1 = Math.sin(k * 12.9898) * 43758.5453, r2 = Math.sin(k * 78.233) * 12345.678;
        const fx = r1 - Math.floor(r1), fy = r2 - Math.floor(r2);
        const t = wall * (0.008 + 0.006 * fx);
        const x = ((fx + t * (k % 2 ? 1 : -1)) % 1 + 1) % 1 * w;
        const y = (((fy - t * 0.7) % 1 + 1) % 1) * h * 0.55;
        const rad = 18 + 46 * fy;
        const b = ctx.createRadialGradient(x, y, 0, x, y, rad);
        b.addColorStop(0, k % 5 === 0 ? 'rgba(120,230,200,0.08)' : 'rgba(90,170,255,0.07)'); b.addColorStop(1, 'rgba(0,0,0,0)');
        ctx.fillStyle = b; ctx.fillRect(x - rad, y - rad, rad * 2, rad * 2);
      }
    }
    // the floor: dark, lighter close to you
    const n0 = P(cam.x, 0, -0.15), n1 = P(cam.x, 0, zFar);
    const fg = ctx.createLinearGradient(n0[0], n0[1], n1[0], n1[1]);
    fg.addColorStop(0, '#0e2436'); fg.addColorStop(1, '#07111b');
    floorQuad(fMin, fMax, -0.15, zFar, fg);
    floorQuad(aFret - 1, aFret - 1 + aWidth, -0.15, zFar, 'rgba(40,150,210,0.16)');
    // the hand's zone, framed in cyan all the way down the road
    for (const xe of [aFret - 1, aFret - 1 + aWidth]) {
      const a = P(xe, 0.01, -0.15), b = P(xe, 0.01, zFar);
      const rg = ctx.createLinearGradient(a[0], a[1], b[0], b[1]);
      rg.addColorStop(0, 'rgba(110,230,255,0.95)'); rg.addColorStop(1, 'rgba(110,230,255,0.15)');
      ctx.strokeStyle = rg; ctx.lineWidth = 3; ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
    }
    if (p.loop) {
      const z0 = Math.max(0, p.loop.a - now), z1 = Math.min(zFar, p.loop.b - now);
      if (z1 > z0) floorQuad(fMin, fMax, z0, z1, 'rgba(230,168,74,0.05)');
    }
    // beats across the floor (bars brighter)
    for (let i = lowerBound(p.beats, now, 'time'); i < p.beats.length && p.beats[i].time <= now + zFar; i++) {
      const b = p.beats[i], z = b.time - now, a = P(fMin, 0, z), c = P(fMax, 0, z);
      const fade = Math.min(1, (zFar - z) / (zFar * 0.3));
      ctx.strokeStyle = b.measure >= 0 ? `rgba(255,255,255,${0.22 * fade})` : `rgba(255,255,255,${0.07 * fade})`;
      ctx.lineWidth = b.measure >= 0 ? Math.max(1, a[2] * 0.03) : 1;
      ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(c[0], c[1]); ctx.stroke();
    }
    // fret lanes into the distance
    for (let f = fMin; f <= fMax; f++) {
      const inZone = f >= aFret - 1 && f <= aFret - 1 + aWidth;
      const a = P(f, 0, -0.15), b = P(f, 0, zFar);
      const lg = ctx.createLinearGradient(a[0], a[1], b[0], b[1]);
      lg.addColorStop(0, inZone ? 'rgba(110,220,255,0.55)' : 'rgba(110,170,210,0.18)'); lg.addColorStop(1, 'rgba(110,170,210,0)');
      ctx.strokeStyle = lg; ctx.lineWidth = f === 0 ? 3 : 1.4;
      ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
    }
    // section names on the floor, at their start
    ctx.textBaseline = 'middle';
    for (const sec of p.sections) {
      const z = sec.time - now;
      if (z < 0 || z > zFar) continue;
      const q = P(fMin + 0.2, 0, z);
      ctx.globalAlpha = Math.min(1, (zFar - z) / (zFar * 0.3));
      ctx.fillStyle = '#e6a84a'; ctx.textAlign = 'left';
      ctx.font = '700 ' + Math.max(10, Math.round(q[2] * 0.16)) + 'px Inter, system-ui, sans-serif';
      ctx.fillText(sectionName(sec), q[0], q[1] - 10);
      ctx.globalAlpha = 1;
    }

    // ---- notes on their way: collect, light their lane, then draw far to near
    const i0 = lowerBound(p.items, now - 12, 't');
    const i1 = lowerBound(p.items, now + zFar, 't');
    const list = [];
    for (let i = i0; i < i1; i++) { const it = p.items[i]; if (it.t + (it.sus || 0) >= now - 0.25) list.push(i); }
    list.sort((a, b) => p.items[b].t - p.items[a].t);
    const soon = new Map();   // fret -> how soon (s) a note arrives on it: lights the lane and the number
    for (const i of list) {
      const nt = p.items[i], z = nt.t - now;
      if ((nt.f | 0) > 0 && z >= -0.05 && z < 1.6) soon.set(nt.f | 0, Math.min(soon.get(nt.f | 0) ?? 9, Math.max(0, z)));
      if ((nt.f | 0) > 0 && z > 0 && z < zFar) {
        const f = nt.f | 0, fade = Math.min(1, (zFar - z) / (zFar * 0.3));
        ctx.globalAlpha = 0.18 * fade;
        floorQuad(f - 1, f, Math.max(0, z - 0.12), z + 0.12, colour(nt.s | 0));
        ctx.globalAlpha = 1;
      }
    }
    for (const [f, z] of soon) {
      ctx.globalAlpha = 0.28 * (1 - z / 1.6);
      floorQuad(f - 1, f, -0.15, Math.min(zFar, z + 0.1), '#7fe3ff');
      ctx.globalAlpha = 1;
    }

    // ---- chord frames (behind their notes)
    const gemSize = (q) => Math.max(5, Math.min(q[2] * 0.7, q[2] * strGap * 1.45));
    for (let ci = lowerBound(p.chords, now - 0.1, 't'); ci < p.chords.length && p.chords[ci].t <= now + zFar; ci++) {
      const c = p.chords[ci], z = Math.max(0, c.t - now);
      let xs = Infinity, xe = -Infinity;
      for (const nt of c.notes || []) { const X = (nt.f | 0) === 0 ? aFret - 1 + aWidth / 2 : (nt.f | 0) - 0.5; xs = Math.min(xs, X); xe = Math.max(xe, X); }
      if (!isFinite(xs)) continue;
      const top = n * strGap + strGap * 0.5;
      const fade = Math.min(1, (zFar - z) / (zFar * 0.3));
      ctx.globalAlpha = fade;
      poly([P(xs - 0.55, 0, z), P(xe + 0.55, 0, z), P(xe + 0.55, top, z), P(xs - 0.55, top, z)], 'rgba(60,170,230,0.08)', 'rgba(110,230,255,0.85)', 2.5);
      const name = p.chordName(c.id), prev = p.chords[ci - 1];
      if (name && !(prev && prev.id === c.id && c.t - prev.t < 2.5)) {
        const q = P((xs + xe) / 2, top, z);
        ctx.fillStyle = '#e8f6ff'; ctx.font = '700 ' + Math.max(10, Math.round(q[2] * 0.2)) + 'px "Space Grotesk", Inter, sans-serif';
        ctx.fillText(name, q[0], q[1] - 10);
      }
      ctx.globalAlpha = 1;
    }

    // ---- the notes, far to near. Strings go in front of the notes still coming (that's depth),
    // the notes arriving go on top of the strings.
    const drawNote = (i) => {
      const nt = p.items[i], str = nt.s | 0;
      if (str < 0 || str >= n) return;
      const c = colour(str), sus = nt.sus || 0;
      const z = Math.max(0, nt.t - now);
      const holding = sus >= 0.08 && now >= nt.t && now <= nt.t + sus;
      const passed = nt.t < now && !holding;
      const mk = p.marks.get(i);
      const open = (nt.f | 0) === 0;
      const X = open ? aFret - 1 + aWidth / 2 : (nt.f | 0) - 0.5;
      const Y = hOf(str);
      const fade = Math.min(1, (zFar - z) / (zFar * 0.25));
      // sustain: a ribbon along the lane at the string's height. A slide moves across to the fret it
      // goes to (like Rocksmith); vibrato makes the ribbon wavy, tremolo picking makes it zigzag.
      const slideTo = nt.sl > 0 ? nt.sl : nt.slu > 0 ? nt.slu : 0;
      const sLen = sus >= 0.08 ? sus : (slideTo || nt.bn) ? 0.35 : 0;
      if (sLen > 0) {
        const t0 = nt.t - now, t1 = nt.t + sLen - now;
        const zs = Math.max(0, t0), ze = Math.min(zFar, t1);
        if (ze > zs) {
          const half = open ? aWidth / 2 - 0.15 : 0.2;
          const Xt = slideTo ? slideTo - 0.5 : X;
          // a bend pushes the tail sideways, like the string being pushed: as far as the bend goes
          // at each moment (the song's bend curve: prebends, releases, bends in steps)
          const xAt = (zz) => {
            const f = Math.min(1, Math.max(0, (zz - t0) / Math.max(0.01, t1 - t0)));
            return X + (Xt - X) * f + BEND_W * bendAt(nt, zz - t0);
          };
          // an unpitched slide drops off at the end
          const yAt = (zz) => {
            if (!(nt.slu > 0)) return Y;
            const f = Math.min(1, Math.max(0, (zz - t0) / Math.max(0.01, t1 - t0)));
            return f < 0.7 ? Y : Y * (1 - (f - 0.7) / 0.3 * 0.85);
          };
          // vibrato: a sawtooth tail; tremolo picking: a tight zigzag
          const wav = nt.vb ? 0.14 : nt.tr ? 0.07 : 0, per = nt.tr ? 0.035 : 0.09;
          const curved = slideTo || nt.bn || (nt.bnv && nt.bnv.length);
          const steps = wav ? Math.max(12, Math.round((ze - zs) / per * 2)) : curved ? 24 : 1;
          const left = [], right = [];
          for (let k = 0; k <= steps; k++) {
            const zz = zs + (ze - zs) * k / steps;
            let off = 0;
            if (wav) { const ph = (zz / per) % 1; off = nt.tr ? ((k % 2) ? wav : -wav) : wav * (ph < 0.75 ? ph / 0.75 : (1 - ph) / 0.25); }
            const yy = yAt(zz);
            left.push(P(xAt(zz) - half + off, yy, zz)); right.push(P(xAt(zz) + half + off, yy, zz));
          }
          ctx.globalAlpha = (mk && !mk.hit ? 0.25 : holding ? 0.9 : 0.55) * fade;
          poly(left.concat(right.reverse()), c);
          if (slideTo) {
            // an arrow at the end and the fret it lands on
            const e = P(xAt(ze), yAt(ze), ze), e0 = P(xAt(ze - 0.05), yAt(ze - 0.05), ze - 0.05);
            const ang = Math.atan2(e[1] - e0[1], e[0] - e0[0]), al = Math.max(6, e[2] * 0.12);
            ctx.fillStyle = c; ctx.beginPath(); ctx.moveTo(e[0] + Math.cos(ang) * al, e[1] + Math.sin(ang) * al);
            ctx.lineTo(e[0] + Math.cos(ang + 2.4) * al, e[1] + Math.sin(ang + 2.4) * al); ctx.lineTo(e[0] + Math.cos(ang - 2.4) * al, e[1] + Math.sin(ang - 2.4) * al); ctx.closePath(); ctx.fill();
            if (nt.sl > 0) { ctx.globalAlpha = fade; ctx.fillStyle = '#fff'; ctx.font = '700 ' + Math.max(10, Math.round(e[2] * 0.13)) + 'px "Space Grotesk", Inter, sans-serif'; ctx.fillText(String(nt.sl), e[0], e[1] - al - 6); }
          }
          ctx.globalAlpha = 1;
        }
      }
      const pre = BEND_W * bendAt(nt, 0);                   // a prebend: the note is already pushed off its string
      const q = P(X + pre, Y, z);
      if (passed) {
        if (!mk || !mk.hit) return;
        const age = (performance.now() - mk.at) / 1000;
        if (age > 0.5) return;
        const q0 = P(X, Y, 0);
        ctx.globalAlpha = 1 - age / 0.5;
        ctx.strokeStyle = c; ctx.lineWidth = 3;
        ctx.beginPath(); ctx.arc(q0[0], q0[1], gemSize(q0) * (0.6 + age), 0, 7); ctx.stroke();
        ctx.globalAlpha = 1;
        return;
      }
      const g = gemSize(q);
      ctx.globalAlpha = fade * (mk && !mk.hit ? 0.45 : 1);
      // stem down to the floor and a shadow: the height (string) reads even far away
      const fq = P(X, 0, z);
      ctx.strokeStyle = c; ctx.lineWidth = Math.max(1, g * 0.08);
      ctx.beginPath(); ctx.moveTo(q[0], q[1]); ctx.lineTo(fq[0], fq[1]); ctx.stroke();
      ctx.fillStyle = 'rgba(0,0,0,0.45)';
      ctx.beginPath(); ctx.ellipse(fq[0], fq[1], open ? Math.abs(P(aFret - 1 + aWidth, 0, z)[0] - P(aFret - 1, 0, z)[0]) / 2 : g * 0.55, g * 0.14 + 1, 0, 0, 7); ctx.fill();
      // the fret number on the floor, under the note (like Rocksmith); at the neck, the neck's number says it
      if (g > 7 && z > 0.18) {
        ctx.fillStyle = '#f4efe6'; ctx.font = '700 ' + Math.round(Math.max(9, g * 0.46)) + 'px "Space Grotesk", Inter, sans-serif';
        ctx.fillText(String(nt.f | 0), fq[0], fq[1] + g * 0.42 + 4);
      }
      const hollow = nt.ho || nt.po || nt.tp;              // hammer-on, pull-off, tap: you don't pick it
      const harmonic = nt.hm || nt.hp;
      const muted = nt.mt || nt.fhm;
      if (open) {
        // open string: just a line in the string's colour across the hand's zone (like Rocksmith)
        const a = P(aFret - 1 + 0.1, Y, z), b = P(aFret - 1 + aWidth - 0.1, Y, z);
        ctx.strokeStyle = mk && !mk.hit ? '#f87171' : c; ctx.lineCap = 'round';
        ctx.shadowColor = c; ctx.shadowBlur = mk && mk.hit ? 18 : 8;
        ctx.lineWidth = Math.max(3, g * (hollow ? 0.16 : 0.26));
        ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
        ctx.shadowBlur = 0; ctx.lineCap = 'butt';
        if (nt.pm) { ctx.strokeStyle = 'rgba(0,0,0,0.5)'; ctx.lineWidth = Math.max(1, g * 0.07); ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke(); }
      } else {
        if (mk && mk.hit) { ctx.shadowColor = c; ctx.shadowBlur = 18; }
        // a solid gem in the string's colour, lit from above, with a bright rim
        const shape = () => {
          if (harmonic) { ctx.beginPath(); ctx.moveTo(q[0], q[1] - g * 0.55); ctx.lineTo(q[0] + g * 0.55, q[1]); ctx.lineTo(q[0], q[1] + g * 0.55); ctx.lineTo(q[0] - g * 0.55, q[1]); ctx.closePath(); }
          else roundRect(ctx, q[0] - g * 0.68, q[1] - g * 0.36, g * 1.36, g * 0.72, g * 0.12);
        };
        const missed = mk && !mk.hit;
        shape();
        if (hollow) { ctx.fillStyle = 'rgba(8,12,18,0.35)'; ctx.fill(); }
        else {
          ctx.fillStyle = missed ? '#5a2a2a' : c; ctx.fill();
          const sh = ctx.createLinearGradient(q[0], q[1] - g * 0.4, q[0], q[1] + g * 0.4);
          sh.addColorStop(0, 'rgba(255,255,255,0.45)'); sh.addColorStop(0.45, 'rgba(255,255,255,0.05)'); sh.addColorStop(1, 'rgba(0,0,0,0.35)');
          ctx.fillStyle = sh; ctx.fill();
        }
        shape();
        ctx.strokeStyle = missed ? '#f87171' : hollow ? c : 'rgba(255,255,255,0.75)';
        ctx.lineWidth = Math.max(hollow ? 2.5 : 1.5, g * (hollow ? 0.16 : 0.06));
        ctx.stroke();
        ctx.shadowBlur = 0;
        // palm mute: a dark band across (the note sounds dampened)
        if (nt.pm && !harmonic) { ctx.fillStyle = 'rgba(0,0,0,0.55)'; ctx.fillRect(q[0] - g * 0.6, q[1] - g * 0.07, g * 1.2, g * 0.14); }
        if (muted && g > 10) { ctx.fillStyle = '#f2f2f3'; ctx.font = '700 ' + Math.round(g * 0.5) + 'px Inter, sans-serif'; ctx.fillText('✕', q[0], q[1] + 1); }
      }
      // bend: little arrows on the note, one for a half step, two for a whole one, three for more
      const bendMax = nt.bnv && nt.bnv.length ? Math.max(...nt.bnv.map((b) => b.v || 0)) : nt.bn || 0;
      if (bendMax > 0 && g > 8) {
        const k = bendMax <= 0.5 ? 1 : bendMax <= 1 ? 2 : 3, cw = g * 0.32, ch = g * 0.16;
        ctx.strokeStyle = '#fff'; ctx.lineWidth = Math.max(1.5, g * 0.08); ctx.lineJoin = 'round';
        for (let j = 0; j < k; j++) {
          const yy = q[1] - g * 0.5 - 5 - j * (ch + 3);
          ctx.beginPath(); ctx.moveTo(q[0] - cw, yy); ctx.lineTo(q[0], yy - ch); ctx.lineTo(q[0] + cw, yy); ctx.stroke();
        }
      }
      if (holding) {
        const done = (now - nt.t) / sus, q0 = P(X, Y, 0);
        ctx.strokeStyle = c; ctx.lineWidth = 4;
        ctx.beginPath(); ctx.arc(q0[0], q0[1], gemSize(q0) * 0.75, -Math.PI / 2, -Math.PI / 2 + done * Math.PI * 2); ctx.stroke();
      }
      const tag = [nt.slp ? 'Slap' : '', nt.plk ? 'Pop' : ''].filter(Boolean).join(' ');
      if (tag && g > 16) {
        ctx.font = '700 ' + Math.max(9, Math.round(g * 0.3)) + 'px Inter, system-ui, sans-serif';
        const tw = ctx.measureText(tag).width + 8, th = Math.max(12, Math.round(g * 0.34));
        ctx.fillStyle = '#2a2a2f'; roundRect(ctx, q[0] - tw / 2, q[1] - g / 2 - th - 3, tw, th, 4); ctx.fill();
        ctx.fillStyle = '#ededef'; ctx.fillText(tag, q[0], q[1] - g / 2 - th / 2 - 2.5);
      }
      ctx.globalAlpha = 1;
    };
    const arriving = (i) => p.items[i].t - now <= 0.06;
    for (const i of list) if (!arriving(i)) drawNote(i);
    // ---- the neck where the notes land: strings across, fret wires, dots, numbers
    const nut = P(fMin, 0, 0), end = P(fMax, 0, 0);
    poly([P(fMin, -0.02, 0), P(fMax, -0.02, 0), P(fMax, n * strGap + strGap * 0.6, 0), P(fMin, n * strGap + strGap * 0.6, 0)], 'rgba(8,18,28,0.55)');
    void nut; void end;
    for (let f = fMin; f <= fMax; f++) {
      const a = P(f, 0, 0), b = P(f, n * strGap + strGap * 0.4, 0);
      ctx.strokeStyle = f === 0 ? '#e6edf2' : 'rgba(200,212,222,0.7)'; ctx.lineWidth = f === 0 ? 5 : 2.2;
      ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
    }
    for (const f of [3, 5, 7, 9, 12, 15, 17, 19, 21, 24]) {
      if (f <= fMin || f > fMax) continue;
      const q = P(f - 0.5, strGap * 0.15, 0);
      ctx.fillStyle = 'rgba(255,255,255,0.16)';
      ctx.beginPath(); ctx.ellipse(q[0], q[1], Math.max(2, q[2] * 0.06), Math.max(1.5, q[2] * 0.03), 0, 0, 7); ctx.fill();
    }
    for (let str = 0; str < n; str++) {
      const a = P(fMin - 0.3, hOf(str), 0), b = P(fMax + 0.3, hOf(str), 0);
      ctx.strokeStyle = colour(str); ctx.globalAlpha = 0.95;
      const ring = stringGlow(str);
      ctx.lineWidth = Math.max(1.4, (1.2 + (n - 1 - str) * 0.3) * Math.min(1.6, a[2] * 0.012)) * (1 + ring);
      if (ring > 0) { ctx.shadowColor = colour(str); ctx.shadowBlur = 20 * ring; }
      ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
      ctx.shadowBlur = 0;
      ctx.globalAlpha = 1;
    }
    ctx.textAlign = 'center';
    for (let f = Math.max(1, fMin + 1); f <= fMax; f++) {
      const q = P(f - 0.5, -0.05, -0.12);
      const inZone = f >= aFret && f <= aFret - 1 + aWidth;
      const lit = soon.has(f);
      const size = Math.round(Math.max(11, Math.min(22, q[2] * 0.2)) * (lit ? 1.25 : 1));
      ctx.font = '700 ' + size + 'px "Space Grotesk", Inter, sans-serif';
      ctx.fillStyle = lit ? '#ffd27a' : inZone ? '#f5a524' : '#4f6474';
      ctx.fillText(String(f), q[0], q[1] + size * 0.7);
    }
    // string names at the left of the neck
    ctx.font = '700 12px Inter, system-ui, sans-serif'; ctx.textAlign = 'right';
    for (let str = 0; str < n; str++) { const q = P(fMin - 0.35, hOf(str), 0); ctx.fillStyle = colour(str); ctx.fillText(NOTE[p.pitches[str] % 12], q[0] - 4, q[1]); }
    ctx.textAlign = 'center';

    for (const i of list) if (arriving(i)) drawNote(i);
    drawFx(ctx, (nt) => {
      const str = nt.s | 0;
      if (str < 0 || str >= n) return null;
      const X = (nt.f | 0) === 0 ? aFret - 1 + aWidth / 2 : (nt.f | 0) - 0.5;
      const q = P(X, hOf(str), 0);
      return [q[0], q[1], Math.max(20, q[2] * 0.62)];
    }, now);
    drawBanner(ctx, w, h);

    // ---- what comes next, top left
    let nextChord = null;
    const ci = lowerBound(p.chords, now - 0.05, 't');
    if (ci < p.chords.length && p.chords[ci].t - now < 2.5) nextChord = p.chordName(p.chords[ci].id);
    ctx.textAlign = 'left';
    if (nextChord) { ctx.fillStyle = '#f3c77e'; ctx.font = '700 22px "Space Grotesk", Inter, sans-serif'; ctx.fillText(nextChord, 14, 26); }
    ctx.fillStyle = '#a89f92'; ctx.font = '600 11px Inter, system-ui, sans-serif';
    let handText = 'mano en el traste ' + aFret;
    if (nextAnchor && nextAnchor.fret !== aFret && nextAnchor.time - now < 2) handText += '  →  ' + nextAnchor.fret;
    ctx.fillText(handText, 14, nextChord ? 48 : 20);
    if (p.items.length && now < p.items[0].t && p.items[0].t - now < 4 && p.playing) {
      ctx.textAlign = 'center'; ctx.fillStyle = 'rgba(243,199,126,0.9)'; ctx.font = '700 40px "Space Grotesk", Inter, sans-serif';
      ctx.fillText(String(Math.ceil(p.items[0].t - now)), w / 2, h * 0.3);
    }
    ctx.textAlign = 'left';
  }

  let lyricKey = null;
  function drawLyrics(now) {
    const p = play;
    if (!opts.lyrics || !p.lyrics.length) { if (lyricKey !== 'off') { lyricKey = 'off'; ui.lyrics.replaceChildren(); ui.lyrics.classList.add('empty'); } return; }
    let li = -1;
    for (let k = 0; k < p.lyrics.length; k++) { if (p.lyrics[k].t - 1.2 <= now) li = k; else break; }
    let line = li >= 0 ? p.lyrics[li] : null;
    if (line && now > line.end + 1.5) line = null;
    const next = p.lyrics[li + 1] || (li < 0 ? p.lyrics[0] : null);
    const key = (line ? li : 'x') + '/' + (next ? p.lyrics.indexOf(next) : 'x');
    if (key !== lyricKey) {
      lyricKey = key;
      ui.lyrics.classList.remove('empty');
      const cur = el('div.g-lyr-now');
      if (line) line.words.forEach((wd, k) => cur.append(el('span', { text: wd.w + (wd.join || k === line.words.length - 1 ? '' : ' ') })));
      const nx = el('div.g-lyr-next', { text: next && next !== line && next.t - now < 8 ? next.words.map((wd, k) => wd.w + (wd.join || k === next.words.length - 1 ? '' : ' ')).join('') : '' });
      ui.lyrics.replaceChildren(cur, nx);
    }
    if (line) {
      const spans = ui.lyrics.firstChild.children;
      line.words.forEach((wd, k) => { if (spans[k]) spans[k].className = now >= wd.t ? (now <= wd.t + Math.max(0.15, wd.d) ? 'now' : 'sung') : ''; });
    }
  }

  function techTag(n) {
    const t = [];
    if (n.bn) t.push('↑' + (n.bn === 0.5 ? '½' : n.bn === 1.5 ? '1½' : String(n.bn)));
    if (n.sl > 0) t.push('↗' + n.sl);
    if (n.slu > 0) t.push('↘' + n.slu);
    // hammer-ons, pull-offs and taps (hollow), harmonics (diamond) and palm mute (dark band) are drawn, not written
    if (n.fhm) t.push('✕');
    if (n.slp) t.push('Slap');
    if (n.plk) t.push('Pop');
    if (n.vb) t.push('Vib.');
    if (n.tr) t.push('Trém.');
    return t.join(' ');
  }

  function drawFretboard(now) {
    const { ctx, w, h } = fit(fb);
    const p = play, n = p.strings;
    const frets = Math.min(24, p.maxFret);
    const left = 26;
    const fw = (w - left - 8) / frets;
    const xOfFret = (f) => left + f * fw;
    const dotX = (f) => (f === 0 ? left - 11 : left + (f - 0.5) * fw);
    const top = 8, bot = 16, laneH = (h - top - bot) / n;
    const rowY = (s) => top + (rowOf(s, n) + 0.5) * laneH;

    ctx.fillStyle = '#241e17'; ctx.fillRect(0, 0, w, h);
    let anchor = null;
    for (const a of p.anchors) { if (a.time <= now + 0.3) anchor = a; else break; }
    if (anchor) {
      const x0 = xOfFret(Math.max(0, anchor.fret - 1)), x1 = xOfFret(Math.min(frets, anchor.fret - 1 + (anchor.width || 4)));
      ctx.fillStyle = 'rgba(230,168,74,0.16)'; ctx.fillRect(x0, 0, x1 - x0, h);
      ctx.fillStyle = '#e6a84a'; ctx.fillRect(x0, 0, 2, h); ctx.fillRect(x1 - 2, 0, 2, h);
    }
    ctx.fillStyle = 'rgba(255,255,255,0.08)';
    for (const f of [3, 5, 7, 9, 15, 17, 19, 21]) if (f <= frets) { ctx.beginPath(); ctx.arc(left + (f - 0.5) * fw, top + (h - top - bot) / 2, 4, 0, 7); ctx.fill(); }
    for (const f of [12, 24]) if (f <= frets) { ctx.beginPath(); ctx.arc(left + (f - 0.5) * fw, top + (h - top - bot) * 0.3, 4, 0, 7); ctx.arc(left + (f - 0.5) * fw, top + (h - top - bot) * 0.7, 4, 0, 7); ctx.fill(); }
    ctx.font = '600 10px Inter, system-ui, sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    for (let f = 0; f <= frets; f++) {
      ctx.fillStyle = f === 0 ? '#e8e2d6' : '#8c8478';
      ctx.fillRect(xOfFret(f) - (f === 0 ? 2 : 1), top - 3, f === 0 ? 4 : 2, h - top - bot + 6);
      if (f > 0) { ctx.fillStyle = '#a89f92'; ctx.fillText(String(f), left + (f - 0.5) * fw, h - 7); }
    }
    for (let s = 0; s < n; s++) { ctx.fillStyle = colour(s); ctx.globalAlpha = 0.8; const th = 1.2 + (n - 1 - s) * 0.3; ctx.fillRect(left - 20, rowY(s) - th / 2, w, th); }
    ctx.globalAlpha = 1;

    const soon = [];
    const i0 = lowerBound(p.items, now - 0.08, 't');
    let firstT = null;
    for (let i = i0; i < p.items.length; i++) {
      const it = p.items[i];
      if (firstT === null) firstT = it.t;
      if (it.t > firstT + 0.06 || it.t > now + 1.2) break;
      soon.push(it);
    }
    for (let i = Math.max(0, i0 - 40); i < i0; i++) { const it = p.items[i]; if (it.t + (it.sus || 0) > now) soon.push(Object.assign({ held: true }, it)); }
    const r = Math.max(6, Math.min(12, laneH * 0.55));
    for (const it of soon) {
      const s = it.s | 0;
      if (s < 0 || s >= n) continue;
      const near = firstT !== null && it.t - now < 0.35;
      ctx.globalAlpha = it.held ? 0.55 : near ? 1 : 0.6;
      ctx.fillStyle = colour(s); ctx.strokeStyle = '#fff'; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(dotX(it.f | 0), rowY(s), r, 0, 7); ctx.fill(); ctx.stroke();
      ctx.fillStyle = '#111'; ctx.font = '700 ' + Math.round(r * 1.05) + 'px Inter, system-ui, sans-serif';
      ctx.fillText(it.mt ? '✕' : String(it.f | 0), dotX(it.f | 0), rowY(s) + 0.5);
    }
    ctx.globalAlpha = 1;

    let nextChord = null;
    const ci = lowerBound(p.chords, now - 0.05, 't');
    if (ci < p.chords.length && p.chords[ci].t - now < 2.5) nextChord = p.chordName(p.chords[ci].id);
    const label = nextChord || (soon.length && !soon[0].held ? NOTE[(p.pitches[soon[0].s | 0] + (soon[0].f | 0)) % 12] : '');
    const hand = anchor ? 'mano en el traste ' + anchor.fret : '';
    if (ui.next._k !== label + hand) {
      ui.next._k = label + hand;
      ui.next.replaceChildren(el('small', { text: 'Ahora viene' }), el('b', { text: label || '—' }), el('span', { text: hand }));
    }
  }

  // ------------------------------------------------------------------ end of the song: how it went
  let endBox = null;
  function showEnd() {
    if (!play || !play.ended) return;
    hideEnd();
    const p = play, sc = p.score;
    const listened = sc.judged > 0;
    const body = [el('h2', { text: '¡Terminaste ' + p.song.title + '!' })];
    let worst = -1;
    if (listened) {
      const acc = Math.round(100 * sc.hits / sc.judged);
      body.push(el('div.g-res-top', null,
        hudCell('Puntos', sc.points.toLocaleString('es-AR')), hudCell('Precisión', acc + ' %'), hudCell('Mejor racha', String(sc.best)),
        hudCell('Notas', sc.hits + ' de ' + sc.judged)));
      // by part
      const rows = el('div.g-res-secs');
      let worstAcc = 2;
      p.sections.forEach((s, i) => {
        const st = sc.sections[i];
        if (!st || !st.total) return;
        const a = st.hits / st.total;
        if (a < worstAcc && a < 0.9 && st.total >= 3) { worstAcc = a; worst = i; }
        rows.append(el('div.g-res-sec', null, el('span', { text: sectionName(s) }),
          el('i', null, el('u', { style: { width: Math.round(a * 100) + '%', background: a >= 0.85 ? '#4ade80' : a >= 0.6 ? '#e6a84a' : '#f87171' } })),
          el('b', { text: Math.round(a * 100) + ' %' })));
      });
      if (rows.children.length) body.push(el('h4', { text: 'Por parte' }), rows);
      // timing
      const ts = sc.timings;
      if (ts.length >= 4) {
        const early = ts.filter((x) => x < -35).length, late = ts.filter((x) => x > 35).length, ok = ts.length - early - late;
        const avg = ts.reduce((a, b) => a + b, 0) / ts.length;
        const bar = (label, v, cls) => el('div.g-res-t', null, el('span', { text: label }), el('i', null, el('u.' + cls, { style: { width: Math.round(100 * v / ts.length) + '%' } })), el('b', { text: String(v) }));
        body.push(el('h4', { text: 'A tiempo' }), el('div.g-res-secs', null, bar('Temprano', early, 'e'), bar('Justo', ok, 'o'), bar('Tarde', late, 'l')));
        if (Math.abs(avg) > 25 && ts.length >= 10) {
          const fix = Math.round(avg / 5) * 5;
          body.push(el('div.g-res-fix', null,
            el('p', { text: `En promedio tocaste ${Math.abs(Math.round(avg))} ms ${avg > 0 ? 'tarde' : 'temprano'}. Si sentiste que ibas a tiempo, es la demora de tu equipo.` }),
            el('button.btn', { onclick: (e) => {
              opts.detectOffsetMs = G.clamp(opts.detectOffsetMs + fix, -150, 250);
              store.set('detectOffsetMs', opts.detectOffsetMs);
              G.call('gameDetectOffset', { offsetMs: opts.detectOffsetMs });
              e.target.replaceWith(el('span.note', { text: 'Listo, compensado.' }));
            } }, 'Compensar')));
        }
      }
    } else body.push(el('p.note', { text: p.guitar ? 'No te escuché tocar: fíjate que la barrita de «Tu sonido» se mueva cuando tocas.' : 'Para darte puntos, elige tu guitarra en «Tu sonido».' }));
    body.push(el('div.row', null,
      el('button.btn.primary', { onclick: () => { hideEnd(); resetScore(); updateScoreHud(); seek(0); G.call('gamePlay'); play.playing = true; updateControls(); } }, 'Otra vez'),
      worst >= 0 ? el('button.btn', { onclick: () => { hideEnd(); loopSection(worst); if (play.speed > 0.8) { play.speed = 0.8; ui.speed.value = 0.8; G.call('gameSpeed', { speed: 0.8 }); } G.call('gamePlay'); play.playing = true; updateControls(); } },
        'Practicar ' + sectionName(play.sections[worst])) : null,
      el('button.btn', { onclick: closeSong }, 'Elegir otra canción')));
    endBox = el('div.g-end', { role: 'dialog', 'aria-label': 'Cómo te fue' }, body);
    root.querySelector('.g-stage').append(endBox);
  }
  function hideEnd() { if (endBox) endBox.remove(); endBox = null; }

  // ------------------------------------------------------------------ keys
  Game.onKey = function (e) {
    if (e.target.closest('input[type=search], input[type=text], input.text, select') && e.code !== 'Escape') return;
    if (!play) { if (e.key === 'Escape') Game.leave(); return; }
    const k = e.key.toLowerCase();
    if (e.code === 'Space') { e.preventDefault(); if (document.activeElement) document.activeElement.blur(); if (!e.repeat) toggle(); return; }
    if (k === 'arrowleft') { e.preventDefault(); seek(displayPos() - (e.shiftKey ? 10 : 3)); }
    else if (k === 'arrowright') { e.preventDefault(); seek(displayPos() + (e.shiftKey ? 10 : 3)); }
    else if (k === 'l') abButton();
    else if (k === 'v') setView({ '2d': '3d', '3d': 'both', both: '2d' }[opts.view] || '2d');
    else if (k === '+' || k === '=') setSize(opts.size + 0.1);
    else if (k === '-' || k === '_') setSize(opts.size - 0.1);
    else if (k === 'escape') { if (panel) togglePanel(null); else if (play.loop || play.markA !== null) setLoop(null); }
    else if (k === 'home' || k === 'enter') seek(0);
    else if (k === 'arrowup' || k === 'arrowdown') {
      e.preventDefault();
      const list = Array.from(ui.speed.options).map((o) => +o.value);
      const i = list.findIndex((v) => Math.abs(v - play.speed) < 1e-3);
      const ni = G.clamp((i < 0 ? 0 : i) + (k === 'arrowup' ? -1 : 1), 0, list.length - 1);
      ui.speed.value = list[ni]; play.speed = list[ni]; G.call('gameSpeed', { speed: play.speed });
      G.toast('Velocidad ' + Math.round(play.speed * 100) + ' %');
    }
  };

  G.on('tick', (t) => Game.onTick(t));
  window.addEventListener('resize', () => { if (play) { applySize(); draw(); } });
})();
