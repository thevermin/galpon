/* Galpon UI — bottom panel: smart controls (sounds & effects), note editor, AI drummer. */
(function () {
  'use strict';
  const G = window.G;
  const { el, $, clamp } = G;
  const P = (G.Panels = {});

  // ------------------------------------------------------------------ knob
  function knob(param, onChange) {
    const dial = el('div.dial', { role: 'slider', tabindex: 0, 'aria-label': param.name, 'aria-valuemin': 0, 'aria-valuemax': 1, 'aria-valuenow': param.value });
    const ind = el('i');
    dial.appendChild(ind);
    const val = el('span.kval', { text: (param.text || '') + (param.label && !String(param.text).includes(param.label) ? ' ' + param.label : '') });
    let value = param.value;
    const paint = () => { ind.style.transform = `rotate(${-135 + value * 270}deg)`; dial.setAttribute('aria-valuenow', value.toFixed(3)); };
    paint();
    const send = G.throttle(async (v) => {
      const r = await G.call('setParam', Object.assign({}, param.target, { index: param.index, value: v }));
      if (r && r.ok) val.textContent = r.data + (param.label && !String(r.data).includes(param.label) ? ' ' + param.label : '');
      onChange && onChange(v);
    }, 30);
    dial.addEventListener('pointerdown', (e) => {
      dial.setPointerCapture(e.pointerId);
      let y = e.clientY;
      const move = (ev) => {
        const fine = ev.shiftKey ? 0.2 : 1;
        value = clamp(value + (y - ev.clientY) / 180 * fine, 0, 1);
        y = ev.clientY;
        paint(); send(value);
      };
      const up = () => { dial.removeEventListener('pointermove', move); dial.removeEventListener('pointerup', up); };
      dial.addEventListener('pointermove', move); dial.addEventListener('pointerup', up);
    });
    dial.addEventListener('wheel', (e) => { e.preventDefault(); value = clamp(value - e.deltaY / 1500, 0, 1); paint(); send(value); }, { passive: false });
    dial.addEventListener('keydown', (e) => {
      const step = e.shiftKey ? 0.01 : 0.05;
      if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { value = clamp(value + step, 0, 1); paint(); send(value); e.preventDefault(); }
      if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { value = clamp(value - step, 0, 1); paint(); send(value); e.preventDefault(); }
    });
    return el('div.knob', null, dial, el('span.kname', { text: param.name }), val);
  }
  P.knob = knob;

  // ------------------------------------------------------------------ smart controls
  // Ready-made chains for audio tracks (all built-in effects)
  const AUDIO_PRESETS = [
    { name: 'Voz limpia', chain: [['eq', { 'Corte de graves': 90, 'Agudos': 2 }], ['comp', { 'Umbral': -20, 'Proporción': 3 }], ['reverb', { 'Mezcla': 0.18 }]] },
    { name: 'Voz con eco', chain: [['eq', { 'Corte de graves': 100 }], ['comp', {}], ['delay', { 'Mezcla': 0.2 }], ['reverb', { 'Mezcla': 0.22 }]] },
    { name: 'Guitarra limpia', chain: [['amp', { 'Ganancia': 0.15 }], ['chorus', { 'Mezcla': 0.3 }], ['reverb', { 'Mezcla': 0.2 }]] },
    { name: 'Guitarra crunch', chain: [['gate', {}], ['amp', { 'Ganancia': 0.6, 'Medios': 3 }], ['reverb', { 'Mezcla': 0.15 }]] },
    { name: 'Guitarra NAM (Tone3000)', chain: [['gate', {}], ['nam', {}], ['cab', {}], ['reverb', { 'Mezcla': 0.12 }]] },
    { name: 'Bajo directo', chain: [['comp', { 'Umbral': -24, 'Proporción': 4 }], ['eq', { 'Graves': 3, 'Medios': -2 }]] },
    { name: 'Sin efectos', chain: [] },
  ];
  P.AUDIO_PRESETS = AUDIO_PRESETS;

  P.applyAudioPreset = async function (track, preset) {
    for (let i = track.inserts.length - 1; i >= 0; i--) await G.call('removeInsert', { trackId: track.id, index: i });
    let index = 0;
    for (const [type, values] of preset.chain) {
      await G.call('addInsert', { trackId: track.id, kind: 'builtin', id: type });
      const r = await G.call('getParams', { trackId: track.id, slot: index });
      if (r && r.ok) {
        for (const p of r.data.params) {
          if (!(p.name in values)) continue;
          // values are given in real units; find the normalised value by searching
          await setReal(track.id, index, p, values[p.name]);
        }
      }
      index++;
    }
    G.toast('Sonido: ' + preset.name);
  };

  async function setReal(trackId, slot, p, real) {
    // Binary search on the displayed value (works for any parameter range/skew)
    let lo = 0, hi = 1;
    for (let i = 0; i < 14; i++) {
      const mid = (lo + hi) / 2;
      const r = await G.call('setParam', { trackId, slot, index: p.index, value: mid });
      const shown = parseFloat(r && r.data);
      if (isNaN(shown)) return;
      if (shown < real) lo = mid; else hi = mid;
    }
  }

  P.controls = async function (body, track) {
    const isMaster = !track;
    const trackId = isMaster ? 0 : track.id;
    const inserts = isMaster ? G.state.master : track.inserts;
    const noInstrument = isMaster || track.kind === 'audio' || track.kind === 'bus';
    if (P._slot === undefined || P._slotTrack !== trackId) { P._slot = noInstrument ? (inserts.length ? 0 : null) : -1; P._slotTrack = trackId; }
    if (P._slot === null && inserts.length) P._slot = 0;   // effects were just added (e.g. a ready-made sound)
    if (P._slot !== null && P._slot >= inserts.length) P._slot = inserts.length ? inserts.length - 1 : (noInstrument ? null : -1);
    if (P._slot === -1 && noInstrument) P._slot = inserts.length ? 0 : null;

    body.innerHTML = '';
    const chain = el('div.chain');
    if (!isMaster && track.kind !== 'audio' && track.instrument) {
      chain.appendChild(el('div.fx' + (P._slot === -1 ? '.sel' : ''), null,
        el('button.fxname', { onclick: () => { P._slot = -1; P.controls(body, track); } }, '🎹 ' + track.instrument.name)));
      chain.appendChild(el('span', { text: '→', style: { color: '#6b6b73' } }));
    }
    const move = (from, to) => { P._slot = to; G.run('moveInsert', { trackId, from, to }); };
    inserts.forEach((s, i) => {
      chain.appendChild(el('div.fx' + (P._slot === i ? '.sel' : '') + (s.bypass ? '.off' : ''), { draggable: 'true', 'data-fx': i },
        i > 0 ? el('button.icon-btn.arrow', { title: 'Mover antes', 'aria-label': 'Mover antes', onclick: () => move(i, i - 1) }, '‹') : null,
        el('button.fxname', { onclick: () => { P._slot = i; P.controls(body, track); } }, s.name + (s.missing ? ' (falta)' : '')),
        s.latency > 0 ? el('span.note', { text: (s.latency / ((G.state && G.state.sampleRate) || 48000) * 1000).toFixed(1) + ' ms', title: 'Demora que agrega este plugin (Galpon la compensa)' }) : null,
        el('button.icon-btn', { title: s.bypass ? 'Activar' : 'Apagar', 'aria-label': 'Encender o apagar', onclick: () => P.toggleBypass(track, i, !s.bypass) }, G.icon('power', 13)),
        i < inserts.length - 1 ? el('button.icon-btn.arrow', { title: 'Mover después', 'aria-label': 'Mover después', onclick: () => move(i, i + 1) }, '›') : null,
        el('button.icon-btn', { title: 'Quitar', 'aria-label': 'Quitar efecto', onclick: () => G.run('removeInsert', { trackId, index: i }) }, G.icon('close', 13))));
    });
    // drag a pedal to reorder
    G.$$('.fx[data-fx]', chain).forEach((n) => {
      n.addEventListener('dragstart', (e) => { e.dataTransfer.setData('text/galpon-fx', n.dataset.fx); });
      n.addEventListener('dragover', (e) => { if (e.dataTransfer.types.includes('text/galpon-fx')) e.preventDefault(); });
      n.addEventListener('drop', (e) => { const from = +e.dataTransfer.getData('text/galpon-fx'); const to = +n.dataset.fx; if (from !== to) move(from, to); });
    });
    chain.appendChild(el('button.pill', { onclick: (e) => addEffectMenu(e, trackId) }, '+ Efecto'));
    body.appendChild(chain);
    if (!isMaster && track.kind !== 'bus') body.appendChild(sendsRow(track));
    if (!isMaster && track.kind === 'bus') {
      const feeders = G.state.tracks.filter((o) => o.output === track.id || (o.sends || []).some((x) => x.busId === track.id));
      body.appendChild(el('p.note', { text: feeders.length
        ? 'Recibe de: ' + feeders.map((o) => o.name).join(', ') + '. Deja los efectos de este bus al 100 % (mezcla 1): el sonido seco ya sale de cada pista.'
        : 'Este bus todavía no recibe nada. Ve a los Controles de una pista y en Envíos elige "' + track.name + '".' }));
    }

    if (!isMaster && track.kind === 'audio') {
      // Input: gain + meter, so you can see the level before recording
      const gain = el('input', { type: 'range', min: -24, max: 40, step: 0.5, value: track.inputGainDb || 0, 'aria-label': 'Ganancia de entrada' });
      const gainText = el('span.note', { text: (track.inputGainDb > 0 ? '+' : '') + (track.inputGainDb || 0).toFixed(1) + ' dB' });
      gain.addEventListener('input', G.throttle(() => {
        track.inputGainDb = +gain.value;
        gainText.textContent = (track.inputGainDb > 0 ? '+' : '') + track.inputGainDb.toFixed(1) + ' dB';
        G.call('setTrack', { trackId: track.id, inputGainDb: track.inputGainDb });
      }, 40));
      gain.addEventListener('dblclick', () => { gain.value = 0; track.inputGainDb = 0; gainText.textContent = '0.0 dB'; G.call('setTrack', { trackId: track.id, inputGainDb: 0 }); });
      const meter = el('div.inmeter', { 'data-input-meter': track.id, title: 'Nivel de entrada: apunta a que los picos queden en la zona amarilla' }, el('i'), el('b'));
      body.appendChild(el('div.inrow', null,
        el('b', { text: 'Entrada' }),
        el('span.note', { text: 'Ganancia' }), gain, gainText, meter,
        el('span.note', { text: 'Toca fuerte: los picos tienen que llegar a lo amarillo, nunca a lo rojo.' })));

      const row = el('div.row', { style: { marginBottom: '12px' } }, el('span.note', { text: 'Sonidos listos:' }));
      AUDIO_PRESETS.forEach((p) => row.appendChild(el('button.pill', { onclick: () => P.applyAudioPreset(track, p) }, p.name)));
      body.appendChild(row);
    }

    if (isMaster) {
      const sw = el('button.switch' + (G.state.song.limiter ? '.on' : ''), { 'aria-label': 'Limitador', 'aria-pressed': String(!!G.state.song.limiter),
        onclick: () => G.run('setSong', { limiter: !G.state.song.limiter }) });
      body.appendChild(el('div.row', { style: { marginBottom: '12px' } }, sw,
        el('b', { text: 'Limitador' }),
        el('span.note', { text: 'Evita que la mezcla sature (techo en −1 dB). Déjalo prendido salvo que uses tu propio limitador.' })));
    }

    if (P._slot === null) {
      body.appendChild(el('p.note', { text: isMaster ? 'Los efectos del master se aplican a toda la canción (por ejemplo un compresor o un ecualizador final).' : 'Esta pista no tiene efectos todavía. Elige un sonido listo o agrega un efecto.' }));
      return;
    }
    const slotInfo = P._slot === -1 ? track.instrument : inserts[P._slot];
    if (!slotInfo) return;

    const area = el('div.split');
    body.appendChild(area);
    const side = el('div.stack', { style: { minWidth: '200px', maxWidth: '260px' } });
    area.appendChild(side);
    side.appendChild(el('b', { text: slotInfo.name }));
    if (slotInfo.kind === 'plugin') {
      side.appendChild(el('button.btn', { onclick: () => G.run('openEditor', { trackId, slot: P._slot }) }, 'Abrir ventana del plugin'));
      if (slotInfo.latency > 0) side.appendChild(el('span.note', { text: `Agrega ${slotInfo.latency} muestras de demora.` }));
    }
    const extra = slotInfo.extra;
    if (extra && extra.loadable === 'sampler') {
      side.appendChild(el('span.note', { text: extra.status || '' }));
      side.appendChild(el('button.btn.primary', { onclick: async () => {
        const r = await G.call('choose', { mode: 'open', title: 'Elige un instrumento .sfz o un sonido', patterns: '*.sfz;*.wav;*.aif;*.aiff;*.flac;*.ogg' });
        if (r && r.ok && r.path) { await G.run('setExtra', { trackId, slot: P._slot, key: 'file', value: r.path }); setTimeout(() => G.App.refresh(), 800); }
      } }, 'Cargar instrumento…'));
      side.appendChild(el('span.note', { text: 'Sirven los .sfz (hay cientos gratis en sfzinstruments.github.io) o un solo sonido .wav: si el nombre dice la nota (por ejemplo "Bajo C2.wav") se afina solo.' }));
    } else if (extra && extra.loadable) {
      const isNam = extra.loadable === 'nam';
      side.appendChild(el('span.note', { text: extra.fileName ? 'Cargado: ' + extra.fileName : (isNam ? 'Sin modelo cargado.' : 'Sin respuesta de impulso cargada.') }));
      if (extra.status && isNam) side.appendChild(el('span.note', { text: extra.status }));
      side.appendChild(el('button.btn.primary', {
        onclick: async () => {
          const r = await G.call('choose', { mode: 'open', title: isNam ? 'Elige un modelo .nam' : 'Elige una respuesta de impulso (.wav)', patterns: isNam ? '*.nam' : '*.wav;*.aif;*.aiff' });
          if (r && r.ok && r.path) {
            const slot = P._slot;
            await G.run('setExtra', { trackId, slot, key: 'file', value: r.path });
            setTimeout(async () => { await G.App.refresh(); if (isNam) P.checkCab(track, slot); }, 900);
          }
        },
      }, isNam ? 'Cargar captura .nam…' : 'Cargar IR…'));
      side.appendChild(el('span.note', { text: isNam ? 'Baja capturas gratis de amplificadores y pedales en tone3000.com y cárgalas acá.' : 'Las IR de gabinetes también se consiguen en tone3000.com.' }));
    }

    const knobs = el('div.knobs');
    area.appendChild(knobs);
    const r = await G.call('getParams', { trackId, slot: P._slot });
    if (!r || !r.ok) { knobs.appendChild(el('span.note', { text: (r && r.error) || 'No se pudieron leer los controles' })); return; }
    r.data.params.slice(0, 40).forEach((p) => knobs.appendChild(knob(Object.assign({ target: { trackId, slot: P._slot } }, p))));
    if (r.data.total > 40) knobs.appendChild(el('span.note', { text: `Este plugin tiene ${r.data.total} controles: abre su ventana para verlos todos.` }));
  };

  // ------------------------------------------------------------------ sends & output
  const buses = () => (G.state ? G.state.tracks.filter((t) => t.kind === 'bus') : []);

  /** Creates a shared reverb/delay bus and sends this track to it. */
  P.newBusFor = async function (track, fx) {
    const id = await G.run('addTrack', { kind: 'bus', preset: fx });
    if (!id) return;
    await G.run('setSend', { trackId: track.id, busId: id, levelDb: -10 });
    await G.call('selectTrack', { id: track.id });
    G.sel.trackId = track.id;
    G.toast(fx === 'delay' ? 'Delay compartido listo: mándale otras pistas también.' : 'Reverb compartida lista: mándale otras pistas también.');
    G.App.refresh();
  };

  function sendSlider(track, snd, compact) {
    const r = el('input' + (compact ? '.mini' : ''), { type: 'range', min: -60, max: 6, step: 0.5, value: snd.levelDb, 'aria-label': 'Nivel de envío', title: 'Envío: ' + G.dbToText(snd.levelDb) });
    r.addEventListener('input', G.throttle(() => {
      snd.levelDb = +r.value;
      r.title = 'Envío: ' + G.dbToText(snd.levelDb);
      if (r._text) r._text.textContent = G.dbToText(snd.levelDb);
      G.call('setSend', { trackId: track.id, busId: snd.busId, levelDb: snd.levelDb });
    }, 40));
    r.addEventListener('dblclick', () => { r.value = snd.levelDb = -12; G.call('setSend', { trackId: track.id, busId: snd.busId, levelDb: -12 }); });
    r.addEventListener('pointerdown', (e) => e.stopPropagation());
    return r;
  }

  function addSendMenu(e, track) {
    const used = new Set((track.sends || []).map((x) => x.busId));
    const items = buses().filter((b) => !used.has(b.id)).map((b) => ({ label: b.name, action: () => G.run('setSend', { trackId: track.id, busId: b.id, levelDb: -10 }) }));
    if (items.length) items.push('-');
    items.push({ label: 'Nueva reverb compartida', action: () => P.newBusFor(track, 'reverb') },
               { label: 'Nuevo delay compartido', action: () => P.newBusFor(track, 'delay') });
    const r = e.target.getBoundingClientRect();
    G.menu(r.left, r.bottom + 4, items);
  }

  function outputSelect(track) {
    const sel = el('select', { 'aria-label': 'Salida', title: 'A dónde va el sonido de la pista', onchange: (e) => G.run('setTrack', { trackId: track.id, output: +e.target.value }) },
      el('option', { value: '0', text: 'Master', selected: !track.output }));
    buses().forEach((b) => sel.appendChild(el('option', { value: String(b.id), text: b.name, selected: track.output === b.id })));
    return sel;
  }

  function sendsRow(track) {
    const row = el('div.sends', null, el('b', { text: 'Envíos' }));
    const byId = new Map(buses().map((b) => [b.id, b]));
    (track.sends || []).forEach((snd) => {
      const bus = byId.get(snd.busId);
      if (!bus) return;
      const slider = sendSlider(track, snd);
      const text = el('span.note', { text: G.dbToText(snd.levelDb) });
      slider._text = text;
      row.appendChild(el('span.send', { style: { '--c': bus.colour } },
        el('span.bname', { text: bus.name }), slider, text,
        el('button.pill.tiny' + (snd.pre ? '.on' : ''), { title: snd.pre ? 'Antes del volumen (pre): el envío no cambia al mover el volumen de la pista' : 'Después del volumen (post): sigue al volumen de la pista',
          onclick: () => G.run('setSend', { trackId: track.id, busId: snd.busId, pre: !snd.pre }) }, snd.pre ? 'pre' : 'post'),
        el('button.icon-btn', { title: 'Quitar envío', 'aria-label': 'Quitar envío', onclick: () => G.run('setSend', { trackId: track.id, busId: snd.busId, remove: true }) }, G.icon('close', 12))));
    });
    row.appendChild(el('button.pill', { onclick: (e) => addSendMenu(e, track) }, '+ Envío'));
    row.appendChild(el('span.grow'));
    row.appendChild(el('span.note', { text: 'Salida' }));
    row.appendChild(outputSelect(track));
    return row;
  }

  // ------------------------------------------------------------------ mixer (like Logic's, simplified)
  P.mixer = function (body) {
    body.innerHTML = '';
    const wrap = el('div.mixer');
    const tracks = G.state.tracks;
    tracks.forEach((t, i) => wrap.appendChild(strip(t, i)));
    wrap.appendChild(masterStrip());
    body.appendChild(wrap);
    if (!buses().length)
      body.appendChild(el('p.note.mixhint', { text: 'Tip: con "+ Envío" en una pista creas una reverb compartida. Varias pistas en la misma reverb suenan más juntas y gastan menos computadora.' }));
  };

  function fader(value, onInput, onReset, label) {
    const f = el('input.vfader', { type: 'range', min: -60, max: 6, step: 0.5, value, 'aria-label': label, orient: 'vertical' });
    f.addEventListener('input', G.throttle(() => onInput(+f.value), 40));
    f.addEventListener('dblclick', () => { f.value = 0; onReset(); });
    return f;
  }

  function strip(t, index) {
    const sel = t.id === G.sel.trackId;
    const open = (slot) => { G.Timeline.select(t.id, 0); P._slot = slot; P._slotTrack = t.id; G.App.showBottom('controls'); };
    const set = (args) => G.run('setTrack', Object.assign({ trackId: t.id }, args));
    const dbText = el('span.db', { text: G.dbToText(t.volumeDb) });
    const vol = fader(t.volumeDb, (v) => { t.volumeDb = v; dbText.textContent = G.dbToText(v); G.call('setTrack', { trackId: t.id, volumeDb: v }); },
      () => { t.volumeDb = 0; dbText.textContent = G.dbToText(0); G.call('setTrack', { trackId: t.id, volumeDb: 0 }); }, 'Volumen de ' + t.name);
    const pan = el('input.pan', { type: 'range', min: -1, max: 1, step: 0.02, value: t.pan, 'aria-label': 'Paneo', title: 'Paneo' });
    pan.addEventListener('input', G.throttle(() => { t.pan = +pan.value; G.call('setTrack', { trackId: t.id, pan: t.pan }); }, 40));
    pan.addEventListener('dblclick', () => { pan.value = t.pan = 0; G.call('setTrack', { trackId: t.id, pan: 0 }); });
    const driven = (target) => !t.autoWrite && (t.automation || []).some((l) => l.target === target && l.on && l.points.length);
    if (driven('volume')) { vol.disabled = true; vol.title = 'Lo maneja la automatización'; dbText.textContent = 'auto'; }
    if (driven('pan')) { pan.disabled = true; pan.title = 'Lo maneja la automatización'; }

    const fx = el('div.mfx');
    if (t.instrument && t.kind !== 'audio') fx.appendChild(el('button.mslot.inst', { title: t.instrument.name, onclick: () => open(-1) }, t.instrument.name));
    t.inserts.forEach((s, i) => fx.appendChild(el('button.mslot' + (s.bypass ? '.off' : ''), { title: s.name, onclick: () => open(i) }, s.name)));
    fx.appendChild(el('button.mslot.add', { title: 'Agregar efecto', onclick: () => open(t.inserts.length ? t.inserts.length - 1 : null) }, '+'));

    const sends = el('div.msends');
    if (t.kind !== 'bus') {
      const byId = new Map(buses().map((b) => [b.id, b]));
      (t.sends || []).forEach((snd) => {
        const b = byId.get(snd.busId);
        if (b) sends.appendChild(el('div.msend', { title: b.name + (snd.pre ? ' (pre)' : '') }, el('span', { text: b.name }), sendSlider(t, snd, true)));
      });
      sends.appendChild(el('button.mslot.add', { title: 'Agregar envío', onclick: (e) => addSendMenu(e, t) }, '+ envío'));
    }

    const meter = el('div.vmeter', { 'data-mixmeter': index }, el('i'), el('i'));
    return el('div.strip' + (sel ? '.sel' : '') + (t.kind === 'bus' ? '.bus' : ''), { style: { '--c': t.colour } },
      el('button.sname', { title: t.name, onclick: () => open(t.kind === 'audio' || t.kind === 'bus' ? (t.inserts.length ? 0 : null) : -1) }, t.name),
      fx, sends,
      t.kind !== 'bus' ? outputSelect(t) : el('span.note.center', { text: '→ Master' }),
      pan,
      el('div.fadrow', null, vol, meter),
      dbText,
      el('div.ms', null,
        el('button.tb.mute' + (t.mute ? '.on' : ''), { 'aria-label': 'Silenciar', onclick: () => set({ mute: !t.mute }) }, 'M'),
        el('button.tb.solo' + (t.solo ? '.on' : ''), { 'aria-label': 'Solo', onclick: () => set({ solo: !t.solo }) }, 'S')));
  }

  function masterStrip() {
    const s = G.state.song;
    const dbText = el('span.db', { text: G.dbToText(s.masterDb) });
    const vol = fader(s.masterDb, (v) => { s.masterDb = v; dbText.textContent = G.dbToText(v); G.call('setSong', { masterDb: v }); },
      () => { s.masterDb = 0; dbText.textContent = G.dbToText(0); G.call('setSong', { masterDb: 0 }); }, 'Volumen general');
    const fx = el('div.mfx');
    G.state.master.forEach((x) => fx.appendChild(el('button.mslot' + (x.bypass ? '.off' : ''), { onclick: () => G.App.showBottom('master') }, x.name)));
    fx.appendChild(el('button.mslot' + (s.limiter ? '' : '.off'), { title: 'Limitador', onclick: () => G.run('setSong', { limiter: !s.limiter }) }, 'Limitador'));
    return el('div.strip.master', { style: { '--c': '#E6A84A' } },
      el('button.sname', { onclick: () => G.App.showBottom('master') }, 'Master'),
      fx, el('div.msends'), el('span.note.center', { text: 'Salida principal' }), el('span'),
      el('div.fadrow', null, vol, el('div.vmeter', { 'data-mixmeter': 'm' }, el('i'), el('i'))),
      dbText, el('div.ms'));
  }

  // Where a new effect goes: amps, drives and plugins before the "space" effects (chorus, delay, reverb),
  // so a reverb never ends up feeding an amp.
  const SPACE = ['chorus', 'delay', 'reverb'];
  const AMPS = ['amp', 'nam'];
  function smartIndex(trackId, type, kind) {
    const list = trackId === 0 ? G.state.master : (G.Timeline.track(trackId) || { inserts: [] }).inserts;
    if (kind === 'builtin' && SPACE.includes(type)) return -1;            // at the end
    if (kind === 'builtin' && type === 'gate') return 0;                 // noise gate goes first
    const firstSpace = list.findIndex((s) => s.kind === 'builtin' && SPACE.includes(s.type));
    return firstSpace >= 0 ? firstSpace : -1;
  }
  P.isAmp = (s) => (s.kind === 'builtin' && AMPS.includes(s.type)) || (s.kind === 'plugin' && /tone ?3000|neural|nam\b|amp|guitar rig|bias|archetype|helix|tonex|kemper/i.test(s.name));

  /** Two amps in a row sound bad: when one goes on, offer to switch the others off. */
  P.checkAmps = async function (track, newIndex) {
    const t = G.Timeline.track(track.id) || track;
    const added = t.inserts[newIndex];
    if (!added || added.bypass || !P.isAmp(added)) return;
    const others = t.inserts.map((s, i) => ({ s, i })).filter(({ s, i }) => i !== newIndex && !s.bypass && P.isAmp(s));
    if (!others.length) return;
    G.modal('Dos amplificadores juntos', (body, close) => {
      body.append(
        el('p.note', { text: `${added.name} ya es un amplificador. Si la guitarra pasa también por ${others.map((o) => o.s.name).join(' y ')}, se distorsiona dos veces y suena mal.` }),
        el('div.row.end', null,
          el('button.btn', { onclick: close }, 'Dejar los dos'),
          el('button.btn.primary', { onclick: async () => {
            close();
            for (const o of others) await G.call('setBypass', { trackId: t.id, slot: o.i, bypass: true });
            G.toast('Apagué ' + others.map((o) => o.s.name).join(' y '));
            G.App.refresh();
          } }, 'Apagar ' + others.map((o) => o.s.name).join(' y '))));
    });
  };

  /** Amp-head captures have no speaker: straight to the headphones they sound like fizz/static. */
  P.checkCab = async function (track, slot) {
    const t = G.Timeline.track(track.id) || track;
    const s = t.inserts[slot];
    if (!s || !s.extra || !s.extra.needsCab) return;
    const hasCab = t.inserts.some((x, i) => i > slot && !x.bypass && ((x.kind === 'builtin' && x.type === 'cab') || (x.kind === 'plugin' && /cab|ir\b|impulse/i.test(x.name))));
    if (hasCab) return;
    G.modal('Esta captura es solo el amplificador', (body, close) => {
      body.append(
        el('p.note', { text: `"${s.extra.fileName}" es un cabezal sin parlante. Sin gabinete, la guitarra suena con un zumbido áspero, como estática.` }),
        el('div.row.end', null,
          el('button.btn', { onclick: close }, 'Dejar así'),
          el('button.btn.primary', { onclick: async () => {
            close();
            await G.call('addInsert', { trackId: t.id, kind: 'builtin', id: 'cab', index: slot + 1 });
            P._slot = slot + 1;
            G.toast('Agregué un Gabinete. Puedes cargarle una IR de tone3000.com.');
            G.App.refresh();
          } }, 'Agregar gabinete')));
    });
  };

  P.toggleBypass = async function (track, i, bypass) {
    await G.run('setBypass', { trackId: track.id || 0, slot: i, bypass });
    if (!bypass && track.id) { await G.App.refresh(); P.checkAmps(track, i); }
  };

  async function addEffect(trackId, kind, id) {
    const index = smartIndex(trackId, id, kind);
    const r = await G.call('addInsert', { trackId, kind, id, index });
    if (r && r.ok === false) { G.toast(r.error, true); return; }
    const list = trackId === 0 ? null : G.Timeline.track(trackId);
    const at = index < 0 ? (list ? list.inserts.length : 0) : index;
    P._slot = at;
    if (kind === 'plugin') { G.toast('Abriendo el plugin…'); setTimeout(async () => { await G.App.refresh(); if (list) P.checkAmps(list, at); }, 900); }
    else if (list) { await G.App.refresh(); P.checkAmps(list, at); }
  }

  function addEffectMenu(e, trackId) {
    const items = [{ heading: 'Efectos de Galpon' }];
    (G.lib.effects || []).forEach((fx) => items.push({ label: fx.name, action: () => addEffect(trackId, 'builtin', fx.type) }));
    const plugins = (G.lib.plugins || []).filter((p) => !p.instrument);
    if (plugins.length) {
      // Third-party plugins first, then Apple's
      const sorted = plugins.slice().sort((a, b) => (a.maker === 'Apple') - (b.maker === 'Apple') || a.name.localeCompare(b.name));
      items.push('-', { heading: 'Plugins' });
      sorted.forEach((p) => items.push({ label: p.name, hint: p.format === 'AudioUnit' ? 'AU' : p.format, action: () => addEffect(trackId, 'plugin', p.id) }));
    } else {
      items.push('-', { label: 'Buscar plugins instalados…', action: () => G.App.openSettings('plugins') });
    }
    const r = e.target.getBoundingClientRect();
    G.menu(r.left, r.bottom + 4, items);
  }

  // ------------------------------------------------------------------ note editor
  const DRUM_ROWS = [
    [49, 'Platillo crash'], [51, 'Ride'], [46, 'Hi-hat abierto'], [42, 'Hi-hat cerrado'], [44, 'Hi-hat pedal'],
    [50, 'Tom agudo'], [47, 'Tom medio'], [45, 'Tom grave'], [43, 'Tom de piso'],
    [39, 'Aplauso'], [38, 'Caja'], [37, 'Aro'], [36, 'Bombo'], [54, 'Pandereta'], [70, 'Shaker'], [56, 'Cencerro'],
  ];

  P.editor = function (body, track, clip) {
    body.innerHTML = '';
    if (!clip) {
      body.appendChild(el('p.note', { text: 'Elige una región (haz doble clic en ella) para editarla acá.' }));
      return;
    }
    if (!clip.midi) return audioEditor(body, track, clip);

    const drums = track.kind === 'drummer' || (track.instrument && track.instrument.type === 'drums');
    let notes = (clip.notes || []).map((n) => ({ beat: n[0], length: n[1], pitch: n[2], vel: n[3] }));
    let selected = -1;
    let grid = 0.25;
    const rows = drums ? DRUM_ROWS.map((r) => r[0]) : Array.from({ length: 73 }, (_, i) => 96 - i);   // C7..C1
    const rowH = drums ? 16 : 12, keyW = drums ? 104 : 46;

    const velSlider = el('input', { type: 'range', min: 1, max: 127, value: 100, disabled: true, 'aria-label': 'Fuerza de la nota' });
    const toolbar = el('div.row', null,
      el('span.note', { text: 'Cuadrícula' }),
      ...[[1, '1/4'], [0.5, '1/8'], [0.25, '1/16'], [0.125, '1/32']].map(([g, label]) =>
        el('button.pill' + (g === grid ? '.on' : ''), { onclick: (e) => { grid = g; G.$$('.gridbtn', toolbar).forEach((b) => b.classList.remove('on')); e.target.classList.add('on'); }, class: 'pill gridbtn' + (g === grid ? ' on' : '') }, label)),
      el('button.pill', { onclick: () => G.run('quantize', { trackId: track.id, clipId: clip.id, grid }) }, 'Cuantizar'),
      drums ? null : el('button.pill', { onclick: () => G.run('transposeClip', { trackId: track.id, clipId: clip.id, semitones: 1 }) }, '+1 semitono'),
      drums ? null : el('button.pill', { onclick: () => G.run('transposeClip', { trackId: track.id, clipId: clip.id, semitones: -1 }) }, '−1 semitono'),
      el('span.note', { text: 'Fuerza' }), velSlider,
      el('span.note', { text: 'Clic: agregar · arrastrar: mover · borde: alargar · doble clic: borrar' }));

    const scroller = el('div.roll', { style: { overflowY: 'auto' } });
    const canvas = el('canvas', { tabindex: 0, 'aria-label': 'Editor de notas' });
    scroller.appendChild(canvas);
    body.appendChild(el('div.roll-wrap', null, toolbar, scroller));

    const L = Math.max(0.25, Math.min(clip.loopLength, clip.length));
    let ppb = 40;
    const xOf = (b) => keyW + b * ppb, yOf = (p) => rows.indexOf(p) * rowH;

    function draw() {
      const w = Math.max(scroller.clientWidth - 2, 300);
      ppb = Math.max(18, (w - keyW - 8) / L);
      const h = rows.length * rowH;
      const dpr = window.devicePixelRatio || 1;
      canvas.width = w * dpr; canvas.height = h * dpr;
      canvas.style.width = w + 'px'; canvas.style.height = h + 'px';
      const g = canvas.getContext('2d');
      g.scale(dpr, dpr);
      g.fillStyle = '#0f0f11'; g.fillRect(0, 0, w, h);
      rows.forEach((p, i) => {
        const black = !drums && [1, 3, 6, 8, 10].includes(p % 12);
        g.fillStyle = black ? '#141417' : (i % 2 && drums ? '#121215' : '#0f0f11');
        g.fillRect(keyW, i * rowH, w, rowH);
        g.fillStyle = drums ? '#232326' : black ? '#1a1a1d' : '#e4e4e7';
        g.fillRect(0, i * rowH, keyW - 2, rowH - 1);
        g.fillStyle = drums ? '#d4d4d8' : black ? '#a1a1aa' : '#3f3f46';
        g.font = '10px Inter, -apple-system, sans-serif';
        if (drums) g.fillText(DRUM_ROWS[i][1], 6, i * rowH + 12);
        else if (p % 12 === 0) g.fillText(G.noteName(p), 4, i * rowH + 10);
      });
      for (let b = 0; b <= L + 1e-9; b += 0.25) {
        const bar = Math.abs(b / (G.Timeline.bpb()) - Math.round(b / G.Timeline.bpb())) < 1e-6;
        g.fillStyle = bar ? '#3a3a3f' : Number.isInteger(b) ? '#26262a' : '#1a1a1d';
        g.fillRect(xOf(b), 0, 1, h);
      }
      notes.forEach((n, i) => {
        const idx = rows.indexOf(n.pitch);
        if (idx < 0) return;
        const a = 0.45 + 0.55 * n.vel;
        g.fillStyle = i === selected ? '#f7d9a3' : track.colour;
        g.globalAlpha = a;
        g.fillRect(xOf(n.beat) + 1, idx * rowH + 1, Math.max(4, n.length * ppb - 2), rowH - 2);
        g.globalAlpha = 1;
      });
      if (G.tick && G.tick.beat >= clip.start && G.tick.beat < clip.start + clip.length) {
        const local = (G.tick.beat - clip.start) % L;
        g.fillStyle = '#f2f2f3';
        g.fillRect(xOf(local), 0, 2, h);
      }
    }
    P._rollDraw = draw;

    const commit = () => G.run('setNotes', { trackId: track.id, clipId: clip.id, notes: notes.map((n) => [n.beat, n.length, n.pitch, n.vel]) });
    const hit = (x, y) => {
      for (let i = notes.length - 1; i >= 0; i--) {
        const n = notes[i], nx = xOf(n.beat), ny = yOf(n.pitch);
        if (ny < 0) continue;
        if (x >= nx && x <= nx + Math.max(4, n.length * ppb) && y >= ny && y < ny + rowH) return { i, edge: x > nx + n.length * ppb - 6 };
      }
      return null;
    };
    const preview = (p, v) => { G.call('noteOn', { pitch: p, velocity: v }); setTimeout(() => G.call('noteOff', { pitch: p }), 180); };

    canvas.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      const r = canvas.getBoundingClientRect();
      const x = e.clientX - r.left, y = e.clientY - r.top;
      if (x < keyW) { const p = rows[Math.floor(y / rowH)]; if (p !== undefined) preview(p, 0.8); return; }
      let h = hit(x, y);
      if (!h) {
        const beat = Math.floor((x - keyW) / ppb / grid) * grid;
        const pitch = rows[Math.floor(y / rowH)];
        if (pitch === undefined || beat >= L) return;
        notes.push({ beat, length: grid, pitch, vel: velSlider.disabled ? 0.8 : velSlider.value / 127 });
        h = { i: notes.length - 1, edge: true };
        preview(pitch, 0.8);
      }
      selected = h.i;
      velSlider.disabled = false;
      velSlider.value = Math.round(notes[selected].vel * 127);
      const n = notes[selected], start = { ...n }, x0 = x, y0 = y;
      canvas.setPointerCapture(e.pointerId);
      const move = (ev) => {
        const dx = (ev.clientX - r.left - x0) / ppb, dy = Math.round((ev.clientY - r.top - y0) / rowH);
        if (h.edge) n.length = Math.max(grid / 2, Math.round((start.length + dx) / grid) * grid || grid);
        else {
          n.beat = clamp(Math.round((start.beat + dx) / grid) * grid, 0, L - grid);
          const idx = clamp(rows.indexOf(start.pitch) + dy, 0, rows.length - 1);
          if (rows[idx] !== n.pitch) { n.pitch = rows[idx]; preview(n.pitch, n.vel); }
        }
        draw();
      };
      const up = () => { canvas.removeEventListener('pointermove', move); canvas.removeEventListener('pointerup', up); commit(); };
      canvas.addEventListener('pointermove', move); canvas.addEventListener('pointerup', up);
      draw();
    });
    canvas.addEventListener('dblclick', (e) => {
      const r = canvas.getBoundingClientRect();
      const h = hit(e.clientX - r.left, e.clientY - r.top);
      if (h) { notes.splice(h.i, 1); selected = -1; draw(); commit(); }
    });
    canvas.addEventListener('keydown', (e) => {
      if ((e.key === 'Backspace' || e.key === 'Delete') && selected >= 0) { notes.splice(selected, 1); selected = -1; draw(); commit(); e.stopPropagation(); e.preventDefault(); }
    });
    velSlider.addEventListener('change', () => { if (selected >= 0) { notes[selected].vel = velSlider.value / 127; draw(); commit(); } });

    setTimeout(() => {
      draw();
      // scroll to where the notes are
      if (!drums && notes.length) {
        const top = Math.min(...notes.map((n) => yOf(n.pitch)).filter((v) => v >= 0));
        scroller.scrollTop = Math.max(0, top - 40);
      } else if (!drums) scroller.scrollTop = yOf(72) - 20;
    }, 0);
  };

  function audioEditor(body, track, clip) {
    const flex = Object.assign({ on: false, followTempo: false, quantizeGrid: 0, quantizeStrength: 1, tuneStrength: 0, tuneChromatic: false, noteShifts: [] }, clip.flex || {});
    let shifts = new Map((flex.noteShifts || []).map(([k, v]) => [k.toFixed(3), v]));
    let analysis = null;

    const gain = el('input', { type: 'range', min: -24, max: 12, step: 0.5, value: clip.gainDb || 0, 'aria-label': 'Volumen de la región' });
    const gainText = el('span.note', { text: G.dbToText(clip.gainDb || 0) });
    gain.addEventListener('input', () => { gainText.textContent = G.dbToText(+gain.value); });
    gain.addEventListener('change', () => G.run('setClip', { trackId: track.id, clipId: clip.id, gainDb: +gain.value }));
    const seconds = clip.length * 60 / G.state.song.bpm;

    // Flex controls (like Logic's Flex Pitch / Flex Time)
    const KEY_ES = { C: 'Do', 'C#': 'Do#', D: 'Re', 'D#': 'Re#', E: 'Mi', F: 'Fa', 'F#': 'Fa#', G: 'Sol', 'G#': 'Sol#', A: 'La', 'A#': 'La#', B: 'Si' };
    const songKey = (KEY_ES[G.state.song.key] || G.state.song.key) + (G.state.song.minor ? ' menor' : ' mayor');
    const tune = el('input', { type: 'range', min: 0, max: 100, step: 5, value: Math.round(flex.tuneStrength * 100), 'aria-label': 'Cuánto afinar' });
    const tuneText = el('span.note', { text: tune.value + ' %' });
    tune.addEventListener('input', () => { tuneText.textContent = tune.value + ' %'; draw(); });
    const scale = el('select', { 'aria-label': 'Escala para afinar' },
      el('option', { value: 'key', text: 'Notas de ' + songKey, selected: !flex.tuneChromatic }),
      el('option', { value: 'chromatic', text: 'Todas las notas (cromática)', selected: flex.tuneChromatic }));
    const quant = el('select', { 'aria-label': 'Cuantizar audio' },
      ...[[0, 'No cuantizar'], [1, 'A negras (1/4)'], [0.5, 'A corcheas (1/8)'], [0.25, 'A semicorcheas (1/16)']]
        .map(([g, label]) => el('option', { value: String(g), text: label, selected: Math.abs((flex.quantizeGrid || 0) - g) < 1e-6 })));
    const qStrength = el('input', { type: 'range', min: 10, max: 100, step: 10, value: Math.round((flex.quantizeStrength || 1) * 100), 'aria-label': 'Fuerza de la cuantización' });
    const follow = el('input', { type: 'checkbox', checked: !!flex.followTempo, 'aria-label': 'Seguir el tempo' });
    const settings = () => ({
      tuneStrength: +tune.value / 100, tuneChromatic: scale.value === 'chromatic',
      quantizeGrid: +quant.value, quantizeStrength: +qStrength.value / 100, followTempo: follow.checked,
      noteShifts: [...shifts.entries()].filter(([, v]) => v !== 0).map(([k, v]) => [parseFloat(k), v]),
    });
    const apply = async () => {
      G.toast('Procesando la región…');
      const ok = await G.run('flexApply', { trackId: track.id, clipId: clip.id, settings: settings() });
      if (ok !== null && ok !== false) G.toast('Listo. Si no te gusta, Cmd+Z o "Quitar Flex".');
    };

    body.append(
      el('div.row', null, el('b', { text: clip.name }), el('span.note', { text: `${seconds.toFixed(2)} s` + (flex.on ? ' · con Flex' : '') }),
        el('span.note', { text: 'Volumen' }), gain, gainText),
      el('div.flexbar', null,
        el('span.fgroup', null, el('b', { text: 'Afinar' }), tune, tuneText, scale),
        el('span.fgroup', null, el('b', { text: 'Tiempo' }), quant, el('span.note', { text: 'fuerza' }), qStrength,
          el('label.note', null, follow, ' Seguir el tempo de la canción')),
        el('button.btn.primary', { onclick: apply }, 'Aplicar'),
        flex.on ? el('button.btn', { onclick: () => G.run('flexReset', { trackId: track.id, clipId: clip.id }) }, 'Quitar Flex') : null));

    const canvas = el('canvas.flexview', { tabindex: 0, 'aria-label': 'Notas detectadas: arrástralas para cambiarles la altura' });
    const hint = el('span.note', { text: 'Analizando la grabación…' });
    body.append(canvas, hint);

    let peaks = null;
    G.call('getWaveform', { file: clip.audioFile, points: 2000 }).then((r) => { if (r && r.ok) { peaks = r.data; draw(); } });
    G.call('flexAnalyze', { trackId: track.id, clipId: clip.id }).then((r) => {
      if (!r || !r.ok) { hint.textContent = 'No se pudo analizar esta región.'; return; }
      analysis = r.data;
      hint.textContent = analysis.notes.length
        ? `${analysis.notes.length} notas detectadas. Arrastra una nota para subirla o bajarla, y toca Aplicar. La línea fina es cómo se cantó; la barra, cómo va a quedar.`
        : 'No encontré notas con altura clara (¿es batería o ruido?). Igual puedes cuantizar el tiempo.';
      draw();
    });

    let range = { lo: 48, hi: 72 };
    let drag = null;
    const H = 190;
    function geometry() {
      const w = canvas.clientWidth || 600;
      return { w, xOf: (beat) => beat / clip.length * w, yOf: (m) => H - 8 - (m - range.lo) / (range.hi - range.lo) * (H - 16) };
    }
    function correctionFor(n) {
      // preview: what the engine will do (tuning towards the nearest semitone/key note + manual shift)
      const manual = shifts.get(n.key.toFixed(3)) || 0;
      if (!analysis) return manual;
      const strength = +tune.value / 100;
      let target = Math.round(n.pitch);
      if (scale.value === 'key') {
        const keys = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
        const root = Math.max(0, keys.indexOf(G.state.song.key));
        const steps = G.state.song.minor ? [0, 2, 3, 5, 7, 8, 10] : [0, 2, 4, 5, 7, 9, 11];
        const ok = (m) => steps.includes((((m - root) % 12) + 12) % 12);
        for (let d = 0; d <= 6; d++) {
          const up = Math.round(n.pitch) + d, down = Math.round(n.pitch) - d;
          if (ok(up) && ok(down)) { target = Math.abs(up - n.pitch) < Math.abs(down - n.pitch) ? up : down; break; }
          if (ok(up)) { target = up; break; }
          if (ok(down)) { target = down; break; }
        }
      }
      return (target - n.pitch) * strength + manual;
    }
    function draw() {
      const dpr = window.devicePixelRatio || 1;
      const { w, xOf, yOf } = geometry();
      canvas.width = w * dpr; canvas.height = H * dpr;
      canvas.style.height = H + 'px';
      const g = canvas.getContext('2d');
      g.scale(dpr, dpr);
      g.fillStyle = '#0f0f11'; g.fillRect(0, 0, w, H);
      if (peaks && peaks.peaks) {
        g.fillStyle = 'rgba(255,255,255,0.07)';
        const spb = peaks.sampleRate * 60 / G.state.song.bpm, startS = clip.offsetSamples, lenS = clip.length * spb;
        for (let x = 0; x < w; x++) {
          const v = peaks.peaks[Math.floor((startS + x / w * lenS) / peaks.samples * peaks.peaks.length)] || 0;
          g.fillRect(x, H / 2 - v * H / 2, 1, Math.max(1, v * H));
        }
      }
      if (!analysis) return;
      const notes = analysis.notes;
      if (notes.length) {
        const ps = notes.map((n) => n.pitch);
        range = { lo: Math.floor(Math.min(...ps)) - 3, hi: Math.ceil(Math.max(...ps)) + 3 };
      }
      // semitone lines with note names
      g.font = '10px Inter, sans-serif';
      for (let m = Math.ceil(range.lo); m <= range.hi; m++) {
        const y = yOf(m);
        g.fillStyle = m % 12 === 0 ? '#2f2f35' : '#1c1c20';
        g.fillRect(0, y, w, 1);
        if (range.hi - range.lo < 30 || m % 12 === 0) { g.fillStyle = '#6b6b73'; g.fillText(G.noteName(m), 4, y - 2); }
      }
      // hits (Flex Time)
      g.fillStyle = 'rgba(230,168,74,0.5)';
      (analysis.hits || []).forEach((b) => g.fillRect(xOf(b), 0, 1, 6));
      // how it was sung
      g.strokeStyle = 'rgba(255,255,255,0.45)'; g.lineWidth = 1;
      g.beginPath();
      let pen = false;
      analysis.curve.forEach(([b, m]) => {
        if (m === null || m === undefined) { pen = false; return; }
        const x = xOf(b), y = yOf(m);
        if (!pen) { g.moveTo(x, y); pen = true; } else g.lineTo(x, y);
      });
      g.stroke();
      // how it will sound
      notes.forEach((n, i) => {
        const corr = drag && drag.i === i ? drag.preview : correctionFor(n);
        const y = yOf(n.pitch + corr);
        g.fillStyle = track.colour;
        g.globalAlpha = 0.85;
        g.fillRect(xOf(n.start), y - 5, Math.max(3, xOf(n.start + n.length) - xOf(n.start)), 10);
        g.globalAlpha = 1;
        if (Math.abs(corr) > 0.05) { g.fillStyle = '#f2f2f3'; g.fillText((corr > 0 ? '+' : '') + corr.toFixed(1), xOf(n.start) + 2, y - 7); }
      });
    }
    function noteAt(x, y) {
      if (!analysis) return -1;
      const { xOf, yOf } = geometry();
      return analysis.notes.findIndex((n) => x >= xOf(n.start) - 2 && x <= xOf(n.start + n.length) + 2 && Math.abs(y - yOf(n.pitch + correctionFor(n))) < 9);
    }
    canvas.addEventListener('pointerdown', (e) => {
      const r = canvas.getBoundingClientRect();
      const i = noteAt(e.clientX - r.left, e.clientY - r.top);
      if (i < 0) return;
      canvas.setPointerCapture(e.pointerId);
      const n = analysis.notes[i];
      drag = { i, y0: e.clientY, base: shifts.get(n.key.toFixed(3)) || 0, preview: correctionFor(n) };
      const pxPerSemi = (H - 16) / (range.hi - range.lo);
      const move = (ev) => {
        const semis = Math.round((drag.y0 - ev.clientY) / pxPerSemi);
        shifts.set(n.key.toFixed(3), drag.base + semis);
        drag.preview = correctionFor(n);
        draw();
      };
      const up = () => { canvas.removeEventListener('pointermove', move); canvas.removeEventListener('pointerup', up); drag = null; draw();
        hint.textContent = 'Nota movida. Toca Aplicar para escucharla (puedes mover varias antes).'; };
      canvas.addEventListener('pointermove', move); canvas.addEventListener('pointerup', up);
    });
    canvas.addEventListener('dblclick', (e) => {
      const r = canvas.getBoundingClientRect();
      const i = noteAt(e.clientX - r.left, e.clientY - r.top);
      if (i >= 0) { shifts.delete(analysis.notes[i].key.toFixed(3)); draw(); }
    });
    [scale].forEach((x) => x.addEventListener('change', draw));
    requestAnimationFrame(draw);
  }

  /** After a tempo change: offer to stretch recordings so they keep playing in time (Flex). */
  P.offerFollowTempo = function () {
    const clips = [];
    G.state.tracks.forEach((t) => t.clips.forEach((c) => {
      if (!c.midi && c.sourceBpm > 0 && Math.abs(c.sourceBpm - G.state.song.bpm) > 0.01 && !(c.flex && c.flex.on && c.flex.followTempo)) clips.push([t, c]);
    }));
    if (!clips.length) return;
    G.modal('Cambiaste el tempo', (body, close) => {
      body.append(
        el('p.note', { text: `Hay ${clips.length === 1 ? 'una grabación' : clips.length + ' grabaciones'} hechas a otro tempo. ¿Las estiro para que sigan sonando a tiempo? (Se puede deshacer.)` }),
        el('div.row.end', null,
          el('button.btn', { onclick: close }, 'Dejarlas como están'),
          el('button.btn.primary', { onclick: async () => {
            close();
            G.toast('Estirando las grabaciones…');
            for (const [t, c] of clips)
              await G.call('flexApply', { trackId: t.id, clipId: c.id, settings: Object.assign({}, c.flex && c.flex.on ? c.flex : {}, { followTempo: true }) });
            await G.App.refresh();
            G.toast('Listo: las grabaciones siguen el tempo nuevo.');
          } }, 'Estirarlas')));
    });
  };

  // ------------------------------------------------------------------ drummer
  P.drummer = function (body, track, clip) {
    body.innerHTML = '';
    const d = Object.assign({}, track.drummer);
    let first = true;
    const push = G.throttle(() => {
      G.call('setDrummer', { trackId: track.id, settings: d, noUndo: !first });
      first = false;
    }, 120);
    const set = (k, v) => { d[k] = v; push(); };

    // Styles
    const styles = el('div.drum-grid');
    (G.lib.drummerStyles || []).forEach((s) => {
      styles.appendChild(el('button.style-btn' + (s.id === d.style ? '.on' : ''), {
        onclick: () => { d.style = s.id; G.call('setDrummer', { trackId: track.id, settings: d }); G.$$('.style-btn', styles).forEach((b) => b.classList.remove('on')); styles.children[(G.lib.drummerStyles || []).indexOf(s)].classList.add('on'); },
      }, el('b', { text: s.name }), el('small', { text: `${s.who} · ${s.description}` })));
    });

    // XY pad
    const puck = el('span.puck');
    const xy = el('div.xy', { role: 'group', 'aria-label': 'Horizontal: de sencillo a complejo. Vertical: de suave a fuerte.' },
      el('span.lbl', { text: 'FUERTE', style: { left: '8px', top: '6px' } }),
      el('span.lbl', { text: 'SUAVE', style: { left: '8px', bottom: '6px' } }),
      el('span.lbl', { text: 'COMPLEJO →', style: { right: '8px', bottom: '6px' } }),
      el('span.lbl', { text: '← SENCILLO', style: { left: '58px', bottom: '6px' } }), puck);
    const placePuck = () => { puck.style.left = d.complexity * 100 + '%'; puck.style.top = (1 - d.loudness) * 100 + '%'; };
    placePuck();
    xy.addEventListener('pointerdown', (e) => {
      xy.setPointerCapture(e.pointerId);
      first = true;
      const move = (ev) => {
        const r = xy.getBoundingClientRect();
        d.complexity = clamp((ev.clientX - r.left) / r.width, 0, 1);
        d.loudness = clamp(1 - (ev.clientY - r.top) / r.height, 0, 1);
        placePuck(); push();
      };
      move(e);
      const up = () => { xy.removeEventListener('pointermove', move); xy.removeEventListener('pointerup', up); };
      xy.addEventListener('pointermove', move); xy.addEventListener('pointerup', up);
    });

    const slider = (label, key, fmt) => {
      const input = el('input', { type: 'range', min: 0, max: 1, step: 0.01, value: d[key], 'aria-label': label });
      const shown = el('span', { text: fmt(d[key]) });
      input.addEventListener('pointerdown', () => { first = true; });
      input.addEventListener('input', () => { shown.textContent = fmt(+input.value); set(key, +input.value); });
      return el('div.slider', null, el('div.lab', null, label, shown), input);
    };
    const pct = (v) => Math.round(v * 100) + '%';
    const amount = (v) => (v < 0.05 ? 'Ninguno' : v < 0.35 ? 'Pocos' : v < 0.7 ? 'Algunos' : 'Muchos');

    // Who it listens to: a recorded guitar (accents, how hard it's played) or the notes of a bass
    const listens = track.listensTo;   // [id, name, isAudio] of the track it's following now
    const candidates = G.state.tracks.filter((t) => t.kind === 'audio' || t.kind === 'instrument');
    const followSel = el('select.sel', { 'aria-label': 'Escucha a', onchange: (e) => set('followTrack', +e.target.value) },
      el('option', { value: -1, text: 'Automático' + (d.followTrack === -1 && listens ? ` (${listens[1]})` : ''), selected: d.followTrack === -1 }),
      el('option', { value: 0, text: 'Nadie (toca libre)', selected: d.followTrack === 0 }),
      ...candidates.map((t) => el('option', { value: t.id, text: t.name + (t.kind === 'audio' ? ' (grabación)' : ' (notas)'), selected: t.id === d.followTrack })));
    const listenNote = el('span.note', { text: !listens ? (d.followTrack === 0 ? 'Toca libre, sin escuchar a nadie.' : 'Todavía no hay nada grabado para escuchar.')
      : listens[2] ? `Escucha a "${listens[1]}": marca sus golpes con el bombo y toca más fuerte o más suave según cómo toques.`
      : `Sigue las notas de "${listens[1]}" con el bombo.` });
    const relisten = el('button.btn', { title: 'Después de grabar, vuelve a escuchar la grabación y rehace la batería', onclick: () => G.run('setDrummer', { trackId: track.id, settings: d }) }, 'Volver a escuchar');

    const left = el('div.stack', { style: { flex: '0 1 300px', minWidth: '260px' } },
      el('div.row', null, el('b', { text: 'Estilo' }), el('span.note', { text: 'Cada baterista toca distinto.' })), styles);

    const mid = el('div.split', null, xy,
      el('div.stack', { style: { minWidth: '200px' } },
        slider('Redobles', 'fills', amount),
        slider('Swing', 'swing', pct),
        slider('Humano (imperfección)', 'humanize', pct),
        el('label.field', null, 'Escucha a', followSel),
        listenNote,
        listens ? el('div.row', null, relisten) : null,
        el('div.row', null,
          el('button.btn', { onclick: () => G.run('varyDrummer', { trackId: track.id, unlock: true }) }, 'Generar todo de nuevo'))));

    const kit = el('div.kit');
    const part = (title, key, levelKey, hint) => {
      const steps = el('div.steps');
      const paint = () => G.$$('button', steps).forEach((b, i) => b.classList.toggle('on', d[key] && i < (d[levelKey] || 0)));
      for (let i = 0; i < 8; i++) steps.appendChild(el('button', { 'aria-label': `${title}: nivel ${i + 1}`, onclick: () => { first = true; d[key] = true; d[levelKey] = i + 1; paint(); sw.classList.add('on'); push(); } }));
      const sw = el('button.switch' + (d[key] ? '.on' : ''), { 'aria-label': title, 'aria-pressed': String(!!d[key]), onclick: () => { first = true; d[key] = !d[key]; sw.classList.toggle('on', d[key]); paint(); push(); } });
      paint();
      return el('div.kitrow', null, el('div.top', null, title, sw), levelKey ? steps : null, el('span.note', { text: hint }));
    };
    kit.append(
      part('Bombo y caja', 'kickSnare', 'kickSnareLevel', 'Cuántos golpes extra mete'),
      part('Platillos y hi-hat', 'cymbals', 'cymbalLevel', '1–2 negras · 3–4 corcheas · 5+ semicorcheas'),
      part('Toms', 'toms', null, 'Se usan en los redobles'),
      part('Percusión', 'percussion', 'percussionLevel', 'Pandereta, shaker, güira…'));

    // The sound: the recorded acoustic kit sounds real; the built-in one is synthesized
    const real = (G.lib.sounds || []).find((x) => x.id === 'virtuosity-drums');
    const usingReal = !!(track.instrument && /real/i.test(track.instrument.name || ''));
    const dlStatus = el('small', { 'data-dl': 'virtuosity-drums', text: '' });
    let soundAction = null;
    if (usingReal) soundAction = el('span.note', { text: 'Grabada de una batería de verdad, con varios micrófonos.' });
    else if (real && real.installed)
      soundAction = el('button.btn.primary', { onclick: async () => { G.toast('Cargando la batería real… (unos segundos)'); await G.run('useRealDrums', { trackId: track.id }); } }, 'Usar la batería acústica real');
    else if (real)
      soundAction = el('button.btn.primary', { disabled: !!real.downloading, onclick: async (e) => {
        e.target.disabled = true;
        const r = await G.call('downloadSound', { id: real.id });
        if (r && r.ok) { G.pendingRealDrums = track.id; dlStatus.textContent = 'Bajando…'; G.toast('Bajando la batería real. Cuando termine, el baterista la usa sola.'); }
        else { e.target.disabled = false; G.toast((r && r.error) || 'No se pudo bajar', true); }
      } }, 'Bajar la batería acústica real' + (real.size ? ` (${real.size})` : ''));
    const sound = el('div.drum-sound' + (usingReal ? '' : '.synth'), null,
      el('b', { text: 'Sonido' }),
      el('span.what', { text: usingReal ? 'Batería acústica real' : 'Batería sintetizada (suena electrónica)' }),
      soundAction, dlStatus);

    // The song in parts: each region of the drummer is one part
    const parts = el('div.row', { style: { marginBottom: '10px' } },
      el('b', { text: 'Partes' }),
      el('span.note', { text: 'Cada región es una parte: alárgala desde su borde (las siguientes se corren) o genérala de nuevo sin tocar las demás.' }),
      el('button.btn', { onclick: () => G.Timeline.arrangeDrummer(track) }, 'Armar la canción por partes'));

    body.appendChild(sound);
    body.appendChild(parts);
    if (clip && track.kind === 'drummer') {
      const kinds = (G.state.sectionKinds || []);
      const colors = { intro: '#7c8cff', verse: '#4aa3df', prechorus: '#3fbfa0', chorus: '#e6a84a', bridge: '#c77dff', solo: '#ff6b6b', outro: '#8e8e96' };
      body.appendChild(el('div.row', { style: { marginBottom: '10px' } },
        el('span.note', { text: `“${clip.name}” es:` }),
        el('div.part-chips', null, ...kinds.map(([id, name]) => el('button.pill' + (clip.part === id ? '.on' : ''), { style: { '--pc': colors[id] }, onclick: () => G.Timeline.setPart(track, clip, id) }, name))),
        el('button.btn', { onclick: () => G.run('varyDrummer', { trackId: track.id, clipId: clip.id, unlock: true }) }, 'Generar esta parte de nuevo'),
        el('button.pill' + (clip.fill ? '.on' : ''), { onclick: () => G.run('setClip', { trackId: track.id, clipId: clip.id, fill: !clip.fill }) }, 'Redoble al final'),
        clip.locked ? el('span.note', { text: 'Editada a mano: el baterista no la cambia (genérala de nuevo para devolvérsela).' }) : null));
    }
    body.appendChild(el('div.split', null, left, mid, kit));
  };
})();
