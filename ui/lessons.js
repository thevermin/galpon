/* Galpon UI — Lecciones de guitarra: teoría corta, diagramas que suenan, preguntas y ejercicios
   que se tocan en la autopista del modo Juego (Galpon te escucha y te da estrellas). */
(function () {
  'use strict';
  const G = window.G;
  const { el } = G;

  // ------------------------------------------------------------------ music basics
  const OPEN = [40, 45, 50, 55, 59, 64];            // E2 A2 D3 G3 B3 E4: string 6 … string 1
  const ES = ['Do', 'Do#', 'Re', 'Re#', 'Mi', 'Fa', 'Fa#', 'Sol', 'Sol#', 'La', 'La#', 'Si'];
  const EN = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const both = (pc) => `${ES[pc % 12]} (${EN[pc % 12]})`;
  const midiOf = (str, fret) => OPEN[6 - str] + fret;     // str: 6 = low E … 1 = high E

  // chord shapes: frets from string 6 to string 1 (-1 = don't play it), and fingers
  const CHORDS = {
    E: [[0, 2, 2, 1, 0, 0], [0, 2, 3, 1, 0, 0]],
    Em: [[0, 2, 2, 0, 0, 0], [0, 2, 3, 0, 0, 0]],
    Em7: [[0, 2, 2, 0, 3, 0], [0, 1, 2, 0, 3, 0]],
    A: [[-1, 0, 2, 2, 2, 0], [0, 0, 1, 2, 3, 0]],
    Am: [[-1, 0, 2, 2, 1, 0], [0, 0, 2, 3, 1, 0]],
    Asus2: [[-1, 0, 2, 2, 0, 0], [0, 0, 1, 2, 0, 0]],
    D: [[-1, -1, 0, 2, 3, 2], [0, 0, 0, 1, 3, 2]],
    Dm: [[-1, -1, 0, 2, 3, 1], [0, 0, 0, 2, 3, 1]],
    Dsus4: [[-1, -1, 0, 2, 3, 3], [0, 0, 0, 1, 3, 4]],
    C: [[-1, 3, 2, 0, 1, 0], [0, 3, 2, 0, 1, 0]],
    G: [[3, 2, 0, 0, 0, 3], [2, 1, 0, 0, 0, 3]],
    E7: [[0, 2, 0, 1, 0, 0], [0, 2, 0, 1, 0, 0]],
    A7: [[-1, 0, 2, 0, 2, 0], [0, 0, 2, 0, 3, 0]],
    D7: [[-1, -1, 0, 2, 1, 2], [0, 0, 0, 2, 1, 3]],
    B7: [[-1, 2, 1, 2, 0, 2], [0, 2, 1, 3, 0, 4]],
    G7: [[3, 2, 0, 0, 0, 1], [3, 2, 0, 0, 0, 1]],
    C7: [[-1, 3, 2, 3, 1, 0], [0, 3, 2, 4, 1, 0]],
    Fmaj7: [[-1, -1, 3, 2, 1, 0], [0, 0, 3, 2, 1, 0]],
    'F (chico)': [[-1, -1, 3, 2, 1, 1], [0, 0, 3, 2, 1, 1]],
    F: [[1, 3, 3, 2, 1, 1], [1, 3, 4, 2, 1, 1]],
    'G (cejilla)': [[3, 5, 5, 4, 3, 3], [1, 3, 4, 2, 1, 1]],
    'A (cejilla)': [[5, 7, 7, 6, 5, 5], [1, 3, 4, 2, 1, 1]],
    'Am (cejilla)': [[5, 7, 7, 5, 5, 5], [1, 3, 4, 1, 1, 1]],
    Bm: [[-1, 2, 4, 4, 3, 2], [0, 1, 3, 4, 2, 1]],
    'C (cejilla)': [[-1, 3, 5, 5, 5, 3], [0, 1, 2, 3, 4, 1]],
    E5: [[0, 2, 2, -1, -1, -1], [0, 1, 2, 0, 0, 0]],
    A5: [[-1, 0, 2, 2, -1, -1], [0, 0, 1, 2, 0, 0]],
    F5: [[1, 3, 3, -1, -1, -1], [1, 3, 4, 0, 0, 0]],
    G5: [[3, 5, 5, -1, -1, -1], [1, 3, 4, 0, 0, 0]],
    'A5 (6ª)': [[5, 7, 7, -1, -1, -1], [1, 3, 4, 0, 0, 0]],
    C5: [[-1, 3, 5, 5, -1, -1], [0, 1, 3, 4, 0, 0]],
    D5: [[-1, 5, 7, 7, -1, -1], [0, 1, 3, 4, 0, 0]],
  };
  const chordLabel = (name) => (G.chordLabel ? G.chordLabel(name.replace(/ \(.*\)$/, '')) : name) + (name.includes(' (') ? ' ' + name.slice(name.indexOf(' (')) : '');

  // ------------------------------------------------------------------ the little language for exercises
  /* Tokens, separated by spaces (a "|" is only for reading):
       6.3        string 6, fret 3 (strings like guitarists say them: 6 = the thick one)
       6.3+4.5    two notes at once
       [G]        strum the chord G (only its strings)
       -          a rest
       :2         after any of those: how many beats it lasts (default: the exercise's unit)
       h6.5 p6.3  hammer-on / pull-off (don't pick it)       5.5/7  slide from 5 to 7
       6.0m       palm mute (apagado con la palma)
     A chart = { bpm, beats (per bar, 4), unit (1), countIn (1 bar), drums: 'rock'|'soft'|'none', seq, repeat } */
  function parse(spec) {
    const unit = spec.unit || 1;
    const events = [];
    let beat = 0;
    // chord names can have spaces ("F (chico)"): keep them in one token
    const seq = (Array.isArray(spec.seq) ? spec.seq.join(' ') : spec.seq).replace(/\[([^\]]*)\]/g, (m, n) => '[' + n.replace(/ /g, '_') + ']');
    for (let rep = 0; rep < (spec.repeat || 1); rep++) {
      for (let tok of seq.split(/\s+/)) {
        if (!tok || tok === '|') continue;
        let dur = unit;
        const m = tok.match(/^(.*?):([\d.]+)$/);
        if (m) { tok = m[1]; dur = parseFloat(m[2]); }
        if (tok === '-' || tok === 'r') { beat += dur; continue; }
        if (tok[0] === '[') {
          if (tok.indexOf(']') < 0) throw new Error('Acorde sin cerrar ' + tok);
          const name = tok.slice(1, tok.indexOf(']')).replace(/_/g, ' ');
          const shape = CHORDS[name];
          if (!shape) throw new Error('Acorde desconocido ' + name);
          const notes = [];
          shape[0].forEach((f, i) => { if (f >= 0) notes.push({ str: 6 - i, fret: f }); });
          events.push({ beat, dur, notes, chord: name, mute: tok.endsWith('m') });
          beat += dur;
          continue;
        }
        const ev = { beat, dur, notes: [] };
        for (let part of tok.split('+')) {
          const n = {};
          if (part[0] === 'h') { n.ho = 1; part = part.slice(1); }
          else if (part[0] === 'p') { n.po = 1; part = part.slice(1); }
          if (part.endsWith('m')) { n.pm = 1; part = part.slice(0, -1); }
          const sl = part.split('/');
          const [s, f] = sl[0].split('.').map(Number);
          if (!(s >= 1 && s <= 6) || !(f >= 0 && f <= 24)) throw new Error('Nota mal escrita ' + tok);
          n.str = s; n.fret = f;
          if (sl[1] !== undefined) n.slideTo = Number(sl[1]);
          ev.notes.push(n);
        }
        events.push(ev);
        beat += dur;
      }
    }
    return { events, beats: beat };
  }

  /** Everything the game needs (chart + audio) for an exercise. */
  function buildChart(spec, title) {
    const { events, beats: total } = parse(spec);
    const bpb = spec.beats || 4;
    const spb = 60 / spec.bpm;
    const lead = (spec.countIn === undefined ? 1 : spec.countIn) * bpb;     // count-in bars
    const T = (b) => (b + lead) * spb;
    const endBeat = lead + Math.ceil(total / bpb) * bpb;
    const notes = [], chords = [], templates = [], tplIndex = {};
    const guide = [];
    for (const ev of events) {
      const t = T(ev.beat);
      const sus = ev.dur * spb >= 0.9 ? ev.dur * spb * 0.85 : 0;
      const raw = ev.notes.map((n) => {
        const o = { s: 6 - n.str, f: n.fret, sus };
        if (n.ho) o.ho = 1;
        if (n.po) o.po = 1;
        if (n.pm || ev.mute) o.pm = 1;
        if (n.slideTo !== undefined) { o.sl = n.slideTo; o.sus = Math.max(sus, ev.dur * spb * 0.8); }
        return o;
      });
      if (ev.chord || raw.length > 2) {
        const name = ev.chord || '';
        if (tplIndex[name] === undefined) { tplIndex[name] = templates.length; templates.push({ name: name.replace(/ \(.*\)$/, '') }); }
        chords.push({ t, id: tplIndex[name], notes: raw });
      } else for (const o of raw) notes.push(Object.assign({ t }, o));
      // the guide guitar plays it (a slide or a legato note just sounds: close enough)
      guide.push({ t, p: ev.notes.map((n) => midiOf(n.str, n.slideTo !== undefined ? n.fret : n.fret)), d: Math.max(0.1, ev.dur * spb * 0.95), m: !!(ev.mute || ev.notes.some((n) => n.pm)) });
      for (const n of ev.notes) if (n.slideTo !== undefined) guide.push({ t: t + ev.dur * spb * 0.4, p: [midiOf(n.str, n.slideTo)], d: ev.dur * spb * 0.55, m: false });
    }
    // where the hand goes: the lowest fret coming up in the next couple of seconds
    const anchors = [];
    const all = notes.map((n) => ({ t: n.t, f: n.f })).concat(...chords.map((c) => c.notes.map((n) => ({ t: c.t, f: n.f })))).sort((a, b) => a.t - b.t);
    let last = null;
    for (const n of all) {
      if (n.f <= 0) continue;
      if (last && n.f >= last.fret && n.f <= last.fret + 3) continue;
      const near = all.filter((x) => x.t >= n.t && x.t < n.t + 1.5 && x.f > 0).map((x) => x.f);
      const lo = Math.min(n.f, ...near);
      last = { time: Math.max(0, n.t - 0.4), fret: Math.max(1, Math.max(lo, Math.max(...near, n.f) - 3)), width: 4 };
      if (n.f < last.fret) last.fret = n.f;
      anchors.push(last);
    }
    if (!anchors.length) anchors.push({ time: 0, fret: 1, width: 4 });
    // beats, bars and the click
    const beatList = [], clicks = [], drums = [];
    for (let b = 0; b <= endBeat; b++) {
      const bar = b % bpb === 0;
      beatList.push({ time: b * spb, measure: bar ? Math.floor(b / bpb) - (lead / bpb) + 1 : -1 });
      if (b < endBeat) clicks.push({ t: b * spb, a: bar });
    }
    const kit = spec.drums === undefined ? 'soft' : spec.drums;
    if (kit !== 'none')
      for (let b = lead; b < endBeat; b++) {
        const inBar = (b - lead) % bpb;
        if (kit === 'rock') {
          if (inBar === 0 || (bpb === 4 && inBar === 2)) drums.push({ t: b * spb, k: 'k' });
          if (bpb === 4 ? inBar % 2 === 1 : inBar > 0) drums.push({ t: b * spb, k: 's' });
          drums.push({ t: b * spb, k: 'h' }, { t: (b + 0.5) * spb, k: 'h' });
        } else {
          if (inBar === 0) drums.push({ t: b * spb, k: 'k' });
          drums.push({ t: b * spb, k: 'h' });
        }
      }
    const sections = [{ time: lead * spb, name: title || 'Ejercicio', number: 1 }];
    const data = {
      arrangements: { leccion: { name: 'Lección', type: 'guitar', tuning: [0, 0, 0, 0, 0, 0], notes, chords, templates, anchors } },
      beats: beatList, sections, lyrics: [],
    };
    return { data, audio: { length: endBeat * spb + 2, clicks, drums, guide }, guide, spb, lead };
  }

  // ------------------------------------------------------------------ listening in the lesson (no engine needed)
  let ac = null;
  function ctx() {
    try { if (!ac) ac = new (window.AudioContext || window.webkitAudioContext)(); if (ac.state === 'suspended') ac.resume(); } catch (e) { ac = null; }
    return ac;
  }
  /** A plucked string (Karplus-Strong), into a buffer. */
  function pluckBuffer(a, midi, dur, muted) {
    const rate = a.sampleRate, f = 440 * Math.pow(2, (midi - 69) / 12);
    const period = Math.max(2, Math.round(rate / f));
    const len = Math.floor((muted ? 0.15 : dur + 0.4) * rate);
    const buf = a.createBuffer(1, len, rate), out = buf.getChannelData(0);
    const line = new Float32Array(period);
    let last = 0;
    for (let i = 0; i < period; i++) { const r = Math.random() * 2 - 1; line[i] = 0.5 * (r + last); last = r; }
    const damp = Math.floor((muted ? 0.02 : dur) * rate), decay = muted ? 0.9 : midi < 50 ? 0.9985 : 0.997;
    let k = 0, lp = 0;
    for (let i = 0; i < len; i++) {
      const nk = (k + 1) % period;
      const y = 0.5 * (line[k] + line[nk]) * (i > damp ? 0.985 : decay);
      line[k] = y; k = nk;
      lp += 0.6 * (y - lp);
      out[i] = lp * 0.5;
    }
    return buf;
  }
  let playingNodes = [];
  function stopSound() { for (const n of playingNodes) { try { n.stop(); } catch (e) { /* already done */ } } playingNodes = []; }
  /** Plays [{t, p:[midi], d, m}] (seconds), chords strummed a hair apart. */
  function playGuide(list) {
    const a = ctx();
    if (!a) return G.toast('No se puede reproducir sonido aquí', true);
    stopSound();
    const t0 = a.currentTime + 0.05;
    for (const ev of list) {
      ev.p.forEach((p, i) => {
        const src = a.createBufferSource();
        src.buffer = pluckBuffer(a, p, ev.d, ev.m);
        const g = a.createGain();
        g.gain.value = 0.9 / Math.sqrt(ev.p.length);
        src.connect(g).connect(a.destination);
        src.start(t0 + ev.t + i * 0.014);
        playingNodes.push(src);
      });
    }
  }
  const hearNotes = (midis, gap = 0.55, together) => playGuide(midis.map((p, i) => ({ t: together ? 0 : i * gap, p: Array.isArray(p) ? p : [p], d: together ? 2 : gap * 1.6 })));
  const chordPitches = (name) => CHORDS[name][0].map((f, i) => (f >= 0 ? OPEN[i] + f : null)).filter((v) => v !== null);
  const hearChord = (name) => playGuide([{ t: 0, p: chordPitches(name), d: 2 }]);
  const hearSeq = (spec) => { const c = buildChart(Object.assign({ countIn: 0 }, spec)); playGuide(c.guide); };

  // ------------------------------------------------------------------ drawings
  const SVG = 'http://www.w3.org/2000/svg';
  function svg(tag, attrs, ...kids) {
    const n = document.createElementNS(SVG, tag);
    for (const k in attrs || {}) n.setAttribute(k, attrs[k]);
    for (const c of kids) if (c) n.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    return n;
  }
  const strColour = (str) => (G.stringColour ? G.stringColour(6 - str) : '#ccc');

  /** A chord box: strings up and down, frets across, dots with the finger number. */
  function chordBox(name) {
    const [frets, fingers] = CHORDS[name];
    const played = frets.filter((f) => f > 0);
    const lo = played.length ? Math.min(...played) : 1, hi = played.length ? Math.max(...played) : 1;
    const base = hi <= 4 ? 1 : lo;
    const W = 112, H = 140, x0 = 18, y0 = 34, dx = 16, dy = 20;
    const s = svg('svg', { viewBox: `0 0 ${W} ${H}`, width: W, height: H, class: 'l-chord', role: 'img', 'aria-label': 'Acorde ' + name });
    if (base === 1) s.appendChild(svg('rect', { x: x0 - 1, y: y0 - 4, width: dx * 5 + 2, height: 4, fill: '#ddd' }));
    else s.appendChild(svg('text', { x: x0 + dx * 5 + 6, y: y0 + dy * 0.7, 'font-size': 11, fill: '#aaa' }, base + 'ª'));
    for (let i = 0; i <= 4; i++) s.appendChild(svg('line', { x1: x0, x2: x0 + dx * 5, y1: y0 + i * dy, y2: y0 + i * dy, stroke: '#666' }));
    for (let i = 0; i < 6; i++) s.appendChild(svg('line', { x1: x0 + i * dx, x2: x0 + i * dx, y1: y0, y2: y0 + 4 * dy, stroke: '#888' }));
    // barre: the same fret with finger 1 on more than one string
    const bar = frets.map((f, i) => (fingers[i] === 1 && f > 0 ? i : -1)).filter((i) => i >= 0);
    if (bar.length > 1 && bar.every((i) => frets[i] === frets[bar[0]])) {
      const y = y0 + (frets[bar[0]] - base + 0.5) * dy;
      s.appendChild(svg('rect', { x: x0 + bar[0] * dx - 6, y: y - 6, width: (bar[bar.length - 1] - bar[0]) * dx + 12, height: 12, rx: 6, fill: '#e8e8ec' }));
    }
    frets.forEach((f, i) => {
      const x = x0 + i * dx;
      if (f < 0) s.appendChild(svg('text', { x, y: y0 - 9, 'text-anchor': 'middle', 'font-size': 11, fill: '#f87171' }, '×'));
      else if (f === 0) s.appendChild(svg('circle', { cx: x, cy: y0 - 13, r: 4, fill: 'none', stroke: '#bbb' }));
      else {
        const y = y0 + (f - base + 0.5) * dy;
        s.appendChild(svg('circle', { cx: x, cy: y, r: 7, fill: '#e8e8ec' }));
        if (fingers[i]) s.appendChild(svg('text', { x, y: y + 4, 'text-anchor': 'middle', 'font-size': 10, 'font-weight': 700, fill: '#111' }, String(fingers[i])));
      }
    });
    s.appendChild(svg('text', { x: W / 2 - 4, y: 14, 'text-anchor': 'middle', 'font-size': 14, 'font-weight': 700, fill: '#fff' }, chordLabel(name)));
    return s;
  }

  /** The neck seen like a tab: string 1 on top, frets across. marks: [{s, f, label, root}] */
  function fretboard(cfg) {
    const from = cfg.from || 0, to = cfg.to || 12;
    const n = to - from + 1;
    const W = Math.min(820, 60 + n * 52), H = 150, x0 = 36, y0 = 18, dy = 22;
    const fx = (f) => x0 + (f - from + (from === 0 ? 0 : 0.5)) * ((W - x0 - 10) / (n - (from === 0 ? 0.5 : 0)));
    const fw = (W - x0 - 10) / (n - (from === 0 ? 0.5 : 0));
    const s = svg('svg', { viewBox: `0 0 ${W} ${H}`, width: W, height: H, class: 'l-fret', role: 'img', 'aria-label': cfg.label || 'Diapasón' });
    s.appendChild(svg('rect', { x: x0, y: y0 - 6, width: W - x0 - 10, height: dy * 5 + 12, rx: 4, fill: '#2a2119' }));
    for (let f = Math.max(1, from); f <= to; f++) {
      const x = fx(f) + fw / 2;
      s.appendChild(svg('line', { x1: x, x2: x, y1: y0 - 6, y2: y0 + dy * 5 + 6, stroke: f === 0 ? '#eee' : '#9a8f80', 'stroke-width': 2 }));
    }
    if (from === 0) s.appendChild(svg('rect', { x: fx(0) + fw / 2 - 3, y: y0 - 6, width: 5, height: dy * 5 + 12, fill: '#e8e2d4' }));
    for (const d of [3, 5, 7, 9, 15, 17]) if (d >= from && d <= to && d > 0) s.appendChild(svg('circle', { cx: fx(d), cy: y0 + dy * 2.5, r: 5, fill: '#5a4d3c' }));
    if (12 >= from && 12 <= to) { s.appendChild(svg('circle', { cx: fx(12), cy: y0 + dy * 1.5, r: 5, fill: '#5a4d3c' })); s.appendChild(svg('circle', { cx: fx(12), cy: y0 + dy * 3.5, r: 5, fill: '#5a4d3c' })); }
    for (let i = 0; i < 6; i++) {
      const str = i + 1, y = y0 + i * dy;
      s.appendChild(svg('line', { x1: x0, x2: W - 10, y1: y, y2: y, stroke: strColour(str), 'stroke-width': 1 + i * 0.35, opacity: 0.8 }));
      s.appendChild(svg('text', { x: 12, y: y + 4, 'font-size': 11, fill: '#999' }, String(str)));
    }
    for (let f = Math.max(from, 1); f <= to; f++) s.appendChild(svg('text', { x: fx(f), y: H - 4, 'text-anchor': 'middle', 'font-size': 10, fill: '#777' }, String(f)));
    for (const m of cfg.marks || []) {
      const x = fx(m.f), y = y0 + (m.s - 1) * dy;
      const g = svg('g', { class: 'l-mark', tabindex: 0, role: 'button', 'aria-label': (m.label || '') + ' cuerda ' + m.s + ' traste ' + m.f });
      g.appendChild(svg('circle', { cx: x, cy: y, r: 9.5, fill: m.root ? '#ef4444' : m.dim ? '#3b3b44' : strColour(m.s), stroke: '#111', 'stroke-width': 1 }));
      if (m.label) g.appendChild(svg('text', { x, y: y + 3.5, 'text-anchor': 'middle', 'font-size': m.label.length > 2 ? 7.5 : 9, 'font-weight': 700, fill: m.dim ? '#ddd' : '#111' }, m.label));
      g.addEventListener('click', () => hearNotes([midiOf(m.s, m.f)]));
      s.appendChild(g);
    }
    return el('div.l-fretwrap', null, s);
  }

  /** A tab of an exercise (first bars), drawn like the ones online. */
  function tab(spec, bars) {
    const { events } = parse(spec);
    const bpb = spec.beats || 4;
    const maxBeat = (bars || 4) * bpb;
    const shown = events.filter((e) => e.beat < maxBeat);
    const total = Math.min(maxBeat, Math.ceil(((shown.length ? shown[shown.length - 1].beat + shown[shown.length - 1].dur : bpb)) / bpb) * bpb);
    const bw = Math.max(26, Math.min(48, 640 / Math.max(1, total)));
    const W = 30 + total * bw + 12, H = 128, y0 = 14, dy = 18;
    const s = svg('svg', { viewBox: `0 0 ${W} ${H}`, width: W, height: H, class: 'l-tab', role: 'img', 'aria-label': 'Tablatura' });
    for (let i = 0; i < 6; i++) s.appendChild(svg('line', { x1: 8, x2: W - 4, y1: y0 + i * dy, y2: y0 + i * dy, stroke: '#666' }));
    'TAB'.split('').forEach((c, i) => s.appendChild(svg('text', { x: 12, y: y0 + 26 + i * 16, 'font-size': 12, 'font-weight': 700, fill: '#888' }, c)));
    for (let b = 0; b <= total; b += bpb) { const x = 30 + b * bw; s.appendChild(svg('line', { x1: x, x2: x, y1: y0, y2: y0 + dy * 5, stroke: '#888' })); }
    for (const e of shown) {
      const x = 30 + (e.beat + 0.5 * Math.min(1, e.dur)) * bw;
      for (const n of e.notes) {
        const y = y0 + (n.str - 1) * dy;
        let label = String(n.fret) + (n.slideTo !== undefined ? '/' + n.slideTo : '');
        if (n.pm || e.mute) label += '';
        s.appendChild(svg('rect', { x: x - (label.length * 4 + 3), y: y - 7, width: label.length * 8 + 6, height: 14, fill: '#18181b' }));
        s.appendChild(svg('text', { x, y: y + 4, 'text-anchor': 'middle', 'font-size': 12, 'font-weight': 600, fill: '#fff' }, label));
        if (n.ho || n.po) s.appendChild(svg('text', { x: x - bw / 2, y: y - 8, 'text-anchor': 'middle', 'font-size': 9, fill: '#e6a84a' }, n.ho ? 'h' : 'p'));
      }
      if (e.notes.some((n) => n.pm) || e.mute) s.appendChild(svg('text', { x, y: y0 + dy * 5 + 16, 'text-anchor': 'middle', 'font-size': 9, fill: '#aaa' }, 'P.M.'));
      if (e.chord) s.appendChild(svg('text', { x, y: H - 4, 'text-anchor': 'middle', 'font-size': 11, 'font-weight': 700, fill: '#e6a84a' }, chordLabel(e.chord)));
    }
    return el('div.l-tabwrap', null, s);
  }

  // ------------------------------------------------------------------ progress (this computer)
  const KEY = 'galpon.lessons.v1';
  let prog = {};
  try { prog = JSON.parse(localStorage.getItem(KEY) || '{}') || {}; } catch (e) { prog = {}; }
  const save = () => { try { localStorage.setItem(KEY, JSON.stringify(prog)); } catch (e) { /* storage unavailable */ } };
  const lp = (id) => (prog[id] = prog[id] || { ex: {}, quiz: {}, page: 0, done: false });
  function lessonStars(L) {
    const exs = exercisesOf(L);
    const p = prog[L.id];
    if (!p) return 0;
    if (!exs.length) return p.done ? 3 : 0;
    return Math.min(...exs.map((x) => p.ex[x.id] || 0));
  }
  const exercisesOf = (L) => L.pages.flatMap((pg) => pg.blocks.filter((b) => b.play).map((b) => b.play));

  // ------------------------------------------------------------------ the course
  // (each page is short on purpose: read, hear it, play it)
  const P = (title, ...blocks) => ({ title, blocks });
  const ex = (id, label, spec, note) => ({ play: Object.assign({ id, label, note }, spec) });
  const quiz = (...qs) => ({ quiz: qs });
  const Q = (q, options, answer, why) => ({ q, options, answer, why });

  const COURSE = [
    { id: 'u1', title: 'Primeros pasos', desc: 'La guitarra, la tablatura, los dedos y el pulso.', lessons: [
      { id: 'l-cuerdas', title: 'Tu guitarra y sus cuerdas', min: 6, pages: [
        P('Bienvenido',
          { p: 'Cada lección es corta: un poco de teoría, la escuchas, y la tocas en la **autopista**, como una canción. Galpon te escucha y te da hasta **3 estrellas** por ejercicio.' },
          { p: 'Para que te escuche, en el ejercicio abre **Tu sonido** y elige por dónde entra tu guitarra. Si no tienes interfaz conectada, igual puedes seguir: lee, escucha y toca con la guía.' },
          { tip: 'Antes de empezar, afina. Una guitarra desafinada hace que todo suene mal aunque lo toques perfecto.', tune: true }),
        P('Seis cuerdas, seis nombres',
          { p: 'La **6ª** es la cuerda más gruesa (la más grave) y la **1ª** la más fina (la más aguda). Con la guitarra en las piernas, la 6ª te queda arriba, más cerca de la cara.' },
          { p: 'Al aire, sin apretar nada, suenan: **Mi La Re Sol Si Mi** (en inglés **E A D G B E**). Cántalo un par de veces: «Mi, La, Re, Sol, Si, Mi».' },
          { fret: { to: 5, marks: [6, 5, 4, 3, 2, 1].map((s) => ({ s, f: 0, label: EN[OPEN[6 - s] % 12] })) } },
          { listen: [6, 5, 4, 3, 2, 1].map((s) => ({ label: `${s}ª · ${ES[OPEN[6 - s] % 12]}`, notes: [midiOf(s, 0)] })) },
          { fun: 'Las dos Mi (la 6ª y la 1ª) son la misma nota, una dos octavas más aguda que la otra. Lo que aprendas en una sirve para la otra.' }),
        P('¿Quedó claro?', quiz(
          Q('¿Cuál es la 6ª cuerda?', ['La más fina', 'La más gruesa', 'La del medio'], 1, 'La 6ª es la gruesa: la más grave.'),
          Q('¿Qué nota suena en la 2ª cuerda al aire?', ['Sol (G)', 'Si (B)', 'Mi (E)'], 1, 'Mi La Re Sol **Si** Mi: la 2ª es Si.'),
          Q('¿Qué cuerdas son Mi?', ['La 6ª y la 1ª', 'La 5ª y la 4ª', 'Solo la 6ª'], 0, 'Las dos de los extremos: Mi grave y Mi agudo.'))),
        P('A tocar',
          { p: 'Toca cada cuerda al aire cuando la nota llega a la línea. Sin apuro: lo importante es tocar **una sola cuerda** por vez.' },
          ex('aire1', 'Cuerdas al aire', { bpm: 70, unit: 2, seq: '6.0 5.0 4.0 3.0 2.0 1.0 | 1.0 2.0 3.0 4.0 5.0 6.0' }),
          ex('aire2', 'Una por tiempo', { bpm: 80, seq: '6.0 6.0 5.0 5.0 4.0 4.0 3.0 3.0 | 2.0 2.0 1.0 1.0 2.0 3.0 4.0 5.0 | 6.0:4' }, 'Un poco más rápido.')),
      ] },
      { id: 'l-tab', title: 'Leer tablatura y la autopista', min: 5, pages: [
        P('La tablatura en 30 segundos',
          { p: 'La **tablatura** (tab) dibuja las 6 cuerdas como 6 líneas. La de **abajo es la 6ª** (la gruesa). Ojo: es al revés de como la ves cuando miras la guitarra desde arriba, como si la guitarra estuviera acostada mirándote.' },
          { p: 'Cada número es **el traste** que aprietas. **0** = al aire. Se lee de izquierda a derecha.' },
          { tab: { seq: '6.0 6.3 6.5 6.3 | 5.0 5.2 5.3 5.2' } },
          { listenSeq: { bpm: 90, seq: '6.0 6.3 6.5 6.3 | 5.0 5.2 5.3 5.2' }, label: 'Escuchar' }),
        P('La autopista de Galpon',
          { p: 'En el juego las notas vienen hacia la **línea blanca**; cada cuerda tiene su color: la 6ª **roja**, la 5ª **amarilla**, la 4ª **azul**, la 3ª **naranja**, la 2ª **verde** y la 1ª **violeta**. El número es el traste.' },
          { p: 'Abajo ves el **diapasón**: se ilumina dónde va el dedo. Con la tecla **V** cambias entre tablatura (2D) y mástil en 3D.' },
          { tip: 'Si te pierdes, baja la **Velocidad** (abajo, o con las flechas ↑ ↓). Tocar bien despacio te hace rápido; tocar rápido mal, no.' }),
        P('Pregunta rápida', quiz(
          Q('En una tab, ¿qué significa un 3 en la línea de abajo?', ['La 1ª cuerda, traste 3', 'La 6ª cuerda, traste 3', 'Tres veces la cuerda 6'], 1, 'La línea de abajo es la 6ª cuerda y el número es el traste.'),
          Q('¿Y un 0?', ['No tocar', 'Cuerda al aire', 'Traste 10'], 1, 'Cero es la cuerda al aire, sin apretar.'))),
        P('Tu primera melodía',
          { tab: { seq: '6.0 6.3 6.5 6.3 | 5.0 5.2 5.3 5.2 | 5.0 6.5 6.3 6.0 | 6.0:4' } },
          ex('mel1', 'Melodía en las cuerdas graves', { bpm: 70, seq: '6.0 6.3 6.5 6.3 | 5.0 5.2 5.3 5.2 | 5.0 6.5 6.3 6.0 | 6.0:4', repeat: 2 })),
      ] },
      { id: 'l-dedos', title: 'La mano izquierda', min: 6, pages: [
        P('Dedos con número',
          { p: 'Los dedos de la mano que aprieta tienen número: **1** índice, **2** medio, **3** anular, **4** meñique. El pulgar va **detrás del mástil**, relajado.' },
          { p: 'La regla de oro: **un dedo por traste**. En los trastes 1 a 4, el 1 va al traste 1, el 2 al 2, y así.' }),
        P('Que suene limpio',
          { p: 'Aprieta con la **punta** del dedo, bien **pegado al traste** (justo antes, no encima). Así necesitas poca fuerza y no zumba.' },
          { tip: '¿Zumba? Acerca el dedo al traste. ¿Suena apagado? Estás tocando otra cuerda con la yema: arquea más el dedo.' },
          { fun: 'Al principio duelen las yemas. En 2 o 3 semanas se forman callos y ya no. Mejor 15 minutos todos los días que 2 horas un domingo.' }),
        P('La araña',
          { p: 'El ejercicio clásico para despertar los dedos: 1-2-3-4 en cada cuerda, trastes 1 a 4. Deja cada dedo apoyado hasta que lo necesites.' },
          { tab: { seq: '6.1 6.2 6.3 6.4 | 5.1 5.2 5.3 5.4' } },
          ex('arana1', 'La araña, subiendo', { bpm: 60, seq: '6.1 6.2 6.3 6.4 | 5.1 5.2 5.3 5.4 | 4.1 4.2 4.3 4.4 | 3.1 3.2 3.3 3.4 | 2.1 2.2 2.3 2.4 | 1.1 1.2 1.3 1.4' }),
          ex('arana2', 'Ida y vuelta', { bpm: 72, unit: 0.5, seq: '6.1 6.2 6.3 6.4 5.1 5.2 5.3 5.4 | 4.1 4.2 4.3 4.4 3.1 3.2 3.3 3.4 | 2.1 2.2 2.3 2.4 1.1 1.2 1.3 1.4 | 1.4 1.3 1.2 1.1 2.4 2.3 2.2 2.1 | 3.4 3.3 3.2 3.1 4.4 4.3 4.2 4.1 | 5.4 5.3 5.2 5.1 6.4 6.3 6.2 6.1 | 6.1:4' }, 'Dos notas por tiempo.')),
      ] },
      { id: 'l-pulso', title: 'El pulso: tocar a tiempo', min: 6, pages: [
        P('El corazón de la música',
          { p: 'El **pulso** es el latido de la canción: lo que marcas con el pie sin pensarlo. Se agrupa en **compases**, casi siempre de 4: «**1** 2 3 4, **1** 2 3 4».' },
          { p: 'Una **negra** dura un pulso. Una **corchea** dura medio: dos por pulso, y se cuenta «1 y 2 y 3 y 4 y».' },
          { listen: [{ label: 'Negras', seq: { bpm: 90, seq: '6.0 6.0 6.0 6.0' } }, { label: 'Corcheas', seq: { bpm: 90, unit: 0.5, seq: '6.0 6.0 6.0 6.0 6.0 6.0 6.0 6.0' } }] },
          { tip: 'El click del ejercicio marca el pulso; el primero de cada compás suena más agudo. Cuenta en voz alta: parece tonto, funciona.' }),
        P('Cuentas', quiz(
          Q('¿Cuántas corcheas entran en un compás de 4?', ['4', '8', '16'], 1, 'Dos por pulso, cuatro pulsos: 8.'),
          Q('¿Cuántos pulsos dura una blanca?', ['1', '2', '4'], 1, 'La blanca dura 2 negras. La redonda, 4.'))),
        P('Con batería',
          ex('pulso1', 'Negras en Mi', { bpm: 80, drums: 'rock', seq: '6.0 6.0 6.0 6.0 | 6.0 6.0 6.0 6.0 | 5.0 5.0 5.0 5.0 | 6.0 6.0 6.0 6.0', repeat: 2 }),
          ex('pulso2', 'Corcheas', { bpm: 80, drums: 'rock', unit: 0.5, seq: '6.0 6.0 6.0 6.0 6.0 6.0 6.0 6.0 | 5.0 5.0 5.0 5.0 5.0 5.0 5.0 5.0 | 4.0 4.0 4.0 4.0 4.0 4.0 4.0 4.0 | 5.0 5.0 5.0 5.0 6.0:2', repeat: 2 }, 'Toca en el «1 y 2 y».')),
      ] },
    ] },

    { id: 'u2', title: 'Notas y diapasón', desc: 'Las 12 notas, dónde están y cómo encontrarlas.', lessons: [
      { id: 'l-12notas', title: 'Las 12 notas', min: 7, pages: [
        P('Solo hay 12',
          { p: 'Toda la música occidental usa **12 notas** que se repiten una y otra vez:' },
          { p: '**Do · Do# · Re · Re# · Mi · Fa · Fa# · Sol · Sol# · La · La# · Si** … y vuelve a empezar en Do, más agudo.' },
          { p: 'En inglés: **C D E F G A B** son Do Re Mi Fa Sol La Si. Vas a ver las dos formas, así que conviene saber ambas.' }),
        P('Trastes y semitonos',
          { p: 'Cada **traste** sube **un semitono** (la distancia más chica). Dos trastes son **un tono**.' },
          { p: 'El **#** (sostenido) sube un semitono; el **b** (bemol) lo baja. Do# y Reb son el mismo sonido con dos nombres.' },
          { tip: 'Entre **Mi-Fa** y **Si-Do** no hay nota en el medio: están a un solo traste. Todas las demás naturales están a dos.' },
          { fret: { to: 12, marks: Array.from({ length: 13 }, (_, f) => ({ s: 6, f, label: EN[(4 + f) % 12], dim: EN[(4 + f) % 12].includes('#') })) } },
          { fun: 'El traste 12 tiene doble punto porque ahí la cuerda está justo en la mitad: suena la misma nota una octava más aguda.' }),
        P('A ver', quiz(
          Q('¿Qué nota viene después de Mi?', ['Mi#', 'Fa', 'Fa#'], 1, 'Entre Mi y Fa no hay nada: están a un semitono.'),
          Q('Dos trastes son…', ['Un semitono', 'Un tono', 'Una octava'], 1, 'Un traste = semitono; dos = un tono.'),
          Q('Fa# es lo mismo que…', ['Solb', 'Fab', 'Sol#'], 0, 'Fa# y Solb suenan igual.'),
          Q('¿Qué letra es Sol?', ['E', 'G', 'A'], 1, 'C D E F G A B = Do Re Mi Fa Sol La Si.'))),
        P('La escala cromática',
          { p: 'Todas las notas, una por traste, en la 6ª cuerda. Fíjate cómo vuelve a Mi en el 12.' },
          ex('crom1', 'Subir por la 6ª', { bpm: 80, seq: '6.0 6.1 6.2 6.3 | 6.4 6.5 6.6 6.7 | 6.8 6.9 6.10 6.11 | 6.12:4' }),
          ex('crom2', 'Subir y bajar', { bpm: 90, unit: 0.5, seq: '6.0 6.1 6.2 6.3 6.4 6.5 6.6 6.7 | 6.8 6.9 6.10 6.11 6.12 6.11 6.10 6.9 | 6.8 6.7 6.6 6.5 6.4 6.3 6.2 6.1 | 6.0:4' })),
      ] },
      { id: 'l-notas65', title: 'Las notas en las cuerdas 6 y 5', min: 7, pages: [
        P('Por qué estas dos',
          { p: 'Casi todos los acordes de rock y las cejillas se nombran por la nota que tocas en la **6ª** o la **5ª** cuerda. Con estas dos ya te mueves por todo el mástil.' },
          { p: 'Las **naturales** (sin #) en la 6ª: **Mi 0 · Fa 1 · Sol 3 · La 5 · Si 7 · Do 8 · Re 10 · Mi 12**.' },
          { fret: { to: 12, marks: [[0, 'E'], [1, 'F'], [3, 'G'], [5, 'A'], [7, 'B'], [8, 'C'], [10, 'D'], [12, 'E']].map(([f, l]) => ({ s: 6, f, label: l })) } }),
        P('La 5ª cuerda',
          { p: 'En la 5ª: **La 0 · Si 2 · Do 3 · Re 5 · Mi 7 · Fa 8 · Sol 10 · La 12**.' },
          { fret: { to: 12, marks: [[0, 'A'], [2, 'B'], [3, 'C'], [5, 'D'], [7, 'E'], [8, 'F'], [10, 'G'], [12, 'A']].map(([f, l]) => ({ s: 5, f, label: l })) } },
          { tip: 'Usa los puntos del mástil como señales: en la 6ª, **Sol en el 3, La en el 5, Si en el 7**. En la 5ª, **Re en el 5, Mi en el 7**.' }),
        P('Adivina la nota', { gen: 'fretNote', strings: [6, 5], count: 8 }),
        P('Tócalas',
          ex('nat6', 'Naturales en la 6ª', { bpm: 70, seq: '6.0 6.1 6.3 6.5 | 6.7 6.8 6.10 6.12 | 6.10 6.8 6.7 6.5 | 6.3 6.1 6.0:2' }),
          ex('nat5', 'Naturales en la 5ª', { bpm: 70, seq: '5.0 5.2 5.3 5.5 | 5.7 5.8 5.10 5.12 | 5.10 5.8 5.7 5.5 | 5.3 5.2 5.0:2' })),
      ] },
      { id: 'l-octavas', title: 'Octavas: la misma nota más arriba', min: 6, pages: [
        P('Mismo nombre, más agudo',
          { p: 'Una **octava** es la misma nota al doble de frecuencia: suena «igual pero más arriba». Por eso tiene el mismo nombre.' },
          { listen: [{ label: 'Sol y su octava', notes: [midiOf(6, 3), midiOf(4, 5)] }, { label: 'Juntas', notes: [[midiOf(6, 3), midiOf(4, 5)]], together: true }] },
          { p: 'La forma: **dos cuerdas más arriba y dos trastes más adelante**. Desde la 6ª cuerda traste 3 (Sol) llegas a la 4ª traste 5 (Sol).' },
          { fret: { to: 8, marks: [{ s: 6, f: 3, label: 'G', root: true }, { s: 4, f: 5, label: 'G' }, { s: 5, f: 3, label: 'C', root: true }, { s: 3, f: 5, label: 'C' }] } }),
        P('La trampa de la 2ª cuerda',
          { p: 'La 2ª cuerda (Si) está afinada un traste más abajo que las demás. Si la octava **cruza a la 2ª o la 1ª**, súmale un traste: desde la 4ª traste 2 (Mi) llegas a la 2ª traste **5**.' },
          { tip: 'Por eso los acordes cambian de forma en las cuerdas agudas. No es capricho: es la afinación.' }),
        P('Prueba', quiz(
          Q('Toco Do en la 5ª traste 3. ¿Dónde está su octava?', ['3ª cuerda traste 5', '4ª cuerda traste 5', '3ª cuerda traste 3'], 0, 'Dos cuerdas arriba, dos trastes adelante.'),
          Q('¿Qué tiene de especial la 2ª cuerda?', ['Es más gruesa', 'Está afinada un traste más abajo que el patrón', 'No tiene octavas'], 1, 'Por eso, al cruzarla, la octava se corre un traste.'))),
        P('Melodía en octavas',
          { p: 'Tocar la melodía con su octava a la vez suena gordo (lo usan mucho en jazz y en rock). Apaga la 5ª cuerda con la yema del dedo 1.' },
          ex('oct1', 'Saltos de octava', { bpm: 70, seq: '6.3 4.5 6.5 4.7 | 5.3 3.5 5.5 3.7 | 6.3 4.5 6.5 4.7 | 6.3:4' }),
          ex('oct2', 'Melodía en octavas', { bpm: 70, seq: '6.3+4.5 6.5+4.7 6.7+4.9 6.8+4.10 | 6.7+4.9:2 6.5+4.7:2 | 6.3+4.5:4', repeat: 2 })),
      ] },
      { id: 'l-notas4321', title: 'Las cuerdas 4, 3, 2 y 1', min: 6, pages: [
        P('Ya sabes más de lo que crees',
          { p: 'La **1ª cuerda** tiene las mismas notas que la 6ª (las dos son Mi). Gratis.' },
          { p: 'En la **4ª** (Re): Re 0 · Mi 2 · Fa 3 · Sol 5 · La 7 · Si 9 · Do 10 · Re 12. En la **3ª** (Sol): Sol 0 · La 2 · Si 4 · Do 5 · Re 7 · Mi 9 · Fa 10 · Sol 12.' },
          { p: 'En la **2ª** (Si): Si 0 · Do 1 · Re 3 · Mi 5 · Fa 6 · Sol 8 · La 10 · Si 12.' },
          { tip: 'Truco: si sabes la 6ª y la 5ª, encuentras cualquier nota con la forma de octava.' }),
        P('Adivina la nota', { gen: 'fretNote', strings: [4, 3, 2, 1], count: 8 }),
        P('Todos los Do',
          { p: 'Busca todos los **Do (C)** de los primeros 12 trastes. Los de la guitarra completa: 5ª-3, 2ª-1, 4ª-10, 3ª-5, 1ª-8 y 6ª-8.' },
          { fret: { to: 12, marks: [[5, 3], [2, 1], [4, 10], [3, 5], [1, 8], [6, 8]].map(([s, f]) => ({ s, f, label: 'C', root: true })) } },
          ex('todosdo', 'Cazar todos los Do', { bpm: 60, unit: 2, seq: '5.3 2.1 3.5 1.8 | 4.10 6.8 5.3:4' })),
      ] },
    ] },

    { id: 'u3', title: 'Primeros acordes', desc: 'Mi menor, La menor, Do, Sol, Re y el primer blues.', lessons: [
      { id: 'l-acorde', title: 'Qué es un acorde', min: 6, pages: [
        P('Tres notas juntas',
          { p: 'Un **acorde** son varias notas a la vez. El más común, la **tríada**, tiene tres: la **raíz** (le da el nombre), la **tercera** y la **quinta**.' },
          { p: 'La tercera decide el carácter: **mayor** suena luminoso, **menor** suena melancólico.' },
          { listen: [{ label: 'Mi mayor (E)', chord: 'E' }, { label: 'Mi menor (Em)', chord: 'Em' }] }),
        P('Cómo leer un diagrama',
          { p: 'Las líneas verticales son las cuerdas (6ª a la izquierda), las horizontales los trastes. El número es **qué dedo** usar. **○** = al aire; **×** = no tocar esa cuerda.' },
          { chords: ['Em', 'E'] },
          { p: 'Mira: entre Em y E cambia **un solo dedo** (el 1 en la 3ª cuerda). Esa nota es la tercera: Sol en Em, Sol# en E.' }),
        P('Una pregunta', quiz(
          Q('¿Qué nota le da el nombre al acorde?', ['La tercera', 'La raíz', 'La quinta'], 1, 'La raíz: en Mi menor, Mi.'),
          Q('¿Qué cambia entre mayor y menor?', ['La raíz', 'La tercera', 'La cantidad de cuerdas'], 1, 'La tercera mayor (4 semitonos) o menor (3).'))),
        P('Rasguea',
          { p: 'Rasguea hacia abajo, todas las cuerdas, una vez por pulso. Deja sonar.' },
          ex('em1', 'Mi menor', { bpm: 70, seq: '[Em] [Em] [Em] [Em] | [Em] [Em] [Em] [Em]' }),
          ex('em2', 'Em y E', { bpm: 70, seq: '[Em]:2 [Em]:2 | [E]:2 [E]:2 | [Em] [Em] [Em] [Em] | [E] [E] [E] [E]', repeat: 2 })),
      ] },
      { id: 'l-emam', title: 'Mi menor y La menor', min: 5, pages: [
        P('La menor',
          { chords: ['Em', 'Am'] },
          { p: '**Am** es casi Em corrido una cuerda: los dedos 2 y 3 se mueven **juntos**, como un bloque, a las cuerdas 4 y 3. Y suma el dedo 1 en la 2ª cuerda.' },
          { listen: [{ label: 'Em → Am', chords: ['Em', 'Am'] }] },
          { tip: 'En Am no toques la 6ª cuerda: empieza el rasgueo en la 5ª.' }),
        P('A tocar',
          ex('emam1', 'Un rasgueo por compás', { bpm: 70, seq: '[Em]:4 | [Am]:4 | [Em]:4 | [Am]:4', repeat: 2 }),
          ex('emam2', 'Cuatro por compás', { bpm: 75, drums: 'rock', seq: '[Em] [Em] [Em] [Em] | [Am] [Am] [Am] [Am]', repeat: 4 })),
      ] },
      { id: 'l-gcd', title: 'Sol, Do y Re', min: 8, pages: [
        P('Los tres de miles de canciones',
          { chords: ['G', 'C', 'D'] },
          { p: 'Con **Sol (G), Do (C) y Re (D)** puedes tocar una cantidad absurda de canciones de folk, rock y pop. Pronto vas a entender por qué funcionan juntos.' },
          { listen: [{ label: 'G – C – D', chords: ['G', 'C', 'D', 'G'] }] }),
        P('Detalles que importan',
          { p: '**G**: los dedos 2 y 1 en las graves, el 3 en la 1ª. Suena con las 6 cuerdas.' },
          { p: '**C**: escalera de dedos 3-2-1. No toques la 6ª.' },
          { p: '**D**: un triángulo en las 3 agudas. Rasguea solo desde la 4ª cuerda.' },
          { tip: 'Si una cuerda no suena, toca el acorde nota por nota para ver cuál dedo la está apagando.' }),
        P('A tocar',
          ex('gcd1', 'Uno por compás', { bpm: 65, seq: '[G]:4 | [C]:4 | [D]:4 | [G]:4', repeat: 2 }),
          ex('gcd2', 'Dos por compás', { bpm: 70, drums: 'rock', seq: '[G]:2 [G]:2 | [C]:2 [C]:2 | [D]:2 [D]:2 | [G]:2 [G]:2', repeat: 3 })),
      ] },
      { id: 'l-cambios', title: 'Cambiar sin cortar', min: 6, pages: [
        P('Los secretos de los cambios',
          { p: '**Mira adelante**: piensa el acorde que sigue antes de llegar.' },
          { p: '**Dedos guía**: si un dedo se repite en los dos acordes, no lo levantes. De C a Am, los dedos 1 y 2 se quedan donde están.' },
          { p: '**El último tiempo es tuyo**: en el pulso 4 puedes rasguear las cuerdas al aire mientras mueves la mano. Nadie lo nota, y la música no se corta.' },
          { chords: ['C', 'Am', 'Em', 'G'] }),
        P('La progresión de los 50',
          { p: 'G – Em – C – D: la de cientos de baladas de los años 50 y 60.' },
          { listen: [{ label: 'Escuchar', chords: ['G', 'Em', 'C', 'D'] }] },
          ex('cambios1', 'G Em C D', { bpm: 70, drums: 'soft', seq: '[G] [G] [G] [G] | [Em] [Em] [Em] [Em] | [C] [C] [C] [C] | [D] [D] [D] [D]', repeat: 2 }),
          ex('cambios2', 'C Am Em G', { bpm: 75, drums: 'soft', seq: '[C]:2 [C]:2 | [Am]:2 [Am]:2 | [Em]:2 [Em]:2 | [G]:2 [G]:2', repeat: 3 })),
      ] },
      { id: 'l-blues', title: 'Séptimas y tu primer blues', min: 8, pages: [
        P('Un poquito de tensión',
          { p: 'Un acorde de **séptima** (E7, A7, B7) le suma una cuarta nota a la tríada: la **séptima menor**. Suena con tensión, con ganas de moverse. Es el sonido del blues.' },
          { chords: ['E7', 'A7', 'B7'] },
          { listen: [{ label: 'E y E7', chords: ['E', 'E7'] }] }),
        P('El blues de 12 compases',
          { p: 'La forma más famosa del blues y del rock and roll: 12 compases que se repiten.' },
          { p: '**E7** ×4 · **A7** ×2 · **E7** ×2 · **B7** · **A7** · **E7** · **B7**' },
          { fun: 'Esta misma forma está detrás de cientos de clásicos del rock and roll de los 50. Cuando la reconozcas, vas a empezar a oírla en todos lados.' }),
        P('A tocar',
          ex('blues1', 'Blues en Mi (uno por compás)', { bpm: 80, drums: 'rock', seq: '[E7]:4 [E7]:4 [E7]:4 [E7]:4 | [A7]:4 [A7]:4 [E7]:4 [E7]:4 | [B7]:4 [A7]:4 [E7]:4 [B7]:4' }),
          ex('blues2', 'Blues en Mi (cuatro por compás)', { bpm: 85, drums: 'rock', seq: '[E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] | [A7] [A7] [A7] [A7] [A7] [A7] [A7] [A7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] [E7] | [B7] [B7] [B7] [B7] [A7] [A7] [A7] [A7] [E7] [E7] [E7] [E7] [B7] [B7] [B7] [B7]' })),
      ] },
    ] },

    { id: 'u4', title: 'Ritmo y rasgueo', desc: 'Abajo y arriba, el rasgueo de siempre y el bajo alternado.', lessons: [
      { id: 'l-abajoarriba', title: 'Abajo y arriba', min: 5, pages: [
        P('La mano derecha es un péndulo',
          { p: 'La mano que rasguea va y viene sin parar: **abajo en los números** (1, 2, 3, 4) y **arriba en los «y»**.' },
          { p: 'Aunque no toques en algún golpe, **la mano sigue moviéndose**. Así nunca pierdes el pulso.' },
          { tip: 'Agarra la púa suave, con poca punta afuera, y mueve la muñeca, no todo el brazo. Hacia arriba basta con tocar 3 o 4 cuerdas agudas.' }),
        P('Corcheas',
          ex('ab1', 'Em en corcheas', { bpm: 70, drums: 'rock', unit: 0.5, seq: '[Em] [Em] [Em] [Em] [Em] [Em] [Em] [Em]', repeat: 4 }),
          ex('ab2', 'G y C en corcheas', { bpm: 70, drums: 'rock', unit: 0.5, seq: '[G] [G] [G] [G] [G] [G] [G] [G] | [C] [C] [C] [C] [C] [C] [C] [C]', repeat: 3 })),
      ] },
      { id: 'l-rasgueo', title: 'El rasgueo de todas las canciones', min: 7, pages: [
        P('Abajo, abajo-arriba, arriba-abajo-arriba',
          { p: 'El patrón que vas a escuchar en mil canciones acústicas:' },
          { p: '**↓ · ↓↑ · ↑↓↑** — se cuenta «**1**, **2 y**, **y 4 y**». En el 3 la mano baja **sin tocar** (por eso hay dos «arriba» seguidos).' },
          { listen: [{ label: 'Escuchar', seq: { bpm: 85, seq: '[G]:1 [G]:0.5 [G]:1 [G]:0.5 [G]:0.5 [G]:0.5', repeat: 2 } }] },
          { tip: 'Dilo en voz alta mientras tocas: «a-ba-jo, a-ba-jo-ar-ri-ba…». Ridículo pero efectivo.' }),
        P('A tocar',
          ex('ras1', 'Patrón en G', { bpm: 70, drums: 'soft', seq: '[G]:1 [G]:0.5 [G]:1 [G]:0.5 [G]:0.5 [G]:0.5', repeat: 4 }),
          ex('ras2', 'Patrón con cambios', { bpm: 75, drums: 'soft', seq: '[G]:1 [G]:0.5 [G]:1 [G]:0.5 [G]:0.5 [G]:0.5 | [C]:1 [C]:0.5 [C]:1 [C]:0.5 [C]:0.5 [C]:0.5 | [D]:1 [D]:0.5 [D]:1 [D]:0.5 [D]:0.5 [D]:0.5 | [Em]:1 [Em]:0.5 [Em]:1 [Em]:0.5 [Em]:0.5 [Em]:0.5', repeat: 2 })),
      ] },
      { id: 'l-bajoacorde', title: 'Bajo y acorde (en 3 y en 4)', min: 7, pages: [
        P('El bajo primero',
          { p: 'Una forma linda de acompañar: tocas **la raíz sola** en el pulso 1 y después **el acorde en las cuerdas agudas**. Suena a piano, a folk, a vals.' },
          { p: 'En **3/4** (vals) se cuenta «**1** 2 3»: bajo, acorde, acorde.' },
          { listen: [{ label: 'Vals en G y C', seq: { bpm: 110, beats: 3, seq: '6.3 4.0+3.0+2.0+1.3 4.0+3.0+2.0+1.3 | 5.3 4.2+3.0+2.1+1.0 4.2+3.0+2.1+1.0' } }] }),
        P('A tocar',
          ex('vals1', 'Vals: G y C', { bpm: 100, beats: 3, seq: '6.3 4.0+3.0+2.0+1.3 4.0+3.0+2.0+1.3 | 6.3 4.0+3.0+2.0+1.3 4.0+3.0+2.0+1.3 | 5.3 4.2+3.0+2.1+1.0 4.2+3.0+2.1+1.0 | 5.3 4.2+3.0+2.1+1.0 4.2+3.0+2.1+1.0', repeat: 2 }),
          ex('bajo4', 'En 4: bajo alternado', { bpm: 80, seq: '6.3 4.0+3.0+2.0+1.3 4.0 4.0+3.0+2.0+1.3 | 5.3 4.2+3.0+2.1+1.0 4.2 4.2+3.0+2.1+1.0 | 4.0 3.2+2.3+1.2 5.0 3.2+2.3+1.2 | 6.3 4.0+3.0+2.0+1.3 4.0 4.0+3.0+2.0+1.3', repeat: 2 }, 'El bajo cambia de cuerda en el pulso 3.')),
      ] },
    ] },

    { id: 'u5', title: 'Escalas e improvisación', desc: 'Intervalos, la escala mayor, la pentatónica y tus primeros licks.', lessons: [
      { id: 'l-intervalos', title: 'Intervalos: distancias entre notas', min: 8, pages: [
        P('Todo es distancia',
          { p: 'Un **intervalo** es la distancia entre dos notas, en semitonos (trastes). Cada uno tiene su color:' },
          { listen: [
            { label: '2ª menor · 1', notes: [52, 53] }, { label: '2ª mayor · 2', notes: [52, 54] }, { label: '3ª menor · 3', notes: [52, 55] },
            { label: '3ª mayor · 4', notes: [52, 56] }, { label: '4ª justa · 5', notes: [52, 57] }, { label: 'Tritono · 6', notes: [52, 58] },
            { label: '5ª justa · 7', notes: [52, 59] }, { label: '6ª mayor · 9', notes: [52, 61] }, { label: '7ª menor · 10', notes: [52, 62] }, { label: 'Octava · 12', notes: [52, 64] }] },
          { fun: 'La 2ª menor es la del tiburón de la película. La 5ª justa abre el tema de Star Wars. El tritono, en la Edad Media, le decían «el diablo en la música».' }),
        P('Las importantes',
          { p: '**3ª menor y mayor**: deciden si algo suena triste o alegre. **5ª justa**: la base del power chord. **Octava**: la misma nota.' },
          { tip: 'Para entrenar el oído, asocia cada intervalo con el comienzo de una canción que conozcas.' }),
        P('Entrena el oído', { gen: 'earInterval', count: 6 }),
        P('Tócalos',
          ex('int1', 'Desde Do', { bpm: 70, unit: 2, seq: '5.3 5.5 | 5.3 4.2 | 5.3 4.3 | 5.3 4.5 | 5.3 3.5:6' }, 'Do-Re, Do-Mi, Do-Fa, Do-Sol, Do-Do.')),
      ] },
      { id: 'l-mayor', title: 'La escala mayor', min: 8, pages: [
        P('La fórmula',
          { p: 'La escala mayor es la de **Do Re Mi Fa Sol La Si Do**. Lo que la define es el orden de las distancias:' },
          { p: '**Tono · Tono · Semitono · Tono · Tono · Tono · Semitono**' },
          { p: 'Empezando en Do no hace falta ningún #. Empezando en Sol, para respetar la fórmula, aparece un **Fa#**.' },
          { fun: 'Los nombres Do, Re, Mi… vienen de las primeras sílabas de los versos de un himno en latín del siglo XI. «Do» antes se llamaba «Ut».' }),
        P('Sol mayor en el mástil',
          { fret: { from: 0, to: 6, marks: [[6, 3, 'G', 1], [6, 5, 'A'], [5, 2, 'B'], [5, 3, 'C'], [5, 5, 'D'], [4, 2, 'E'], [4, 4, 'F#'], [4, 5, 'G', 1], [3, 2, 'A'], [3, 4, 'B'], [3, 5, 'C'], [2, 3, 'D'], [2, 5, 'E'], [1, 2, 'F#'], [1, 3, 'G', 1]].map(([s, f, label, r]) => ({ s, f, label, root: !!r })) } },
          { p: 'En rojo, las Sol (la raíz). Mano en la zona de los trastes 2 a 5.' },
          { listenSeq: { bpm: 140, seq: '6.3 6.5 5.2 5.3 5.5 4.2 4.4 4.5' }, label: 'Escuchar una octava' }),
        P('A tocar',
          ex('may1', 'Una octava', { bpm: 70, seq: '6.3 6.5 5.2 5.3 | 5.5 4.2 4.4 4.5 | 4.4 4.2 5.5 5.3 | 5.2 6.5 6.3:2' }),
          ex('may2', 'Dos octavas', { bpm: 80, unit: 0.5, seq: '6.3 6.5 5.2 5.3 5.5 4.2 4.4 4.5 | 3.2 3.4 3.5 2.3 2.5 1.2 1.3:1 | 1.2 2.5 2.3 3.5 3.4 3.2 4.5 4.4 | 4.2 5.5 5.3 5.2 6.5 6.3:1.5' })),
      ] },
      { id: 'l-penta', title: 'La pentatónica menor', min: 8, pages: [
        P('La escala del rock',
          { p: 'Solo **5 notas**: La, Do, Re, Mi, Sol (en La menor). Le saca las dos notas «peligrosas» a la escala, así que es casi imposible que suene mal. La usan el blues, el rock, el pop… todos.' },
          { fret: { from: 4, to: 9, marks: [[6, 5, 'A', 1], [6, 8, 'C'], [5, 5, 'D'], [5, 7, 'E'], [4, 5, 'G'], [4, 7, 'A', 1], [3, 5, 'C'], [3, 7, 'D'], [2, 5, 'E'], [2, 8, 'G'], [1, 5, 'A', 1], [1, 8, 'C']].map(([s, f, label, r]) => ({ s, f, label, root: !!r })) } },
          { p: 'Es la famosa **«caja 1»**, en el traste 5. Dedo **1** en el traste 5, dedo **3** (o **4**) en el 7 u 8.' }),
        P('A tocar',
          ex('pen1', 'Subir', { bpm: 70, seq: '6.5 6.8 5.5 5.7 | 4.5 4.7 3.5 3.7 | 2.5 2.8 1.5 1.8 | 1.5:4' }),
          ex('pen2', 'Subir y bajar', { bpm: 85, unit: 0.5, drums: 'rock', seq: '6.5 6.8 5.5 5.7 4.5 4.7 3.5 3.7 | 2.5 2.8 1.5 1.8 1.5 2.8 2.5 3.7 | 3.5 4.7 4.5 5.7 5.5 6.8 6.5:1', repeat: 2 })),
      ] },
      { id: 'l-licks', title: 'Tus primeros licks', min: 7, pages: [
        P('Frases, no escalas',
          { p: 'Un **lick** es una frase corta, como una palabra en un idioma. La escala es el alfabeto; los licks son lo que dices con ella.' },
          { p: 'Truco de improvisador: **pregunta y respuesta**. Toca una frase que «quede abierta» y después una que «cierre» en La.' },
          { tab: { seq: '2.8 2.5 3.7 3.5 | 4.7:4' } },
          { listenSeq: { bpm: 90, seq: '2.8 2.5 3.7 3.5 | 4.7:4' }, label: 'Escuchar el lick 1' }),
        P('A tocar',
          ex('lick1', 'Lick 1: bajada', { bpm: 80, drums: 'rock', seq: '2.8 2.5 3.7 3.5 | 4.7:4 | 2.8 2.5 3.7 3.5 | 4.7 4.5 6.5:2', repeat: 2 }),
          ex('lick2', 'Pregunta y respuesta', { bpm: 80, drums: 'rock', seq: '1.5 1.8 1.5 2.8 | 2.5:2 - - | 3.7 3.5 4.7 4.5 | 4.7:4', repeat: 2 }),
          { tip: 'Cuando te salgan, inventa los tuyos: misma caja, mismas notas, cambia el orden y el ritmo. No hay respuesta incorrecta.' }),
      ] },
      { id: 'l-bluesescala', title: 'La escala de blues', min: 6, pages: [
        P('La nota azul',
          { p: 'Agrégale a la pentatónica **una nota más**: la **quinta disminuida** (Mib en La). Es la «**blue note**»: una nota de paso, con mugre, que hace sonar todo a blues.' },
          { fret: { from: 4, to: 9, marks: [[6, 5, 'A', 1], [6, 8, 'C'], [5, 5, 'D'], [5, 6, 'Eb', 0, 1], [5, 7, 'E'], [4, 5, 'G'], [4, 7, 'A', 1], [3, 5, 'C'], [3, 7, 'D'], [3, 8, 'Eb', 0, 1], [2, 5, 'E'], [2, 8, 'G'], [1, 5, 'A', 1], [1, 8, 'C']].map(([s, f, label, r, b]) => ({ s, f, label, root: !!r, dim: !!b })) } },
          { tip: 'No te quedes en la nota azul: pásala de camino a otra.' }),
        P('A tocar',
          ex('blu1', 'Escala de blues', { bpm: 70, seq: '6.5 6.8 5.5 5.6 | 5.7 4.5 4.7 3.5 | 3.7 3.8 2.5 2.8 | 1.5:4' }),
          ex('blu2', 'Lick de blues', { bpm: 80, drums: 'rock', seq: '3.5 3.7 3.8 3.7 | 3.5 4.7:3 | 5.5 5.6 5.7 4.5 | 4.7:4', repeat: 2 })),
      ] },
    ] },

    { id: 'u6', title: 'Armonía sin miedo', desc: 'Por qué ciertos acordes van juntos, y cómo cambiar de tono.', lessons: [
      { id: 'l-grados', title: 'Los acordes de una tonalidad', min: 8, pages: [
        P('Una familia de 7 acordes',
          { p: 'Si armas un acorde sobre cada nota de la escala mayor (usando solo notas de esa escala), salen 7 acordes que «combinan». Se numeran con romanos:' },
          { p: '**I** mayor · **ii** menor · **iii** menor · **IV** mayor · **V** mayor · **vi** menor · **vii°** disminuido' },
          { p: 'En **Do**: C · Dm · Em · F · G · Am · B°. En **Sol**: G · Am · Bm · C · D · Em · F#°.' }),
        P('Por eso G, C y D',
          { p: 'G, C y D son el **I, IV y V** de Sol: los tres mayores de la familia. Y Em es el **vi**. Ahora entiendes por qué la progresión de los 50 suena tan natural.' },
          { tip: 'El **V** siempre quiere volver al **I**. Esa tensión y resolución es el motor de casi toda la música.' },
          { listen: [{ label: 'V → I (D → G)', chords: ['D', 'G'] }] }),
        P('Prueba', quiz(
          Q('¿Cuál es el V de Do?', ['F', 'G', 'Am'], 1, 'C D E F **G**: quinto grado.'),
          Q('¿Am pertenece a la tonalidad de Do?', ['Sí, es el vi', 'No'], 0, 'Am es el sexto grado de Do.'),
          Q('¿Cuál es el IV de Sol?', ['C', 'D', 'Em'], 0, 'G A B **C**.'),
          Q('En una tonalidad mayor, el ii, iii y vi son…', ['Mayores', 'Menores', 'Disminuidos'], 1, 'I, IV y V mayores; ii, iii y vi menores.'))),
        P('Tócalo',
          ex('grados1', 'I – IV – V – I en Sol', { bpm: 75, drums: 'soft', seq: '[G] [G] [G] [G] | [C] [C] [C] [C] | [D] [D] [D] [D] | [G]:4', repeat: 2 })),
      ] },
      { id: 'l-progresiones', title: 'Progresiones famosas', min: 7, pages: [
        P('Recetas que funcionan',
          { p: '**I – V – vi – IV**: los «cuatro acordes del pop». En Sol: G – D – Em – C.' },
          { p: '**vi – IV – I – V**: la misma, empezando en el menor. Más dramática: Em – C – G – D.' },
          { p: '**I – vi – IV – V**: la de los 50 (ya la tocaste).' },
          { p: '**ii – V – I**: la base del jazz. En Do: Dm – G – C.' },
          { listen: [{ label: 'I – V – vi – IV', chords: ['G', 'D', 'Em', 'C'] }, { label: 'vi – IV – I – V', chords: ['Em', 'C', 'G', 'D'] }, { label: 'ii – V – I', chords: ['Dm', 'G', 'C'] }] }),
        P('A tocar',
          ex('prog1', 'G – D – Em – C', { bpm: 75, drums: 'soft', seq: '[G]:1 [G]:0.5 [G]:1 [G]:0.5 [G]:0.5 [G]:0.5 | [D]:1 [D]:0.5 [D]:1 [D]:0.5 [D]:0.5 [D]:0.5 | [Em]:1 [Em]:0.5 [Em]:1 [Em]:0.5 [Em]:0.5 [Em]:0.5 | [C]:1 [C]:0.5 [C]:1 [C]:0.5 [C]:0.5 [C]:0.5', repeat: 2 }),
          ex('prog2', 'Dm – G – C', { bpm: 75, drums: 'soft', seq: '[Dm] [Dm] [Dm] [Dm] | [G] [G] [G] [G] | [C] [C] [C] [C] | [C]:4', repeat: 2 })),
      ] },
      { id: 'l-relativos', title: 'Mayor y menor: relativos', min: 5, pages: [
        P('Mismas notas, otro humor',
          { p: 'Do mayor y La menor usan **exactamente las mismas notas**. Son **relativos**. Lo que cambia es dónde «descansa» la música.' },
          { p: 'Para encontrar el relativo menor: el **vi** grado, o bajar **3 semitonos** desde la tónica. Sol → Mi menor. Re → Si menor.' },
          { tip: 'Por eso la pentatónica de La menor sirve para una canción en Do mayor. Mismas notas.' }),
        P('Prueba', quiz(
          Q('¿Cuál es el relativo menor de Sol?', ['Am', 'Em', 'Bm'], 1, 'Tres semitonos abajo de Sol: Mi.'),
          Q('¿Cuál es el relativo mayor de Am?', ['C', 'A', 'G'], 0, 'Am y C comparten notas.'),
          Q('¿Y el relativo menor de Re?', ['Bm', 'F#m', 'Em'], 0, 'Re → Si menor.'))),
      ] },
      { id: 'l-capo', title: 'La cejilla (capo) y transportar', min: 6, pages: [
        P('El capo sube todo',
          { p: 'El **capo** es una abrazadera que hace de «cejilla postiza»: todo suena más agudo, un semitono por traste.' },
          { p: 'Con capo en el 2, las formas de **G C D** suenan como **A D E**. Mismos dedos, otra tonalidad, ideal para acomodarte a una voz.' },
          { p: '**Transportar** es mover todos los acordes la misma distancia. Si subes 2 semitonos: G→A, C→D, Em→F#m.' }),
        P('Prueba', quiz(
          Q('Forma de G con capo en el 2. ¿Qué suena?', ['G#', 'A', 'B'], 1, 'Dos semitonos arriba de Sol: La.'),
          Q('Forma de C con capo en el 3. ¿Qué suena?', ['Mib (Eb)', 'Re (D)', 'Fa (F)'], 0, 'Do + 3 semitonos = Mib.'),
          Q('Transporto Em un tono arriba. Queda…', ['Fm', 'F#m', 'Gm'], 1, 'Un tono = 2 semitonos: Mi → Fa#.'))),
      ] },
    ] },

    { id: 'u7', title: 'Rock: power chords y riffs', desc: 'Quintas, palm mute y tu primer riff.', lessons: [
      { id: 'l-power', title: 'Power chords', min: 7, pages: [
        P('Dos notas, mucha potencia',
          { p: 'El **power chord** («5») es solo la **raíz y la quinta** (a veces con la octava). No es mayor ni menor, por eso con distorsión suena limpio y enorme.' },
          { chords: ['G5', 'C5'] },
          { p: 'La forma es **móvil**: dedo 1 en la raíz (6ª o 5ª cuerda), dedos 3 y 4 dos trastes adelante en las cuerdas siguientes. El nombre es la nota de la raíz.' },
          { tip: 'Apaga las otras cuerdas apoyando suave la yema del dedo 1. Así puedes rasguear fuerte sin miedo.' }),
        P('Muévelo',
          { listen: [{ label: 'G5 – A5 – C5 – D5', chords: ['G5', 'A5 (6ª)', 'C5', 'D5'] }] },
          ex('pow1', 'G5 A5 C5 D5', { bpm: 80, drums: 'rock', seq: '[G5]:2 [G5]:2 | [A5 (6ª)]:2 [A5 (6ª)]:2 | [C5]:2 [C5]:2 | [D5]:2 [D5]:2', repeat: 2 }),
          ex('pow2', 'En corcheas', { bpm: 90, drums: 'rock', unit: 0.5, seq: '[G5] [G5] [G5] [G5] [G5] [G5] [G5] [G5] | [F5] [F5] [F5] [F5] [F5] [F5] [F5] [F5] | [A5 (6ª)] [A5 (6ª)] [A5 (6ª)] [A5 (6ª)] [A5 (6ª)] [A5 (6ª)] [A5 (6ª)] [A5 (6ª)] | [G5] [G5] [G5] [G5] [G5]:2', repeat: 2 })),
      ] },
      { id: 'l-palm', title: 'Palm mute y galope', min: 6, pages: [
        P('Con la palma',
          { p: 'Apoya el **costado de la palma** sobre las cuerdas, justo donde salen del puente. Suena apagado y percusivo: el «chug» del metal y el punk.' },
          { tip: 'Si apoyas muy adelante, no suena la nota; muy atrás, no se apaga. Busca el punto justo moviendo la mano de a milímetros.' }),
        P('A tocar',
          ex('pm1', 'Chug en Mi', { bpm: 90, drums: 'rock', unit: 0.5, seq: '6.0m 6.0m 6.0m 6.0m 6.0m 6.0m [E5]:1 | 6.0m 6.0m 6.0m 6.0m [G5]:1 [A5 (6ª)]:1', repeat: 3 }),
          ex('pm2', 'Galope', { bpm: 80, drums: 'rock', seq: '6.0m:0.5 6.0m:0.25 6.0m:0.25 6.0m:0.5 6.0m:0.25 6.0m:0.25 6.0m:0.5 6.0m:0.25 6.0m:0.25 [E5]:1 | 6.0m:0.5 6.0m:0.25 6.0m:0.25 6.0m:0.5 6.0m:0.25 6.0m:0.25 [G5]:1 [A5 (6ª)]:1', repeat: 3 }, 'Una corchea y dos semicorcheas: «ta-ca-ta».')),
      ] },
      { id: 'l-riff', title: 'Tu primer riff', min: 6, pages: [
        P('Un riff es una idea que se repite',
          { p: 'El **riff** es la frase de guitarra que hace reconocible a una canción. Casi siempre es corto, en las cuerdas graves, y se repite.' },
          { tab: { unit: 0.5, seq: '6.0m 6.0m 6.3 6.0m 6.5 6.0m 6.6 6.5 | 6.0m 6.0m 6.3 6.0m 6.5:1 6.3:1' } },
          { listenSeq: { bpm: 100, unit: 0.5, seq: '6.0m 6.0m 6.3 6.0m 6.5 6.0m 6.6 6.5 | 6.0m 6.0m 6.3 6.0m 6.5:1 6.3:1' }, label: 'Escuchar' }),
        P('A tocar',
          ex('riff1', 'Riff en Mi', { bpm: 90, drums: 'rock', unit: 0.5, seq: '6.0m 6.0m 6.3 6.0m 6.5 6.0m 6.6 6.5 | 6.0m 6.0m 6.3 6.0m 6.5:1 6.3:1', repeat: 4 }),
          { tip: 'Ahora cambia un par de notas y hazlo tuyo. Así nacen las canciones.' }),
      ] },
    ] },

    { id: 'u8', title: 'Técnicas', desc: 'Ligados, slides, bends y vibrato.', lessons: [
      { id: 'l-ligados', title: 'Hammer-on y pull-off', min: 6, pages: [
        P('Notas sin púa',
          { p: '**Hammer-on**: tocas una nota y, sin volver a pulsar, **martillas** otro dedo más adelante. **Pull-off**: lo contrario, sacas el dedo **tirando un poco** de la cuerda hacia abajo, y suena la nota de atrás.' },
          { p: 'Juntos se llaman **ligados**: hacen que las frases suenen fluidas.' },
          { tab: { seq: '3.5 h3.7 3.7 p3.5' } },
          { tip: 'El hammer-on se hace con fuerza y precisión, con la punta del dedo, justo detrás del traste.' }),
        P('A tocar',
          ex('lig1', 'Ligados en la caja', { bpm: 70, unit: 0.5, seq: '3.5 h3.7 3.7 p3.5 2.5 h2.8 2.8 p2.5 | 4.5 h4.7 4.7 p4.5 3.5 h3.7 3.5:1', repeat: 2 }),
          ex('lig2', 'Lick ligado', { bpm: 80, drums: 'rock', unit: 0.5, seq: '2.8 p2.5 3.7 p3.5 4.7 p4.5 5.7:1 | 5.5 h5.7 4.5 h4.7 4.7:2', repeat: 2 })),
      ] },
      { id: 'l-slides', title: 'Slides', min: 5, pages: [
        P('Deslizar',
          { p: 'Tocas una nota y **deslizas el dedo** a otro traste sin levantarlo y sin volver a pulsar. La presión tiene que mantenerse para que no se apague.' },
          { tab: { seq: '3.5/7:2 3.7/5:2' } },
          { listenSeq: { bpm: 80, seq: '3.5/7:2 3.7/5:2' }, label: 'Escuchar' }),
        P('A tocar',
          ex('sl1', 'Slides', { bpm: 70, seq: '3.5/7:2 3.7 3.5 | 4.5/7:2 4.7 4.5 | 3.5/7:2 2.5 2.8 | 1.5:4', repeat: 2 })),
      ] },
      { id: 'l-bends', title: 'Bends y vibrato', min: 6, pages: [
        P('Estirar la cuerda',
          { p: 'En un **bend** empujas la cuerda hacia arriba (o abajo) mientras suena, y la nota sube. El más común es de **un tono**: el traste 7 de la 3ª cuerda tiene que sonar como el traste 9.' },
          { listen: [{ label: 'La nota objetivo (3ª, traste 9)', notes: [midiOf(3, 9)] }] },
          { tip: 'Usa **tres dedos** juntos (1, 2 y 3) para empujar, y gira la muñeca como si abrieras una puerta. Con un solo dedo no llegas.' }),
        P('Practica con el afinador',
          { p: 'Abre el afinador, toca el traste 9 de la 3ª cuerda y mira la nota: **La**. Ahora haz el bend desde el traste 7 hasta que el afinador marque La.' },
          { tune: true },
          { p: 'El **vibrato** es un bend chiquito que va y viene, parejo. Es la firma de cada guitarrista: escucha cómo lo hacen tus favoritos.' }),
      ] },
    ] },

    { id: 'u9', title: 'Cejillas', desc: 'El temido Fa y los acordes móviles.', lessons: [
      { id: 'l-fa', title: 'El acorde de Fa', min: 8, pages: [
        P('El jefe final (que no es tan jefe)',
          { p: 'En la **cejilla** el dedo 1 aprieta varias cuerdas a la vez. El Fa (F) es el primero que casi todos sufren. Respira: se aprende.' },
          { chords: ['F (chico)', 'F'] },
          { p: 'Empieza por el **Fa chico**: solo 4 cuerdas, cejilla en dos. Cuando suene limpio, pasa al completo.' }),
        P('Trucos de cejilla',
          { p: '**Gira** un poco el dedo 1 hacia el lado del hueso: es más duro que la yema.' },
          { p: 'El **pulgar** detrás, a la altura del dedo 2. La fuerza viene de **tirar con el brazo** hacia atrás, no de apretar como pinza.' },
          { p: 'Pega la cejilla **al traste**, como siempre.' },
          { fun: 'No es fuerza: es posición. Mucha gente con poca fuerza toca cejillas perfectas.' }),
        P('A tocar',
          ex('fa1', 'C y Fa chico', { bpm: 65, seq: '[C]:4 | [F (chico)]:4 | [C]:4 | [F (chico)]:4', repeat: 2 }),
          ex('fa2', 'C – F – G', { bpm: 70, drums: 'soft', seq: '[C]:2 [C]:2 | [F]:2 [F]:2 | [G]:2 [G]:2 | [C]:4', repeat: 2 })),
      ] },
      { id: 'l-moviles', title: 'Acordes móviles', min: 8, pages: [
        P('Una forma, doce acordes',
          { p: 'La cejilla de Fa es la forma de **Mi (E)** corrida. Si la mueves, el acorde se llama como la nota de la **6ª cuerda**: traste 1 = F, 3 = G, 5 = A.' },
          { p: 'Saca el dedo 2 y tienes la versión **menor** (Fm, Gm, Am).' },
          { chords: ['G (cejilla)', 'Am (cejilla)'] }),
        P('Forma de La',
          { p: 'Con raíz en la **5ª cuerda** usas la forma de **La (A)**: traste 2 = Bm (la versión menor), traste 3 = C.' },
          { chords: ['Bm', 'C (cejilla)'] },
          { tip: 'Junto con las notas de la 6ª y la 5ª cuerda que ya sabes, ahora puedes tocar cualquier acorde mayor o menor.' }),
        P('A tocar',
          ex('mov1', 'G – Bm – C – D', { bpm: 70, drums: 'soft', seq: '[G (cejilla)]:2 [G (cejilla)]:2 | [Bm]:2 [Bm]:2 | [C (cejilla)]:2 [C (cejilla)]:2 | [D]:2 [D]:2', repeat: 2 }),
          ex('mov2', 'F – G – Am', { bpm: 70, drums: 'rock', seq: '[F] [F] [F] [F] | [G (cejilla)] [G (cejilla)] [G (cejilla)] [G (cejilla)] | [Am (cejilla)] [Am (cejilla)] [Am (cejilla)] [Am (cejilla)] | [Am (cejilla)]:4', repeat: 2 })),
      ] },
    ] },

    { id: 'u10', title: 'Con los dedos', desc: 'Arpegios y bajo alternado (Travis).', lessons: [
      { id: 'l-pima', title: 'Arpegios: p-i-m-a', min: 7, pages: [
        P('Cada dedo, su cuerda',
          { p: 'Sin púa: **p** pulgar (cuerdas 6, 5 y 4), **i** índice (3ª), **m** medio (2ª), **a** anular (1ª).' },
          { p: 'Un **arpegio** es un acorde tocado nota por nota. El patrón **p-i-m-a-m-i** suena a balada en cualquier acorde.' },
          { tab: { unit: 0.5, seq: '5.3 3.0 2.1 1.0 2.1 3.0 - - | 5.0 3.2 2.1 1.0 2.1 3.2 - -' } },
          { listenSeq: { bpm: 80, unit: 0.5, seq: '5.3 3.0 2.1 1.0 2.1 3.0 3.0:1 | 5.0 3.2 2.1 1.0 2.1 3.2 3.2:1' }, label: 'Escuchar' }),
        P('A tocar',
          ex('pima1', 'C – Am – Em – G', { bpm: 70, unit: 0.5, seq: '5.3 3.0 2.1 1.0 2.1 3.0 2.1 3.0 | 5.0 3.2 2.1 1.0 2.1 3.2 2.1 3.2 | 6.0 3.0 2.0 1.0 2.0 3.0 2.0 3.0 | 6.3 3.0 2.0 1.3 2.0 3.0 2.0 3.0', repeat: 2 }, 'Deja los dedos del acorde apretados todo el compás.')),
      ] },
      { id: 'l-travis', title: 'Bajo alternado (Travis)', min: 8, pages: [
        P('El pulgar es el baterista',
          { p: 'En el **Travis picking** el pulgar marca los cuatro pulsos alternando entre dos cuerdas graves, y los dedos tocan en el medio. Suena a dos guitarristas a la vez.' },
          { p: 'Empieza **solo con el pulgar** hasta que vaya solo. Después suma los dedos.' },
          { tab: { unit: 0.5, seq: '5.3+2.1 3.0 4.2 2.1 5.3+1.0 3.0 4.2 2.1' } }),
        P('A tocar',
          ex('trav1', 'Solo el pulgar', { bpm: 70, seq: '5.3 4.2 5.3 4.2 | 6.3 4.0 6.3 4.0', repeat: 3 }),
          ex('trav2', 'Travis en C y G', { bpm: 65, unit: 0.5, seq: '5.3+2.1 3.0 4.2 2.1 5.3+1.0 3.0 4.2 2.1 | 6.3+2.0 3.0 4.0 2.0 6.3+1.3 3.0 4.0 2.0', repeat: 3 })),
        P('Y ahora…',
          { p: '¡Terminaste el curso! Lo que sigue es lo más divertido: **canciones**. Ve a **Canciones**, elige una que te guste y bájale la velocidad al 70 %.' },
          { fun: 'Los guitarristas que más avanzan no son los que más practican escalas: son los que más tocan música que les gusta.' }),
      ] },
    ] },
  ];
  const ALL = COURSE.flatMap((u) => u.lessons.map((l) => Object.assign(l, { unit: u })));
  for (const L of ALL) for (const x of exercisesOf(L)) x.lessonId = L.id;

  // ------------------------------------------------------------------ text with **bold**
  function rich(text) {
    const out = [];
    String(text).split(/(\*\*[^*]+\*\*)/).forEach((part) => {
      if (part.startsWith('**') && part.endsWith('**')) out.push(el('b', { text: part.slice(2, -2) }));
      else if (part) out.push(document.createTextNode(part));
    });
    return out;
  }

  // ------------------------------------------------------------------ screens
  let root = null, current = null;
  const Lessons = (G.Lessons = {});
  Lessons.course = COURSE;
  Lessons.buildChart = buildChart;
  Lessons.parse = parse;

  Lessons.show = function (r, lessonId) {
    root = r || root;
    stopSound();
    if (lessonId) { const L = ALL.find((x) => x.id === lessonId); if (L) return openLesson(L, lp(L.id).page || 0); }
    renderCourse();
  };

  function nextLesson() { return ALL.find((L) => lessonStars(L) === 0 && !(prog[L.id] && prog[L.id].done)) || null; }

  function renderCourse() {
    current = null;
    const done = ALL.filter((L) => prog[L.id] && prog[L.id].done).length;
    const next = nextLesson();
    const head = el('header.g-head', null,
      el('button.g-mode', { title: 'Volver al estudio', onclick: G.Game.leave }, G.icon('back', 16), 'Estudio'),
      el('h1', { text: 'Juego' }),
      G.Game.tabs('lessons'),
      el('div.spacer'),
      el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 15), 'Afinar'));
    const hero = el('section.l-hero', null,
      el('div', null,
        el('h2', { text: done ? `Vas ${done} de ${ALL.length} lecciones` : 'Aprende guitarra tocando' }),
        el('p', { text: done ? 'Sigue donde quedaste, o repite una para sacarle las 3 estrellas.' : `${ALL.length} lecciones cortas: teoría que se escucha, preguntas rápidas y ejercicios en la autopista que Galpon corrige.` }),
        el('div.l-bar', null, el('i', { style: { width: Math.round(100 * done / ALL.length) + '%' } }))),
      next ? el('button.btn.primary.l-go', { onclick: () => openLesson(next, lp(next.id).page || 0) }, done ? 'Seguir: ' + next.title : 'Empezar') : el('p.l-alldone', { text: '¡Curso completo! 🎸' }));
    const units = COURSE.map((u, ui) => el('section.l-unit', null,
      el('div.l-unithead', null, el('span.l-unum', { text: String(ui + 1) }), el('div', null, el('h3', { text: u.title }), el('p', { text: u.desc }))),
      el('div.l-cards', null, u.lessons.map((L) => {
        const st = lessonStars(L), p = prog[L.id], isDone = p && p.done;
        return el('button.l-card' + (isDone ? '.done' : '') + (next === L ? '.next' : ''), { onclick: () => openLesson(L, (p && !isDone && p.page) || 0) },
          el('b', { text: L.title }),
          el('small', { text: `${L.min} min · ${exercisesOf(L).length ? exercisesOf(L).length + (exercisesOf(L).length === 1 ? ' ejercicio' : ' ejercicios') : 'teoría'}` }),
          el('span.l-mini', { 'aria-label': st + ' de 3 estrellas' }, [0, 1, 2].map((i) => el('i' + (i < st ? '.on' : ''), { text: '★' }))));
      }))));
    root.replaceChildren(el('div.g-lib.l-course', null, head, el('div.l-scroll', null, hero, ...units)));
  }

  function openLesson(L, page) {
    stopSound();
    current = { L, page: Math.max(0, Math.min(page || 0, L.pages.length - 1)) };
    renderLesson();
  }

  function renderLesson() {
    const { L } = current;
    const p = lp(L.id);
    p.page = current.page;
    save();
    const pg = L.pages[current.page];
    const last = current.page === L.pages.length - 1;
    const idx = ALL.indexOf(L);
    const dots = el('div.l-dots', null, L.pages.map((x, i) => el('button' + (i === current.page ? '.on' : i < current.page ? '.seen' : ''), { 'aria-label': 'Página ' + (i + 1) + ': ' + x.title, title: x.title, onclick: () => { current.page = i; renderLesson(); } })));
    const head = el('header.g-head.l-lhead', null,
      el('button.g-mode', { title: 'Volver a las lecciones', onclick: () => { stopSound(); renderCourse(); } }, G.icon('back', 16), 'Lecciones'),
      el('div.l-ltitle', null, el('small', { text: L.unit.title }), el('b', { text: L.title })),
      dots,
      el('div.spacer'),
      el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 15), 'Afinar'));
    const body = el('article.l-page', { 'data-notr': '' }, el('h2', { text: pg.title }), ...pg.blocks.map((b) => renderBlock(b, L)));
    const nav = el('footer.l-nav', null,
      el('button.btn', { disabled: current.page === 0 && idx === 0, onclick: () => { if (current.page > 0) { current.page--; renderLesson(); } else openLesson(ALL[idx - 1], ALL[idx - 1].pages.length - 1); } }, '← Atrás'),
      el('span.l-pageof', { text: `${current.page + 1} de ${L.pages.length}` }),
      last
        ? el('button.btn.primary', { onclick: () => finishLesson(L) }, ALL[idx + 1] ? 'Terminar y seguir →' : 'Terminar el curso')
        : el('button.btn.primary', { onclick: () => { current.page++; renderLesson(); } }, 'Siguiente →'));
    root.replaceChildren(el('div.l-lesson', null, head, el('div.l-scroll', null, body), nav));
    root.querySelector('.l-scroll').scrollTop = 0;
  }

  function finishLesson(L) {
    const p = lp(L.id);
    p.done = true; p.page = 0;
    save();
    const exs = exercisesOf(L);
    const missing = exs.filter((x) => !(p.ex[x.id] > 0));
    if (missing.length) G.toast(`Lección hecha. Te ${missing.length === 1 ? 'falta 1 ejercicio' : `faltan ${missing.length} ejercicios`} por estrellas: vuelve cuando quieras.`);
    else G.toast('¡Lección completa!');
    const idx = ALL.indexOf(L);
    if (ALL[idx + 1]) openLesson(ALL[idx + 1], 0); else renderCourse();
  }

  // ------------------------------------------------------------------ blocks
  function renderBlock(b, L) {
    if (b.p) return el('p', null, ...rich(b.p));
    if (b.tip) return el('div.l-tip', null, el('b', { text: 'Truco' }), el('p', null, ...rich(b.tip)), b.tune ? el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 14), 'Abrir el afinador') : null);
    if (b.tune) return el('div.row', null, el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 14), 'Abrir el afinador'));
    if (b.fun) return el('div.l-fun', null, el('b', { text: 'Dato curioso' }), el('p', null, ...rich(b.fun)));
    if (b.fret) return el('figure.l-fig', null, fretboard(b.fret), el('figcaption', { text: 'Toca un punto para escucharlo.' }));
    if (b.chords) return el('figure.l-fig', null, el('div.l-chords', null, b.chords.map((c) => el('button.l-chordbtn', { title: 'Escuchar ' + chordLabel(c), onclick: () => hearChord(c) }, chordBox(c)))), el('figcaption', { text: 'Toca un acorde para escucharlo.' }));
    if (b.tab) return el('figure.l-fig', null, tab(b.tab));
    if (b.listenSeq) return el('div.l-listen', null, listenBtn(b.label || 'Escuchar', () => hearSeq(Object.assign({ bpm: 90 }, b.listenSeq))));
    if (b.listen) return el('div.l-listen', null, b.listen.map((x) => listenBtn(x.label, () => {
      if (x.chord) hearChord(x.chord);
      else if (x.chords) playGuide(x.chords.map((c, i) => ({ t: i * 1.2, p: chordPitches(c), d: 1.1 })));
      else if (x.seq) hearSeq(x.seq);
      else if (x.together) hearNotes(x.notes[0], 0, true);
      else hearNotes(x.notes, 0.6);
    })));
    if (b.quiz) return renderQuiz(b.quiz, L);
    if (b.gen === 'fretNote') return genFretQuiz(b);
    if (b.gen === 'earInterval') return genEarQuiz(b);
    if (b.play) return renderExercise(b.play, L);
    return null;
  }
  const listenBtn = (label, fn) => el('button.l-hear', { onclick: fn }, G.icon('play', 12), label);

  function renderQuiz(qs, L) {
    const p = lp(L.id);
    return el('div.l-quiz', null, qs.map((q, qi) => {
      const key = current.page + ':' + qi;
      const box = el('div.l-q');
      const why = el('p.l-why');
      const answered = p.quiz[key];
      const btns = q.options.map((o, oi) => el('button.l-opt', { onclick: () => {
        btns.forEach((b, bi) => { b.disabled = true; b.classList.toggle('right', bi === q.answer); b.classList.toggle('wrong', bi === oi && oi !== q.answer); });
        why.replaceChildren(el('b', { text: oi === q.answer ? '¡Bien! ' : 'Casi. ' }), ...rich(q.why || ''));
        p.quiz[key] = oi === q.answer ? 1 : -1; save();
      } }, o));
      if (answered) { btns.forEach((b, bi) => { b.classList.toggle('right', bi === q.answer); }); why.replaceChildren(...rich(q.why || '')); }
      box.append(el('p.l-qq', null, ...rich(q.q)), el('div.l-opts', null, btns), why);
      return box;
    }));
  }

  /** «Which note is this?» on the strings you choose, new questions every time. */
  function genFretQuiz(cfg) {
    const box = el('div.l-quiz.l-gen');
    let n = 0, right = 0;
    const naturals = [0, 2, 4, 5, 7, 9, 11];
    function ask() {
      if (n >= (cfg.count || 8)) {
        box.replaceChildren(el('p.l-score', { text: `${right} de ${n}. ` + (right === n ? '¡Perfecto!' : right >= n * 0.7 ? 'Muy bien.' : 'Repasa el diagrama y prueba otra vez.') }),
          el('button.btn', { onclick: () => { n = 0; right = 0; ask(); } }, 'Otra ronda'));
        return;
      }
      let s, f, pc;
      do { s = cfg.strings[Math.floor(Math.random() * cfg.strings.length)]; f = Math.floor(Math.random() * 13); pc = midiOf(s, f) % 12; } while (!naturals.includes(pc));
      const opts = new Set([pc]);
      while (opts.size < 4) opts.add(naturals[Math.floor(Math.random() * 7)]);
      const list = [...opts].sort(() => Math.random() - 0.5);
      const fb = fretboard({ to: 12, marks: [{ s, f, label: '?' }] });
      const why = el('p.l-why');
      const btns = list.map((o) => el('button.l-opt', { onclick: () => {
        n++; if (o === pc) right++;
        btns.forEach((b, i) => { b.disabled = true; b.classList.toggle('right', list[i] === pc); b.classList.toggle('wrong', list[i] === o && o !== pc); });
        why.textContent = `Cuerda ${s}, traste ${f}: ${both(pc)}.`;
        hearNotes([midiOf(s, f)]);
        setTimeout(ask, 1300);
      } }, both(o)));
      box.replaceChildren(el('p.l-qq', { text: `Pregunta ${n + 1} de ${cfg.count || 8}: ¿qué nota es?` }), fb, el('div.l-opts', null, btns), why);
    }
    ask();
    return box;
  }

  /** Hear two notes, say the interval. */
  function genEarQuiz(cfg) {
    const SET = [[3, '3ª menor'], [4, '3ª mayor'], [5, '4ª justa'], [7, '5ª justa'], [12, 'Octava']];
    const box = el('div.l-quiz.l-gen');
    let n = 0, right = 0;
    function ask() {
      if (n >= (cfg.count || 6)) {
        box.replaceChildren(el('p.l-score', { text: `${right} de ${n}. ` + (right === n ? '¡Tienes oído!' : 'El oído se entrena: vuelve a escuchar los ejemplos y prueba otra ronda.') }),
          el('button.btn', { onclick: () => { n = 0; right = 0; ask(); } }, 'Otra ronda'));
        return;
      }
      const [st, name] = SET[Math.floor(Math.random() * SET.length)];
      const low = 48 + Math.floor(Math.random() * 8);
      const play = () => hearNotes([low, low + st], 0.7);
      const why = el('p.l-why');
      const btns = SET.map(([s2, n2]) => el('button.l-opt', { onclick: () => {
        n++; if (s2 === st) right++;
        btns.forEach((b, i) => { b.disabled = true; b.classList.toggle('right', SET[i][0] === st); b.classList.toggle('wrong', SET[i][0] === s2 && s2 !== st); });
        why.textContent = 'Era ' + name + '.';
        setTimeout(ask, 1300);
      } }, n2));
      box.replaceChildren(el('p.l-qq', { text: `Pregunta ${n + 1} de ${cfg.count || 6}: ¿qué intervalo es?` }), listenBtn('Escuchar de nuevo', play), el('div.l-opts', null, btns), why);
      setTimeout(play, 200);
    }
    const start = el('button.btn.primary', { onclick: () => ask() }, 'Empezar (con sonido)');
    box.append(el('p.l-qq', { text: 'Vas a escuchar dos notas. ¿Qué distancia hay entre ellas?' }), start);
    return box;
  }

  function renderExercise(x, L) {
    const p = lp(L.id);
    const st = p.ex[x.id] || 0;
    const preview = tab(x, 2);
    return el('div.l-ex', null,
      el('div.l-exhead', null,
        el('div', null, el('b', { text: x.label }), el('small', { text: `${x.bpm} pulsos por minuto` + (x.note ? ' · ' + x.note : '') })),
        el('span.l-mini', { 'aria-label': st + ' de 3 estrellas' }, [0, 1, 2].map((i) => el('i' + (i < st ? '.on' : ''), { text: '★' })))),
      preview,
      el('div.row', null,
        el('button.btn.primary', { onclick: () => startExercise(x, L) }, G.icon('play', 13), st ? 'Tocar de nuevo' : 'Tocar'),
        listenBtn('Escuchar primero', () => hearSeq(Object.assign({}, x, { countIn: 0 })))));
  }

  function startExercise(x, L) {
    stopSound();
    const chart = buildChart(x, x.label);
    const exs = exercisesOf(L);
    const i = exs.indexOf(x);
    G.Game.openLesson({
      title: L.title + ' · ' + x.label, lessonId: L.id, data: chart.data, audio: chart.audio,
      onResult: ({ stars }) => {
        const p = lp(L.id);
        if (stars > (p.ex[x.id] || 0)) { p.ex[x.id] = stars; save(); }
        const nx = exs[i + 1];
        if (nx) return { next: () => startExercise(nx, L), nextLabel: 'Siguiente: ' + nx.label };
        return null;
      },
    });
  }
})();
