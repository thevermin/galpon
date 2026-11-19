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
  // language: the lessons carry their own English (lessons_en.js); generated text uses L2
  const isEn = () => G.lang === 'en';
  const tr = (s) => (isEn() && G.LessonsEN && G.LessonsEN[s]) || s;
  const L2 = (es, en) => (isEn() ? en : es);
  const noteName = (pc) => (isEn() ? EN[pc % 12] : `${ES[pc % 12]} (${EN[pc % 12]})`);
  const noteShort = (pc) => (isEn() ? EN[pc % 12] : ES[pc % 12]);
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
    'C (tríada)': [[-1, -1, -1, 5, 5, 3], [0, 0, 0, 3, 4, 1]],
    'Am (tríada)': [[-1, -1, -1, 5, 5, 5], [0, 0, 0, 1, 1, 1]],
    'F (tríada)': [[-1, -1, -1, 5, 6, 5], [0, 0, 0, 1, 2, 1]],
    'G (tríada)': [[-1, -1, -1, 7, 8, 7], [0, 0, 0, 1, 2, 1]],
    E5: [[0, 2, 2, -1, -1, -1], [0, 1, 2, 0, 0, 0]],
    A5: [[-1, 0, 2, 2, -1, -1], [0, 0, 1, 2, 0, 0]],
    F5: [[1, 3, 3, -1, -1, -1], [1, 3, 4, 0, 0, 0]],
    G5: [[3, 5, 5, -1, -1, -1], [1, 3, 4, 0, 0, 0]],
    'A5 (6ª)': [[5, 7, 7, -1, -1, -1], [1, 3, 4, 0, 0, 0]],
    C5: [[-1, 3, 5, 5, -1, -1], [0, 1, 3, 4, 0, 0]],
    D5: [[-1, 5, 7, 7, -1, -1], [0, 1, 3, 4, 0, 0]],
  };
  const SHAPE_EN = { '(chico)': '(small)', '(cejilla)': '(barre)', '(tríada)': '(triad)', '(6ª)': '(6th)' };
  const chordLabel = (name) => {
    const base = G.chordLabel ? G.chordLabel(name.replace(/ \(.*\)$/, '')) : name.replace(/ \(.*\)$/, '');
    if (!name.includes(' (')) return base;
    const tag = name.slice(name.indexOf(' (') + 1);
    return base + ' ' + (G.lang === 'en' && SHAPE_EN[tag] ? SHAPE_EN[tag] : tag);
  };

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
        const m = tok.match(/^(.*?):([\d.]+)([a-z]*)$/);   // duration, maybe followed by u / d / m
        if (m) { tok = m[1] + m[3]; dur = parseFloat(m[2]); }
        if (tok === '-' || tok === 'r') { beat += dur; continue; }
        if (tok[0] === '[') {
          if (tok.indexOf(']') < 0) throw new Error('Acorde sin cerrar ' + tok);
          let name = tok.slice(1, tok.indexOf(']')).replace(/_/g, ' ');
          let only = null;
          if (/:[1-6]+$/.test(name)) { only = name.slice(name.lastIndexOf(':') + 1).split('').map(Number); name = name.slice(0, name.lastIndexOf(':')); }
          const shape = CHORDS[name];
          if (!shape) throw new Error('Acorde desconocido ' + name);
          const after = tok.slice(tok.indexOf(']') + 1);
          const dir = after.includes('u') ? 'u' : after.includes('d') ? 'd' : (Math.abs(beat - Math.round(beat)) > 0.01 ? 'u' : 'd');
          let notes = [];
          shape[0].forEach((f, i) => { if (f >= 0 && (!only || only.includes(6 - i))) notes.push({ str: 6 - i, fret: f }); });
          // an upstroke only catches the thin strings
          if (dir === 'u' && !only && notes.length > 4) notes = notes.filter((n) => n.str <= 4).slice(-4);
          events.push({ beat, dur, notes, chord: name, mute: after.includes('m'), dir, part: !!only || dir === 'u' });
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
      // a strum is a hit, not a held note (unless it's a long one, a whole bar or half)
      const sus = ev.dur * spb >= 0.9 && !(ev.chord && ev.dur < 2) ? ev.dur * spb * 0.85 : 0;
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
        const prev = chords[chords.length - 1];
        const c = { t, id: tplIndex[name], notes: raw };
        if (ev.chord) c.sd = ev.dir;
        // the same chord again soon: Rocksmith-style, only its frame (you keep the shape)
        if (prev && prev.id === c.id && t - prev.t < 2.5) c.rp = 1;
        chords.push(c);
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
    const sections = [{ time: lead * spb, name: title || L2('Ejercicio', 'Exercise'), number: 1 }];
    const data = {
      arrangements: { leccion: { name: L2('Lección', 'Lesson'), type: 'guitar', tuning: [0, 0, 0, 0, 0, 0], notes, chords, templates, anchors } },
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
  let engineSound = null;   // the engine plays them (same output as Galpon: your interface, not the Mac's speakers)
  function stopSound() {
    for (const n of playingNodes) { try { n.stop(); } catch (e) { /* already done */ } }
    playingNodes = [];
    if (engineSound) G.call('lessonSound', { stop: true });
  }
  /** Plays [{t, p:[midi], d, m}] (seconds), chords strummed a hair apart. */
  async function playGuide(list) {
    stopSound();
    if (engineSound !== false) {
      const r = await G.call('lessonSound', { guide: list.map((e) => ({ t: e.t, p: e.p, d: e.d, m: !!e.m })) });
      engineSound = !!(r && r.ok);
      if (engineSound) return;
    }
    webPlay(list);
  }
  function webPlay(list) {
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
    const W = 118, H = 140, x0 = 24, y0 = 34, dx = 16, dy = 20;
    const s = svg('svg', { viewBox: `0 0 ${W} ${H}`, width: W, height: H, class: 'l-chord', role: 'img', 'aria-label': 'Acorde ' + name });
    if (base === 1) s.appendChild(svg('rect', { x: x0 - 1, y: y0 - 4, width: dx * 5 + 2, height: 4, fill: '#ddd' }));
    else s.appendChild(svg('text', { x: x0 - 9, y: y0 + dy * 0.7, 'text-anchor': 'end', 'font-size': 11, fill: '#aaa' }, String(base)));
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
    const tun = cfg.tuning || OPEN, ns = tun.length;   // low string first
    const colour = (str) => (G.stringColour ? G.stringColour(ns - str) : '#ccc');
    const from = cfg.from || 0, to = cfg.to || 12;
    const n = to - from + 1;
    const x0 = 36, y0 = 18, dy = 22, span = dy * (ns - 1);
    const W = Math.min(820, 60 + n * 52), H = y0 + span + 42;
    const fx = (f) => x0 + (f - from + (from === 0 ? 0 : 0.5)) * ((W - x0 - 10) / (n - (from === 0 ? 0.5 : 0)));
    const fw = (W - x0 - 10) / (n - (from === 0 ? 0.5 : 0));
    const s = svg('svg', { viewBox: `0 0 ${W} ${H}`, width: W, height: H, class: 'l-fret', role: 'img', 'aria-label': cfg.label || L2('Diapasón', 'Fretboard') });
    s.appendChild(svg('rect', { x: x0, y: y0 - 6, width: W - x0 - 10, height: span + 12, rx: 4, fill: '#2a2119' }));
    for (let f = Math.max(1, from); f <= to; f++) {
      const x = fx(f) + fw / 2;
      s.appendChild(svg('line', { x1: x, x2: x, y1: y0 - 6, y2: y0 + span + 6, stroke: '#9a8f80', 'stroke-width': 2 }));
    }
    if (from === 0) s.appendChild(svg('rect', { x: fx(0) + fw / 2 - 3, y: y0 - 6, width: 5, height: span + 12, fill: '#e8e2d4' }));
    for (const d of [3, 5, 7, 9, 15, 17]) if (d >= from && d <= to && d > 0) s.appendChild(svg('circle', { cx: fx(d), cy: y0 + span / 2, r: 5, fill: '#5a4d3c' }));
    if (12 >= from && 12 <= to) for (const k of [0.3, 0.7]) s.appendChild(svg('circle', { cx: fx(12), cy: y0 + span * k, r: 5, fill: '#5a4d3c' }));
    for (let i = 0; i < ns; i++) {
      const str = i + 1, y = y0 + i * dy;
      const thick = (1 + i * (ns === 4 ? 0.7 : 0.35));
      if (cfg.hl === str) s.appendChild(svg('rect', { x: x0, y: y - 9, width: W - x0 - 10, height: 18, fill: colour(str), opacity: 0.18 }));
      s.appendChild(svg('line', { x1: x0, x2: W - 10, y1: y, y2: y, stroke: colour(str), 'stroke-width': thick * (cfg.hl === str ? 1.8 : 1), opacity: cfg.hl && cfg.hl !== str ? 0.35 : 0.8 }));
      s.appendChild(svg('text', { x: 12, y: y + 4, 'font-size': 11, fill: '#999' }, String(str)));
    }
    for (let f = Math.max(from, 1); f <= to; f++) s.appendChild(svg('text', { x: fx(f), y: H - 4, 'text-anchor': 'middle', 'font-size': 10, fill: '#777' }, String(f)));
    if (cfg.onPick)   // click a fret (for when there's no instrument at hand)
      for (let i = 0; i < ns; i++) for (let f = from; f <= to; f++) {
        const r = svg('rect', { x: fx(f) - fw / 2, y: y0 + i * dy - dy / 2, width: fw, height: dy, fill: 'transparent', class: 'l-pick' });
        r.addEventListener('click', () => cfg.onPick(i + 1, f));
        s.appendChild(r);
      }
    for (const m of cfg.marks || []) {
      const x = fx(m.f), y = y0 + (m.s - 1) * dy;
      const g = svg('g', { class: 'l-mark', tabindex: 0, role: 'button', 'aria-label': (m.label || '') + ' · ' + m.s + ' · ' + m.f });
      if (m.ring) g.appendChild(svg('circle', { cx: x, cy: y, r: 12.5, fill: 'none', stroke: '#f2f2f3', 'stroke-width': 1.5 }));
      if (m.glow) g.appendChild(svg('circle', { cx: x, cy: y, r: 15, fill: '#4ade80', opacity: 0.35 }));
      const fill = m.good ? '#4ade80' : m.bad ? '#f87171' : m.root ? '#ef4444' : m.dim ? '#3b3b44' : colour(m.s);
      g.appendChild(svg('circle', { cx: x, cy: y, r: 9.5, fill, stroke: m.glow ? '#fff' : '#111', 'stroke-width': m.glow ? 2 : 1 }));
      if (m.label) g.appendChild(svg('text', { x, y: y + 3.5, 'text-anchor': 'middle', 'font-size': m.label.length > 2 ? 7.5 : 9, 'font-weight': 700, fill: m.dim ? '#ddd' : '#111' }, m.label));
      g.addEventListener('click', () => hearNotes([tun[ns - m.s] + m.f]));
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
          { listen: [6, 5, 4, 3, 2, 1].map((s) => ({ label: () => L2(`${s}ª · ${ES[OPEN[6 - s] % 12]}`, `String ${s} · ${EN[OPEN[6 - s] % 12]}`), notes: [midiOf(s, 0)] })) },
          { fun: 'Las dos Mi (la 6ª y la 1ª) son la misma nota, una dos octavas más aguda que la otra. Lo que aprendas en una sirve para la otra.' }),
        P('¿Quedó claro?', quiz(
          Q('¿Cuál es la 6ª cuerda?', ['La más fina', 'La más gruesa', 'La del medio'], 1, 'La 6ª es la gruesa: la más grave.'),
          Q('¿Qué nota suena en la 2ª cuerda al aire?', ['Sol (G)', 'Si (B)', 'Mi (E)'], 1, 'Mi La Re Sol **Si** Mi: la 2ª es Si.'),
          Q('¿Qué cuerdas son Mi?', ['La 6ª y la 1ª', 'La 5ª y la 4ª', 'Solo la 6ª'], 0, 'Las dos de los extremos: Mi grave y Mi agudo.'))),
        P('Pruébalo',
          { p: 'Antes de la autopista, una prueba simple: toca las seis cuerdas al aire, de la 6ª a la 1ª. Galpon te escucha y te marca cada una.' },
          { tryit: { label: 'Las seis cuerdas al aire, de la 6ª a la 1ª', notes: [[6, 0], [5, 0], [4, 0], [3, 0], [2, 0], [1, 0]] } }),
        P('A tocar',
          { p: 'Toca cada cuerda al aire cuando la nota llega a la línea. Sin apuro: lo importante es tocar **una sola cuerda** por vez.' },
          ex('aire1', 'Cuerdas al aire', { bpm: 70, unit: 2, seq: '6.0 5.0 4.0 3.0 2.0 1.0 | 1.0 2.0 3.0 4.0 5.0 6.0' }),
          ex('aire2', 'Una por tiempo', { bpm: 80, seq: '6.0 6.0 5.0 5.0 4.0 4.0 3.0 3.0 | 2.0 2.0 1.0 1.0 2.0 3.0 4.0 5.0 | 6.0:4' }, 'Un poco más rápido.')),
      ] },
      { id: 'l-tab', title: 'Leer tablatura y la autopista', min: 5, pages: [
        P('La tablatura en 30 segundos',
          { p: 'La **tablatura** (tab) dibuja las 6 cuerdas como 6 líneas. La de **abajo es la 6ª** (la gruesa). Ojo: es al revés de como la ves cuando tocas, mirando hacia abajo (ahí la 6ª te queda arriba).' },
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
      { id: 'l-practicar', title: 'Cómo practicar (y avanzar rápido)', min: 4, pages: [
        P('Poco y seguido',
          { p: '**20 minutos todos los días** te llevan más lejos que 3 horas el sábado. Los dedos y el cerebro aprenden mientras descansas.' },
          { p: 'Una rutina simple:' },
          { p: '**3 min** de calentamiento (la araña) · **5 min** de notas (en Práctica) · **5 min** de cambios de acordes · **el resto, música que te guste.**' },
          { tip: 'Usa siempre la velocidad que te sale **bien**. Cuando algo sale 3 veces seguidas perfecto, súbele un 10 %.' }),
        P('Lo que más ayuda', quiz(
          Q('Algo no te sale a 100 %. ¿Qué haces?', ['Lo repito rápido hasta que salga', 'Lo bajo de velocidad hasta que salga perfecto', 'Lo salteo'], 1, 'Despacio y bien: la velocidad llega sola.'),
          Q('¿Qué es mejor?', ['2 horas un día por semana', '20 minutos todos los días'], 1, 'Poco y seguido gana siempre.')),
          { fun: 'En la sección **Práctica** (arriba en la portada de lecciones) hay entrenadores que no se terminan nunca: lo que más te cuesta aparece más seguido.' }),
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
        P('En tu guitarra',
          { p: 'Toca Mi, Fa y Sol en la 6ª cuerda (trastes 0, 1 y 3), y después Fa#, que está justo en el medio de Fa y Sol.' },
          { tryit: { label: 'Mi, Fa, Sol y Fa# en la 6ª cuerda', notes: [[6, 0], [6, 1], [6, 3], [6, 2]] } }),
        P('A ver', quiz(
          Q('¿Qué nota viene después de Mi?', ['Mi#', 'Fa', 'Fa#'], 1, 'Entre Mi y Fa no hay nada: están a un semitono.'),
          Q('Dos trastes son…', ['Un semitono', 'Un tono', 'Una octava'], 1, 'Un traste = semitono; dos = un tono.'),
          Q('Fa# es lo mismo que…', ['Solb', 'Fab', 'Sol#'], 0, 'Fa# y Solb suenan igual.'),
          Q('¿Qué letra es Sol?', ['E', 'G', 'A'], 1, 'C D E F G A B = Do Re Mi Fa Sol La Si.'))),
        P('¿Qué nota es?',
          { p: 'Te muestro un punto en la 6ª cuerda y eliges su nombre. Cuenta desde el Mi al aire: un traste, un semitono.' },
          { trainer: 'name', level: 0, count: 6 }),
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
        P('Adivina la nota', { trainer: 'name', level: 0, count: 5 }, { trainer: 'name', level: 1, count: 5 }),
        P('Encuéntrala en tu guitarra',
          { p: 'Ahora al revés: te pido una nota y la buscas **tocando**. Te escucho y te digo si caíste, o cuántos trastes te faltan.' },
          { tip: 'Sin la guitarra a mano también puedes tocar el traste en el dibujo.' },
          { trainer: 'find', level: 0, count: 6 }),
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
        P('Pruébalo',
          { p: 'Toca cada nota y después su octava. Fíjate cómo la mano hace siempre el mismo movimiento.' },
          { tryit: { label: 'Sol y su octava, Do y su octava', notes: [[6, 3], [4, 5], [5, 3], [3, 5]] } }),
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
        P('Adivina la nota', { trainer: 'name', level: 3, count: 5 }, { trainer: 'name', level: 4, count: 5 }),
        P('Encuéntrala en tu guitarra', { trainer: 'find', level: 3, count: 8 }),
        P('Todos los Do',
          { p: 'Busca todos los **Do (C)** de los primeros 12 trastes. Los de la guitarra completa: 5ª-3, 2ª-1, 4ª-10, 3ª-5, 1ª-8 y 6ª-8.' },
          { fret: { to: 12, marks: [[5, 3], [2, 1], [4, 10], [3, 5], [1, 8], [6, 8]].map(([s, f]) => ({ s, f, label: 'C', root: true })) } },
          ex('todosdo', 'Cazar todos los Do', { bpm: 60, unit: 2, seq: '5.3 2.1 3.5 1.8 | 4.10 6.8 5.3:4' })),
      ] },
      { id: 'l-memorizar', title: 'Memorizar el mástil: el método', min: 8, pages: [
        P('Un plan que funciona',
          { p: 'Memorizar 78 posiciones de memoria no sirve. Lo que funciona es **ver dibujos que se repiten**. El método, en tres pasos:' },
          { p: '**1. Una nota en todas las cuerdas.** Elige una (por ejemplo La) y encuéntrala en las seis. Siempre forma el mismo dibujo: octavas y la 1ª igual a la 6ª.' },
          { p: '**2. Lo que hay alrededor.** Desde esa nota, la 3ª y la 5ª siempre quedan en el mismo lugar. Así ves acordes y escalas, no puntos sueltos.' },
          { p: '**3. Una nota por día.** Lunes La, martes Re… En dos semanas, el mástil empieza a hablar solo.' }),
        P('Paso 1: todas sus posiciones',
          { p: 'Te pido una nota y la buscas en cada cuerda, de la 6ª a la 1ª. Con la guitarra o tocando el dibujo.' },
          { trainer: 'every', level: 0, count: 3 }),
        P('Paso 2: la 3ª y la 5ª',
          { p: 'Alrededor de **La** (en rojo): la **3ª** (Do#) y la **5ª** (Mi). Fíjate que el dibujo se repite en cada octava.' },
          { fret: { to: 12, marks: [[6, 5, 'A', 1], [6, 9, 'C#'], [5, 7, 'E'], [5, 0, 'A', 1], [5, 4, 'C#'], [4, 7, 'A', 1], [4, 2, 'E'], [3, 6, 'C#'], [3, 9, 'E'], [3, 2, 'A', 1], [2, 5, 'E'], [2, 10, 'A', 1], [2, 2, 'C#'], [1, 5, 'A', 1], [1, 9, 'C#'], [1, 0, 'E']].map(([s, f, label, r]) => ({ s, f, label, root: !!r })) } },
          { tip: 'En el **Explorador del mástil** (Práctica) puedes ver esto en cualquier tonalidad, con los grados en vez de los nombres.' }),
        P('Para cerrar', quiz(
          Q('¿Cuál es la mejor forma de aprender las notas del mástil?', ['Memorizar cada traste', 'Ver los dibujos que se repiten', 'Leer la lista muchas veces'], 1, 'Los dibujos (octavas, 3ª, 5ª) se repiten: se aprenden una vez y sirven para todo.'),
          Q('Desde la 6ª cuerda, ¿dónde está la octava?', ['Dos cuerdas arriba, dos trastes adelante', 'Una cuerda arriba, mismo traste', 'Tres cuerdas arriba, mismo traste'], 0, 'La forma de octava que ya conoces.'))),
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
        P('¿Mayor o menor?',
          { p: 'Escucha y decide: ¿luminoso o melancólico? Con práctica lo vas a oír al instante.' },
          { trainer: 'chordEar', level: 0, count: 6 }),
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
        P('El minuto de cambios',
          { p: 'Un ejercicio famoso de **JustinGuitar**: un minuto, dos acordes, ¿cuántas veces cambias? No importa que suene sucio: importa que los dedos lleguen. Meta: **30**; con el tiempo, **60**.' },
          { minute: ['C', 'G'] },
          { tip: 'Hazlo con el cambio que más te cueste, un minuto por día. En una semana vas a notar la diferencia.' }),
        P('La progresión de los 50',
          { p: 'G – Em – C – D: la de cientos de baladas de los años 50 y 60.' },
          { listen: [{ label: 'Escuchar', chords: ['G', 'Em', 'C', 'D'] }] },
          ex('cambios1', 'G Em C D', { bpm: 70, drums: 'soft', seq: '[G] [G] [G] [G] | [Em] [Em] [Em] [Em] | [C] [C] [C] [C] | [D] [D] [D] [D]', repeat: 2 }),
          ex('cambios2', 'C Am Em G', { bpm: 75, drums: 'soft', seq: '[C]:2 [C]:2 | [Am]:2 [Am]:2 | [Em]:2 [Em]:2 | [G]:2 [G]:2', repeat: 3 })),
      ] },
      { id: 'l-apagar', title: 'Apagar las cuerdas que no van', min: 7, pages: [
        P('El secreto del sonido limpio',
          { p: 'Un acorde suena «profesional» no solo por las cuerdas que suenan, sino por **las que no suenan**. Si en D se cuela la 6ª cuerda al aire, todo suena turbio.' },
          { p: 'La solución no es rasguear con cuidado: es **apagarlas**, así puedes rasguear suelto y con ganas.' },
          { listen: [{ label: 'D limpio', chord: 'D' }, { label: 'D con la 6ª colada', notes: [[40, 50, 57, 62, 66]], together: true }] }),
        P('Quién apaga qué',
          { p: '**D**: asoma la punta del **pulgar** por arriba del mástil y apoya la 6ª cuerda. La 5ª al aire (La) puede sonar: es parte del acorde.' },
          { p: '**C**: el dedo 3 (en la 5ª cuerda) toca apenas la 6ª con su yema. Un roce alcanza.' },
          { p: '**Power chords**: la yema del dedo 1, acostada, apaga las cuerdas de abajo; el costado de los dedos, las de arriba.' },
          { chords: ['D', 'C', 'G5'] },
          { tip: 'Arma el apagado **junto con el acorde**, no después. Si lo agregas tarde, se corta el rasgueo.' }),
        P('Cortar el sonido a propósito',
          { p: 'Si **aflojas** la presión de la mano izquierda (sin levantar los dedos), las cuerdas dejan de sonar al instante. Así se hacen los acordes cortos, «picados», del funk y el reggae.' },
          { p: 'Aprieta en el golpe, afloja enseguida: «**chac**, silencio, **chac**, silencio».' }),
        P('Prueba', quiz(
          Q('¿Con qué apagas la 6ª cuerda en D?', ['Con el pulgar, por arriba del mástil', 'Rasgueando con cuidado', 'Con el dedo 4'], 0, 'El pulgar asoma y la toca apenas.'),
          Q('¿Cómo cortas un acorde sin levantar los dedos?', ['Aflojando la presión', 'Apretando más fuerte', 'Con la púa'], 0, 'Aflojas y las cuerdas se apagan solas.'))),
        P('A tocar',
          ex('apag1', 'Acordes cortos (D y G)', { bpm: 80, drums: 'rock', seq: '[D]:0.5 -:0.5 [D]:0.5 -:0.5 [D]:0.5 -:0.5 [D]:0.5 -:0.5 | [G]:0.5 -:0.5 [G]:0.5 -:0.5 [G]:0.5 -:0.5 [G]:0.5 -:0.5', repeat: 3 }, 'Toca y corta: el silencio también suena.'),
          ex('apag2', 'Rasgueo suelto con D y C', { bpm: 75, drums: 'soft', unit: 0.5, seq: '[D] [D] [D] [D] [D] [D] [D] [D] | [C] [C] [C] [C] [C] [C] [C] [C]', repeat: 3 }, 'Con el apagado puesto, rasguea todas las cuerdas sin miedo.')),
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
      { id: 'l-semicorcheas', title: 'Semicorcheas y ritmo sincopado', min: 8, pages: [
        P('Cuatro por pulso',
          { p: 'Si partes cada pulso en **cuatro**, tienes semicorcheas. Se cuentan «**1** e y a, **2** e y a…».' },
          { p: 'La mano va y viene **el doble de rápido**: abajo en el número y en la «y», arriba en la «e» y en la «a». Sigue moviéndose aunque no toques.' },
          { listen: [{ label: 'Semicorcheas', seq: { bpm: 70, unit: 0.25, seq: '6.0 6.0 6.0 6.0 6.0 6.0 6.0 6.0' } }] }),
        P('La síncopa',
          { p: 'La gracia está en **saltear golpes**: si tocas en la «a» y no en el número que sigue, el acento cae «entre» los pulsos. Eso es la **síncopa**: lo que hace bailar al funk, al reggaetón o al pop.' },
          { p: 'El patrón de este ejercicio: **1** · · a · · y · **3** · · a · · y · — con la mano siempre en movimiento.' },
          { listen: [{ label: 'Escuchar el patrón', seq: { bpm: 80, seq: '[Em]:0.75d [Em]:0.75u [Em]:0.5d [Em]:0.75d [Em]:0.75u [Em]:0.5d', repeat: 2 } }] },
          { tip: 'Las flechas de la autopista te dicen la dirección. Si la mano va al revés, el patrón no sale: ese es el truco.' }),
        P('A tocar',
          ex('sinc1', 'Síncopa en Em', { bpm: 65, drums: 'rock', seq: '[Em]:0.75d [Em]:0.75u [Em]:0.5d [Em]:0.75d [Em]:0.75u [Em]:0.5d', repeat: 6 }),
          ex('sinc2', 'Con cambios: Em – C – G – D', { bpm: 70, drums: 'rock', seq: '[Em]:0.75d [Em]:0.75u [Em]:0.5d [Em]:0.75d [Em]:0.75u [Em]:0.5d | [C]:0.75d [C]:0.75u [C]:0.5d [C]:0.75d [C]:0.75u [C]:0.5d | [G]:0.75d [G]:0.75u [G]:0.5d [G]:0.75d [G]:0.75u [G]:0.5d | [D]:0.75d [D]:0.75u [D]:0.5d [D]:0.75d [D]:0.75u [D]:0.5d', repeat: 2 })),
      ] },
      { id: 'l-bajoacorde', title: 'Bajo y acorde (en 3 y en 4)', min: 7, pages: [
        P('El bajo primero',
          { p: 'Una forma linda de acompañar: tocas **la raíz sola** en el pulso 1 y después **el acorde en las cuerdas agudas**. Suena a piano, a folk, a vals.' },
          { p: 'En **3/4** (vals) se cuenta «**1** 2 3»: bajo, acorde, acorde.' },
          { listen: [{ label: 'Vals en G y C', seq: { bpm: 110, beats: 3, seq: '6.3 [G:4321] [G:4321] | 5.3 [C:4321] [C:4321]' } }] }),
        P('A tocar',
          ex('vals1', 'Vals: G y C', { bpm: 100, beats: 3, seq: '6.3 [G:4321] [G:4321] | 6.3 [G:4321] [G:4321] | 5.3 [C:4321] [C:4321] | 5.3 [C:4321] [C:4321]', repeat: 2 }),
          ex('bajo4', 'En 4: bajo alternado', { bpm: 80, seq: '6.3 [G:4321] 4.0 [G:4321] | 5.3 [C:4321] 4.2 [C:4321] | 4.0 [D:321] 5.0 [D:321] | 6.3 [G:4321] 4.0 [G:4321]', repeat: 2 }, 'El bajo cambia de cuerda en el pulso 3.')),
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
        P('Entrena el oído', { trainer: 'ear', level: 0, count: 6 }),
        P('Tócalos',
          ex('int1', 'Desde Do', { bpm: 70, unit: 2, seq: '5.3 5.5 | 5.3 4.2 | 5.3 4.3 | 5.3 4.5 | 5.3 3.5:6' }, 'Do-Re, Do-Mi, Do-Fa, Do-Sol, Do-Do.')),
      ] },
      { id: 'l-mayor', title: 'La escala mayor', min: 8, pages: [
        P('La fórmula',
          { p: 'La escala mayor es la de **Do Re Mi Fa Sol La Si Do**. Lo que la define es el orden de las distancias:' },
          { p: '**Tono · Tono · Semitono · Tono · Tono · Tono · Semitono**' },
          { p: 'Empezando en Do no hace falta ningún #. Empezando en Sol, para respetar la fórmula, aparece un **Fa#**.' },
          { fun: 'Los nombres Do, Re, Mi… vienen de las primeras sílabas de los versos de un himno en latín que usó el monje Guido d’Arezzo, en el siglo XI, para enseñar a cantar. «Do» antes se llamaba «Ut».' }),
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
          { jam: 'rock-am' },
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
          { jam: 'blues-a' },
          ex('blu2', 'Lick de blues', { bpm: 80, drums: 'rock', seq: '3.5 3.7 3.8 3.7 | 3.5 4.7:3 | 5.5 5.6 5.7 4.5 | 4.7:4', repeat: 2 })),
      ] },
      { id: 'l-improvisar', title: 'Improvisar sin miedo', min: 10, pages: [
        P('No hay notas equivocadas (casi)',
          { p: 'Improvisar no es tocar rápido ni saber mil escalas: es **decir algo** con pocas notas. Cinco reglas que funcionan desde el primer día:' },
          { p: '**1. Pocas notas.** Tres notas bien elegidas dicen más que veinte.' },
          { p: '**2. Repite.** Toca una idea, repítela, y cámbiala un poquito la tercera vez. El oído ama reconocer.' },
          { p: '**3. Pregunta y respuesta.** Una frase que queda abierta, otra que cierra.' },
          { p: '**4. Termina en una nota del acorde.** En la pantalla de improvisar tienen un anillo: son las más seguras para descansar.' },
          { p: '**5. Deja silencio.** Respirar entre frases es lo que más diferencia a un principiante de alguien que suena musical.' }),
        P('Mayor y menor con la misma forma',
          { p: 'La pentatónica de **Mi menor** y la pentatónica de **Sol mayor** tienen **las mismas notas** (son relativas). Lo que cambia es la nota donde descansas: sobre una base en Sol, volver a Sol suena alegre.' },
          { jam: 'balada-em' },
          { jam: 'pop-g' }),
        P('Un sabor nuevo: el modo dórico',
          { p: 'El **dórico** es una escala menor con la sexta mayor. Suena menos triste, más «cool»: funk, jazz, rock latino. En Re dórico esa nota especial es **Si (B)**.' },
          { jam: 'groove-dm' },
          { fun: 'Hay improvisadores famosos que tocaban un solo entero sobre un único acorde dórico durante minutos. El secreto era el ritmo, no la cantidad de notas.' }),
        P('Para cerrar', quiz(
          Q('¿Cuál es la nota más segura para terminar una frase?', ['Cualquiera de la escala', 'Una nota del acorde que suena', 'La más aguda'], 1, 'Las notas del acorde «descansan»; las otras piden seguir.'),
          Q('Em pentatónica y G pentatónica mayor…', ['Comparten notas', 'No tienen nada que ver', 'Son la misma escala que la de blues'], 0, 'Son relativas: mismas notas, distinto centro.'))),
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
      { id: 'l-triadas', title: 'Tríadas: acordes chicos que suenan grande', min: 9, pages: [
        P('Tres notas, tres cuerdas',
          { p: 'Un acorde no necesita seis cuerdas. Con **tres notas** (raíz, 3ª y 5ª) en las **tres cuerdas agudas** tienes una **tríada**: suena clara, no choca con el bajo ni con otra guitarra, y se mueve por todo el mástil.' },
          { p: 'La misma tríada aparece en tres órdenes (las **inversiones**): con la raíz arriba, en el medio o abajo. Mira Do mayor subiendo por el mástil:' },
          { fret: { to: 15, marks: [[3, 0, 'G'], [2, 1, 'C', 1], [1, 0, 'E'], [3, 5, 'C', 1], [2, 5, 'E'], [1, 3, 'G'], [3, 9, 'E'], [2, 8, 'G'], [1, 8, 'C', 1], [3, 12, 'G'], [2, 13, 'C', 1], [1, 12, 'E']].map(([s, f, label, r]) => ({ s, f, label, root: !!r })) } },
          { fun: 'Muchos guitarristas de funk, soul y pop casi no tocan acordes completos: viven en las tríadas de las cuerdas agudas.' }),
        P('Cuatro acordes en un mismo lugar',
          { p: 'Lo mejor: puedes tocar una progresión entera **sin mover la mano**. C – Am – F – G, todo cerca del traste 5:' },
          { chords: ['C (tríada)', 'Am (tríada)', 'F (tríada)', 'G (tríada)'] },
          { tip: 'Fíjate qué nota cambia de un acorde al siguiente: casi siempre es una sola, y por un traste. Eso es **conducción de voces**.' }),
        P('Pruébalo',
          { p: 'Toca la tríada de Do nota por nota: 3ª cuerda traste 5, 2ª traste 5, 1ª traste 3.' },
          { tryit: { label: 'Do mayor en las tres cuerdas agudas', notes: [[3, 5], [2, 5], [1, 3]] } }),
        P('A tocar',
          ex('tri1', 'C – Am – F – G en tríadas', { bpm: 75, drums: 'soft', seq: '[C (tríada)]:2 [C (tríada)]:2 | [Am (tríada)]:2 [Am (tríada)]:2 | [F (tríada)]:2 [F (tríada)]:2 | [G (tríada)]:2 [G (tríada)]:2', repeat: 2 }),
          ex('tri2', 'Arpegiadas', { bpm: 80, unit: 0.5, seq: '3.5 2.5 1.3 2.5 3.5 2.5 1.3 2.5 | 3.5 2.5 1.5 2.5 3.5 2.5 1.5 2.5 | 3.5 2.6 1.5 2.6 3.5 2.6 1.5 2.6 | 3.7 2.8 1.7 2.8 3.7 2.8 1.7 2.8', repeat: 2 }, 'Una nota por vez: escucha cómo se mueve una sola voz.')),
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
      { id: 'l-bends', title: 'Bends y vibrato', min: 8, pages: [
        P('Estirar la cuerda',
          { p: 'En un **bend** empujas la cuerda hacia arriba (o abajo) mientras suena, y la nota sube. El más común es de **un tono**: el traste 7 de la 3ª cuerda (Re) tiene que llegar a sonar como el traste 9 (**Mi**).' },
          { listen: [{ label: 'Desde dónde (3ª, traste 7)', notes: [midiOf(3, 7)] }, { label: 'A dónde llegar (3ª, traste 9)', notes: [midiOf(3, 9)] }] },
          { tip: 'Usa **tres dedos** juntos (1, 2 y 3) para empujar, y gira la muñeca como si abrieras una puerta. Con un solo dedo no llegas.' }),
        P('Medio tono',
          { p: 'Empieza por bends chicos. Toca, estira, y mira la aguja: tiene que entrar en la zona verde y **quedarse ahí** un momento.' },
          { trainer: 'bend', level: 0, count: 5 }),
        P('Un tono',
          { p: 'Ahora un tono entero en la 3ª cuerda. Si te pasas, la aguja se pone roja: afloja un poco.' },
          { trainer: 'bend', level: 1, count: 5 }),
        P('Vibrato',
          { p: 'El **vibrato** es un bend chiquito que va y viene, parejo, como una voz. Es la firma de cada guitarrista: escucha cómo lo hacen tus favoritos.' },
          { p: 'Pruébalo al final de una frase: toca la nota, espera un instante, y empieza a moverla. Más lento suena a blues; más rápido, a rock.' },
          { fun: 'En el violín y la guitarra clásica el vibrato se hace a lo largo de la cuerda; en la guitarra eléctrica casi siempre se hace estirándola.' }),
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

  // ------------------------------------------------------------------ text with **bold** (translated)
  function rich(text) {
    const out = [];
    String(tr(text)).split(/(\*\*[^*]+\*\*)/).forEach((part) => {
      if (part.startsWith('**') && part.endsWith('**')) out.push(el('b', { text: part.slice(2, -2) }));
      else if (part) out.push(document.createTextNode(part));
    });
    return out;
  }
  const txt = (x) => (typeof x === 'function' ? x() : tr(x));

  // ------------------------------------------------------------------ hearing your guitar (the tuner's pitch, live)
  const Ear = {
    guitar: null, cb: null, last: null,
    /** Starts listening; fn(reading) gets {midi (float), level} or null for silence, ~30 times a second. */
    async start(fn) {
      this.cb = fn;
      const g = await G.call('gameGuitar', {});
      this.guitar = g && g.ok ? g.data : null;
      await G.call('setTuner', { on: true, trackId: this.guitar ? this.guitar.trackId : 0 });
      G.App.onTuner = (r) => {
        if (!this.cb) return;
        this.last = r;
        if (!r.active || !(r.freq > 20) || r.level < 0.002) return this.cb(null, r);
        this.cb({ midi: 69 + 12 * Math.log2(r.freq / 440), level: r.level }, r);
      };
      return this.guitar;
    },
    async create() {
      const r = await G.run('gameGuitar', { create: true });
      if (r) { this.guitar = r; await G.call('setTuner', { on: true, trackId: r.trackId }); }
      return this.guitar;
    },
    stop() {
      if (!this.cb) return;
      this.cb = null;
      G.App.onTuner = null;
      G.call('setTuner', { on: false });
    },
  };
  /** Which input of the interface the instrument comes in on (changes the listening track's input). */
  function inputSelect(g, after) {
    const sources = (G.audio && G.audio.sources) || [];
    if (!sources.length) return el('span', { text: L2('entrada ', 'input ') + ((g.input || 0) + 1) });
    return el('select.g-select.l-insel', { 'aria-label': L2('Entrada', 'Input'), onchange: async (e) => {
      const ch = parseInt(e.target.value, 10);
      await G.call('setTrack', { trackId: g.trackId, inputChannel: ch, stereoInput: false });
      g.input = ch;
      await G.call('setTuner', { on: true, trackId: g.trackId });
      if (after) after();
    } }, sources.map((name, i) => el('option', { value: i, selected: i === g.input, text: name })));
  }

  /** The «where your guitar comes in» line, with a level meter. */
  function earBar(onReady) {
    const meter = el('i.l-lvl');
    const line = el('div.l-earbar', null, el('span.l-lvlwrap', null, meter), el('span', { text: L2('Conectando…', 'Connecting…') }));
    const show = () => {
      const g = Ear.guitar;
      line.replaceChildren(el('span.l-lvlwrap', { title: L2('Lo que entra de tu guitarra', 'What comes in from your guitar') }, meter),
        g ? el('span.l-earsrc', null, L2('Te escucho por ', 'Listening through ') + g.name + ' · ', inputSelect(g, show))
          : el('span', null, L2('No tengo por dónde escucharte. ', "I can't hear you yet. "),
            el('button.btn', { onclick: async () => { await Ear.create(); show(); if (onReady) onReady(); } }, L2('Escuchar mi guitarra', 'Listen to my guitar'))));
    };
    line._meter = (r) => { const lv = r && r.level > 0 ? 20 * Math.log10(r.level) : -100; meter.style.width = Math.max(0, Math.min(100, (lv + 60) * 100 / 54)) + '%'; };
    line._show = show;
    return line;
  }

  // ------------------------------------------------------------------ progress for the trainers
  const tp = (kind) => { prog.tr = prog.tr || {}; const k = kind + (noteKind(kind) && isBass() ? ':bass' : ''); return (prog.tr[k] = prog.tr[k] || { lv: {}, best: 0, miss: {} }); };
  const starsFor = (acc) => (acc >= 0.95 ? 3 : acc >= 0.8 ? 2 : acc >= 0.6 ? 1 : 0);
  function pickWeighted(items, kind, keyOf) {
    const miss = tp(kind).miss;
    const w = items.map((it) => 1 + 2 * Math.min(5, miss[keyOf(it)] || 0));
    let r = Math.random() * w.reduce((a, b) => a + b, 0);
    for (let i = 0; i < items.length; i++) { r -= w[i]; if (r <= 0) return items[i]; }
    return items[items.length - 1];
  }
  function noteMiss(kind, key, wrong) {
    const m = tp(kind).miss;
    m[key] = Math.max(0, (m[key] || 0) + (wrong ? 1 : -0.5));
    save();
  }

  // ------------------------------------------------------------------ the trainers
  const NATURALS = [0, 2, 4, 5, 7, 9, 11];
  // guitar or bass (for the note trainers and the jam): 4-string bass E1 A1 D2 G2
  const BASS_OPEN = [28, 33, 38, 43];
  const isBass = () => prog.inst === 'bass';
  const tuning = () => (isBass() ? BASS_OPEN : OPEN);
  const openOf = (str, tun = tuning()) => tun[tun.length - str];
  const BASS_LEVELS = [
    { name: ['Cuerda 4 (Mi)', 'String 4 (E)'], strings: [4] },
    { name: ['Cuerda 3 (La)', 'String 3 (A)'], strings: [3] },
    { name: ['Cuerdas 4 y 3, con sostenidos', 'Strings 4 and 3, with sharps'], strings: [4, 3], sharps: true },
    { name: ['Cuerdas 2 y 1 (Re y Sol)', 'Strings 2 and 1 (D and G)'], strings: [2, 1] },
    { name: ['Todo el mástil', 'The whole neck'], strings: [4, 3, 2, 1], sharps: true },
  ];
  const BASS_ENDLESS = { name: ['Práctica eterna', 'Endless practice'], strings: [4, 3, 2, 1], sharps: true };
  const noteKind = (kind) => kind === 'find' || kind === 'name' || kind === 'every';
  const levelsOf = (kind) => (kind !== 'every' && noteKind(kind) && isBass() ? BASS_LEVELS : TRAINERS[kind].levels);
  const endlessOf = (kind) => (kind !== 'every' && noteKind(kind) && isBass() ? BASS_ENDLESS : TRAINERS[kind].endless);
  const NOTE_LEVELS = [
    { name: ['Cuerda 6', 'String 6'], strings: [6] },
    { name: ['Cuerda 5', 'String 5'], strings: [5] },
    { name: ['Cuerdas 6 y 5, con sostenidos', 'Strings 6 and 5, with sharps'], strings: [6, 5], sharps: true },
    { name: ['Cuerdas 4 y 3', 'Strings 4 and 3'], strings: [4, 3] },
    { name: ['Cuerdas 2 y 1', 'Strings 2 and 1'], strings: [2, 1] },
    { name: ['Todo el mástil', 'The whole neck'], strings: [6, 5, 4, 3, 2, 1], sharps: true },
  ];
  const ENDLESS_NOTES = { name: ['Práctica eterna', 'Endless practice'], strings: [6, 5, 4, 3, 2, 1], sharps: true };
  const INTERVALS = { 1: ['2ª menor', 'minor 2nd'], 2: ['2ª mayor', 'major 2nd'], 3: ['3ª menor', 'minor 3rd'], 4: ['3ª mayor', 'major 3rd'], 5: ['4ª justa', 'perfect 4th'], 6: ['Tritono', 'Tritone'], 7: ['5ª justa', 'perfect 5th'], 8: ['6ª menor', 'minor 6th'], 9: ['6ª mayor', 'major 6th'], 10: ['7ª menor', 'minor 7th'], 11: ['7ª mayor', 'major 7th'], 12: ['Octava', 'Octave'] };
  const EAR_LEVELS = [
    { name: ['3ª mayor, 5ª y octava', 'Major 3rd, 5th and octave'], set: [4, 7, 12] },
    { name: ['+ 3ª menor y 4ª', '+ minor 3rd and 4th'], set: [3, 4, 5, 7, 12] },
    { name: ['+ 2ª, 6ª y 7ª', '+ 2nd, 6th and 7th'], set: [2, 3, 4, 5, 7, 9, 10, 12] },
    { name: ['Todos, subiendo y bajando', 'All of them, up and down'], set: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12], down: true },
  ];
  const CHORD_KINDS = { M: [[0, 4, 7], ['Mayor', 'Major']], m: [[0, 3, 7], ['Menor', 'Minor']], 7: [[0, 4, 7, 10], ['Séptima (7)', 'Dominant 7']], maj7: [[0, 4, 7, 11], ['Séptima mayor (maj7)', 'Major 7']], m7: [[0, 3, 7, 10], ['Menor séptima (m7)', 'Minor 7']], 5: [[0, 7, 12], ['Power chord (5)', 'Power chord (5)']], dim: [[0, 3, 6], ['Disminuido', 'Diminished']], sus4: [[0, 5, 7], ['Suspendido (sus4)', 'Suspended (sus4)']] };
  const CHORD_LEVELS = [
    { name: ['Mayor o menor', 'Major or minor'], set: ['M', 'm'] },
    { name: ['+ séptima', '+ dominant 7'], set: ['M', 'm', '7'] },
    { name: ['+ maj7, m7 y power chord', '+ maj7, m7 and power chord'], set: ['M', 'm', '7', 'maj7', 'm7', '5'] },
    { name: ['Todos', 'All of them'], set: ['M', 'm', '7', 'maj7', 'm7', '5', 'dim', 'sus4'] },
  ];
  // bends: string, fret, how many semitones up
  const BEND_LEVELS = [
    { name: ['Medio tono', 'Half step'], set: [[3, 7, 1], [2, 8, 1], [2, 10, 1], [1, 8, 1]] },
    { name: ['Un tono en la 3ª cuerda', 'Whole step on string 3'], set: [[3, 7, 2], [3, 9, 2], [3, 5, 2], [3, 4, 2]] },
    { name: ['Un tono en la 2ª y la 1ª', 'Whole step on strings 2 and 1'], set: [[2, 8, 2], [2, 10, 2], [1, 8, 2], [1, 10, 2], [2, 5, 2]] },
    { name: ['Hasta tono y medio', 'Up to a step and a half'], set: [[3, 7, 2], [2, 8, 2], [2, 8, 3], [3, 7, 3], [2, 10, 1], [1, 10, 2]] },
  ];
  const EVERY_LEVELS = [
    { name: ['Notas naturales', 'Natural notes'] },
    { name: ['Con sostenidos y bemoles', 'With sharps and flats'], sharps: true },
  ];
  const TRAINERS = {
    every: { icon: '🧭', title: ['Todas sus posiciones', 'Every position'], desc: ['Una nota en todas las cuerdas, una tras otra. El primer paso para memorizar el mástil: ves que siempre forma el mismo dibujo.', 'One note on every string, one after another. The first step to memorising the neck: it always makes the same pattern.'], levels: EVERY_LEVELS, endless: { name: ['Práctica eterna', 'Endless practice'], sharps: true }, live: true },
    find: { icon: '🎯', title: ['Encuentra la nota', 'Find the note'], desc: ['Te pido una nota en una cuerda y la buscas en tu guitarra. Te escucho y te digo dónde caíste.', 'I ask for a note on a string and you find it on your guitar. I listen and tell you where you landed.'], levels: NOTE_LEVELS, endless: ENDLESS_NOTES, live: true },
    name: { icon: '👀', title: ['¿Qué nota es?', 'Which note is it?'], desc: ['Te muestro un punto en el mástil y eliges su nombre.', 'I show a spot on the neck and you pick its name.'], levels: NOTE_LEVELS, endless: ENDLESS_NOTES },
    ear: { icon: '👂', title: ['Intervalos de oído', 'Ear: intervals'], desc: ['Escuchas dos notas y dices qué distancia hay.', 'Hear two notes and name the distance.'], levels: EAR_LEVELS, endless: { name: ['Práctica eterna', 'Endless practice'], set: EAR_LEVELS[3].set, down: true } },
    chordEar: { icon: '🎵', title: ['Acordes de oído', 'Ear: chords'], desc: ['¿Mayor, menor, séptima…? Escúchalo y elige.', 'Major, minor, seventh…? Listen and choose.'], levels: CHORD_LEVELS, endless: { name: ['Práctica eterna', 'Endless practice'], set: CHORD_LEVELS[3].set } },
    bend: { icon: '〰️', title: ['Bends afinados', 'Bends in tune'], desc: ['Estira la cuerda hasta la nota justa. Te muestro en vivo si te falta o te pasas.', 'Bend the string up to the right note. I show you live if you are short or past it.'], levels: BEND_LEVELS, endless: { name: ['Práctica eterna', 'Endless practice'], set: BEND_LEVELS[3].set.concat(BEND_LEVELS[0].set) }, live: true },
  };
  const lvName = (lv) => L2(lv.name[0], lv.name[1]);

  /** A trainer, in a box. opts: {kind, level (index, or 'endless'), count, onExit, inline} */
  function trainer(opts) {
    const T = TRAINERS[opts.kind];
    const endless = opts.level === 'endless';
    const lv = endless ? endlessOf(opts.kind) : levelsOf(opts.kind)[opts.level];
    const count = endless ? Infinity : (opts.count || 10);
    const box = el('div.l-trainer' + (opts.inline ? '.inline' : ''));
    const stat = { n: 0, right: 0, streak: 0, best: tp(opts.kind).best || 0, startedAt: 0 };
    const head = el('div.l-trhead');
    const stage = el('div.l-trstage');
    const foot = el('div.l-trfoot');
    let bar = null, cleanup = null, finished = false;
    box.append(head, stage, foot);
    if (T.live) { bar = earBar(() => {}); box.insertBefore(bar, stage); }

    function header() {
      head.replaceChildren(
        el('div', null, el('b', { text: (opts.inline ? '' : T.icon + ' ') + L2(T.title[0], T.title[1]) }), el('small', { text: lvName(lv) })),
        el('div.l-trstats', null,
          el('span', null, el('small', { text: L2('Bien', 'Right') }), el('b', { text: endless ? String(stat.right) : `${stat.right}/${Math.min(stat.n, count)}` })),
          el('span', null, el('small', { text: L2('Racha', 'Streak') }), el('b', { text: String(stat.streak) })),
          endless ? el('span', null, el('small', { text: L2('Récord', 'Record') }), el('b', { text: String(stat.best) })) : null));
    }
    function result(ok, key) {
      stat.n++;
      if (ok) { stat.right++; stat.streak++; } else stat.streak = 0;
      if (endless && stat.streak > stat.best) { stat.best = stat.streak; tp(opts.kind).best = stat.best; save(); }
      if (key !== undefined) noteMiss(opts.kind, key, !ok);
      header();
    }
    function next() {
      if (finished) return;
      if (cleanup) { cleanup(); cleanup = null; }
      if (stat.n >= count) return finish();
      cleanup = ASK[opts.kind](lv, stage, (ok, key) => result(ok, key), () => setTimeout(next, 0), endless, opts.kind) || null;
    }
    function finish() {
      finished = true;
      if (cleanup) { cleanup(); cleanup = null; }
      Ear.stop();
      const acc = stat.n ? stat.right / stat.n : 0;
      const st = starsFor(acc);
      if (!endless) { const t = tp(opts.kind); t.lv[opts.level] = Math.max(t.lv[opts.level] || 0, st); save(); }
      const nextLv = !endless && levelsOf(opts.kind)[opts.level + 1];
      stage.replaceChildren(
        el('div.l-stars', null, [0, 1, 2].map((i) => el('span' + (i < st ? '.on' : ''), { text: '★' }))),
        el('p.l-score', { text: `${stat.right} ${L2('de', 'of')} ${stat.n}. ` + (st === 3 ? L2('¡Perfecto!', 'Perfect!') : st === 2 ? L2('Muy bien.', 'Very good.') : st === 1 ? L2('Vas bien: una vuelta más.', 'Getting there: one more round.') : L2('Repasa y prueba otra vez, sin apuro.', 'Review and try again, no rush.')) }),
        el('div.row', null,
          el('button.btn' + (st >= 2 && nextLv ? '' : '.primary'), { onclick: () => restart(opts.level) }, L2('Otra ronda', 'Another round')),
          nextLv && !opts.inline ? el('button.btn' + (st >= 2 ? '.primary' : ''), { onclick: () => restart(opts.level + 1) }, L2('Siguiente nivel', 'Next level')) : null,
          opts.onExit ? el('button.btn', { onclick: opts.onExit }, L2('Volver', 'Back')) : null));
      foot.replaceChildren();
    }
    function restart(level) {
      const nb = trainer(Object.assign({}, opts, { level }));
      box.replaceWith(nb);
    }
    header();
    if (endless || !opts.inline) foot.append(el('button.btn', { onclick: () => (endless ? finish() : opts.onExit ? (stop(), opts.onExit()) : finish()) }, endless ? L2('Terminar', 'Finish') : L2('Salir', 'Quit')));
    // keys: 1…9 pick an answer, R plays it again
    const onKey = (e) => {
      if (!box.isConnected || finished) return document.removeEventListener('keydown', onKey, true);
      if (e.target.closest && e.target.closest('input, select, textarea')) return;
      const k = parseInt(e.key, 10);
      if (k >= 1 && k <= 9) { const b = [...stage.querySelectorAll('.l-opt:not(:disabled)')][k - 1]; if (b) { e.preventDefault(); e.stopPropagation(); b.click(); } }
      else if (e.key === 'r' || e.key === 'R') { const h = stage.querySelector('.l-hear'); if (h) { e.preventDefault(); e.stopPropagation(); h.click(); } }
    };
    document.addEventListener('keydown', onKey, true);
    const stop = () => { finished = true; document.removeEventListener('keydown', onKey, true); if (cleanup) cleanup(); cleanup = null; Ear.stop(); };
    box._stop = stop;
    if (T.live) {
      Ear.start((r, raw) => { bar._meter(raw); if (box._onPitch) box._onPitch(r); }).then(() => { bar._show(); next(); });
    } else next();
    // when the box leaves the page, stop listening
    activeTrainers.push(box);
    return box;
  }
  const activeTrainers = [];
  function stopTrainers() { while (activeTrainers.length) { const b = activeTrainers.pop(); if (b._stop) b._stop(); } if (activeTry) activeTry._stop(); stopJam(); stopSound(); }

  const noteBtnLabel = (pc) => (NATURALS.includes(pc) ? noteName(pc) : (G.lang === 'en' ? `${EN[pc]} / ${FLAT_EN[pc]}` : `${ES[pc]} / ${FLAT_ES[pc]}`));
  const FLAT_ES = { 1: 'Reb', 3: 'Mib', 6: 'Solb', 8: 'Lab', 10: 'Sib' };
  const FLAT_EN = { 1: 'Db', 3: 'Eb', 6: 'Gb', 8: 'Ab', 10: 'Bb' };

  /** Each kind of question: draws itself in `stage`, calls done(ok, key) once and next() when it's time. Returns a cleanup. */
  const ASK = {
    every(lv, stage, done, next, endless, kind) {
      const pcs = lv.sharps ? [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] : NATURALS;
      let pc;
      do { pc = pickWeighted(pcs, kind, (x) => String(x)); } while (ASK._lastE === pc && pcs.length > 1);
      ASK._lastE = pc;
      const tun = tuning(), ns = tun.length;
      const order = Array.from({ length: ns }, (_, i) => ns - i);     // thick string first
      const found = new Map();                                       // string → fret
      let wrong = 0, finished = false, stable = { m: null, n: 0 }, lastJudged = null, armed = true;
      const fb = el('div'), say = el('p.l-trsay');
      const target = () => order.find((s) => !found.has(s));
      const draw = () => fb.replaceChildren(fretboard({ to: 12, tuning: tun, hl: target(), marks: [...found].map(([s, f]) => ({ s, f, label: EN[pc], good: true })), onPick: (s, f) => judge(tun[ns - s] + f, true, s) }));
      stage.replaceChildren(el('p.l-trq', null, ...rich(L2(`Encuentra **${noteBtnLabel(pc)}** en todas las cuerdas, de la más gruesa a la más fina`, `Find **${noteBtnLabel(pc)}** on every string, thickest to thinnest`))), fb, say,
        el('div.row', null, el('button.btn', { onclick: () => { for (const s of order) if (!found.has(s)) { for (let f = 0; f <= 12; f++) if ((tun[ns - s] + f) % 12 === pc) { found.set(s, f); break; } } wrong += 3; draw(); end(); } }, L2('Mostrar', 'Show me'))));
      draw();
      function end() {
        if (finished) return;
        finished = true;
        say.replaceChildren(el('b.l-ok', { text: wrong === 0 ? L2('¡Perfecto! ', 'Perfect! ') : L2('¡Completo! ', 'Complete! ') }), document.createTextNode(L2('Mira el dibujo que forman: es el mismo en todas las notas.', 'Look at the pattern they make: it’s the same for every note.')));
        done(wrong <= 1, String(pc));
        setTimeout(next, 2200);
      }
      function judge(m, clicked, clickedString) {
        if (finished) return;
        const t = target();
        const f = m - tun[ns - t];
        if (clicked) hearNotes([m]);
        if (((m % 12) + 12) % 12 === pc && (clicked ? clickedString === t : true) && f >= 0 && f <= 15) {
          found.set(t, f);
          say.replaceChildren(el('b.l-ok', { text: '✓ ' }), document.createTextNode(L2(`Cuerda ${t}, traste ${f}.`, `String ${t}, fret ${f}.`) + (target() ? L2(` Ahora la ${target()}.`, ` Now string ${target()}.`) : '')));
          draw();
          if (!target()) end();
          return;
        }
        wrong++;
        say.replaceChildren(el('b.l-bad', { text: L2('Casi: ', 'Close: ') }), document.createTextNode(((m % 12) + 12) % 12 === pc ? L2(`es ${noteBtnLabel(pc)}, pero búscala en la cuerda ${t}.`, `that's ${noteBtnLabel(pc)}, but look for it on string ${t}.`) : L2(`eso fue ${noteName(((m % 12) + 12) % 12)}.`, `that was ${noteName(((m % 12) + 12) % 12)}.`)));
      }
      const host = stage.parentElement;
      host._onPitch = (r) => {
        if (!r) { stable = { m: null, n: 0 }; armed = true; return; }
        const m = Math.round(r.midi);
        if (Math.abs(r.midi - m) > 0.4) return;
        if (stable.m === m) stable.n++; else stable = { m, n: 1 };
        if (stable.n === 3 && (armed || m !== lastJudged)) { armed = false; lastJudged = m; judge(m, false); }
      };
      return () => { host._onPitch = null; };
    },

    find(lv, stage, done, next, endless, kind) {
      const pcs = lv.sharps ? [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] : NATURALS;
      const items = [];
      for (const s of lv.strings) for (const pc of pcs) items.push([s, pc]);
      let it;
      do { it = pickWeighted(items, kind, (x) => x.join(':')); } while (ASK._last === it.join(':') && items.length > 1);
      ASK._last = it.join(':');
      const [str, pc] = it;
      const open = openOf(str);
      const frets = []; for (let f = 0; f <= 12; f++) if ((open + f) % 12 === pc) frets.push(f);
      let tries = 0, judged = false, armed = true, stable = { m: null, n: 0 }, lastJudged = null;
      const marks = [];
      const say = el('p.l-trsay');
      const t0 = performance.now();
      const draw = () => fb.replaceChildren(fretboard({ to: 12, hl: str, marks, tuning: tuning(), onPick: (s, f) => { if (s === str) judge(open + f, true); } }));
      const fb = el('div');
      stage.replaceChildren(
        el('p.l-trq', null, ...rich(L2(`Toca **${noteBtnLabel(pc)}** en la cuerda **${str}**`, `Play **${noteBtnLabel(pc)}** on string **${str}**`))),
        fb, say,
        el('div.row', null, el('button.l-hear', { onclick: () => hearNotes([open + frets[0]]) }, G.icon('play', 12), L2('¿Cómo suena?', 'How does it sound?')),
          el('button.btn', { onclick: () => reveal(true) }, L2('Mostrar', 'Show me'))));
      draw();
      function reveal(gaveUp) {
        if (judged) return;
        judged = true;
        for (const f of frets) marks.push({ s: str, f, label: EN[pc], good: true });
        draw();
        say.replaceChildren(...rich(L2(`Está en el traste **${frets.join(' y ')}**.`, `It's on fret **${frets.join(' and ')}**.`)));
        done(false, it.join(':'));
        setTimeout(next, gaveUp ? 2200 : 2000);
      }
      function judge(midi, clicked) {
        if (judged) return;
        const f = midi - open;
        if (midi % 12 === pc && f >= 0 && f <= 15) {
          judged = true;
          marks.push({ s: str, f, label: EN[pc], good: true });
          draw();
          const secs = (performance.now() - t0) / 1000;
          say.replaceChildren(el('b.l-ok', { text: L2('¡Sí! ', 'Yes! ') }), ...rich(L2(`Traste ${f}`, `Fret ${f}`) + (secs < 3 ? L2(' · rapidísimo', ' · lightning fast') : ` · ${secs.toFixed(1)} s`)));
          if (clicked) hearNotes([midi]);
          done(tries === 0, it.join(':'));
          setTimeout(next, 1100);
          return;
        }
        tries++;
        const name = noteName(((midi % 12) + 12) % 12);
        let msg;
        if (midi % 12 === pc) msg = L2(`Es ${noteBtnLabel(pc)}, pero en otra cuerda u octava. Búscala en la cuerda ${str}.`, `That's ${noteBtnLabel(pc)}, but on another string or octave. Look for it on string ${str}.`);
        else if (f >= 0 && f <= 15) {
          marks.push({ s: str, f, label: EN[((midi % 12) + 12) % 12], bad: true });
          const target = frets.reduce((a, b) => (Math.abs(b - f) < Math.abs(a - f) ? b : a));
          const d = target - f;
          msg = L2(`Eso es ${name} (traste ${f}). Te ${d > 0 ? 'faltan' : 'pasaste por'} ${Math.abs(d)} ${Math.abs(d) === 1 ? 'traste' : 'trastes'}.`, `That's ${name} (fret ${f}). ${d > 0 ? 'Go up' : 'Go down'} ${Math.abs(d)} ${Math.abs(d) === 1 ? 'fret' : 'frets'}.`);
          draw();
        } else msg = L2(`Eso es ${name}, y no está en la cuerda ${str}. ¿Otra cuerda?`, `That's ${name}, and it isn't on string ${str}. Another string?`);
        say.replaceChildren(el('b.l-bad', { text: L2('Casi. ', 'Close. ') }), document.createTextNode(msg));
        if (clicked) hearNotes([midi]);
        if (tries >= 3) reveal(false);
      }
      // live: a note counts when it holds steady for a moment; the same note again needs a new attack
      stage.closest && null;
      const host = stage.parentElement;
      host._onPitch = (r) => {
        if (!r) { stable = { m: null, n: 0 }; armed = true; return; }
        const m = Math.round(r.midi);
        if (Math.abs(r.midi - m) > 0.4) return;
        if (stable.m === m) stable.n++; else stable = { m, n: 1 };
        if (stable.n === 3 && (armed || m !== lastJudged)) { armed = false; lastJudged = m; judge(m, false); }
      };
      return () => { host._onPitch = null; };
    },

    name(lv, stage, done, next, endless, kind) {
      const pcs = lv.sharps ? [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] : NATURALS;
      const items = [];
      for (const s of lv.strings) for (let f = 0; f <= 12; f++) if (pcs.includes((openOf(s) + f) % 12)) items.push([s, f]);
      let it;
      do { it = pickWeighted(items, kind, (x) => x.join(':')); } while (ASK._lastN === it.join(':'));
      ASK._lastN = it.join(':');
      const [str, f] = it, pc = (openOf(str) + f) % 12;
      const say = el('p.l-trsay');
      const opts = pcs.length > 7 ? [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] : NATURALS;
      const btns = opts.map((o) => el('button.l-opt', { onclick: () => {
        btns.forEach((b, i) => { b.disabled = true; b.classList.toggle('right', opts[i] === pc); b.classList.toggle('wrong', opts[i] === o && o !== pc); });
        say.replaceChildren(...rich(L2(`Cuerda ${str}, traste ${f}: **${noteBtnLabel(pc)}**.`, `String ${str}, fret ${f}: **${noteBtnLabel(pc)}**.`)));
        hearNotes([openOf(str) + f]);
        done(o === pc, it.join(':'));
        setTimeout(next, o === pc ? 900 : 1800);
      } }, noteBtnLabel(o)));
      stage.replaceChildren(el('p.l-trq', { text: L2('¿Qué nota es?', 'Which note is it?') }), fretboard({ to: 12, tuning: tuning(), marks: [{ s: str, f, label: '?' }] }), el('div.l-opts' + (opts.length > 7 ? '.wide' : ''), null, btns), say);
    },

    ear(lv, stage, done, next, endless, kind) {
      const st = pickWeighted(lv.set, kind, (x) => String(x));
      const down = lv.down && Math.random() < 0.4;
      const low = 50 + Math.floor(Math.random() * 10);
      const notes = down ? [low + st, low] : [low, low + st];
      const playIt = () => hearNotes(notes, 0.75);
      const say = el('p.l-trsay');
      const btns = lv.set.map((o) => el('button.l-opt', { onclick: () => {
        btns.forEach((b, i) => { b.disabled = true; b.classList.toggle('right', lv.set[i] === st); b.classList.toggle('wrong', lv.set[i] === o && o !== st); });
        say.textContent = L2('Era ', 'It was ') + L2(INTERVALS[st][0], INTERVALS[st][1]) + (down ? L2(', bajando.', ', going down.') : '.');
        done(o === st, String(st));
        setTimeout(next, o === st ? 1000 : 2000);
      } }, L2(INTERVALS[o][0], INTERVALS[o][1])));
      stage.replaceChildren(el('p.l-trq', { text: L2('¿Qué intervalo es?', 'Which interval is it?') }), el('div.row', null, listenBtn(L2('Escuchar de nuevo', 'Hear it again'), playIt)), el('div.l-opts', null, btns), say);
      setTimeout(playIt, 250);
    },

    chordEar(lv, stage, done, next, endless, kind) {
      const k = pickWeighted(lv.set, kind, (x) => x);
      const root = 45 + Math.floor(Math.random() * 10);
      const ps = CHORD_KINDS[k][0].map((x) => root + x);
      const playIt = () => playGuide([{ t: 0, p: ps, d: 1.6 }, { t: 1.9, p: ps.slice(0, 1), d: 0.5 }].concat(ps.map((p, i) => ({ t: 2.5 + i * 0.35, p: [p], d: 0.5 }))));
      const say = el('p.l-trsay');
      const btns = lv.set.map((o) => el('button.l-opt', { onclick: () => {
        btns.forEach((b, i) => { b.disabled = true; b.classList.toggle('right', lv.set[i] === k); b.classList.toggle('wrong', lv.set[i] === o && o !== k); });
        say.textContent = L2('Era ', 'It was ') + L2(CHORD_KINDS[k][1][0], CHORD_KINDS[k][1][1]).toLowerCase() + '.';
        done(o === k, k);
        setTimeout(next, o === k ? 1000 : 2000);
      } }, L2(CHORD_KINDS[o][1][0], CHORD_KINDS[o][1][1])));
      stage.replaceChildren(el('p.l-trq', { text: L2('¿Qué tipo de acorde es?', 'What kind of chord is it?') }), el('p.note', { text: L2('Suena junto, después nota por nota.', 'Strummed, then note by note.') }), el('div.row', null, listenBtn(L2('Escuchar de nuevo', 'Hear it again'), playIt)), el('div.l-opts', null, btns), say);
      setTimeout(playIt, 250);
    },

    bend(lv, stage, done, next, endless, kind) {
      const it = pickWeighted(lv.set, kind, (x) => x.join(':'));
      const [str, fret, semis] = it;
      const from = midiOf(str, fret), target = from + semis;
      const amount = { 1: L2('medio tono', 'a half step'), 2: L2('un tono', 'a whole step'), 3: L2('tono y medio', 'a step and a half') }[semis];
      const W = 520, lo = -0.6, hi = semis + 0.7;
      const X = (st) => 20 + (st - lo) / (hi - lo) * (W - 40);
      const needle = svg('rect', { x: X(0) - 2, y: 18, width: 4, height: 44, rx: 2, fill: '#888' });
      const zone = svg('rect', { x: X(semis - 0.2), y: 14, width: X(semis + 0.2) - X(semis - 0.2), height: 52, rx: 6, fill: '#173b25', stroke: '#4ade80' });
      const meter = svg('svg', { viewBox: `0 0 ${W} 90`, width: W, height: 90, class: 'l-bendm' },
        svg('rect', { x: 20, y: 36, width: W - 40, height: 8, rx: 4, fill: '#2a2a30' }), zone,
        svg('line', { x1: X(0), x2: X(0), y1: 22, y2: 58, stroke: '#666', 'stroke-dasharray': '3 3' }),
        svg('text', { x: X(0), y: 82, 'text-anchor': 'middle', 'font-size': 12, fill: '#aaa' }, noteShort(from % 12) + ' · ' + L2('traste ', 'fret ') + fret),
        svg('text', { x: X(semis), y: 82, 'text-anchor': 'middle', 'font-size': 13, 'font-weight': 700, fill: '#4ade80' }, noteShort(target % 12) + ' ✓'),
        needle);
      const say = el('p.l-trsay', { text: L2('Toca la nota y estírala…', 'Play the note and bend it…') });
      let held = 0, finished = false, last = performance.now();
      stage.replaceChildren(
        el('p.l-trq', null, ...rich(L2(`Cuerda **${str}**, traste **${fret}**: súbela **${amount}**, hasta **${noteBtnLabel(target % 12)}**`, `String **${str}**, fret **${fret}**: bend it up **${amount}**, to **${noteBtnLabel(target % 12)}**`))),
        el('div.row', null, listenBtn(L2('Escuchar el objetivo', 'Hear the target'), () => hearNotes([target])), listenBtn(L2('Desde dónde', 'Where it starts'), () => hearNotes([from]))),
        el('div.l-bendwrap', null, meter), say,
        el('div.row', null, el('button.btn', { onclick: () => { if (!finished) { finished = true; done(false, it.join(':')); next(); } } }, L2('Saltar', 'Skip'))));
      const host = stage.parentElement;
      host._onPitch = (r) => {
        if (finished) return;
        const now = performance.now(), dt = now - last; last = now;
        if (!r) { needle.setAttribute('fill', '#888'); held = 0; return; }
        // the nearest octave of the bent note, around where it should be
        let st = r.midi - from;
        st -= 12 * Math.round((st - semis / 2) / 12);
        if (st < lo - 0.5 || st > hi + 1) return;
        needle.setAttribute('x', Math.max(14, Math.min(W - 18, X(st) - 2)));
        const off = st - semis;
        if (Math.abs(off) <= 0.2) {
          held += dt;
          needle.setAttribute('fill', '#4ade80');
          say.textContent = L2('¡Ahí! Mantenla…', 'There! Hold it…');
          if (held > 450) {
            finished = true;
            say.replaceChildren(el('b.l-ok', { text: L2('¡Afinado! ', 'In tune! ') }), document.createTextNode(Math.abs(off) < 0.08 ? L2('Clavado.', 'Spot on.') : ''));
            done(true, it.join(':'));
            setTimeout(next, 1200);
          }
        } else {
          held = 0;
          needle.setAttribute('fill', off > 0 ? '#f87171' : '#e6a84a');
          say.textContent = st < 0.3 ? L2('Toca la nota y estírala…', 'Play the note and bend it…') : off < 0 ? L2('Un poco más…', 'A bit more…') : L2('Te pasaste: afloja un poco.', 'Too far: ease off a little.');
        }
      };
      return () => { host._onPitch = null; };
    },
  };

  // ------------------------------------------------------------------ free improvisation over a backing track
  const JAMS = [
    { id: 'rock-am', title: ['Rock en La menor', 'Rock in A minor'], bpm: 100, drums: 'rock', prog: ['Am', 'G', 'F', 'G'], roots: [9, 7, 5, 7], scale: [9, 0, 2, 4, 7], root: 9,
      scaleName: ['Pentatónica menor de La', 'A minor pentatonic'], tip: ['Empieza en la caja 1 (traste 5). Cuando cambie el acorde, busca las notas con anillo: son las del acorde.', 'Start in box 1 (fret 5). When the chord changes, look for the ringed notes: they belong to the chord.'] },
    { id: 'blues-a', title: ['Blues en La', 'Blues in A'], bpm: 90, drums: 'rock', prog: ['A7', 'A7', 'A7', 'A7', 'D7', 'D7', 'A7', 'A7', 'E7', 'D7', 'A7', 'E7'], roots: [9, 9, 9, 9, 2, 2, 9, 9, 4, 2, 9, 4], scale: [9, 0, 2, 3, 4, 7], blue: 3, root: 9,
      scaleName: ['Escala de blues de La', 'A blues scale'], tip: ['Doce compases. La nota azul (Mib) es de paso: tócala de camino a otra.', 'Twelve bars. The blue note (Eb) is a passing note: play it on the way to another.'] },
    { id: 'pop-g', title: ['Pop en Sol', 'Pop in G'], bpm: 85, drums: 'soft', prog: ['G', 'D', 'Em', 'C'], roots: [7, 2, 4, 0], scale: [7, 9, 11, 2, 4], root: 7,
      scaleName: ['Pentatónica mayor de Sol', 'G major pentatonic'], tip: ['Es la misma forma que la pentatónica de Mi menor: suena alegre si vuelves a Sol.', 'Same shape as E minor pentatonic: it sounds happy when you come back to G.'] },
    { id: 'balada-em', title: ['Balada en Mi menor', 'Ballad in E minor'], bpm: 70, drums: 'soft', prog: ['Em', 'C', 'G', 'D'], roots: [4, 0, 7, 2], scale: [4, 7, 9, 11, 2], root: 4,
      scaleName: ['Pentatónica menor de Mi', 'E minor pentatonic'], tip: ['Lento: toca pocas notas y déjalas sonar. El silencio también es música.', 'Slow: play few notes and let them ring. Silence is music too.'] },
    { id: 'groove-dm', title: ['Groove en Re menor', 'Groove in D minor'], bpm: 96, drums: 'rock', prog: ['Dm', 'Dm', 'G', 'G'], roots: [2, 2, 7, 7], scale: [2, 4, 5, 7, 9, 11, 0], root: 2,
      scaleName: ['Re dórico (menor con 6ª mayor)', 'D Dorian (minor with a major 6th)'], tip: ['El Si (B) es la nota con sabor de este modo: búscala cuando suena Sol.', 'B is the flavour note of this mode: look for it over the G chord.'] },
  ];
  let jamState = null;
  G.on('tick', (t) => { if (jamState && jamState.onTick) jamState.onTick(t); });
  function stopJam() {
    if (!jamState) return;
    const j = jamState; jamState = null;
    Ear.stop();
    G.call('gameClose');
  }
  /** The backing band in three versions (soft, normal, loud) of drums, guitar and bass; the jam
      crossfades between them following how you play. */
  function jamAudio(J) {
    const spb = 60 / J.bpm, bpb = 4, cycles = Math.max(2, Math.ceil(40 / (J.prog.length * bpb * spb)));
    const bars = J.prog.length * cycles;
    const parts = {};
    const P = (id) => (parts[id] = parts[id] || { id, drums: [], guide: [] });
    const rock = J.drums === 'rock';
    for (let b = 0; b < bars; b++) {
      const name = J.prog[b % J.prog.length], root = J.roots[b % J.prog.length];
      const ps = chordPitches(name), top = ps.slice(-4);
      const t = b * bpb * spb;
      const bass = 28 + ((root - 4 + 12) % 12);
      // guitar: 1 = a chord that rings, 2 = the strum, 3 = driving eighths
      P('gtr1').guide.push({ t, p: ps, d: bpb * spb * 0.95, v: 0.75 });
      for (const [o, d] of [[0, 1], [1, 0.5], [1.5, 0.5], [2, 0.5], [2.5, 0.5], [3, 1]]) P('gtr2').guide.push({ t: t + o * spb, p: o % 1 ? top : ps, d: d * spb * 0.9 });
      for (let k = 0; k < 8; k++) P('gtr3').guide.push({ t: t + k * spb / 2, p: k % 2 ? top : ps, d: spb * 0.45, v: k % 2 ? 0.8 : 1.1 });
      // bass: 1 = one note per bar, 2 = root on 1 and 3 (fifth in the soft styles), 3 = eighths
      P('bass1').guide.push({ t, p: [bass], d: bpb * spb * 0.9, v: 0.9 });
      P('bass2').guide.push({ t, p: [bass], d: 2 * spb * 0.9 }, { t: t + 2 * spb, p: [bass + (rock ? 0 : 7)], d: 2 * spb * 0.9 });
      for (let k = 0; k < 8; k++) P('bass3').guide.push({ t: t + k * spb / 2, p: [bass + (k === 6 ? 12 : 0)], d: spb * 0.42, v: k % 2 ? 0.85 : 1.05 });
      // drums: 1 = kick and soft hats, 2 = the groove, 3 = bigger (crash every 2 bars, more kick, open hat)
      for (let k = 0; k < bpb; k++) {
        const tt = t + k * spb;
        if (k === 0) P('drums1').drums.push({ t: tt, k: 'k', v: 0.8 });
        P('drums1').drums.push({ t: tt, k: 'h', v: 0.6 });
        if (rock) { P('drums2').drums.push({ t: tt, k: k % 2 ? 's' : 'k' }); P('drums2').drums.push({ t: tt, k: 'h' }, { t: tt + spb / 2, k: 'h' }); }
        else { if (k === 0) P('drums2').drums.push({ t: tt, k: 'k' }); if (k === 2) P('drums2').drums.push({ t: tt, k: 's' }); P('drums2').drums.push({ t: tt, k: 'h' }); }
        P('drums3').drums.push({ t: tt, k: k % 2 ? 's' : 'k', v: 1.1 });
        if (k === 2) P('drums3').drums.push({ t: tt + spb / 2, k: 'k' });
        P('drums3').drums.push({ t: tt, k: k === 0 && b % 2 === 0 ? 'c' : 'h' }, { t: tt + spb / 2, k: k === 3 ? 'o' : 'h' });
      }
    }
    const length = bars * bpb * spb;
    const names = { gtr: L2('Guitarra', 'Guitar'), bass: L2('Bajo', 'Bass'), drums: L2('Batería', 'Drums') };
    const extra = Object.values(parts).map((x) => Object.assign(x, { name: names[x.id.replace(/\d/, '')] + ' ' + x.id.slice(-1), gain: x.id.endsWith('2') ? 1 : 0 }));
    return { length: length + 1, extra, loopEnd: length, spb, bpb };
  }

  /** Follows the tempo you play at: where your notes fall against the band's eighths, and how that
      drifts. Returns a new speed now and then (small steps), or null. Pure: tested on its own. */
  function TempoFollower(spb) {
    let pts = [], lastChange = 0;
    return {
      reset() { pts = []; },
      onset(pos, now) {
        const e = pos / (spb / 2);
        pts.push({ pos, now, err: e - Math.round(e) });
        pts = pts.filter((p) => now - p.now < 9);
      },
      suggest(speed, now) {
        if (now - lastChange < 2.5) return null;
        const use = pts.filter((p) => Math.abs(p.err) < 0.4);
        if (use.length < 8 || use[use.length - 1].pos - use[0].pos < 3) return null;
        // unwrap: a steady drift can cross half an eighth
        let prev = use[0].err;
        const xs = [], ys = [];
        for (const p of use) { let e = p.err; while (e - prev > 0.5) e -= 1; while (prev - e > 0.5) e += 1; prev = e; xs.push(p.pos); ys.push(e); }
        const mx = xs.reduce((a, b) => a + b, 0) / xs.length, my = ys.reduce((a, b) => a + b, 0) / ys.length;
        let sxy = 0, sxx = 0, syy = 0;
        for (let i = 0; i < xs.length; i++) { sxy += (xs[i] - mx) * (ys[i] - my); sxx += (xs[i] - mx) ** 2; syy += (ys[i] - my) ** 2; }
        if (sxx <= 0) return null;
        const slope = sxy / sxx;
        const fit = syy > 0 ? (sxy * sxy) / (sxx * syy) : 0;
        const ratio = -slope * spb / 2;          // +0.05 = you're 5 % faster than the band
        // only when your notes sit on a steady pulse (playing freely, rubato, shouldn't drag the band)
        let res = 0;
        for (let i = 0; i < xs.length; i++) res += (ys[i] - (my + slope * (xs[i] - mx))) ** 2;
        const rms = Math.sqrt(res / xs.length);
        if (Math.abs(ratio) < 0.012 || fit < 0.6 || rms > 0.13) return null;
        const step = Math.max(-0.04, Math.min(0.04, ratio));
        lastChange = now;
        pts = [];
        return Math.max(0.75, Math.min(1.25, speed * (1 + step)));
      },
    };
  }

  async function openJam(J, back) {
    stopTrainers();
    const bass = isBass();
    const tun = tuning(), ns = tun.length;
    const a = jamAudio(J);
    root.replaceChildren(el('div.g-loading', null, el('b', { text: L2(J.title[0], J.title[1]) }), el('span', { text: L2('Armando la banda…', 'Getting the band together…') })));
    const r = await G.call('gameOpenLesson', { data: { arrangements: {} }, length: a.length, extra: a.extra, clickOn: false });
    if (!r || !r.ok) { G.toast((r && r.error) || L2('No se pudo armar la base', "Couldn't build the backing track"), true); return back(); }
    await G.call('gameLoop', { on: true, a: 0, b: a.loopEnd });
    const st = { J, a, playing: false, inScale: 0, total: 0, lit: new Map(), speed: 1, level: 2, want: 2, wantSince: 0, onsets: [], levels: [], tickPos: 0, tickAt: 0,
      followLevel: true, followTempo: false, parts: { gtr: true, bass: !bass, drums: true } };
    jamState = st;
    const follower = TempoFollower(a.spb);
    // the band's mix: one version of each part at a time (they crossfade)
    function applyMix() {
      for (const part of ['gtr', 'bass', 'drums'])
        for (const v of [1, 2, 3]) G.call('gameStemVolume', { id: part + v, gain: st.parts[part] && v === st.level ? 1 : 0 });
      levelView();
    }
    const chordNow = el('div.l-jamnow'), chordNext = el('div.l-jamnext');
    const fbHost = el('div.l-jamfb');
    const score = el('div.l-jamscore');
    const levelBox = el('div.l-jamlevel');
    const tempoBox = el('span.l-jamtempo');
    function levelView() {
      levelBox.replaceChildren(el('small', { text: L2('La banda', 'The band') }),
        el('div.l-jamdots', null, [1, 2, 3].map((i) => el('i' + (i <= st.level ? '.on' : '')))),
        el('b', { text: [L2('Suave', 'Soft'), L2('Normal', 'Normal'), L2('Fuerte', 'Loud')][st.level - 1] }));
    }
    function tempoView() { tempoBox.textContent = `${Math.round(J.bpm * st.speed)} BPM` + (st.followTempo ? L2(' · te sigo', ' · following you') : ''); }
    const setSpeed = (v) => {
      st.speed = v;
      // exactly 1 switches the stretcher off; when following, stay a hair away to keep it smooth
      G.call('gameSpeed', { speed: st.followTempo && Math.abs(v - 1) < 0.002 ? 1.002 : v });
      tempoView();
    };
    const playBtn = el('button.btn.primary', { onclick: () => { G.call(st.playing ? 'gamePause' : 'gamePlay'); st.playing = !st.playing; playBtn.textContent = st.playing ? L2('Pausa', 'Pause') : L2('Tocar la base', 'Play the backing'); } }, L2('Tocar la base', 'Play the backing'));
    const speed = el('select.g-select', { 'aria-label': L2('Velocidad', 'Speed'), onchange: (e) => { follower.reset(); setSpeed(+e.target.value); } },
      [1.2, 1.1, 1, 0.9, 0.8, 0.7].map((v) => el('option', { value: v, selected: v === 1, text: Math.round(v * 100) + ' %' })));
    const showAll = el('input', { type: 'checkbox', checked: true, onchange: () => drawFb() });
    const toggle = (label, on, fn) => el('label.l-jamstem', null, el('input', { type: 'checkbox', checked: on, onchange: (e) => fn(e.target.checked) }), label);
    const bar = earBar(() => {});
    function chordAt(pos) { const i = Math.floor(pos / (4 * a.spb)) % J.prog.length; return [i, (i + 1) % J.prog.length]; }
    let shownChord = -1;
    function drawFb() {
      const [ci] = chordAt(st.tickPos);
      const chordPcs = new Set(chordPitches(J.prog[ci]).map((p) => p % 12));
      const marks = [];
      for (let s = 1; s <= ns; s++) for (let f = 0; f <= 15; f++) {
        const pc = (tun[ns - s] + f) % 12;
        if (!J.scale.includes(pc)) continue;
        if (!showAll.checked && !(f >= 4 && f <= 8)) continue;
        const lit = st.lit.get(s + ':' + f);
        marks.push({ s, f, label: pc === J.blue ? 'Eb' : EN[pc], root: pc === J.root, dim: pc === J.blue, ring: chordPcs.has(pc), glow: lit && performance.now() - lit < 600 });
      }
      for (const [key, at] of st.lit) {
        if (performance.now() - at > 600) continue;
        const [s, f] = key.split(':').map(Number);
        const pc = (tun[ns - s] + f) % 12;
        if (!J.scale.includes(pc)) marks.push({ s, f, label: EN[pc], bad: true });
      }
      fbHost.replaceChildren(fretboard({ to: 15, marks, tuning: tun }));
    }
    function updateChord(pos) {
      const [ci, ni] = chordAt(pos);
      if (ci === shownChord) return;
      shownChord = ci;
      chordNow.replaceChildren(el('small', { text: L2('Ahora', 'Now') }), el('b', { text: chordLabel(J.prog[ci]) }));
      chordNext.replaceChildren(el('small', { text: L2('Después', 'Next') }), el('b', { text: chordLabel(J.prog[ni]) }));
      drawFb();
    }
    // how hard you're playing → the band's level, changing on a beat after it's been clear for a moment
    function intensity(now) {
      st.onsets = st.onsets.filter((t) => now - t < 4);
      st.levels = st.levels.filter((x) => now - x.t < 4);
      const nps = st.onsets.length / 4;
      const lastNote = st.onsets.length ? now - st.onsets[st.onsets.length - 1] : Infinity;
      let want = lastNote > 4 ? 2 : nps < 1.0 ? 1 : nps > 3.0 ? 3 : 2;
      if (st.levels.length > 10 && want < 3) {
        const loud = st.levels.filter((x) => now - x.t < 1.5).map((x) => x.db), all = st.levels.map((x) => x.db).sort((p, q) => p - q);
        const median = all[Math.floor(all.length / 2)];
        if (loud.length && Math.max(...loud) > median + 9) want++;
      }
      if (want !== st.want) { st.want = want; st.wantSince = now; }
      return want !== st.level && now - st.wantSince > 1.5;
    }
    let lastBeat = -1;
    st.onTick = (t) => {
      if (jamState !== st || !t.game) return;
      st.tickPos = t.game.pos; st.tickAt = performance.now() / 1000;
      updateChord(t.game.pos);
      if (t.game.playing !== st.playing) { st.playing = t.game.playing; playBtn.textContent = st.playing ? L2('Pausa', 'Pause') : L2('Tocar la base', 'Play the backing'); }
      const beat = Math.floor(t.game.pos / a.spb);
      if (beat !== lastBeat) {
        lastBeat = beat;
        const now = performance.now() / 1000;
        if (st.followLevel && st.playing && intensity(now)) { st.level = st.want; applyMix(); }
        if (st.followTempo && st.playing) { const v = follower.suggest(st.speed, now); if (v) setSpeed(v); }
      }
    };
    let stable = { m: null, n: 0 }, lastCounted = null, redraw = 0;
    await Ear.start((rr, raw) => {
      bar._meter(raw);
      if (jamState !== st) return;
      const now = performance.now() / 1000;
      if (raw && raw.level > 0) st.levels.push({ t: now, db: 20 * Math.log10(raw.level) });
      if (!rr) { stable = { m: null, n: 0 }; lastCounted = null; return; }
      const m = Math.round(rr.midi);
      if (stable.m === m) stable.n++; else stable = { m, n: 1 };
      if (stable.n < 2) return;
      for (let s = 1; s <= ns; s++) { const f = m - tun[ns - s]; if (f >= 0 && f <= 15) st.lit.set(s + ':' + f, performance.now()); }
      if (lastCounted !== m) {
        lastCounted = m; st.total++;
        st.onsets.push(now);
        // where the band was when you played it (the tick is a few ms old: move it on)
        if (st.playing) follower.onset(st.tickPos + (now - st.tickAt) * st.speed - 0.07 * st.speed, now);
        if (J.scale.includes(((m % 12) + 12) % 12)) st.inScale++;
        score.replaceChildren(el('small', { text: L2('Notas en la escala', 'Notes in the scale') }), el('b', { text: Math.round(100 * st.inScale / st.total) + ' %' }), el('small', { text: `${st.total} ${L2('notas', 'notes')}` }));
      }
      if (performance.now() - redraw > 90) { redraw = performance.now(); drawFb(); }
    });
    bar._show();
    score.replaceChildren(el('small', { text: L2('Notas en la escala', 'Notes in the scale') }), el('b', { text: '—' }));
    const head = el('header.g-head.l-lhead', null,
      el('button.g-mode', { onclick: () => { stopJam(); back(); } }, G.icon('back', 16), L2('Volver', 'Back')),
      el('div.l-ltitle', null, el('small', { text: L2('Improvisación libre', 'Free improvisation') + (bass ? L2(' · bajo', ' · bass') : '') }), el('b', { text: L2(J.title[0], J.title[1]) })),
      el('div.spacer'),
      el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 15), L2('Afinar', 'Tune')));
    const body = el('div.l-jam', { 'data-notr': '' },
      el('div.l-jamtop', null, chordNow, chordNext, levelBox, score),
      el('p', null, ...rich(L2(`Escala: **${J.scaleName[0]}**. En rojo la tónica; con anillo, las notas del acorde que suena (las más seguras para terminar una frase).`, `Scale: **${J.scaleName[1]}**. The root in red; ringed, the notes of the chord that's playing (the safest to end a phrase on).`))),
      fbHost,
      el('div.l-tip', null, el('b', { text: L2('Truco', 'Tip') }), el('p', { text: bass ? L2('En el bajo: empieza por la tónica de cada acorde en el pulso 1, y después prueba pasar por la quinta y la octava.', 'On bass: start with each chord\'s root on beat 1, then try passing through the fifth and the octave.') : L2(J.tip[0], J.tip[1]) })),
      bar,
      el('div.row.l-jamctl', null, playBtn, el('label.g-lab', null, L2('Velocidad', 'Speed'), speed), tempoBox),
      el('div.row.l-jamctl', null,
        toggle(L2('La banda sigue tu intensidad', 'The band follows your intensity'), true, (on) => { st.followLevel = on; if (!on) { st.level = 2; applyMix(); } }),
        toggle(L2('Seguir mi tempo (experimental)', 'Follow my tempo (experimental)'), false, (on) => { st.followTempo = on; follower.reset(); if (!on) { setSpeed(+speed.value); } else setSpeed(st.speed); }),
        toggle(L2('Todo el mástil', 'Whole neck'), true, (on) => { showAll.checked = on; drawFb(); })),
      el('div.row.l-jamctl', null,
        toggle(L2('Guitarra', 'Guitar'), st.parts.gtr, (on) => { st.parts.gtr = on; applyMix(); }),
        toggle(L2('Bajo', 'Bass'), st.parts.bass, (on) => { st.parts.bass = on; applyMix(); }),
        toggle(L2('Batería', 'Drums'), st.parts.drums, (on) => { st.parts.drums = on; applyMix(); })),
      el('p.note', { text: L2('No hay notas obligatorias: toca lo que quieras. Si tocas más y más fuerte, la banda sube; si tocas poco, baja. Cuando te sales de la escala, el punto se pinta de rojo.', "There are no set notes: play whatever you like. Play more and harder and the band builds up; play less and it backs off. When you play outside the scale, the dot turns red.") }));
    root.replaceChildren(el('div.l-lesson', { 'data-notr': '' }, head, el('div.l-scroll', null, body)));
    applyMix();
    tempoView();
    updateChord(0);
    const fade = setInterval(() => { if (jamState !== st) return clearInterval(fade); if ([...st.lit.values()].some((t) => performance.now() - t < 900)) drawFb(); }, 200);
  }

  // ------------------------------------------------------------------ «try it on your guitar»: a few notes, heard live
  let activeTry = null;
  /** cfg: {notes: [[string, fret], …], label, octave (any octave counts)} */
  function tryBlock(cfg, L, key) {
    const box = el('div.l-try');
    const p = L ? lp(L.id) : null;
    let idx = 0, listening = false, stable = { m: null, n: 0 }, lastJudged = null;
    const marks = () => cfg.notes.map(([s, f], i) => ({ s, f, label: String(i + 1), good: i < idx, ring: i === idx && listening, dim: i > idx }));
    const fb = el('div');
    const say = el('p.l-trsay');
    const bar = earBar(() => {});
    const startBtn = el('button.btn.primary', { onclick: () => (listening ? stop() : start()) }, G.icon('play', 13), L2('Probar', 'Try it'));
    const draw = () => fb.replaceChildren(fretboard({ to: Math.max(5, ...cfg.notes.map((n) => n[1] + 2)) > 12 ? 15 : 12, marks: marks(), onPick: (s, f) => judge(midiOf(s, f), true, s, f) }));
    function judge(m, clicked) {
      if (idx >= cfg.notes.length) return;
      const [s, f] = cfg.notes[idx];
      const want = midiOf(s, f);
      const ok = cfg.octave ? ((m - want) % 12 + 12) % 12 === 0 : m === want;
      if (clicked) hearNotes([m]);
      if (ok) {
        idx++;
        say.replaceChildren(el('b.l-ok', { text: '✓ ' }), document.createTextNode(idx < cfg.notes.length ? L2(`Bien. Ahora la ${idx + 1}.`, `Good. Now number ${idx + 1}.`) : L2('¡Listo! Lo tocaste todo.', 'Done! You played it all.')));
        if (idx >= cfg.notes.length) { stop(); if (p) { p.tries = p.tries || {}; p.tries[key] = 1; save(); } box.classList.add('done'); }
      } else {
        const fr = m - OPEN[6 - s];
        say.replaceChildren(el('b.l-bad', { text: L2('Casi: ', 'Close: ') }), document.createTextNode(L2(`eso fue ${noteName(((m % 12) + 12) % 12)}`, `that was ${noteName(((m % 12) + 12) % 12)}`) + (fr >= 0 && fr <= 15 && fr !== f ? L2(` (en la cuerda ${s} sería el traste ${fr}).`, ` (on string ${s} that's fret ${fr}).`) : '.')));
      }
      draw();
    }
    async function start() {
      if (activeTry && activeTry !== box) activeTry._stop();
      stopTrainersOnly();
      activeTry = box;
      listening = true;
      if (idx >= cfg.notes.length) { idx = 0; box.classList.remove('done'); }
      startBtn.replaceChildren(L2('Dejar de escuchar', 'Stop listening'));
      say.textContent = L2('Te escucho: toca la nota 1.', 'Listening: play note 1.');
      bar.hidden = false;
      draw();
      await Ear.start((r, raw) => {
        bar._meter(raw);
        if (!listening) return;
        if (!r) { stable = { m: null, n: 0 }; lastJudged = null; return; }
        const m = Math.round(r.midi);
        if (Math.abs(r.midi - m) > 0.4) return;
        if (stable.m === m) stable.n++; else stable = { m, n: 1 };
        if (stable.n === 3 && m !== lastJudged) { lastJudged = m; judge(m, false); }
      });
      bar._show();
    }
    function stop() {
      if (!listening) return;
      listening = false;
      if (activeTry === box) { activeTry = null; Ear.stop(); }
      startBtn.replaceChildren(G.icon('play', 13), idx >= cfg.notes.length ? L2('Otra vez', 'Again') : L2('Probar', 'Try it'));
      draw();
    }
    box._stop = stop;
    bar.hidden = true;
    if (p && p.tries && p.tries[key]) { box.classList.add('done'); idx = cfg.notes.length; }
    box.append(el('div.l-exhead', null, el('div', null, el('b', { text: '🎸 ' + L2('Pruébalo en tu guitarra', 'Try it on your guitar') }), el('small', { text: txt(cfg.label) }))), fb, say, bar,
      el('div.row', null, startBtn, el('button.btn', { onclick: () => { idx = 0; box.classList.remove('done'); say.textContent = ''; draw(); } }, L2('Reiniciar', 'Reset'))),
      el('p.note', { text: L2('Sin guitarra a mano: toca los puntos en el dibujo, en orden.', 'No guitar at hand: click the dots in the drawing, in order.') }));
    draw();
    return box;
  }
  function stopTrainersOnly() { while (activeTrainers.length) { const b = activeTrainers.pop(); if (b._stop) b._stop(); } }

  // ------------------------------------------------------------------ fretboard explorer: any key, any scale, click to hear, see what you play
  const SCALES = [
    { id: 'major', name: ['Escala mayor', 'Major scale'], iv: [0, 2, 4, 5, 7, 9, 11], deg: ['1', '2', '3', '4', '5', '6', '7'] },
    { id: 'minor', name: ['Escala menor natural', 'Natural minor scale'], iv: [0, 2, 3, 5, 7, 8, 10], deg: ['1', '2', 'b3', '4', '5', 'b6', 'b7'] },
    { id: 'pmin', name: ['Pentatónica menor', 'Minor pentatonic'], iv: [0, 3, 5, 7, 10], deg: ['1', 'b3', '4', '5', 'b7'] },
    { id: 'pmaj', name: ['Pentatónica mayor', 'Major pentatonic'], iv: [0, 2, 4, 7, 9], deg: ['1', '2', '3', '5', '6'] },
    { id: 'blues', name: ['Escala de blues', 'Blues scale'], iv: [0, 3, 5, 6, 7, 10], deg: ['1', 'b3', '4', 'b5', '5', 'b7'] },
    { id: 'dorian', name: ['Modo dórico', 'Dorian mode'], iv: [0, 2, 3, 5, 7, 9, 10], deg: ['1', '2', 'b3', '4', '5', '6', 'b7'] },
    { id: 'mixo', name: ['Modo mixolidio', 'Mixolydian mode'], iv: [0, 2, 4, 5, 7, 9, 10], deg: ['1', '2', '3', '4', '5', '6', 'b7'] },
    { id: 'triM', name: ['Acorde mayor (1 3 5)', 'Major chord (1 3 5)'], iv: [0, 4, 7], deg: ['1', '3', '5'] },
    { id: 'trim', name: ['Acorde menor (1 b3 5)', 'Minor chord (1 b3 5)'], iv: [0, 3, 7], deg: ['1', 'b3', '5'] },
    { id: 'dom7', name: ['Acorde de séptima (1 3 5 b7)', 'Dominant 7 chord (1 3 5 b7)'], iv: [0, 4, 7, 10], deg: ['1', '3', '5', 'b7'] },
    { id: 'nat', name: ['Notas naturales', 'Natural notes'], abs: NATURALS },
    { id: 'all', name: ['Todas las notas', 'All the notes'], abs: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] },
  ];
  function renderExplorer() {
    stopTrainers();
    const st = Object.assign({ key: 9, scale: 'pmin', labels: 'names', from: 0, to: 15 }, prog.explorer || {});
    const fbHost = el('div.l-jamfb');
    const lit = new Map();
    const tun = tuning(), ns = tun.length;
    const scale = () => SCALES.find((x) => x.id === st.scale);
    function pcsOf() { const sc = scale(); return sc.abs ? sc.abs : sc.iv.map((i) => (st.key + i) % 12); }
    function label(pc) {
      const sc = scale();
      if (st.labels === 'none') return '';
      if (st.labels === 'deg' && sc.iv) { const k = sc.iv.indexOf((pc - st.key + 12) % 12); return k >= 0 ? sc.deg[k] : ''; }
      return EN[pc];
    }
    function draw() {
      const pcs = pcsOf(), sc = scale();
      const marks = [];
      for (let s = 1; s <= ns; s++) for (let f = st.from; f <= st.to; f++) {
        const pc = (tun[ns - s] + f) % 12;
        if (!pcs.includes(pc)) continue;
        const at = lit.get(s + ':' + f);
        marks.push({ s, f, label: label(pc), root: !!sc.iv && pc === st.key, glow: at && performance.now() - at < 700 });
      }
      for (const [k, at] of lit) {
        if (performance.now() - at > 700) continue;
        const [s, f] = k.split(':').map(Number);
        const pc = (tun[ns - s] + f) % 12;
        if (!pcs.includes(pc)) marks.push({ s, f, label: EN[pc], bad: true });
      }
      fbHost.replaceChildren(fretboard({ from: st.from, to: st.to, marks, tuning: tun, onPick: (s, f) => { const m = tun[ns - s] + f; hearNotes([m]); lit.set(s + ':' + f, performance.now()); draw(); info.textContent = `${noteName(m % 12)} · ${L2('cuerda', 'string')} ${s}, ${L2('traste', 'fret')} ${f}`; } }));
    }
    const info = el('p.l-trsay');
    const keep = () => { prog.explorer = st; save(); draw(); };
    const sel = (label, value, options, set) => el('label.g-lab', null, label, el('select.g-select', { onchange: (e) => { set(e.target.value); keep(); } }, options.map(([v, t]) => el('option', { value: v, selected: String(v) === String(value), text: t }))));
    const playScale = () => {
      const sc = scale();
      if (!sc.iv) return;
      // one octave up and back, from the root on the lowest string
      let start = tun[0] + ((st.key - tun[0] % 12) + 12) % 12;
      if (start - tun[0] < 3) start += 12;
      const up = sc.iv.map((i) => start + i).concat([start + 12]);
      const seq = up.concat(up.slice(0, -1).reverse());
      playGuide(seq.map((m, i) => ({ t: i * 0.32, p: [m], d: 0.45 })));
    };
    const bar = earBar(() => {});
    subScreen(L2('Explorador del mástil', 'Fretboard explorer'), L2('Práctica', 'Practice') + (isBass() ? L2(' · bajo', ' · bass') : ''), [
      el('h2', { text: '🗺️ ' + L2('Explorador del mástil', 'Fretboard explorer') }),
      el('p', { text: L2('Elige una tonalidad y una escala (o un acorde) y mira dónde están todas sus notas. Toca un punto para escucharlo; si tocas tu instrumento, lo que tocas se ilumina.', 'Pick a key and a scale (or a chord) and see where all its notes are. Click a dot to hear it; play your instrument and what you play lights up.') }),
      el('div.row.l-explctl', null,
        sel(L2('Tonalidad', 'Key'), st.key, Array.from({ length: 12 }, (_, i) => [i, noteShort(i) + (isEn() ? '' : ` (${EN[i]})`)]), (v) => { st.key = +v; }),
        sel(L2('Qué mostrar', 'What to show'), st.scale, SCALES.map((x) => [x.id, L2(x.name[0], x.name[1])]), (v) => { st.scale = v; }),
        sel(L2('Etiquetas', 'Labels'), st.labels, [['names', L2('Nombres', 'Names')], ['deg', L2('Grados (1, 3, 5…)', 'Degrees (1, 3, 5…)')], ['none', L2('Sin nombre (para memorizar)', 'None (to memorise)')]], (v) => { st.labels = v; }),
        sel(L2('Zona', 'Area'), `${st.from}-${st.to}`, [['0-15', L2('Todo (0 a 15)', 'All (0 to 15)')], ['0-5', '0–5'], ['3-8', '3–8'], ['5-10', '5–10'], ['7-12', '7–12'], ['10-15', '10–15']], (v) => { const [a, b] = v.split('-').map(Number); st.from = a; st.to = b; }),
        listenBtn(L2('Escuchar la escala', 'Hear the scale'), playScale)),
      fbHost, info, bar,
      el('div.l-tip', null, el('b', { text: L2('Truco', 'Tip') }), el('p', null, ...rich(L2('Pon **Sin nombre** y trata de decir cada nota antes de tocarla. Con **Grados** ves la función de cada nota: el 1 es la casa, el 3 dice mayor o menor, el 5 es el apoyo del power chord.', 'Choose **None** and try to name each note before you click it. With **Degrees** you see what each note does: 1 is home, 3 says major or minor, 5 is the power chord’s support.'))))]);
    draw();
    let stable = { m: null, n: 0 };
    Ear.start((r, raw) => {
      bar._meter(raw);
      if (!r) { stable = { m: null, n: 0 }; return; }
      const m = Math.round(r.midi);
      if (stable.m === m) stable.n++; else stable = { m, n: 1 };
      if (stable.n !== 2) return;
      for (let s = 1; s <= ns; s++) { const f = m - tun[ns - s]; if (f >= st.from && f <= st.to) lit.set(s + ':' + f, performance.now()); }
      info.textContent = L2('Tocaste ', 'You played ') + noteName(((m % 12) + 12) % 12) + (pcsOf().includes(((m % 12) + 12) % 12) ? L2(' · está en la escala', ' · in the scale') : L2(' · fuera de la escala', ' · outside the scale'));
      draw();
    }).then(() => bar._show());
    const fade = setInterval(() => { if (!fbHost.isConnected) return clearInterval(fade); if ([...lit.values()].some((t) => performance.now() - t < 900)) draw(); }, 200);
    activeTrainers.push({ _stop: () => { Ear.stop(); clearInterval(fade); } });
  }

  // ------------------------------------------------------------------ one-minute changes (JustinGuitar's classic): how many changes in 60 s
  function oneMinute(pair) {
    prog.omc = prog.omc || {};
    const key = pair.join('-');
    const hist = prog.omc[key] = prog.omc[key] || [];
    const best = () => Math.max(0, ...hist);
    let count = 0, left = 60, timer = null, running = false;
    const big = el('div.l-omcbig', { text: '0' });
    const clock = el('div.l-omcclock', { text: '1:00' });
    const msg = el('p.l-trsay', { text: L2('Toca «Empezar» y cambia entre los dos acordes todas las veces que puedas. Cada vez que llegas al otro acorde, suma uno (barra espaciadora o el botón grande).', 'Press «Start» and switch between the two chords as many times as you can. Every time you land the other chord, add one (space bar or the big button).') });
    const plus = el('button.l-omcplus', { disabled: true, onclick: () => add() }, '+1');
    const startBtn = el('button.btn.primary', { onclick: () => (running ? finish() : start()) }, L2('Empezar', 'Start'));
    const histBox = el('div.l-omchist');
    function drawHist() {
      const last = hist.slice(-12);
      const top = Math.max(60, ...last);
      histBox.replaceChildren(el('small', { text: L2(`Tus últimos minutos · récord ${best()} · meta: 30, después 60`, `Your last minutes · record ${best()} · target: 30, then 60`) }),
        el('div.l-omcbars', null, last.map((v) => el('i', { style: { height: Math.max(4, 70 * v / top) + 'px' }, title: String(v) }, el('span', { text: String(v) })))));
    }
    function add() { if (!running) return; count++; big.textContent = String(count); big.classList.remove('pop'); void big.offsetWidth; big.classList.add('pop'); }
    function start() {
      count = 0; left = 60; running = true; big.textContent = '0'; plus.disabled = false;
      startBtn.textContent = L2('Terminar', 'Stop');
      msg.textContent = L2('¡Vamos! Sin mirar el reloj: cambia y suma.', 'Go! Don’t watch the clock: change and count.');
      timer = setInterval(() => { left--; clock.textContent = `0:${String(Math.max(0, left)).padStart(2, '0')}`; if (left <= 0) finish(); }, 1000);
      if (playGuide) playGuide([{ t: 0, p: [76], d: 0.15 }]);
    }
    function finish() {
      if (!running) return;
      running = false; clearInterval(timer); plus.disabled = true;
      startBtn.textContent = L2('Otra vez', 'Again');
      const full = left <= 0;
      if (full && count > 0) { hist.push(count); save(); }
      const rec = full && count >= best() && hist.length > 1;
      msg.textContent = !full ? L2('Cortaste antes del minuto: no cuenta para el récord.', 'You stopped early: it doesn’t count for the record.')
        : rec ? L2(`¡Récord nuevo: ${count} cambios!`, `New record: ${count} changes!`)
        : count >= 60 ? L2(`${count}: uno por segundo. Este cambio ya es tuyo.`, `${count}: one a second. You own this change.`)
        : count >= 30 ? L2(`${count}. Buenísimo: apunta a 60.`, `${count}. Great: aim for 60.`)
        : L2(`${count}. Haz uno por día: en una semana se nota.`, `${count}. Do one a day: you’ll notice it within a week.`);
      clock.textContent = '1:00';
      if (full) playGuide([{ t: 0, p: [76], d: 0.15 }, { t: 0.2, p: [81], d: 0.3 }]);
      drawHist();
    }
    const onKey = (e) => { if (!box.isConnected) return document.removeEventListener('keydown', onKey, true); if (e.code === 'Space' && running) { e.preventDefault(); e.stopPropagation(); add(); } };
    document.addEventListener('keydown', onKey, true);
    const box = el('div.l-omc', null,
      el('div.l-omchead', null, el('div.l-chords', null, pair.map((c) => el('button.l-chordbtn', { onclick: () => hearChord(c) }, chordBox(c)))),
        el('div.l-omcnums', null, clock, big)),
      msg, el('div.row', null, startBtn, plus), histBox);
    activeTrainers.push({ _stop: () => { clearInterval(timer); running = false; document.removeEventListener('keydown', onKey, true); } });
    drawHist();
    return box;
  }

  // ------------------------------------------------------------------ "all its places": one note on every string (fretboard memory, step 1)
  // ------------------------------------------------------------------ chord changes (on the highway)
  const CHANGE_PAIRS = [['Em', 'Am'], ['G', 'C'], ['C', 'D'], ['G', 'D'], ['D', 'A'], ['Am', 'E'], ['C', 'Am'], ['G', 'Em'], ['E', 'A'], ['C', 'F'], ['G5', 'C5'], ['F', 'G']];
  function changesExercise(pair, bpm, per) {
    const bar = (c) => Array(4 / per).fill(`[${c}]:${per}`).join(' ');
    return { id: 'chg', label: `${chordLabel(pair[0])} ↔ ${chordLabel(pair[1])}`, bpm, drums: 'soft', seq: [bar(pair[0]), bar(pair[1])].join(' | '), repeat: 8 };
  }

  // ------------------------------------------------------------------ fit the window: everything grows on big screens
  function fitWindow() {
    const z = Math.max(1, Math.min(1.9, Math.min(window.innerWidth / 1280, window.innerHeight / 800)));
    document.documentElement.style.setProperty('--lz', z.toFixed(3));
  }
  window.addEventListener('resize', fitWindow);
  fitWindow();

  // ------------------------------------------------------------------ screens
  let root = null, current = null;
  const Lessons = (G.Lessons = {});
  Lessons.course = COURSE;
  Lessons.buildChart = buildChart;
  Lessons.parse = parse;
  Lessons.trainers = TRAINERS;
  Lessons.jams = JAMS;
  Lessons.stopAll = () => stopTrainers();
  Lessons.TempoFollower = TempoFollower;

  Lessons.show = function (r, lessonId) {
    root = r || root;
    stopTrainers();
    if (lessonId === '#practice') return renderCourse();
    if (lessonId) { const L = ALL.find((x) => x.id === lessonId); if (L) return openLesson(L, lp(L.id).page || 0); }
    renderCourse();
  };

  function nextLesson() { return ALL.find((L) => lessonStars(L) === 0 && !(prog[L.id] && prog[L.id].done)) || null; }

  function renderCourse() {
    current = null;
    stopTrainers();
    const done = ALL.filter((L) => prog[L.id] && prog[L.id].done).length;
    const next = nextLesson();
    const head = el('header.g-head', null,
      el('button.g-mode', { title: L2('Volver al estudio', 'Back to the studio'), onclick: G.Game.leave }, G.icon('back', 16), L2('Estudio', 'Studio')),
      el('h1', { text: L2('Juego', 'Game') }),
      G.Game.tabs('lessons'),
      el('div.spacer'),
      el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 15), L2('Afinar', 'Tune')));
    const hero = el('section.l-hero', null,
      el('div', null,
        el('h2', { text: done ? L2(`Vas ${done} de ${ALL.length} lecciones`, `You've done ${done} of ${ALL.length} lessons`) : L2('Aprende guitarra tocando', 'Learn guitar by playing') }),
        el('p', { text: done ? L2('Sigue donde quedaste, o repite una para sacarle las 3 estrellas.', 'Pick up where you left off, or replay one to get all 3 stars.') : L2(`${ALL.length} lecciones cortas: teoría que se escucha, preguntas rápidas y ejercicios en la autopista que Galpon corrige.`, `${ALL.length} short lessons: theory you can hear, quick questions and exercises on the highway that Galpon checks.`) }),
        el('div.l-bar', null, el('i', { style: { width: Math.round(100 * done / ALL.length) + '%' } }))),
      next ? el('button.btn.primary.l-go', { onclick: () => openLesson(next, lp(next.id).page || 0) }, done ? L2('Seguir: ', 'Continue: ') + txt(next.title) : L2('Empezar', 'Start')) : el('p.l-alldone', { text: L2('¡Curso completo! 🎸', 'Course complete! 🎸') }));
    // practice: trainers that never run out, with levels
    const practice = el('section.l-unit', null,
      el('div.l-unithead', null, el('span.l-unum', { text: '∞' }), el('div', null, el('h3', { text: L2('Práctica', 'Practice') }), el('p', { text: L2('Entrenadores con niveles y práctica eterna, improvisación libre y cambios de acordes.', 'Trainers with levels and endless practice, free improvisation and chord changes.') })),
        el('div.spacer'),
        el('div.seg.l-inst', { role: 'group', 'aria-label': L2('Instrumento', 'Instrument') }, [['guitar', L2('Guitarra', 'Guitar')], ['bass', L2('Bajo', 'Bass')]].map(([k, label]) =>
          el('button' + ((prog.inst || 'guitar') === k ? '.on' : ''), { 'aria-pressed': String((prog.inst || 'guitar') === k), onclick: () => { prog.inst = k; save(); renderCourse(); } }, label)))),
      el('div.l-cards', null,
        ...Object.entries(TRAINERS).filter(([k]) => !(k === 'bend' && isBass())).map(([k, T]) => {
          const t = tp(k);
          const total = levelsOf(k).reduce((a, _, i) => a + (t.lv[i] || 0), 0);
          return el('button.l-card.l-trcard', { onclick: () => renderTrainerHome(k) },
            el('b', { text: T.icon + ' ' + L2(T.title[0], T.title[1]) }),
            el('small', { text: L2(T.desc[0], T.desc[1]) }),
            el('span.l-trprog', { text: `★ ${total}/${levelsOf(k).length * 3}` + (t.best ? ' · ' + L2('récord ', 'record ') + t.best : '') + (T.live ? ' · 🎸' : '') }));
        }),
        el('button.l-card.l-trcard', { onclick: renderJamHome }, el('b', { text: '🎶 ' + L2('Improvisar', 'Improvise') }), el('small', { text: L2('Bases para tocar lo que quieras, con la escala en el mástil y tus notas en vivo.', 'Backing tracks to play whatever you want, with the scale on the neck and your notes live.') }), el('span.l-trprog', { text: JAMS.length + L2(' bases · 🎸', ' backing tracks · 🎸') })),
        el('button.l-card.l-trcard', { onclick: renderExplorer }, el('b', { text: '🗺️ ' + L2('Explorador del mástil', 'Fretboard explorer') }), el('small', { text: L2('Cualquier escala o acorde en cualquier tonalidad, en todo el mástil. Toca y escucha.', 'Any scale or chord in any key, all over the neck. Click and listen.') }), el('span.l-trprog', { text: SCALES.length + L2(' escalas y acordes', ' scales and chords') })),
        el('button.l-card.l-trcard', { onclick: renderChangesHome }, el('b', { text: '🔁 ' + L2('Cambios de acordes', 'Chord changes') }), el('small', { text: L2('Elige dos acordes y cámbialos a tiempo en la autopista, cada vez más rápido.', 'Pick two chords and switch between them in time on the highway, faster and faster.') }), el('span.l-trprog', { text: CHANGE_PAIRS.length + L2(' pares', ' pairs') }))));
    const units = COURSE.map((u, ui) => el('section.l-unit', null,
      el('div.l-unithead', null, el('span.l-unum', { text: String(ui + 1) }), el('div', null, el('h3', { text: txt(u.title) }), el('p', { text: txt(u.desc) }))),
      el('div.l-cards', null, u.lessons.map((L) => {
        const st = lessonStars(L), p = prog[L.id], isDone = p && p.done;
        const nEx = exercisesOf(L).length;
        return el('button.l-card' + (isDone ? '.done' : '') + (next === L ? '.next' : ''), { onclick: () => openLesson(L, (p && !isDone && p.page) || 0) },
          el('b', { text: txt(L.title) }),
          el('small', { text: `${L.min} min · ${nEx ? nEx + (nEx === 1 ? L2(' ejercicio', ' exercise') : L2(' ejercicios', ' exercises')) : L2('teoría', 'theory')}` }),
          el('span.l-mini', { 'aria-label': st + ' / 3' }, [0, 1, 2].map((i) => el('i' + (i < st ? '.on' : ''), { text: '★' }))));
      }))));
    root.replaceChildren(el('div.g-lib.l-course', { 'data-notr': '' }, head, el('div.l-scroll', null, hero, practice, ...units)));
  }

  function subScreen(title, subtitle, content) {
    const head = el('header.g-head.l-lhead', null,
      el('button.g-mode', { onclick: () => { stopTrainers(); renderCourse(); } }, G.icon('back', 16), L2('Lecciones', 'Lessons')),
      el('div.l-ltitle', null, el('small', { text: subtitle }), el('b', { text: title })),
      el('div.spacer'),
      el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 15), L2('Afinar', 'Tune')));
    root.replaceChildren(el('div.l-lesson', { 'data-notr': '' }, head, el('div.l-scroll', null, el('div.l-page', null, ...content))));
  }

  function renderTrainerHome(kind) {
    stopTrainers();
    const T = TRAINERS[kind], t = tp(kind);
    const lvls = levelsOf(kind).map((lv, i) => el('button.l-card', { onclick: () => runTrainer(kind, i) },
      el('b', { text: L2('Nivel ', 'Level ') + (i + 1) }), el('small', { text: lvName(lv) }),
      el('span.l-mini', null, [0, 1, 2].map((k) => el('i' + (k < (t.lv[i] || 0) ? '.on' : ''), { text: '★' })))));
    lvls.push(el('button.l-card.l-endless', { onclick: () => runTrainer(kind, 'endless') }, el('b', { text: '∞ ' + L2('Práctica eterna', 'Endless practice') }),
      el('small', { text: L2('Sin final. Lo que más te cuesta aparece más seguido.', 'No end. What you miss most comes up more often.') }), el('span.l-trprog', { text: L2('Récord: ', 'Record: ') + (t.best || 0) })));
    subScreen(L2(T.title[0], T.title[1]), L2('Práctica', 'Practice') + (noteKind(kind) && isBass() ? L2(' · bajo', ' · bass') : ''), [
      el('h2', { text: T.icon + ' ' + L2(T.title[0], T.title[1]) + (noteKind(kind) && isBass() ? L2(' (bajo)', ' (bass)') : '') }), el('p', { text: L2(T.desc[0], T.desc[1]) }),
      T.live ? el('p.note', { text: L2('Necesita tu guitarra conectada. Si no la tienes a mano, en «Encuentra la nota» también puedes tocar el traste en el dibujo.', 'Needs your guitar plugged in. Without it, in «Find the note» you can also click the fret in the drawing.') }) : null,
      el('p.note', { text: L2('Cada ronda son 10 preguntas: 3 estrellas con 95 % de aciertos.', 'Each round is 10 questions: 3 stars at 95% right.') }),
      el('div.l-cards', null, lvls)]);
  }
  function runTrainer(kind, level) {
    const T = TRAINERS[kind];
    subScreen(L2(T.title[0], T.title[1]), level === 'endless' ? L2('Práctica eterna', 'Endless practice') : L2('Nivel ', 'Level ') + (level + 1), [trainer({ kind, level, onExit: () => renderTrainerHome(kind) })]);
  }

  function renderJamHome() {
    stopTrainers();
    subScreen(L2('Improvisar', 'Improvise'), L2('Práctica', 'Practice'), [
      el('h2', { text: '🎶 ' + L2('Improvisación libre', 'Free improvisation') }),
      el('p', null, ...rich(L2('Elige una base. Suena en loop, con batería, guitarra y bajo; en el mástil ves la escala y, mientras tocas, **se ilumina dónde está cada nota que tocas**.', 'Pick a backing track. It loops with drums, guitar and bass; the neck shows the scale and, as you play, **every note you play lights up where it is**.'))),
      el('div.l-tip', null, el('b', { text: L2('Para empezar', 'To start') }), el('p', null, ...rich(L2('Pocas notas, bien tocadas. Repite una idea dos veces y cámbiala la tercera. Termina las frases en una nota con anillo.', 'Few notes, played well. Repeat an idea twice and change it the third time. End phrases on a ringed note.')))),
      el('div.l-cards', null, JAMS.map((J) => el('button.l-card', { onclick: () => openJam(J, renderJamHome) },
        el('b', { text: L2(J.title[0], J.title[1]) }), el('small', { text: `${L2(J.scaleName[0], J.scaleName[1])} · ${J.bpm} BPM` }), el('span.l-trprog', { text: J.prog.slice(0, 4).map(chordLabel).join(' – ') + (J.prog.length > 4 ? ' …' : '') }))))]);
  }

  function renderChangesHome() {
    stopTrainers();
    const omcHost = el('div');
    let bpm = 70, per = 2;
    const list = el('div.l-cards');
    const draw = () => list.replaceChildren(...CHANGE_PAIRS.map((pair) => {
      const key = 'chg:' + pair.join('-');
      const st = (prog.chg || {})[key] || 0;
      return el('button.l-card', { onclick: () => startChanges(pair, bpm, per) },
        el('b', { text: pair.map(chordLabel).join(' ↔ ') }),
        el('span.l-mini', null, [0, 1, 2].map((k) => el('i' + (k < st ? '.on' : ''), { text: '★' }))));
    }));
    draw();
    subScreen(L2('Cambios de acordes', 'Chord changes'), L2('Práctica', 'Practice'), [
      el('h2', { text: '🔁 ' + L2('Cambios de acordes', 'Chord changes') }),
      el('p', null, ...rich(L2('Lo que más frena al principio no son los acordes: son **los cambios**. Elige un par y cámbialo a tiempo durante 16 compases. Cuando salga con 3 estrellas, súbele el tempo.', "What holds you back at first isn't the chords: it's **the changes**. Pick a pair and switch in time for 16 bars. When you get 3 stars, raise the tempo."))),
      el('div.row', null,
        el('label.g-lab', null, L2('Tempo', 'Tempo'), el('select.g-select', { onchange: (e) => { bpm = +e.target.value; } }, [50, 60, 70, 80, 90, 100, 120].map((v) => el('option', { value: v, selected: v === bpm, text: v + ' BPM' })))),
        el('label.g-lab', null, L2('Rasgueos por acorde', 'Strums per chord'), el('select.g-select', { onchange: (e) => { per = +e.target.value; } },
          [[4, L2('1 (redonda)', '1 (whole)')], [2, L2('2 (blancas)', '2 (halves)')], [1, L2('4 (negras)', '4 (quarters)')]].map(([v, t]) => el('option', { value: v, selected: v === per, text: t }))))),
      list,
      el('h3.l-sub', { text: '⏱️ ' + L2('El minuto de cambios', 'One-minute changes') }),
      el('p', null, ...rich(L2('El clásico de JustinGuitar: un minuto, dos acordes, ¿cuántos cambios? No importa si suena sucio: importa que los dedos lleguen. Meta: **30**, y con el tiempo **60** (uno por segundo).', 'JustinGuitar’s classic: one minute, two chords, how many changes? It doesn’t matter if it sounds messy: what matters is that your fingers get there. Target: **30**, and in time **60** (one a second).'))),
      el('div.row', null, el('label.g-lab', null, L2('Par', 'Pair'), el('select.g-select', { onchange: (e) => { omcHost.replaceChildren(oneMinute(CHANGE_PAIRS[+e.target.value])); } }, CHANGE_PAIRS.map((p, i) => el('option', { value: i, text: p.map(chordLabel).join(' ↔ ') }))))),
      omcHost]);
    omcHost.replaceChildren(oneMinute(CHANGE_PAIRS[0]));
  }
  function startChanges(pair, bpm, per) {
    const x = changesExercise(pair, bpm, per);
    const chart = buildChart(x, x.label);
    G.Game.openLesson({
      title: L2('Cambios: ', 'Changes: ') + x.label + ` · ${bpm} BPM`, lessonId: '#changes', data: chart.data, audio: chart.audio,
      onResult: ({ stars }) => { prog.chg = prog.chg || {}; const key = 'chg:' + pair.join('-'); if (stars > (prog.chg[key] || 0)) { prog.chg[key] = stars; save(); } return stars === 3 && bpm < 120 ? { next: () => startChanges(pair, bpm + 10, per), nextLabel: L2('Más rápido: ', 'Faster: ') + (bpm + 10) + ' BPM' } : null; },
    });
  }
  Lessons.backTo = function (id) { if (id === '#changes') return renderChangesHome(); return Lessons.show(root, id); };

  // ------------------------------------------------------------------ a lesson
  function openLesson(L, page) {
    stopTrainers();
    current = { L, page: Math.max(0, Math.min(page || 0, L.pages.length - 1)) };
    renderLesson();
  }

  function renderLesson() {
    stopTrainers();
    const { L } = current;
    const p = lp(L.id);
    p.page = current.page;
    save();
    const pg = L.pages[current.page];
    const last = current.page === L.pages.length - 1;
    const idx = ALL.indexOf(L);
    const dots = el('div.l-dots', null, L.pages.map((x, i) => el('button' + (i === current.page ? '.on' : i < current.page ? '.seen' : ''), { 'aria-label': (i + 1) + ': ' + txt(x.title), title: txt(x.title), onclick: () => { current.page = i; renderLesson(); } })));
    const head = el('header.g-head.l-lhead', null,
      el('button.g-mode', { title: L2('Volver a las lecciones', 'Back to the lessons'), onclick: () => { stopTrainers(); renderCourse(); } }, G.icon('back', 16), L2('Lecciones', 'Lessons')),
      el('div.l-ltitle', null, el('small', { text: txt(L.unit.title) }), el('b', { text: txt(L.title) })),
      dots,
      el('div.spacer'),
      el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 15), L2('Afinar', 'Tune')));
    const body = el('article.l-page', null, el('h2', { text: txt(pg.title) }), ...pg.blocks.map((b) => renderBlock(b, L)));
    const nav = el('footer.l-nav', null,
      el('button.btn', { disabled: current.page === 0 && idx === 0, onclick: () => { if (current.page > 0) { current.page--; renderLesson(); } else openLesson(ALL[idx - 1], ALL[idx - 1].pages.length - 1); } }, L2('← Atrás', '← Back')),
      el('span.l-pageof', { text: `${current.page + 1} ${L2('de', 'of')} ${L.pages.length}` }),
      last
        ? el('button.btn.primary', { onclick: () => finishLesson(L) }, ALL[idx + 1] ? L2('Terminar y seguir →', 'Finish and continue →') : L2('Terminar el curso', 'Finish the course'))
        : el('button.btn.primary', { onclick: () => { current.page++; renderLesson(); } }, L2('Siguiente →', 'Next →')));
    root.replaceChildren(el('div.l-lesson', { 'data-notr': '' }, head, el('div.l-scroll', null, body), nav));
    root.querySelector('.l-scroll').scrollTop = 0;
  }

  function finishLesson(L) {
    const p = lp(L.id);
    p.done = true; p.page = 0;
    save();
    const missing = exercisesOf(L).filter((x) => !(p.ex[x.id] > 0)).length;
    if (missing) G.toast(L2(`Lección hecha. Te ${missing === 1 ? 'falta 1 ejercicio' : `faltan ${missing} ejercicios`} por estrellas: vuelve cuando quieras.`, `Lesson done. ${missing === 1 ? '1 exercise is' : missing + ' exercises are'} still waiting for stars: come back any time.`));
    else G.toast(L2('¡Lección completa!', 'Lesson complete!'));
    const idx = ALL.indexOf(L);
    if (ALL[idx + 1]) openLesson(ALL[idx + 1], 0); else renderCourse();
  }

  // ------------------------------------------------------------------ blocks
  function renderBlock(b, L) {
    if (b.p) return el('p', null, ...rich(b.p));
    if (b.tip) return el('div.l-tip', null, el('b', { text: L2('Truco', 'Tip') }), el('p', null, ...rich(b.tip)), b.tune ? el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 14), L2('Abrir el afinador', 'Open the tuner')) : null);
    if (b.tune) return el('div.row', null, el('button.btn', { onclick: () => G.App.showTuner() }, G.icon('tuner', 14), L2('Abrir el afinador', 'Open the tuner')));
    if (b.fun) return el('div.l-fun', null, el('b', { text: L2('Dato curioso', 'Fun fact') }), el('p', null, ...rich(b.fun)));
    if (b.fret) return el('figure.l-fig', null, fretboard(b.fret), el('figcaption', { text: L2('Toca un punto para escucharlo.', 'Click a dot to hear it.') }));
    if (b.chords) return el('figure.l-fig', null, el('div.l-chords', null, b.chords.map((c) => el('button.l-chordbtn', { title: L2('Escuchar ', 'Hear ') + chordLabel(c), onclick: () => hearChord(c) }, chordBox(c)))), el('figcaption', { text: L2('Toca un acorde para escucharlo.', 'Click a chord to hear it.') }));
    if (b.tab) return el('figure.l-fig', null, tab(b.tab));
    if (b.listenSeq) return el('div.l-listen', null, listenBtn(txt(b.label || 'Escuchar'), () => hearSeq(Object.assign({ bpm: 90 }, b.listenSeq))));
    if (b.listen) return el('div.l-listen', null, b.listen.map((x) => listenBtn(txt(x.label), () => {
      if (x.chord) hearChord(x.chord);
      else if (x.chords) playGuide(x.chords.map((c, i) => ({ t: i * 1.2, p: chordPitches(c), d: 1.1 })));
      else if (x.seq) hearSeq(x.seq);
      else if (x.together) hearNotes(x.notes[0], 0, true);
      else hearNotes(x.notes, 0.6);
    })));
    if (b.quiz) return renderQuiz(b.quiz, L);
    if (b.trainer) return trainer({ kind: b.trainer, level: b.level || 0, count: b.count || 8, inline: true });
    if (b.jam) { const J = JAMS.find((j) => j.id === b.jam); return el('div.l-ex', null, el('div.l-exhead', null, el('div', null, el('b', { text: '🎶 ' + L2('Improvisa: ', 'Improvise: ') + L2(J.title[0], J.title[1]) }), el('small', { text: L2(J.scaleName[0], J.scaleName[1]) + ` · ${J.bpm} BPM` }))),
      el('p.note', { text: L2('Una base en loop para tocar lo que quieras. Te muestro la escala y dónde cae cada nota que tocas.', 'A looping backing track to play whatever you want. I show you the scale and where every note you play lands.') }),
      el('div.row', null, el('button.btn.primary', { onclick: () => { const keep = current; openJam(J, () => { current = keep; renderLesson(); }); } }, G.icon('play', 13), L2('Improvisar', 'Improvise')))); }
    if (b.tryit) return tryBlock(b.tryit, L, current.page + ':' + (b.tryit.key || b.tryit.label));
    if (b.minute) return oneMinute(b.minute);
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
        why.replaceChildren(el('b', { text: oi === q.answer ? L2('¡Bien! ', 'Right! ') : L2('Casi. ', 'Not quite. ') }), ...rich(q.why || ''));
        p.quiz[key] = oi === q.answer ? 1 : -1; save();
      } }, txt(o)));
      if (answered) { btns.forEach((b, bi) => { b.classList.toggle('right', bi === q.answer); }); why.replaceChildren(...rich(q.why || '')); }
      box.append(el('p.l-qq', null, ...rich(q.q)), el('div.l-opts', null, btns), why);
      return box;
    }));
  }

  function renderExercise(x, L) {
    const p = lp(L.id);
    const st = p.ex[x.id] || 0;
    return el('div.l-ex', null,
      el('div.l-exhead', null,
        el('div', null, el('b', { text: txt(x.label) }), el('small', { text: `${x.bpm} ${L2('pulsos por minuto', 'beats per minute')}` + (x.note ? ' · ' + txt(x.note) : '') })),
        el('span.l-mini', { 'aria-label': st + ' / 3' }, [0, 1, 2].map((i) => el('i' + (i < st ? '.on' : ''), { text: '★' })))),
      tab(x, 2),
      el('div.row', null,
        el('button.btn.primary', { onclick: () => startExercise(x, L) }, G.icon('play', 13), st ? L2('Tocar de nuevo', 'Play again') : L2('Tocar', 'Play')),
        listenBtn(L2('Escuchar primero', 'Listen first'), () => hearSeq(Object.assign({}, x, { countIn: 0 })))));
  }

  function startExercise(x, L) {
    stopTrainers();
    const chart = buildChart(x, txt(x.label));
    const exs = exercisesOf(L);
    const i = exs.indexOf(x);
    G.Game.openLesson({
      title: txt(L.title) + ' · ' + txt(x.label), lessonId: L.id, data: chart.data, audio: chart.audio,
      onResult: ({ stars }) => {
        const p = lp(L.id);
        if (stars > (p.ex[x.id] || 0)) { p.ex[x.id] = stars; save(); }
        const nx = exs[i + 1];
        if (nx) return { next: () => startExercise(nx, L), nextLabel: L2('Siguiente: ', 'Next: ') + txt(nx.label) };
        return null;
      },
    });
  }
})();
