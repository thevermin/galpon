/* Galpon UI — tracks, regions, ruler and playhead. */
(function () {
  'use strict';
  const G = window.G;
  const { el, $, clamp } = G;

  const T = (G.Timeline = {});
  G.ppb = 28;                  // pixels per beat
  G.sel = { trackId: 0, clipId: 0 };
  const waveCache = new Map();

  T.bpb = () => (G.state ? G.state.song.sigNum * 4 / G.state.song.sigDen : 4);
  T.grid = (fine) => {
    if (fine) return 1 / 16;
    if (G.ppb < 14) return T.bpb();
    if (G.ppb < 56) return 1;
    return 0.25;
  };
  T.snap = (beat, ev) => {
    const g = T.grid(ev && (ev.altKey || ev.metaKey));
    return Math.max(0, Math.round(beat / g) * g);
  };
  T.track = (id) => (G.state ? G.state.tracks.find((t) => t.id === id) : null);
  T.clip = (trackId, clipId) => { const t = T.track(trackId); return t ? t.clips.find((c) => c.id === clipId) : null; };
  T.totalBeats = () => {
    const s = G.state ? G.state.song : { length: 0 };
    const visible = ($('#tracks-scroll').clientWidth || 1000) / G.ppb;
    return Math.max(s.length + T.bpb() * 16, visible + T.bpb() * 4, T.bpb() * 64);
  };

  // ------------------------------------------------------------------ render
  T.render = function () {
    const s = G.state;
    if (!s) return;
    const headers = $('#headers'), lanes = $('#lanes');
    headers.innerHTML = '';
    lanes.innerHTML = '';
    const width = Math.ceil(T.totalBeats() * G.ppb);
    lanes.style.width = width + 'px';
    $('#tracks-inner').style.width = (width + 240) + 'px';

    s.tracks.forEach((t, i) => {
      headers.appendChild(renderHeader(t, i));
      lanes.appendChild(renderLane(t));
    });
    // Draw the regions now that they're in the page (not on the next animation frame:
    // WebKit pauses those while the window is covered, which left regions blank)
    G.$$('.clip', lanes).forEach((n) => n._draw && n._draw());
    lanes.style.backgroundImage = gridBackground();
    $('#track-count').textContent = s.tracks.length === 1 ? '1 pista' : s.tracks.length + ' pistas';
    $('#empty-hint').classList.toggle('hidden', s.tracks.length > 0);
    T.renderChords();
    T.drawRuler();
    T.updatePlayhead(G.tick ? G.tick.beat : 0);
  };

  // ------------------------------------------------------------------ chord track
  const ROOTS = ['Do', 'Do#', 'Re', 'Mib', 'Mi', 'Fa', 'Fa#', 'Sol', 'Lab', 'La', 'Sib', 'Si'];
  const QUAL = ['', 'm', '7', 'm7'];
  /** "Lam", "la m", "Am", "F#7", "Sol7"… -> { root, quality } */
  T.parseChord = function (text) {
    let s = String(text || '').trim().replace(/\s+/g, '');
    if (!s) return null;
    const lower = s.toLowerCase();
    const es = [['do', 0], ['re', 2], ['mi', 4], ['fa', 5], ['sol', 7], ['la', 9], ['si', 11]];
    const en = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };
    let root = -1, rest = '';
    for (const [n, v] of es) if (lower.startsWith(n)) { root = v; rest = s.slice(n.length); break; }
    if (root < 0 && en[lower[0]] !== undefined) { root = en[lower[0]]; rest = s.slice(1); }
    if (root < 0) return null;
    if (rest[0] === '#') { root += 1; rest = rest.slice(1); } else if (rest[0] === 'b' && rest.length !== 1 || rest === 'b') { root -= 1; rest = rest.slice(1); }
    const r = rest.toLowerCase().replace('menor', 'm').replace('min', 'm').replace('mayor', '').replace('maj', '');
    const quality = r.startsWith('m7') ? 3 : r.startsWith('m') ? 1 : r.startsWith('7') ? 2 : 0;
    return { root: (root + 12) % 12, quality };
  };

  T.renderChords = function () {
    const strip = $('#chord-strip');
    if (!strip || !G.state) return;
    strip.innerHTML = '';
    strip.style.width = $('#lanes').style.width;
    const chords = G.state.chords || [];
    if (!chords.length) strip.appendChild(el('span.chord-empty', { text: 'Sin acordes todavía: toca "Identificar…" y Galpon los saca de lo que grabaste.' }));
    chords.forEach((c, i) => {
      const chip = el('button.chord', { style: { left: c.beat * G.ppb + 1 + 'px', width: Math.max(14, c.length * G.ppb - 3) + 'px' }, title: `${c.name} (${c.en}) — clic para cambiarlo` }, c.name);
      chip.addEventListener('click', (e) => G.menu(e.clientX, e.clientY, [
        { label: 'Cambiar…', action: async () => {
          const v = await G.prompt('Acorde (por ejemplo Lam, Sol7, F#m)', c.name);
          const p = T.parseChord(v);
          if (v && !p) { G.toast('No entendí ese acorde', true); return; }
          if (p) G.run('setChord', { index: i, root: p.root, quality: p.quality });
        } },
        { label: 'Ir acá', action: () => G.call('setPosition', { beat: c.beat }) },
        '-',
        { label: 'Borrar este acorde', danger: true, action: () => G.run('setChord', { index: i, remove: true }) },
      ]));
      strip.appendChild(chip);
    });
  };

  T.separateStems = function (t, c) {
    const minutes = c.length * 60 / G.state.song.bpm / 60;
    G.modal('Separar en pistas', (body, close) => {
      body.append(
        el('p.note', { text: `Galpon va a separar "${c.name}" en voz, batería, bajo y el resto, cada uno en su pista. La original queda silenciada (no se borra).` }),
        el('p.note', { text: 'Se hace en tu computadora con inteligencia artificial (Demucs). La primera vez baja el modelo (84 MB). ' +
          (minutes > 1 ? `Para ${minutes.toFixed(1)} minutos de audio puede tardar varios minutos: puedes seguir trabajando mientras.` : 'Tarda un ratito: puedes seguir trabajando mientras.') }),
        el('div.row.end', null,
          el('button.btn', { onclick: close }, 'Cancelar'),
          el('button.btn.primary', { onclick: async () => { close(); const r = await G.run('separateStems', { trackId: t.id, clipId: c.id }); if (r !== null) G.toast('Separando… mira el progreso arriba a la derecha.'); } }, 'Separar')));
    });
  };

  T.detectChords = async function (trackId, clipId) {
    G.toast('Escuchando la canción para sacar los acordes…');
    const n = await G.run('detectChords', { trackId: trackId || 0, clipId: clipId || 0 });
    if (n) G.toast('Listo: los acordes están arriba de las pistas. Toca uno para corregirlo.');
  };

  T.chordsMenu = function (e) {
    const t = T.track(G.sel.trackId);
    const c = t && G.sel.clipId ? T.clip(t.id, G.sel.clipId) : null;
    const r = e.target.getBoundingClientRect();
    G.menu(r.left, r.bottom + 4, [
      { label: 'De toda la canción (sin la batería)', action: () => T.detectChords(0, 0) },
      { label: c ? 'De la región "' + c.name + '"' : 'De la región seleccionada', disabled: !c, action: () => T.detectChords(t.id, c.id) },
      '-',
      { label: 'Crear una pista que toque los acordes', disabled: !(G.state.chords || []).length, action: () => G.run('chordsToTrack') },
      { label: 'Borrar todos los acordes', danger: true, disabled: !(G.state.chords || []).length, action: () => G.run('clearChords') },
    ]);
  };

  // ------------------------------------------------------------------ automation
  T.autoShown = {};            // trackId -> target being edited
  const paramCache = {};       // "trackId:slot:name" -> [{index, name}]

  function autoRange(target) {
    if (target === 'pan') return { lo: -1, hi: 1, text: (v) => Math.abs(v) < 0.02 ? 'C' : (v < 0 ? 'I ' : 'D ') + Math.round(Math.abs(v) * 100) };
    if (target === 'volume' || target.startsWith('send:')) return { lo: -60, hi: 6, text: (v) => G.dbToText(v) };
    return { lo: 0, hi: 1, text: (v) => Math.round(v * 100) + ' %' };
  }
  function autoLabel(t, target) {
    if (target === 'volume') return 'Volumen';
    if (target === 'pan') return 'Paneo';
    if (target.startsWith('send:')) { const b = T.track(+target.slice(5)); return 'Envío a ' + (b ? b.name : 'bus'); }
    const [, slot, idx] = target.split(':');
    const s = t.inserts[+slot];
    const p = s && (paramCache[`${t.id}:${slot}:${s.name}`] || []).find((x) => x.index === +idx);
    return (s ? s.name : 'Efecto') + ': ' + (p ? p.name : 'control ' + (+idx + 1));
  }
  function staticValue(t, target) {
    if (target === 'volume') return t.volumeDb;
    if (target === 'pan') return t.pan;
    if (target.startsWith('send:')) { const s = (t.sends || []).find((x) => x.busId === +target.slice(5)); return s ? s.levelDb : -12; }
    return 0.5;
  }

  function autoHeader(t) {
    const target = T.autoShown[t.id];
    const select = el('select', { 'aria-label': 'Qué automatizar', onclick: (e) => e.stopPropagation(), onpointerdown: (e) => e.stopPropagation(),
      onchange: (e) => { T.autoShown[t.id] = e.target.value; T.render(); } });
    const add = (value, label) => select.appendChild(el('option', { value, text: label, selected: value === target }));
    add('volume', 'Volumen'); add('pan', 'Paneo');
    (t.sends || []).forEach((s) => add('send:' + s.busId, autoLabel(t, 'send:' + s.busId)));
    t.inserts.forEach((s, i) => {
      const key = `${t.id}:${i}:${s.name}`;
      const group = el('optgroup', { label: s.name });
      select.appendChild(group);
      const fill = (params) => params.forEach((p) => group.appendChild(el('option', { value: `fx:${i}:${p.index}`, text: p.name, selected: `fx:${i}:${p.index}` === target })));
      if (paramCache[key]) fill(paramCache[key]);
      else G.call('getParams', { trackId: t.id, slot: i }).then((r) => {
        if (!r || !r.ok) return;
        paramCache[key] = r.data.params.map((p) => ({ index: p.index, name: p.name }));
        fill(paramCache[key]);
      });
    });
    if (!select.querySelector(`option[value="${target}"]`) && target.startsWith('fx:')) add(target, autoLabel(t, target));
    const lane = (t.automation || []).find((l) => l.target === target);
    return el('div.autorow', { onpointerdown: (e) => e.stopPropagation() },
      select,
      el('button.tb.write' + (t.autoWrite ? '.on' : ''), { title: 'Grabar movimientos: con esto prendido, lo que muevas mientras suena queda grabado en la curva',
        'aria-pressed': String(!!t.autoWrite), onclick: (e) => { e.stopPropagation(); G.run('setTrack', { trackId: t.id, autoWrite: !t.autoWrite }); } }, '● Escribir'),
      lane && lane.points.length ? el('button.tb', { title: lane.on ? 'Apagar esta curva (sin borrarla)' : 'Prender esta curva',
        onclick: (e) => { e.stopPropagation(); G.run('setAutomation', { trackId: t.id, target, points: lane.points, on: !lane.on }); } }, lane.on ? 'On' : 'Off') : null);
  }

  function autoLane(t, target) {
    const range = autoRange(target);
    const existing = (t.automation || []).find((l) => l.target === target);
    let points = existing ? existing.points.map((p) => [p[0], p[1]]) : [];
    const on = existing ? existing.on : true;
    const wrap = el('div.autolane' + (on ? '' : '.off'));
    const canvas = el('canvas', { 'aria-label': 'Curva de ' + autoLabel(t, target) + ': clic para agregar puntos, arrastra para moverlos, doble clic para borrarlos' });
    const label = el('span.autolabel', { text: autoLabel(t, target) });
    wrap.append(canvas, label);
    const H = 58;
    const yOf = (v) => 4 + (1 - (v - range.lo) / (range.hi - range.lo)) * (H - 8);
    const vOf = (y) => Math.min(range.hi, Math.max(range.lo, range.lo + (1 - (y - 4) / (H - 8)) * (range.hi - range.lo)));
    function draw(hover) {
      const w = Math.max(10, parseFloat($('#lanes').style.width) || 1000);
      const dpr = window.devicePixelRatio || 1;
      canvas.width = w * dpr; canvas.height = H * dpr;
      canvas.style.width = w + 'px'; canvas.style.height = H + 'px';
      const g = canvas.getContext('2d');
      g.scale(dpr, dpr);
      g.clearRect(0, 0, w, H);
      g.strokeStyle = '#2c2c31'; g.beginPath(); g.moveTo(0, yOf(range.lo + (range.hi - range.lo) / 2)); g.lineTo(w, yOf(range.lo + (range.hi - range.lo) / 2)); g.stroke();
      if (!points.length) {
        g.setLineDash([4, 4]); g.strokeStyle = 'rgba(255,255,255,0.35)';
        const y = yOf(staticValue(t, target));
        g.beginPath(); g.moveTo(0, y); g.lineTo(w, y); g.stroke(); g.setLineDash([]);
        g.fillStyle = '#7a7a82'; g.font = '11px Inter, sans-serif';
        g.fillText('Clic para agregar puntos · con "● Escribir" prendido, graba los movimientos mientras suena', 120, H - 8);
        return;
      }
      g.strokeStyle = t.colour; g.lineWidth = 2;
      g.beginPath();
      g.moveTo(0, yOf(points[0][1]));
      points.forEach(([b, v]) => g.lineTo(b * G.ppb, yOf(v)));
      g.lineTo(w, yOf(points[points.length - 1][1]));
      g.stroke();
      g.lineWidth = 1;
      const many = points.length > 400;
      points.forEach(([b, v], i) => {
        if (many && i % 8 !== 0 && i !== hover) return;
        g.fillStyle = i === hover ? '#fff' : t.colour;
        g.beginPath(); g.arc(b * G.ppb, yOf(v), i === hover ? 5 : 3.5, 0, Math.PI * 2); g.fill();
      });
      if (hover !== undefined && points[hover]) {
        g.fillStyle = '#f2f2f3'; g.font = '11px Inter, sans-serif';
        g.fillText(range.text(points[hover][1]), points[hover][0] * G.ppb + 8, Math.max(12, yOf(points[hover][1]) - 6));
      }
    }
    const hit = (x, y) => points.findIndex(([b, v]) => Math.abs(b * G.ppb - x) < 7 && Math.abs(yOf(v) - y) < 7);
    const send = (drag) => G.call('setAutomation', { trackId: t.id, target, points, on, drag });
    const sendDrag = G.throttle(() => send(true), 50);
    canvas.addEventListener('pointerdown', (e) => {
      e.stopPropagation();
      if (e.button !== 0) return;
      const r = canvas.getBoundingClientRect();
      const x = e.clientX - r.left, y = e.clientY - r.top;
      let i = hit(x, y);
      send(false);       // undo step with the curve as it was
      if (i < 0) {
        const beat = Math.max(0, x / G.ppb);
        const v = points.length ? (vOf(y)) : vOf(y);
        points.push([beat, v]);
        points.sort((a, b) => a[0] - b[0]);
        i = points.findIndex((p) => p[0] === beat);
      }
      canvas.setPointerCapture(e.pointerId);
      const move = (ev) => {
        const nx = ev.clientX - r.left, ny = ev.clientY - r.top;
        const lo = i > 0 ? points[i - 1][0] : 0, hi = i < points.length - 1 ? points[i + 1][0] : Infinity;
        points[i] = [Math.min(hi, Math.max(lo, nx / G.ppb)), vOf(ny)];
        draw(i);
        sendDrag();
      };
      const up = () => {
        canvas.removeEventListener('pointermove', move);
        canvas.removeEventListener('pointerup', up);
        send(true).then(() => G.App.refresh());
      };
      canvas.addEventListener('pointermove', move);
      canvas.addEventListener('pointerup', up);
      draw(i);
    });
    canvas.addEventListener('pointermove', (e) => {
      if (e.buttons) return;
      const r = canvas.getBoundingClientRect();
      const i = hit(e.clientX - r.left, e.clientY - r.top);
      canvas.style.cursor = i >= 0 ? 'grab' : 'crosshair';
      draw(i >= 0 ? i : undefined);
    });
    canvas.addEventListener('dblclick', (e) => {
      e.stopPropagation();
      const r = canvas.getBoundingClientRect();
      const i = hit(e.clientX - r.left, e.clientY - r.top);
      if (i < 0) return;
      points.splice(i, 1);
      G.run('setAutomation', { trackId: t.id, target, points, on });
    });
    canvas.addEventListener('contextmenu', (e) => {
      e.preventDefault(); e.stopPropagation();
      G.menu(e.clientX, e.clientY, [
        { label: on ? 'Apagar esta curva' : 'Prender esta curva', disabled: !points.length, action: () => G.run('setAutomation', { trackId: t.id, target, points, on: !on }) },
        { label: 'Aplanar (borrar los puntos)', danger: true, disabled: !points.length, action: () => G.run('removeAutomation', { trackId: t.id, target }) },
        '-',
        { label: 'Cerrar la automatización', action: () => { T.autoShown[t.id] = undefined; T.render(); } },
      ]);
    });
    requestAnimationFrame(() => draw());
    setTimeout(() => draw(), 0);
    return wrap;
  }

  function gridBackground() {
    const bar = T.bpb() * G.ppb;
    const beat = G.ppb;
    return `repeating-linear-gradient(90deg, #2a2a2e 0 1px, transparent 1px ${bar}px),` +
           (beat >= 10 ? `repeating-linear-gradient(90deg, #1f1f23 0 1px, transparent 1px ${beat}px)` : 'none');
  }

  function subtitle(t) {
    if (t.kind === 'audio') return null;
    if (t.kind === 'bus') {
      const n = G.state.tracks.filter((o) => o.output === t.id || (o.sends || []).some((s) => s.busId === t.id)).length;
      return 'Bus · ' + (n ? (n === 1 ? 'recibe de 1 pista' : `recibe de ${n} pistas`) : 'sin envíos todavía');
    }
    if (t.kind === 'drummer') {
      const st = (G.lib && G.lib.drummerStyles || []).find((x) => x.id === t.drummer.style);
      return st ? `${st.name} · ${st.who}` : 'Baterista';
    }
    return t.instrument ? (t.instrument.missing ? '⚠ ' : '') + t.instrument.name : 'Sin instrumento';
  }

  function renderHeader(t, index) {
    const sel = t.id === G.sel.trackId;
    const set = (args) => G.run('setTrack', Object.assign({ trackId: t.id }, args));
    const btn = (cls, label, on, title, onclick) =>
      el('button.tb.' + cls + (on ? '.on' : ''), { title, 'aria-label': title, 'aria-pressed': String(!!on), onclick: (e) => { e.stopPropagation(); onclick(); } }, label);

    const names = el('div.names', null, el('span.tname', { text: t.name }));
    if (t.kind === 'audio') {
      const sources = (G.audio && G.audio.sources) || [];
      const select = el('select', { 'aria-label': 'Entrada', title: 'De dónde graba esta pista', onclick: (e) => e.stopPropagation(),
        onchange: (e) => {
          const v = e.target.value;
          if (v === 'device') { e.target.value = t.stereoInput ? t.inputChannel + 's' : String(t.inputChannel); G.App.openSettings('audio'); return; }
          set({ inputChannel: parseInt(v, 10), stereoInput: v.endsWith('s') });
        } });
      if (!sources.length) select.appendChild(el('option', { text: 'Sin entradas activas' }));
      sources.forEach((name, i) => {
        select.appendChild(el('option', { value: String(i), text: name, selected: !t.stereoInput && t.inputChannel === i }));
        if (i + 1 < sources.length && i % 2 === 0)
          select.appendChild(el('option', { value: i + 's', text: `${name} + ${sources[i + 1]} (estéreo)`, selected: t.stereoInput && t.inputChannel === i }));
      });
      // The interface itself is chosen in Configuración: say which one, and link there
      const dev = (G.audio && G.audio.inputDevice) || '';
      select.appendChild(el('option', { disabled: true, text: '──────────' }));
      select.appendChild(el('option', { value: 'device', text: (dev ? 'Interfaz: ' + dev + ' · ' : '') + 'Cambiar interfaz…' }));
      names.appendChild(select);
    } else {
      names.appendChild(el('span.tsub', { text: subtitle(t) }));
    }

    const btns = el('div.btns', null,
      btn('mute', 'M', t.mute, 'Silenciar', () => set({ mute: !t.mute })),
      btn('solo', 'S', t.solo, 'Solo', () => set({ solo: !t.solo })),
      t.kind !== 'drummer' && t.kind !== 'bus' ? btn('arm', '●', t.armed, 'Activar grabación', () => set({ armed: !t.armed })) : null,
      btn('auto', 'A', !!T.autoShown[t.id] || (t.automation || []).some((l) => l.points.length), 'Automatización (volumen, paneo y efectos que cambian en el tiempo)', () => {
        T.autoShown[t.id] = T.autoShown[t.id] ? undefined : ((t.automation || [])[0] || {}).target || 'volume';
        T.render();
      }),
      t.kind === 'audio' ? btn('mon', '🎧', t.monitor, 'Escuchar la entrada (monitoreo)', () => {
        set({ monitor: !t.monitor });
        if (!t.monitor && G.audio && !/macbook|imac|integrad|built-in|interno/i.test(G.audio.inputDevice || ''))
          G.toast('Si tu interfaz tiene monitoreo directo (perilla MONITOR o MIX), ponla del lado de la computadora (USB/DAW): si no, escuchas la guitarra sin efectos mezclada con la de Galpon.');
      }) : null);

    const vol = el('input', { type: 'range', min: -60, max: 6, step: 0.5, value: t.volumeDb, title: 'Volumen: ' + G.dbToText(t.volumeDb), 'aria-label': 'Volumen' });
    vol.addEventListener('input', G.throttle(() => { t.volumeDb = +vol.value; vol.title = 'Volumen: ' + G.dbToText(t.volumeDb); G.call('setTrack', { trackId: t.id, volumeDb: t.volumeDb }); }, 40));
    vol.addEventListener('dblclick', () => { vol.value = t.volumeDb = 0; G.call('setTrack', { trackId: t.id, volumeDb: 0 }); });
    const pan = el('input.pan', { type: 'range', min: -1, max: 1, step: 0.02, value: t.pan, title: 'Paneo', 'aria-label': 'Paneo' });
    pan.addEventListener('input', G.throttle(() => { t.pan = +pan.value; G.call('setTrack', { trackId: t.id, pan: t.pan }); }, 40));
    pan.addEventListener('dblclick', () => { pan.value = t.pan = 0; G.call('setTrack', { trackId: t.id, pan: 0 }); });
    [vol, pan].forEach((x) => x.addEventListener('pointerdown', (e) => e.stopPropagation()));

    const meter = el('div.tmeter', { 'data-meter': index }, el('i'));
    // a control that automation is driving can't also be moved by hand (unless recording movements)
    const driven = (target) => !t.autoWrite && (t.automation || []).some((l) => l.target === target && l.on && l.points.length);
    if (driven('volume')) { vol.disabled = true; vol.title = 'El volumen lo maneja la automatización (A)'; }
    if (driven('pan')) { pan.disabled = true; pan.title = 'El paneo lo maneja la automatización (A)'; }
    const node = el('div.thead' + (sel ? '.sel' : '') + (T.autoShown[t.id] ? '.withauto' : ''), { style: { '--c': t.colour }, 'data-track': t.id },
      el('div.top', null, names, btns),
      el('div.bottomrow', null, vol, pan, meter),
      T.autoShown[t.id] ? autoHeader(t) : null);

    node.addEventListener('pointerdown', () => T.select(t.id, 0));
    node.addEventListener('dblclick', async (e) => {
      if (!e.target.closest('.tname')) return;
      const name = await G.prompt('Nombre de la pista', t.name);
      if (name) G.run('setTrack', { trackId: t.id, name });
    });
    node.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      T.select(t.id, 0);
      G.menu(e.clientX, e.clientY, [
        { label: 'Renombrar…', action: async () => { const n = await G.prompt('Nombre de la pista', t.name); if (n) G.run('setTrack', { trackId: t.id, name: n }); } },
        { label: 'Subir', disabled: index === 0, action: () => G.run('moveTrack', { id: t.id, index: index - 1 }) },
        { label: 'Bajar', disabled: index === G.state.tracks.length - 1, action: () => G.run('moveTrack', { id: t.id, index: index + 1 }) },
        t.kind === 'audio' ? { label: 'Importar audio aquí…', action: () => G.App.importAudio(t.id) } : null,
        '-',
        { label: 'Eliminar pista', danger: true, action: () => G.run('deleteTrack', { id: t.id }) },
      ].filter(Boolean));
    });
    return node;
  }

  function renderLane(t) {
    const lane = el('div.lane' + (t.id === G.sel.trackId ? '.sel' : '') + (t.kind === 'bus' ? '.bus' : '') + (T.autoShown[t.id] ? '.withauto' : ''), { 'data-track': t.id });
    for (const c of t.clips) lane.appendChild(renderClip(t, c));
    if (T.autoShown[t.id]) lane.appendChild(autoLane(t, T.autoShown[t.id]));

    lane.addEventListener('pointerdown', (e) => {
      if (e.target !== lane) return;
      T.select(t.id, 0);
    });
    lane.addEventListener('dblclick', (e) => {
      if (e.target !== lane || t.kind === 'audio' || t.kind === 'bus') return;
      const beat = Math.floor(e.offsetX / G.ppb / T.bpb()) * T.bpb();
      G.run('addClip', { trackId: t.id, start: beat }).then((id) => { if (id) T.select(t.id, id); });
    });
    lane.addEventListener('dragover', (e) => {
      if (t.kind !== 'bus' && e.dataTransfer.types.includes('text/galpon-loop')) { e.preventDefault(); lane.classList.add('drop'); }
    });
    lane.addEventListener('dragleave', () => lane.classList.remove('drop'));
    lane.addEventListener('drop', (e) => {
      lane.classList.remove('drop');
      const id = e.dataTransfer.getData('text/galpon-loop');
      if (!id) return;
      e.preventDefault();
      const beat = T.snap(e.offsetX / G.ppb);
      G.run('addLoop', { loopId: id, beat: Math.floor(beat / T.bpb()) * T.bpb(), trackId: t.kind === 'instrument' ? t.id : 0 });
    });
    return lane;
  }

  function renderClip(t, c) {
    const sel = G.sel.clipId === c.id;
    const node = el('div.clip' + (sel ? '.sel' : ''), {
      style: { '--c': t.colour, left: c.start * G.ppb + 'px', width: Math.max(6, c.length * G.ppb - 2) + 'px' },
      'data-clip': c.id, 'data-track': t.id, title: c.name,
    });
    const title = el('span.ctitle', { text: (c.locked && t.kind === 'drummer' ? '✎ ' : '') + c.name });
    if (!c.midi && c.takes && c.takes.length > 1)
      title.appendChild(el('span.takes', { text: `${c.activeTake + 1}/${c.takes.length}`, title: 'Tomas: clic derecho para elegir otra' }));
    node.appendChild(title);
    const canvas = el('canvas');
    node.appendChild(canvas);
    node.appendChild(el('div.grip', { title: 'Alargar o acortar' }));
    if (c.midi && t.kind !== 'drummer') node.appendChild(el('div.loopgrip', { title: 'Repetir (loop)' }));
    node._draw = () => drawClip(canvas, t, c);

    node.addEventListener('pointerdown', (e) => clipPointerDown(e, t, c, node));
    node.addEventListener('dblclick', () => { T.select(t.id, c.id); G.App.showBottom(t.kind === 'drummer' ? 'drummer' : 'editor'); });
    node.addEventListener('contextmenu', (e) => { e.preventDefault(); T.select(t.id, c.id); clipMenu(e, t, c); });
    return node;
  }

  function drawClip(canvas, t, c) {
    const w = Math.min(16000, Math.max(6, Math.round(c.length * G.ppb)));
    const h = canvas.clientHeight || 40;   // regions are drawn once they're in the page, so this is the real height
    const dpr = window.devicePixelRatio || 1;
    canvas.width = w * dpr; canvas.height = h * dpr;
    canvas.style.width = w + 'px';
    const g = canvas.getContext('2d');
    g.scale(dpr, dpr);
    g.fillStyle = t.colour;

    if (c.midi) {
      const notes = c.notes || [];
      if (!notes.length) return;
      let lo = 127, hi = 0;
      for (const n of notes) { lo = Math.min(lo, n[2]); hi = Math.max(hi, n[2]); }
      const range = Math.max(12, hi - lo + 1);
      const rowH = Math.max(1.5, (h - 6) / range);
      const L = Math.max(0.25, Math.min(c.loopLength, c.length));
      for (let rep = 0; rep * L < c.length; rep++) {
        g.globalAlpha = rep === 0 ? 0.95 : 0.55;
        for (const n of notes) {
          if (n[0] >= L) continue;
          const b = rep * L + n[0];
          if (b >= c.length) continue;
          const y = h - 3 - (n[2] - lo + 1) * ((h - 6) / range);
          g.fillRect(b * G.ppb, y, Math.max(2, Math.min(n[1], c.length - b) * G.ppb - 1), Math.min(rowH, 4));
        }
        if (rep > 0) { g.globalAlpha = 0.6; g.fillRect(rep * L * G.ppb, 0, 1, h); }
      }
      g.globalAlpha = 1;
    } else {
      const draw = (wave) => {
        if (!wave || !wave.peaks) return;
        const peaks = wave.peaks, total = wave.samples;
        const spb = wave.sampleRate * 60 / G.state.song.bpm;
        const mid = h / 2;
        g.globalAlpha = 0.9;
        for (let x = 0; x < w; x++) {
          const sample = c.offsetSamples + (x / G.ppb) * spb;
          const idx = Math.floor(sample / total * peaks.length);
          if (idx < 0 || idx >= peaks.length) continue;
          const v = peaks[idx] * Math.pow(10, (c.gainDb || 0) / 20);
          const a = Math.min(mid, v * mid * 1.6);
          g.fillRect(x, mid - a, 1, Math.max(1, a * 2));
        }
      };
      const key = c.audioFile;
      if (waveCache.has(key)) draw(waveCache.get(key));
      else G.call('getWaveform', { file: c.audioFile, points: 4000 }).then((r) => { if (r && r.ok) { waveCache.set(key, r.data); draw(r.data); } });
    }
  }

  T.forgetWaveforms = () => waveCache.clear();

  // ------------------------------------------------------------------ selection
  T.select = function (trackId, clipId) {
    const changedTrack = trackId !== G.sel.trackId;
    G.sel = { trackId, clipId };
    G.$$('.thead').forEach((n) => n.classList.toggle('sel', +n.dataset.track === trackId));
    G.$$('.lane').forEach((n) => n.classList.toggle('sel', +n.dataset.track === trackId));
    G.$$('.clip').forEach((n) => n.classList.toggle('sel', +n.dataset.clip === clipId));
    if (changedTrack) G.call('selectTrack', { id: trackId });
    G.App.onSelectionChanged(changedTrack);
  };

  // ------------------------------------------------------------------ dragging regions
  function clipPointerDown(e, t, c, node) {
    if (e.button !== 0) return;
    e.stopPropagation();
    T.select(t.id, c.id);
    const mode = e.target.classList.contains('grip') ? 'resize' : e.target.classList.contains('loopgrip') ? 'loop' : 'move';
    const x0 = e.clientX, y0 = e.clientY;
    let moved = false, newStart = c.start, newLength = c.length, targetTrack = t.id;
    node.setPointerCapture(e.pointerId);

    const onMove = (ev) => {
      const dx = (ev.clientX - x0) / G.ppb;
      if (!moved && Math.abs(ev.clientX - x0) < 3 && Math.abs(ev.clientY - y0) < 3) return;
      moved = true;
      if (mode === 'move') {
        newStart = T.snap(c.start + dx, ev);
        node.style.left = newStart * G.ppb + 'px';
        const laneEl = document.elementsFromPoint(ev.clientX, ev.clientY).find((n) => n.classList && n.classList.contains('lane'));
        G.$$('.lane').forEach((l) => l.classList.remove('drop'));
        if (laneEl) {
          const dest = T.track(+laneEl.dataset.track);
          if (dest && (dest.kind === 'audio') === (t.kind === 'audio')) { targetTrack = dest.id; if (dest.id !== t.id) laneEl.classList.add('drop'); }
        }
      } else {
        newLength = Math.max(T.grid(true), T.snap(c.start + c.length + dx, ev) - c.start);
        node.style.width = newLength * G.ppb + 'px';
      }
    };
    const onUp = () => {
      node.removeEventListener('pointermove', onMove);
      node.removeEventListener('pointerup', onUp);
      G.$$('.lane').forEach((l) => l.classList.remove('drop'));
      if (!moved) return;
      const args = { trackId: t.id, clipId: c.id };
      if (mode === 'move') { args.start = newStart; if (targetTrack !== t.id) args.toTrackId = targetTrack; }
      else { args.length = newLength; if (mode === 'loop') args.loop = true; }
      G.run('setClip', args).then(() => { if (args.toTrackId) T.select(targetTrack, c.id); });
    };
    node.addEventListener('pointermove', onMove);
    node.addEventListener('pointerup', onUp);
  }

  function clipMenu(e, t, c) {
    const beat = G.tick ? G.tick.beat : 0;
    const items = [
      { label: 'Renombrar…', action: async () => { const n = await G.prompt('Nombre de la región', c.name); if (n) G.run('setClip', { trackId: t.id, clipId: c.id, name: n }); } },
      { label: 'Duplicar', hint: '⌘D', action: () => T.duplicate() },
      { label: 'Dividir en el cursor', hint: '⌘T', disabled: !(beat > c.start && beat < c.start + c.length), action: () => T.split() },
    ];
    if (c.midi && t.kind !== 'drummer') {
      items.push('-',
        { label: 'Cuantizar a 1/16', action: () => G.run('quantize', { trackId: t.id, clipId: c.id, grid: 0.25 }) },
        { label: 'Cuantizar a 1/8', action: () => G.run('quantize', { trackId: t.id, clipId: c.id, grid: 0.5 }) },
        { label: 'Subir una octava', action: () => G.run('transposeClip', { trackId: t.id, clipId: c.id, semitones: 12 }) },
        { label: 'Bajar una octava', action: () => G.run('transposeClip', { trackId: t.id, clipId: c.id, semitones: -12 }) });
    }
    if (t.kind === 'drummer') {
      items.push('-',
        { label: 'Variar este ritmo', action: () => G.run('varyDrummer', { trackId: t.id, clipId: c.id, unlock: true }) },
        { label: c.fill ? 'Sin redoble al final' : 'Con redoble al final', action: () => G.run('setClip', { trackId: t.id, clipId: c.id, fill: !c.fill }) },
        { label: 'Editar las notas a mano', action: () => { T.select(t.id, c.id); G.App.showBottom('editor'); } });
    }
    if (t.kind !== 'drummer' && t.kind !== 'bus') items.push({ label: 'Identificar acordes', action: () => T.detectChords(t.id, c.id) });
    if (!c.midi) items.push({ label: 'Separar en pistas (voz, batería, bajo, otros)…', action: () => T.separateStems(t, c) });
    if (!c.midi && c.takes && c.takes.length > 1) {
      items.push('-', { heading: 'Tomas' });
      c.takes.forEach((tk, i) => items.push({ label: `Toma ${i + 1}` + (i === c.activeTake ? '  ✓' : ''), disabled: i === c.activeTake,
        action: () => G.run('setTake', { trackId: t.id, clipId: c.id, take: i }) }));
    }
    if (!c.midi) {
      items.push('-',
        { label: 'Subir volumen 3 dB', action: () => G.run('setClip', { trackId: t.id, clipId: c.id, gainDb: (c.gainDb || 0) + 3 }) },
        { label: 'Bajar volumen 3 dB', action: () => G.run('setClip', { trackId: t.id, clipId: c.id, gainDb: (c.gainDb || 0) - 3 }) });
    }
    items.push('-', { label: 'Eliminar', danger: true, hint: '⌫', action: () => T.deleteSelected() });
    G.menu(e.clientX, e.clientY, items);
  }

  T.deleteSelected = function () {
    if (!G.sel.clipId) return;
    G.run('deleteClip', { trackId: G.sel.trackId, clipId: G.sel.clipId });
    G.sel.clipId = 0;
  };
  T.duplicate = function () {
    if (!G.sel.clipId) return;
    G.run('duplicateClip', { trackId: G.sel.trackId, clipId: G.sel.clipId }).then((id) => { if (id) T.select(G.sel.trackId, id); });
  };
  T.split = function () {
    if (!G.sel.clipId) return;
    G.run('splitClip', { trackId: G.sel.trackId, clipId: G.sel.clipId, beat: G.tick ? G.tick.beat : 0 });
  };

  // ------------------------------------------------------------------ ruler & cycle
  T.drawRuler = function () {
    const canvas = $('#ruler');
    const s = G.state;
    if (!s) return;
    const scroll = $('#tracks-scroll');
    const w = $('#ruler-scroll').clientWidth;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = w * dpr; canvas.height = 34 * dpr;
    canvas.style.width = w + 'px';
    const g = canvas.getContext('2d');
    g.scale(dpr, dpr);
    g.clearRect(0, 0, w, 34);
    const off = scroll.scrollLeft;
    const bpb = T.bpb();
    const barPx = bpb * G.ppb;
    const strip = $('#chord-strip');
    if (strip) strip.style.transform = `translateX(${-off}px)`;

    if (s.song.cycle || T.dragCycle) {
      const a = s.song.cycleStart * G.ppb - off, b = s.song.cycleEnd * G.ppb - off;
      g.fillStyle = s.song.cycle ? 'rgba(230,168,74,0.35)' : 'rgba(230,168,74,0.12)';
      g.fillRect(a, 0, b - a, 12);
      g.fillStyle = '#e6a84a';
      g.fillRect(a, 11, b - a, 2);
    }

    const every = barPx < 30 ? (barPx < 14 ? 8 : 4) : 1;
    g.font = '11px Inter, -apple-system, sans-serif';
    const firstBar = Math.floor(off / barPx);
    for (let bar = firstBar; bar * barPx - off < w; bar++) {
      const x = bar * barPx - off;
      if (bar % every === 0) {
        g.fillStyle = '#9a9aa2';
        g.fillText(String(bar + 1), x + 4, 27);
        g.fillStyle = '#3a3a3f';
        g.fillRect(x, 14, 1, 20);
      }
      if (G.ppb >= 12)
        for (let b = 1; b < bpb; b++) { g.fillStyle = '#2a2a2e'; g.fillRect(x + b * G.ppb, 26, 1, 8); }
    }
  };

  /** Makes the timeline wide enough to show up to `beat` (recording or dragging past the end). */
  T.ensureWidth = function (beat) {
    const lanes = $('#lanes');
    const need = Math.ceil(beat * G.ppb);
    if (need <= (parseFloat(lanes.style.width) || 0)) return;
    lanes.style.width = need + 'px';
    $('#tracks-inner').style.width = (need + 240) + 'px';
    const strip = $('#chord-strip');
    if (strip) strip.style.width = lanes.style.width;
  };

  /** While dragging on the ruler, holding the pointer near an edge scrolls the timeline that way
      (faster the further out), so a selection can go past what fits on screen. */
  function edgeScroller(canvas, onMove) {
    let last = null, raf = 0;
    const step = () => {
      raf = 0;
      if (!last) return;
      const r = canvas.getBoundingClientRect();
      const edge = 48;
      let d = 0;
      if (last.clientX > r.right - edge) d = (last.clientX - (r.right - edge)) / edge;
      else if (last.clientX < r.left + edge) d = -((r.left + edge) - last.clientX) / edge;
      if (!d) return;
      const scroll = $('#tracks-scroll');
      const before = scroll.scrollLeft;
      const speed = Math.min(2.5, Math.abs(d)) * 16;   // px per frame
      if (d > 0) T.ensureWidth((before + scroll.clientWidth + speed) / G.ppb + T.bpb() * 2);
      scroll.scrollLeft = Math.max(0, before + Math.sign(d) * speed);
      if (scroll.scrollLeft !== before) { T.drawRuler(); onMove(last); }
      raf = requestAnimationFrame(step);
    };
    return {
      move(ev) { last = ev; onMove(ev); if (!raf) raf = requestAnimationFrame(step); },
      stop() { last = null; if (raf) cancelAnimationFrame(raf); raf = 0; },
    };
  }

  function setupRuler() {
    const canvas = $('#ruler');
    canvas.addEventListener('pointerdown', (e) => {
      // read the scroll every time: it changes while dragging near the edges
      const beatAt = (ev) => Math.max(0, (ev.clientX - canvas.getBoundingClientRect().left + $('#tracks-scroll').scrollLeft) / G.ppb);
      canvas.setPointerCapture(e.pointerId);
      if (e.offsetY < 13) {
        // drag in the top strip = choose the section to repeat
        const bpb = T.bpb();
        const a = Math.floor(beatAt(e) / bpb) * bpb;
        T.dragCycle = true;
        const edges = edgeScroller(canvas, (ev) => {
          const b = beatAt(ev);
          const lo = Math.min(a, Math.floor(b / bpb) * bpb), hi = Math.max(a + bpb, Math.ceil(b / bpb) * bpb);
          G.state.song.cycleStart = lo; G.state.song.cycleEnd = hi; G.state.song.cycle = true;
          T.drawRuler();
        });
        const move = (ev) => edges.move(ev);
        const up = () => {
          T.dragCycle = false;
          edges.stop();
          canvas.removeEventListener('pointermove', move); canvas.removeEventListener('pointerup', up); canvas.removeEventListener('pointercancel', up);
          G.run('setSong', { cycle: true, cycleStart: G.state.song.cycleStart, cycleEnd: G.state.song.cycleEnd });
        };
        move(e);
        canvas.addEventListener('pointermove', move); canvas.addEventListener('pointerup', up); canvas.addEventListener('pointercancel', up);
      } else {
        const edges = edgeScroller(canvas, (ev) => { const b = T.snap(beatAt(ev), ev); T.updatePlayhead(b); G.call('setPosition', { beat: b }); });
        const seek = (ev) => edges.move(ev);
        seek(e);
        const up = () => {
          edges.stop();
          canvas.removeEventListener('pointermove', seek); canvas.removeEventListener('pointerup', up); canvas.removeEventListener('pointercancel', up);
        };
        canvas.addEventListener('pointermove', seek); canvas.addEventListener('pointerup', up); canvas.addEventListener('pointercancel', up);
      }
    });
  }

  // ------------------------------------------------------------------ playhead
  T.updatePlayhead = function (beat) {
    const ph = $('#playhead');
    const x = Math.max(0, beat) * G.ppb;
    ph.style.transform = `translateX(${x}px)`;
    const scroll = $('#tracks-scroll');
    const view = scroll.clientWidth - 240;
    const offscreen = x > scroll.scrollLeft + view - 40 || x < scroll.scrollLeft;
    // Follow the playhead while playing, and when it jumps somewhere you can't see (e.g. "back to start")
    const jumped = T._lastBeat !== undefined && Math.abs(beat - T._lastBeat) > 1;
    T._lastBeat = beat;
    if (offscreen && ((G.tick && G.tick.playing) || jumped)) scroll.scrollLeft = Math.max(0, x - 80);
  };

  // ------------------------------------------------------------------ the take being recorded
  // While recording, each track being recorded shows a red region that grows with the playhead,
  // with the input level drawn as a waveform, so you can see the take is coming in.
  const REC_CHUNK = 1024;   // px per canvas: only the last one is redrawn as the take grows
  T.rec = null;             // { start, tracks: {id: [[beat, level], ...]}, last }

  function recNode(lane, id) {
    let node = lane.querySelector('.clip.rec');
    if (node) return node;
    const tr = T.track(id);
    node = el('div.clip.rec', { 'data-rec': id });
    node.appendChild(el('span.ctitle', { text: '● Grabando' + (tr && tr.kind !== 'audio' ? ' MIDI' : '') }));
    node.appendChild(el('div.recwave'));
    node._drawnTo = 0;   // px already drawn for good
    lane.appendChild(node);
    return node;
  }

  function drawRecChunks(node, points, start) {
    const box = node.lastChild;
    const h = box.clientHeight || 40;
    const dpr = window.devicePixelRatio || 1;
    const endX = points.length ? (points[points.length - 1][0] - start) * G.ppb : 0;
    const need = Math.max(1, Math.ceil(endX / REC_CHUNK));
    while (box.children.length < need) {
      const c = el('canvas');
      c.style.left = box.children.length * REC_CHUNK + 'px';
      c.style.width = REC_CHUNK + 'px';
      c.width = REC_CHUNK * dpr; c.height = h * dpr;
      box.appendChild(c);
    }
    const first = Math.floor(node._drawnTo / REC_CHUNK);
    const mid = h / 2;
    for (let i = first; i < need; i++) {
      const canvas = box.children[i];
      const g = canvas.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, REC_CHUNK, h);
      g.fillStyle = '#ff6b6b';
      const x0 = i * REC_CHUNK;
      for (let k = 0; k < points.length; k++) {
        const x = (points[k][0] - start) * G.ppb - x0;
        const nx = k + 1 < points.length ? (points[k + 1][0] - start) * G.ppb - x0 : x + 1;
        if (nx < 0) continue;
        if (x > REC_CHUNK) break;
        const a = Math.min(mid, Math.sqrt(points[k][1]) * mid);
        g.fillRect(x, mid - a, Math.max(1, nx - x), Math.max(1, a * 2));
      }
    }
    node._drawnTo = Math.max(0, (need - 1) * REC_CHUNK);
  }

  T.updateRecording = function (t) {
    const r = t.rec;
    if (!r || !G.state) {
      if (T.rec) { G.$$('.clip.rec').forEach((n) => n.remove()); T.rec = null; }
      return;
    }
    const beat = t.beat;
    if (!T.rec || Math.abs(T.rec.start - r.start) > 1e-6) T.rec = { start: r.start, tracks: {}, last: -1e9 };
    const rec = T.rec;
    if (t.countIn || beat < rec.start) return;   // still counting in
    // With the cycle on, every pass round the section is a new take: start drawing again
    if (beat < rec.last - 0.25) { rec.tracks = {}; G.$$('.clip.rec').forEach((n) => n.remove()); }
    rec.last = beat;
    T.ensureWidth(beat + T.bpb() * 8);

    for (const id of r.tracks || []) {
      const idx = G.state.tracks.findIndex((x) => x.id === id);
      if (idx < 0) continue;
      const pts = rec.tracks[id] || (rec.tracks[id] = []);
      const tr = G.state.tracks[idx];
      const level = tr.kind === 'audio' ? Math.min(1, (t.inputs && t.inputs[idx]) || 0) : 0;
      pts.push([beat, level]);
      const lane = G.$(`.lane[data-track="${id}"]`);
      if (!lane) continue;
      const node = recNode(lane, id);
      node.style.left = rec.start * G.ppb + 'px';
      node.style.width = Math.max(6, (beat - rec.start) * G.ppb) + 'px';
      if (!node._built) { node._built = true; node._drawnTo = 0; }   // a fresh node (after a redraw) draws everything
      if (tr.kind === 'audio') drawRecChunks(node, pts, rec.start);
    }
  };

  T.setZoom = function (ppb, anchorBeat) {
    const scroll = $('#tracks-scroll');
    const anchor = anchorBeat !== undefined ? anchorBeat : (scroll.scrollLeft + 200) / G.ppb;
    G.ppb = clamp(ppb, 6, 200);
    $('#zoom').value = G.ppb;
    T.render();
    scroll.scrollLeft = Math.max(0, anchor * G.ppb - 200);
  };

  T.init = function () {
    setupRuler();
    $('#btn-chords').addEventListener('click', (e) => T.chordsMenu(e));
    const scroll = $('#tracks-scroll');
    scroll.addEventListener('scroll', () => T.drawRuler());
    $('#zoom').addEventListener('input', (e) => T.setZoom(+e.target.value));
    scroll.addEventListener('wheel', (e) => {
      if (e.ctrlKey || e.metaKey) {
        e.preventDefault();
        const rect = $('#lanes').getBoundingClientRect();
        const beat = (e.clientX - rect.left) / G.ppb;
        T.setZoom(G.ppb * (e.deltaY < 0 ? 1.15 : 1 / 1.15), beat);
      }
    }, { passive: false });
    window.addEventListener('resize', () => T.drawRuler());
  };
})();
