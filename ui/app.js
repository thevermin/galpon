/* Galpon UI — app shell: top bar, library, loops, keyboard, dialogs, shortcuts. */
(function () {
  'use strict';
  const G = window.G;
  const { el, $, $$ } = G;
  const A = (G.App = {});
  const T = G.Timeline;

  const ui = { library: false, bottom: null, loops: false, keys: false };
  let lastVersion = -1, refreshing = false, pendingRefresh = false, lastPlaying = null;

  // While the user drags a slider/knob/pad, don't rebuild the controls under the mouse.
  G.busy = 0;
  function settle() {
    if (G.busy > 0) { G.busy = 0; if (pendingRefresh) A.refresh(); }
  }
  window.addEventListener('pointerup', () => setTimeout(settle, 0));
  window.addEventListener('pointercancel', () => setTimeout(settle, 0));
  document.addEventListener('pointerdown', (e) => {
    if (e.target.closest('input[type=range], .dial, .xy, .roll canvas, .clip, #ruler, #bottom-handle')) G.busy = 1;
  }, true);

  // ------------------------------------------------------------------ state
  A.refresh = async function () {
    if (refreshing) return;
    if (G.busy) { pendingRefresh = true; return; }
    pendingRefresh = false;
    refreshing = true;
    try {
      const r = await G.call('getState');
      if (r && r.ok) {
        G.state = r.data;
        lastVersion = G.state.version;
        if (!A._crashChecked) { A._crashChecked = true; setTimeout(A.checkCrashReport, 1500); }
        if (G.state.folder !== A._lostCheckedFor) { A._lostCheckedFor = G.state.folder; setTimeout(A.checkLostRecordings, 800); }
        if (!G.state.tracks.find((t) => t.id === G.sel.trackId)) G.sel = { trackId: G.state.selected || 0, clipId: 0 };
        if (G.sel.trackId === 0 && G.state.tracks.length) G.sel.trackId = G.state.tracks[0].id;
        if (G.sel.clipId && !T.clip(G.sel.trackId, G.sel.clipId)) G.sel.clipId = 0;
        if (G.sel.trackId !== G.state.selected) G.call('selectTrack', { id: G.sel.trackId });   // keyboard/MIDI go to this track
        renderAll();
      }
    } finally { refreshing = false; }
  };

  A.refreshLibrary = async function () {
    const r = await G.call('getLibrary');
    if (r && r.ok) G.lib = r.data;
  };

  A.refreshAudio = async function () {
    const r = await G.call('getAudioSetup');
    if (r && r.ok) { G.audio = r.data; A.checkSplitDevices(); }
  };

  /** Recording with one device and listening through another (e.g. an audio interface in, the computer's speakers out)
      means two clocks that drift apart: that's heard as crackles and static. Offer the fix once. */
  A.checkSplitDevices = function () {
    const a = G.audio;
    if (!a || !a.inputDevice || !a.outputDevice || a.inputDevice === a.outputDevice) return;
    const key = a.inputDevice + '>' + a.outputDevice;
    if (A._splitWarned === key) return;
    A._splitWarned = key;
    const canUse = (a.outputDevices || []).includes(a.inputDevice);
    G.modal('Entrada y salida en aparatos distintos', (body, close) => {
      body.append(
        el('p.note', { text: `Grabas con ${a.inputDevice} y escuchas por ${a.outputDevice}. Cada aparato tiene su propio reloj y se van desfasando: eso se escucha como chasquidos o estática, sobre todo con amplificadores y plugins.` }),
        el('p.note', { text: canUse ? `Conecta los auriculares o monitores en ${a.inputDevice} y úsalo también para escuchar.` : 'Usa el mismo aparato para entrar y salir.' }),
        el('div.row.end', null,
          el('button.btn', { onclick: close }, 'Dejar así'),
          canUse ? el('button.btn.primary', { onclick: async () => {
            close();
            const r = await G.call('setAudioSetup', { outputDevice: a.inputDevice });
            if (r && r.ok) { G.audio = r.data; G.toast('Escuchando por ' + a.inputDevice); }
            else G.toast((r && r.error) || 'No se pudo cambiar la salida', true);
          } }, 'Escuchar por ' + a.inputDevice) : null));
    });
  };

  let lastLibraryKey = '';
  function renderAll() {
    renderTopbar();
    T.render();
    // The library only depends on the selected track and its sound; the loops never change with the song
    const t = currentTrack();
    const libKey = ui.library + ':' + (t ? t.id + ':' + t.kind + ':' + (t.instrument ? t.instrument.name : '') : '') + ':' + (G.lib ? G.lib.plugins.length : 0);
    if (libKey !== lastLibraryKey) { lastLibraryKey = libKey; renderLibrary(); }
    renderBottom();
    if (!$('#loops').childElementCount || $('#loops').classList.contains('hidden') === ui.loops) renderLoops();
  }

  function currentTrack() { return T.track(G.sel.trackId); }

  // ------------------------------------------------------------------ top bar
  const KEYS = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const KEY_ES = ['Do', 'Do#', 'Re', 'Re#', 'Mi', 'Fa', 'Fa#', 'Sol', 'Sol#', 'La', 'La#', 'Si'];

  function renderTopbar() {
    const s = G.state.song;
    $('#lcd-bpm b').textContent = Math.round(s.bpm * 10) / 10;
    $('#lcd-sig b').textContent = `${s.sigNum}/${s.sigDen}`;
    $('#lcd-key b').textContent = KEY_ES[Math.max(0, KEYS.indexOf(s.key))] + (s.minor ? ' men' : ' may');
    $('#btn-cycle').classList.toggle('on', s.cycle);
    $('#btn-metro').classList.toggle('on', s.metronome);
    $('#btn-countin').classList.toggle('on', s.countIn);
    $('#master-vol').value = s.masterDb;
    $('#btn-library').classList.toggle('on', ui.library);
    $('#btn-loops').classList.toggle('on', ui.loops);
    $('#btn-keys').classList.toggle('on', ui.keys);
    $('#btn-controls').classList.toggle('on', ui.bottom === 'controls' || ui.bottom === 'master');
    $('#btn-editor').classList.toggle('on', ui.bottom === 'editor' || ui.bottom === 'drummer');
    $('#btn-mixer').classList.toggle('on', ui.bottom === 'mixer');
    $('#save-state').textContent = G.state.dirty ? 'Sin guardar' : 'Guardado';
    document.title = s.name + ' — Galpon';
  }

  function onTick(t) {
    G.tick = t;
    const bpb = T.bpb();
    const beat = Math.max(0, t.beat);
    const bar = Math.floor(beat / bpb) + 1, b = Math.floor(beat % bpb) + 1;
    $('#lcd-pos b').textContent = t.countIn ? 'cuenta' : `${bar} . ${b}`;
    $('#lcd').classList.toggle('counting', !!t.countIn);
    if (t.playing !== lastPlaying) {
      // only touch the button when it changes: replacing it 30 times a second ate the clicks
      lastPlaying = t.playing;
      const play = $('#btn-play');
      play.replaceChildren(G.icon(t.playing ? 'stop' : 'play', 18));
      play.classList.toggle('playing', t.playing);
      play.setAttribute('aria-label', t.playing ? 'Detener (Espacio)' : 'Reproducir (Espacio)');
    }
    $('#btn-record').classList.toggle('active', t.recording);
    T.updatePlayhead(t.beat);

    // meters
    const toWidth = (v) => Math.max(0, Math.min(100, (20 * Math.log10(Math.max(v, 1e-5)) + 60) / 66 * 100));
    const mm = $$('#master-meter i');
    if (t.master) { mm[0].style.clipPath = `inset(0 ${100 - toWidth(t.master[0])}% 0 0)`; mm[1].style.clipPath = `inset(0 ${100 - toWidth(t.master[1])}% 0 0)`; }
    (t.meters || []).forEach((m, i) => {
      const bar = $(`[data-meter="${i}"] i`);
      if (bar) bar.style.height = toWidth(Math.max(m[0], m[1])) + '%';
      const mix = $(`[data-mixmeter="${i}"]`);
      if (mix) { mix.children[0].style.clipPath = `inset(${100 - toWidth(m[0])}% 0 0 0)`; mix.children[1].style.clipPath = `inset(${100 - toWidth(m[1])}% 0 0 0)`; }
    });
    const mixMaster = $('[data-mixmeter="m"]');
    if (mixMaster && t.master) { mixMaster.children[0].style.clipPath = `inset(${100 - toWidth(t.master[0])}% 0 0 0)`; mixMaster.children[1].style.clipPath = `inset(${100 - toWidth(t.master[1])}% 0 0 0)`; }
    // input meter (Controles of an audio track), with a peak marker that falls slowly
    (t.inputs || []).forEach((v, i) => {
      const track = G.state && G.state.tracks[i];
      if (!track) return;
      const m = $(`[data-input-meter="${track.id}"]`);
      if (!m) return;
      const w = toWidth(v);
      m._peak = Math.max(w, (m._peak || 0) - 0.6);
      m.firstChild.style.width = w + '%';
      m.lastChild.style.left = m._peak + '%';
      m.title = v >= 0.99 ? '¡Satura! Baja la ganancia' : 'Nivel de entrada: apunta a que los picos queden en la zona amarilla';
    });
    // A clipped input sounds like static whatever comes after it, so say it even with the panel closed
    (t.inputs || []).forEach((v, i) => {
      const track = G.state && G.state.tracks[i];
      if (!track || v < 0.99 || !(track.monitor || track.armed)) return;
      if (Date.now() - (A._clipToast || 0) < 20000) return;
      A._clipToast = Date.now();
      const dev = (G.audio && G.audio.inputDevice) || 'la interfaz';
      G.toast(`La entrada de "${track.name}" satura y eso suena a estática: baja la perilla de ganancia de ${dev}` + (track.inputGainDb > 0 ? ' o la ganancia de entrada en Controles.' : '.'), true);
    });
    if (t.tuner && A.onTuner) A.onTuner(t.tuner);
    if (t.downloads) onDownloads(t.downloads);
    if (t.recLost > 0 && !A._recLostWarned) {
      A._recLostWarned = true;
      G.toast(`Ojo: el disco no da abasto y se perdieron ${t.recLost.toFixed(1)} s de esta toma. Cierra otras apps.`, true);
    }
    if (!t.recording) A._recLostWarned = false;
    const job = $('#job');
    if (t.stems) {
      const s = t.stems;
      const label = s.phase === 1 ? `Bajando el modelo… ${Math.round(s.progress * 100)}%` : s.phase === 2 ? 'Preparando la separación…'
        : s.phase === 3 ? `Separando "${s.name}"… ${Math.round(s.progress * 100)}%` : '';
      job.textContent = label;
      job.classList.toggle('hidden', !label);
      job.onclick = () => G.menu(job.getBoundingClientRect().left, 60, [{ label: 'Cancelar la separación', danger: true, action: () => G.call('cancelStems') }]);
      if (s.phase === 4) G.toast('Listo: la región quedó separada en 4 pistas (voz, batería, bajo y otros).');
      if (s.phase === 5) G.toast('No se pudo separar: ' + (s.error || 'error desconocido'), true);
    } else if (job) job.classList.add('hidden');

    // CPU and dropouts: when the computer can't keep up you hear clicks and crackles
    const cpu = $('#cpu');
    if (cpu && t.cpu !== undefined) {
      cpu.textContent = 'CPU ' + Math.round(t.cpu * 100) + '%';
      cpu.classList.toggle('hot', t.cpu > 0.7);
    }
    if (t.xruns !== undefined) {
      if (A._xruns !== undefined && t.xruns > A._xruns && Date.now() - (A._xrunToast || 0) > 15000) {
        A._xrunToast = Date.now();
        const b = G.audio && G.audio.bufferSize;
        G.toast('El audio se cortó: la computadora no llegó a tiempo' + (b && b < 512 ? '. Prueba subir el búfer a ' + b * 2 + ' en Configuración.' : '.'), true);
      }
      A._xruns = t.xruns;
    }
    if (t.error) G.toast(t.error, true);
    if (t.playing && P_rollVisible()) G.Panels._rollDraw && G.Panels._rollDraw();
    if (t.version !== lastVersion) A.refresh();
  }
  const P_rollVisible = () => ui.bottom === 'editor' && !$('#bottom').classList.contains('hidden');

  function setupSplitter() {
    const handle = $('#bottom-handle'), panel = $('#bottom');
    try { const h = parseInt(localStorage.getItem('galpon.bottomHeight'), 10); if (h) panel.style.height = h + 'px'; } catch (e) { /* storage unavailable */ }
    handle.addEventListener('pointerdown', (e) => {
      handle.setPointerCapture(e.pointerId);
      const y0 = e.clientY, h0 = panel.getBoundingClientRect().height;
      const move = (ev) => { panel.style.height = Math.max(160, Math.min(window.innerHeight * 0.75, h0 + (y0 - ev.clientY))) + 'px'; };
      const up = () => {
        handle.removeEventListener('pointermove', move); handle.removeEventListener('pointerup', up);
        try { localStorage.setItem('galpon.bottomHeight', parseInt(panel.style.height, 10)); } catch (e) { /* ignore */ }
        if (G.Panels._rollDraw && ui.bottom === 'editor') G.Panels._rollDraw();
      };
      handle.addEventListener('pointermove', move); handle.addEventListener('pointerup', up);
    });
  }

  function setupTopbar() {
    const icons = { 'btn-home': 'home', 'btn-library': 'library', 'btn-controls': 'controls', 'btn-editor': 'editor', 'btn-start': 'start', 'btn-play': 'play',
      'btn-cycle': 'cycle', 'btn-metro': 'metronome', 'btn-countin': 'countin', 'btn-tuner': 'tuner', 'btn-keys': 'keyboard', 'btn-loops': 'loops',
      'btn-export': 'export', 'btn-settings': 'settings', 'btn-add-track': 'plus', 'btn-mixer': 'mixer', 'btn-game': 'game' };
    for (const [id, icon] of Object.entries(icons)) $('#' + id).prepend(G.icon(icon, id === 'btn-play' ? 18 : 16));

    $('#btn-home').onclick = () => A.showHome();
    $('#btn-game').onclick = () => G.Game && G.Game.enter();
    $('#btn-library').onclick = () => { ui.library = !ui.library; renderLibrary(); renderTopbar(); };
    $('#btn-loops').onclick = () => { ui.loops = !ui.loops; renderLoops(); renderTopbar(); };
    $('#btn-keys').onclick = () => { ui.keys = !ui.keys; renderKeys(); renderTopbar(); };
    $('#btn-controls').onclick = () => A.toggleBottom('controls');
    $('#btn-mixer').onclick = () => A.toggleBottom('mixer');
    $('#btn-editor').onclick = () => A.toggleBottom(currentTrack() && currentTrack().kind === 'drummer' && !G.sel.clipId ? 'drummer' : 'editor');
    $('#btn-start').onclick = () => G.call('setPosition', { beat: 0 });
    $('#btn-play').onclick = () => G.call('togglePlay');
    $('#btn-record').onclick = () => A.record();
    $('#btn-cycle').onclick = () => G.run('setSong', { cycle: !G.state.song.cycle });
    $('#btn-metro').onclick = () => G.run('setSong', { metronome: !G.state.song.metronome });
    $('#btn-countin').onclick = () => G.run('setSong', { countIn: !G.state.song.countIn });
    $('#btn-tuner').onclick = () => A.showTuner();
    $('#btn-export').onclick = () => A.exportSong();
    $('#btn-settings').onclick = () => A.openSettings('audio');
    $('#btn-add-track').onclick = () => A.newTrackDialog();
    $('#btn-empty-add').onclick = () => A.newTrackDialog();
    $('#master-vol').addEventListener('input', G.throttle((e) => { G.state.song.masterDb = +e.target.value; G.call('setSong', { masterDb: G.state.song.masterDb }); }, 40));
    $('#master-vol').addEventListener('dblclick', (e) => { e.target.value = 0; G.call('setSong', { masterDb: 0 }); });

    $('#lcd-bpm').onclick = async () => {
      const v = await G.prompt('Tempo (pulsos por minuto)', String(G.state.song.bpm));
      if (v && !isNaN(parseFloat(v))) { await G.run('setSong', { bpm: parseFloat(v) }); await A.refresh(); G.Panels.offerFollowTempo(); }
    };
    $('#lcd-sig').onclick = (e) => G.menu(e.clientX, e.clientY, [[4, 4], [3, 4], [2, 4], [6, 8], [5, 4], [7, 8], [12, 8]]
      .map(([n, d]) => ({ label: `${n}/${d}`, action: () => G.run('setSong', { sigNum: n, sigDen: d }) })));
    $('#lcd-key').onclick = (e) => {
      const items = KEYS.map((k, i) => ({ label: KEY_ES[i] + ' mayor', action: () => G.run('setSong', { key: k, minor: false }) }));
      items.push('-');
      KEYS.forEach((k, i) => items.push({ label: KEY_ES[i] + ' menor', action: () => G.run('setSong', { key: k, minor: true }) }));
      G.menu(e.clientX, 60, items);
    };
    $('#lcd-pos').onclick = async () => {
      const v = await G.prompt('Ir al compás', String(Math.floor(Math.max(0, G.tick ? G.tick.beat : 0) / T.bpb()) + 1));
      if (v && !isNaN(parseInt(v, 10))) G.call('setPosition', { beat: (parseInt(v, 10) - 1) * T.bpb() });
    };
  }

  A.record = function () {
    const t = currentTrack();
    if (!G.tick || !G.tick.recording) {
      if (!t) { G.toast('Primero agrega una pista para grabar', true); return; }
      if (t.kind === 'audio' && !(G.audio && G.audio.sources && G.audio.sources.length)) {
        G.toast('No hay entradas de audio activas. Revisa Configuración → Audio.', true);
      }
    }
    G.run('record');      // if it can't record safely (no disk space, missing input) it says why
  };

  /** Recordings in the song folder that aren't on any track (crash, power cut, undone take). */
  A.checkLostRecordings = async function () {
    const r = await G.call('findLostRecordings');
    if (!r || !r.ok || !r.data.length) return;
    const files = r.data;
    G.modal('Encontré grabaciones sueltas', (body, close) => {
      body.append(
        el('p.note', { text: files.length === 1
          ? 'Hay una grabación en la carpeta de esta canción que no está en ninguna pista (puede ser de un cierre inesperado o una toma que deshiciste).'
          : `Hay ${files.length} grabaciones en la carpeta de esta canción que no están en ninguna pista (pueden ser de un cierre inesperado o tomas que deshiciste).` }),
        el('ul.note', null, ...files.slice(0, 8).map((f) => el('li', { text: f }))),
        el('div.row.end', null,
          el('button.btn', { onclick: () => { close(); G.call('recoverRecordings', { files, ignore: true }); } }, 'No las quiero'),
          el('button.btn.primary', { onclick: async () => {
            close();
            const n = await G.run('recoverRecordings', { files });
            if (n) G.toast(n === 1 ? 'Recuperada en una pista nueva al principio de la canción.' : `${n} grabaciones recuperadas en pistas nuevas.`);
          } }, files.length === 1 ? 'Recuperarla' : 'Recuperarlas')));
    });
  };

  /** If Galpon closed by itself last time, it left a note: show it once, so it can be sent and fixed. */
  A.checkCrashReport = async function () {
    const r = await G.call('crashReport');
    if (!r || !r.ok || !r.data || !r.data.text) return;
    G.modal('Galpon se cerró la última vez', (body, close) => {
      body.append(
        el('p.note', { text: 'Perdón por eso. Guardé un informe de dónde estaba. Si me lo mandas, se puede arreglar. Tus grabaciones se guardan solas mientras grabas.' }),
        el('pre.g-crash', { text: r.data.text.slice(0, 4000) }),
        el('div.row.end', null,
          el('button.btn', { onclick: () => { navigator.clipboard && navigator.clipboard.writeText(r.data.text); G.toast('Copiado'); } }, 'Copiar el informe'),
          el('button.btn', { onclick: () => G.call('revealFile', { path: r.data.path }) }, 'Mostrar el archivo'),
          el('button.btn.primary', { onclick: close }, 'Seguir')));
    });
  };

  // ------------------------------------------------------------------ bottom panel
  A.toggleBottom = function (which) {
    ui.bottom = ui.bottom === which ? null : which;
    renderBottom(); renderTopbar();
  };
  A.showBottom = function (which) { ui.bottom = which; renderBottom(); renderTopbar(); };

  function renderBottom() {
    const panel = $('#bottom'), tabs = $('#bottom-tabs'), body = $('#bottom-body');
    const t = currentTrack();
    if (!ui.bottom || (!t && ui.bottom !== 'master' && ui.bottom !== 'mixer')) { panel.classList.add('hidden'); return; }
    panel.classList.remove('hidden');
    tabs.innerHTML = '';
    const clip = t && G.sel.clipId ? T.clip(t.id, G.sel.clipId) : null;
    const tab = (id, label) => el('button.pill' + (ui.bottom === id ? '.on' : ''), { onclick: () => A.showBottom(id) }, label);

    tabs.append(el('span.label', { text: ui.bottom === 'mixer' ? 'Mezclador' : ui.bottom === 'master' || !t ? 'Master' : t.name }));
    if (t) tabs.append(tab('controls', 'Controles'));
    if (t && t.kind === 'drummer') tabs.append(tab('drummer', 'Baterista'));
    if (t && t.kind !== 'bus') tabs.append(tab('editor', 'Editor'));
    tabs.append(tab('master', 'Master'), tab('mixer', 'Mezclador'));

    panel.classList.toggle('mixing', ui.bottom === 'mixer');
    if (ui.bottom === 'mixer') { G.Panels.mixer(body); return; }
    if (ui.bottom === 'editor' && t.kind === 'bus') ui.bottom = 'controls';
    if (ui.bottom === 'controls') G.Panels.controls(body, t);
    else if (ui.bottom === 'master') G.Panels.controls(body, null);
    else if (ui.bottom === 'drummer' && t.kind === 'drummer') G.Panels.drummer(body, t, clip);
    else {
      // Nothing selected: edit the region under the playhead, or the first one
      let target = clip;
      if (!target && t.clips.length) {
        const beat = G.tick ? Math.max(0, G.tick.beat) : 0;
        target = t.clips.find((c) => beat >= c.start && beat < c.start + c.length) || t.clips[0];
        G.sel.clipId = target.id;
        $$('.clip').forEach((n) => n.classList.toggle('sel', +n.dataset.clip === target.id));
      }
      G.Panels.editor(body, t, target || null);
    }
  }

  A.onSelectionChanged = function (trackChanged) {
    if (trackChanged) { G.Panels._slot = undefined; renderLibrary(); }
    const t = currentTrack();
    if (ui.bottom === 'drummer' && t && t.kind !== 'drummer') ui.bottom = 'controls';
    renderBottom(); renderTopbar();
  };

  // ------------------------------------------------------------------ library (left)
  function renderLibrary() {
    const side = $('#library');
    side.classList.toggle('hidden', !ui.library);
    if (!ui.library || !G.lib) return;
    side.innerHTML = '';
    const t = currentTrack();
    if (!t) { side.appendChild(el('p.note', { text: 'Elige una pista para ver sus sonidos.' })); return; }

    if (t.kind === 'audio') {
      side.appendChild(el('div.side-title', { text: 'Sonidos para ' + t.name }));
      G.Panels.AUDIO_PRESETS.forEach((p) => side.appendChild(el('button.lib-item', { onclick: () => G.Panels.applyAudioPreset(t, p) }, p.name)));
      return;
    }

    if (t.kind === 'bus') { side.appendChild(el('p.note', { text: 'Los buses no tienen instrumento: agrégales efectos en Controles.' })); return; }

    const current = t.instrument ? t.instrument.name : '';
    renderRealSounds(side, t, current);

    const groups = {};
    G.lib.presets.forEach((p) => { (groups[p.group] = groups[p.group] || []).push(p); });
    for (const [group, list] of Object.entries(groups)) {
      side.appendChild(el('div.side-title', { text: group }));
      list.forEach((p) => side.appendChild(el('button.lib-item' + (p.name === current ? '.on' : ''), {
        onclick: () => G.run('setInstrument', { trackId: t.id, kind: 'builtin', id: p.type, preset: p.name, rename: t.kind === 'instrument' }),
      }, p.name)));
    }
    const instruments = (G.lib.plugins || []).filter((p) => p.instrument);
    side.appendChild(el('div.side-title', { text: 'Plugins (AU / VST3)' }));
    instruments.forEach((p) => side.appendChild(el('button.lib-item' + (p.name === current ? '.on' : ''), {
      onclick: () => G.run('setInstrument', { trackId: t.id, kind: 'plugin', id: p.id }).then(() => G.toast('Abriendo ' + p.name + '…')),
    }, p.name, el('small', { text: p.format }))));
    side.appendChild(el('button.lib-item.dashed', { onclick: () => A.openSettings('plugins') }, instruments.length ? 'Buscar más plugins…' : 'Buscar plugins instalados…'));
  }

  /** Real recorded instruments: download once, then use on any track. */
  function renderRealSounds(side, t, current) {
    const sounds = (G.lib.sounds || []).filter((s) => t.kind !== 'drummer' || s.drums);
    side.appendChild(el('div.side-title', { text: 'Instrumentos grabados' }));
    sounds.forEach((s) => {
      const status = el('small', { 'data-dl': s.id, text: s.installed ? s.licence : (s.downloading ? 'Bajando…' : s.licence + (s.size ? ' · ' + s.size : '')) });
      const action = s.installed
        ? el('button.pill.tiny' + (current === s.name ? '.on' : ''), { onclick: () => useSound(s, t) }, current === s.name ? 'En uso' : 'Usar')
        : el('button.pill.tiny', { disabled: s.downloading, onclick: async (e) => {
            e.target.disabled = true;
            const r = await G.call('downloadSound', { id: s.id });
            if (r && r.ok) { status.textContent = 'Bajando…'; G.toast(`Bajando ${s.name}. Puede tardar unos minutos: sigue trabajando tranquilo.`); }
            else { e.target.disabled = false; G.toast((r && r.error) || 'No se pudo bajar', true); }
          } }, 'Bajar');
      side.appendChild(el('div.lib-item.real', { title: `${s.description}\nDe ${s.author}. Licencia: ${s.licence}.` },
        el('span', null, el('b', { text: s.name, style: { display: 'block', fontSize: '12px' } }), status), action));
    });
    side.appendChild(el('button.lib-item.dashed', { onclick: async () => {
      const r = await G.call('choose', { mode: 'open', title: 'Elige un instrumento .sfz o un sonido', patterns: '*.sfz;*.wav;*.aif;*.aiff;*.flac;*.ogg' });
      if (r && r.ok && r.path) useSound({ file: r.path }, t);
    } }, 'Cargar un .sfz o sonido propio…'));
  }

  async function useSound(s, t) {
    const id = await G.run('useSound', s.id ? { id: s.id, trackId: t.id } : { file: s.file, trackId: t.id });
    if (id) { G.toast('Cargando muestras… (la primera vez tarda unos segundos)'); await A.refresh(); T.select(id, 0); }
  }

  /** Download progress, from the tick. */
  function onDownloads(list) {
    list.forEach((d) => {
      const n = $(`[data-dl="${d.id}"]`);
      if (d.phase === 3) { G.toast(`${d.name}: ${d.error || 'falló la descarga'}`, true); A.refreshLibrary().then(renderLibrary); return; }
      if (d.phase === 2) { G.toast(`Listo: ${d.name} está instalado. Toca "Usar".`); A.refreshLibrary().then(() => { lastLibraryKey = ''; renderLibrary(); }); return; }
      if (n) n.textContent = d.phase === 1 ? 'Instalando…' : d.progress < 0 ? `Bajando… ${Math.round(-d.progress)} MB` : `Bajando… ${Math.round(d.progress / 0.9 * 100)}%`;
    });
  }

  // ------------------------------------------------------------------ loops (right)
  let loopFilter = 'Todos';
  function renderLoops() {
    const side = $('#loops');
    side.classList.toggle('hidden', !ui.loops);
    if (!ui.loops || !G.lib) return;
    side.innerHTML = '';
    side.appendChild(el('div.side-title', { text: 'Loops' }));
    const cats = ['Todos', ...new Set(G.lib.loops.map((l) => l.category))];
    side.appendChild(el('div.row', null, ...cats.map((c) => el('button.pill' + (c === loopFilter ? '.on' : ''), { onclick: () => { loopFilter = c; renderLoops(); } }, c))));
    G.lib.loops.filter((l) => loopFilter === 'Todos' || l.category === loopFilter).forEach((l) => {
      const item = el('div.lib-item', { draggable: 'true', style: { background: '#232326', cursor: 'grab' }, title: 'Arrástralo a una pista, o haz clic en + para agregarlo en el cursor' },
        el('span', null, el('b', { text: l.name, style: { display: 'block', fontSize: '12px' } }), el('small', { text: `${l.category} · ${l.length / T.bpb()} compases · sigue el tono` })),
        el('button.icon-btn', { 'aria-label': 'Agregar en el cursor', onclick: () => {
          const beat = Math.floor((G.tick ? Math.max(0, G.tick.beat) : 0) / T.bpb()) * T.bpb();
          G.run('addLoop', { loopId: l.id, beat });
        } }, G.icon('plus', 14)));
      item.addEventListener('dragstart', (e) => { e.dataTransfer.setData('text/galpon-loop', l.id); e.dataTransfer.effectAllowed = 'copy'; });
      side.appendChild(item);
    });
    side.appendChild(el('p.note', { text: 'Estos loops son MIDI propios de Galpon: siguen el tempo y el tono de la canción, y puedes cambiarles el instrumento.' }));
  }

  // ------------------------------------------------------------------ musical typing / keyboard panel
  const TYPE_MAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12, o: 13, l: 14, p: 15, 'ñ': 16, ';': 16, "'": 17 };
  let octave = 4, velocity = 0.8;
  const down = new Map();

  function playNote(p, on) {
    if (on) { if (down.has(p)) return; down.set(p, true); G.call('noteOn', { pitch: p, velocity }); }
    else { if (!down.has(p)) return; down.delete(p); G.call('noteOff', { pitch: p }); }
    $$(`[data-pitch="${p}"]`).forEach((k) => k.classList.toggle('down', on));
  }

  function chordFor(degree) {
    const s = G.state.song;
    const root = 48 + Math.max(0, KEYS.indexOf(s.key));
    const scale = s.minor ? [0, 2, 3, 5, 7, 8, 10] : [0, 2, 4, 5, 7, 9, 11];
    const at = (d) => root + Math.floor(d / 7) * 12 + scale[d % 7];
    return [at(degree), at(degree + 2), at(degree + 4)];
  }

  function renderKeys() {
    const panel = $('#keys-panel');
    panel.classList.toggle('hidden', !ui.keys);
    if (!ui.keys || !G.state) return;
    panel.innerHTML = '';
    const s = G.state.song;
    const romans = s.minor ? ['i', 'ii°', 'III', 'iv', 'v', 'VI', 'VII'] : ['I', 'ii', 'iii', 'IV', 'V', 'vi', 'vii°'];
    const chords = el('div.chords');
    romans.forEach((r, i) => {
      const notes = chordFor(i);
      const name = KEY_ES[notes[0] % 12] + (r === r.toLowerCase() ? (r.includes('°') ? '°' : 'm') : '');
      const b = el('button', { text: name, title: r });
      b.addEventListener('pointerdown', () => { b.classList.add('down'); notes.forEach((n) => playNote(n, true)); });
      const off = () => { b.classList.remove('down'); notes.forEach((n) => playNote(n, false)); };
      b.addEventListener('pointerup', off); b.addEventListener('pointerleave', off);
      chords.appendChild(b);
    });

    const keys = el('div.pkeys');
    const base = octave * 12;
    const whites = [], letters = Object.entries(TYPE_MAP).reduce((m, [k, v]) => { if (!(v in m)) m[v] = k.toUpperCase(); return m; }, {});
    for (let i = 0; i < 24; i++) if (![1, 3, 6, 8, 10].includes(i % 12)) whites.push(i);
    const W = 100 / whites.length;
    whites.forEach((i) => {
      const p = base + i;
      const k = el('button.w', { 'data-pitch': p, 'aria-label': G.noteName(p) }, i % 12 === 0 ? G.noteName(p) : '', letters[i] ? el('kbd', { text: letters[i] }) : null);
      keys.appendChild(k);
    });
    for (let i = 0; i < 24; i++) {
      if (![1, 3, 6, 8, 10].includes(i % 12)) continue;
      const p = base + i;
      const before = whites.filter((w) => w < i).length;
      keys.appendChild(el('button.b', { 'data-pitch': p, 'aria-label': G.noteName(p), style: { left: `calc(${before * W}% - ${W * 0.3}%)`, width: W * 0.6 + '%' } }, letters[i] || ''));
    }
    keys.addEventListener('pointerdown', (e) => {
      const k = e.target.closest('[data-pitch]');
      if (!k) return;
      const p = +k.dataset.pitch;
      playNote(p, true);
      const up = () => { playNote(p, false); window.removeEventListener('pointerup', up); };
      window.addEventListener('pointerup', up);
    });

    panel.append(
      el('div.row', null,
        el('b', { text: 'Teclado' }),
        el('span.note', { text: `Octava ${octave - 1} (Z/X) · Fuerza ${Math.round(velocity * 127)} (C/V) · Acordes de ${KEY_ES[Math.max(0, KEYS.indexOf(s.key))]} ${s.minor ? 'menor' : 'mayor'}` }),
        el('button.pill', { onclick: () => { octave = Math.max(1, octave - 1); renderKeys(); } }, '− Octava'),
        el('button.pill', { onclick: () => { octave = Math.min(8, octave + 1); renderKeys(); } }, '+ Octava')),
      chords, keys);
  }

  // ------------------------------------------------------------------ dialogs
  A.newTrackDialog = function () {
    const kinds = [
      { kind: 'drummer', tag: 'IA', color: '#E6A84A', name: 'Baterista IA', desc: 'Un baterista que toca solo y se adapta a tu tema' },
      { kind: 'instrument', preset: 'Piano de cola', tag: 'TEC', color: '#4ADE80', name: 'Teclado', desc: 'Pianos, órganos y sintes. Con mouse, teclado o MIDI' },
      { kind: 'audio', tag: 'MIC', color: '#F472B6', name: 'Grabar voz o instrumento', desc: 'Micrófono o interfaz de audio' },
      { kind: 'audio', guitar: true, tag: 'AMP', color: '#C084FC', name: 'Guitarra o bajo', desc: 'Conecta y elige un amplificador (o una captura NAM)' },
      { kind: 'instrument', preset: 'Batería acústica', tag: 'PAD', color: '#60A5FA', name: 'Batería para tocar', desc: 'Toca los ritmos tú mismo' },
      { kind: 'bus', preset: 'reverb', tag: 'FX', color: '#94A3B8', name: 'Bus de efectos', desc: 'Una reverb o un delay compartidos: le mandas sonido desde varias pistas' },
      { kind: 'import', tag: '↓', color: '#A1A1A8', name: 'Importar', desc: 'Audio (WAV, AIFF, FLAC, MP3, M4A) o MIDI' },
    ];
    G.modal('¿Qué quieres agregar?', (body, close) => {
      const cards = el('div.cards');
      kinds.forEach((k) => cards.appendChild(el('button.card', {
        onclick: async () => {
          close();
          if (k.kind === 'import') return A.importAny();
          const id = await G.run('addTrack', { kind: k.kind, preset: k.preset || '' });
          if (!id) return;
          await A.refresh();
          T.select(id, 0);
          if (k.guitar) {
            const t = T.track(id);
            await G.run('setTrack', { trackId: id, name: 'Guitarra', monitor: true });
            await G.Panels.applyAudioPreset(t, G.Panels.AUDIO_PRESETS[3]);
            A.showBottom('controls');
          } else if (k.kind === 'drummer') A.showBottom('drummer');
          else if (k.kind === 'bus') { G.toast('Ahora mándale sonido: en Controles de cada pista, sección Envíos.'); A.showBottom('mixer'); }
          else if (k.kind === 'instrument') { ui.library = true; renderLibrary(); renderTopbar(); }
        },
      }, el('span.chip', { text: k.tag, style: { background: k.color } }), el('span.stack', { style: { gap: '2px' } }, el('b', { text: k.name }), el('small', { text: k.desc })))));
      body.appendChild(cards);
    }, { wide: true });
  };

  A.importAny = async function () {
    const r = await G.call('choose', { mode: 'open', title: 'Importar audio o MIDI', patterns: '*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg;*.mid;*.midi' });
    if (!r || !r.ok || !r.path) return;
    if (/\.midi?$/i.test(r.path)) await G.run('importMidi', { path: r.path });
    else await G.run('importAudio', { path: r.path, trackId: 0, beat: Math.max(0, G.tick ? G.tick.beat : 0) });
  };

  A.importAudio = async function (trackId) {
    const r = await G.call('choose', { mode: 'open', title: 'Importar audio', patterns: '*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg' });
    if (r && r.ok && r.path) G.run('importAudio', { path: r.path, trackId: trackId || 0, beat: Math.max(0, G.tick ? G.tick.beat : 0) });
  };

  const EXPORT_FORMATS = [
    { ext: 'mp3', name: 'MP3', desc: 'Para mandar por WhatsApp o subir. Archivo chico (256 kbps).' },
    { ext: 'm4a', name: 'M4A (AAC)', desc: 'Como MP3 pero suena un poco mejor. Ideal para Apple.' },
    { ext: 'wav', name: 'WAV', desc: 'Calidad completa, sin compresión. Para mezclar o masterizar.' },
    { ext: 'aiff', name: 'AIFF', desc: 'Calidad completa (formato de Apple).' },
    { ext: 'flac', name: 'FLAC', desc: 'Calidad completa y la mitad de tamaño que WAV.' },
  ];

  A.exportSong = async function () {
    if (!G.state.song.length) { G.toast('La canción está vacía: graba o agrega algo primero', true); return; }
    const format = await new Promise((resolve) => {
      let chosen = null;
      G.modal('Exportar canción', (body, close) => {
        const cards = el('div.cards');
        EXPORT_FORMATS.forEach((f) => cards.appendChild(el('button.card', { onclick: () => { chosen = f; close(); } },
          el('span.chip', { text: f.ext.toUpperCase(), style: { background: 'var(--accent)' } }),
          el('span.stack', { style: { gap: '2px' } }, el('b', { text: f.name }), el('small', { text: f.desc })))));
        body.append(cards, el('p.note', { text: 'Se exporta la canción completa, con un poco de silencio al final para que terminen los ecos.' }));
      }, { wide: true, onClose: () => resolve(chosen) });
    });
    if (!format) return;
    const r = await G.call('choose', { mode: 'save', title: 'Exportar como ' + format.name, patterns: '*.' + format.ext, name: G.state.song.name + '.' + format.ext });
    if (!r || !r.ok || !r.path) return;
    let path = r.path;
    if (!path.toLowerCase().endsWith('.' + format.ext)) path += '.' + format.ext;
    G.toast('Exportando…');
    const res = await G.call('export', { path, bitDepth: 24 });
    if (res && res.ok) G.toast('Listo: ' + path.split(/[\\/]/).pop());
    else G.toast((res && res.error) || 'No se pudo exportar', true);
  };

  A.save = async function (as) {
    if (as) {
      const r = await G.call('choose', { mode: 'saveSong', title: 'Guardar canción como', name: G.state.song.name });
      if (!r || !r.ok || !r.path) return;
      const name = r.path.split(/[\\/]/).pop().replace(/\.galpon$/, '');
      await G.call('setSong', { name });
      const res = await G.run('saveSong', { path: r.path });
      if (res !== undefined) G.toast('Guardada');
      return;
    }
    const res = await G.call('saveSong');
    if (res && res.ok) G.toast('Guardada'); else G.toast((res && res.error) || 'No se pudo guardar', true);
  };

  A.openSong = async function () {
    const r = await G.call('choose', { mode: 'song', title: 'Abrir canción (carpeta .galpon)', patterns: '*.galpon;project.json' });
    if (r && r.ok && r.path) { const ok = await G.run('loadSong', { path: r.path }); if (ok !== undefined) { T.forgetWaveforms(); A.refresh(); } }
  };

  A.showHome = async function () {
    const r = await G.call('recentSongs');
    const songs = (r && r.ok) ? r.data : [];
    const colors = ['#E6A84A', '#60A5FA', '#4ADE80', '#F472B6', '#C084FC'];
    G.modal('Mis canciones', (body, close) => {
      body.append(el('div.row', null,
        el('button.btn.primary', { onclick: async () => {
          close();
          const name = await G.prompt('Nombre de la canción nueva', 'Canción nueva');
          if (name === null) return;
          await G.run('newSong', { name });
          T.forgetWaveforms();
          await A.refresh();
          A.newTrackDialog();
        } }, '+ Canción nueva'),
        el('button.btn', { onclick: () => { close(); A.openSong(); } }, 'Abrir otra…'),
        el('button.btn', { onclick: close }, 'Seguir con la actual')));
      const grid = el('div.home-songs');
      songs.forEach((s, i) => grid.appendChild(el('button.song-card', { style: { '--c': colors[i % colors.length] },
        onclick: async () => { close(); const ok = await G.run('loadSong', { path: s.path }); if (ok !== undefined) { T.forgetWaveforms(); A.refresh(); } } },
        el('span.cover'), el('span.meta', null, el('b', { text: s.name }), el('small', { text: new Date(s.modified).toLocaleString('es-AR', { dateStyle: 'medium', timeStyle: 'short' }) })))));
      if (!songs.length) grid.appendChild(el('p.note', { text: 'Todavía no hay canciones guardadas. Se guardan solas en tu carpeta Música › Galpon.' }));
      body.append(grid);
    }, { wide: true });
  };

  /** A new audio interface was plugged in: offer to use it (like GarageBand does). */
  A.offerDevice = function (d) {
    G.modal('Se conectó ' + d.name, (body, close) => {
      body.append(
        el('p.note', { text: `¿Quieres usar ${d.name} para grabar` + (d.hasOutput ? ' y escuchar?' : '?') + ' Vas a poder enchufar tu guitarra o micrófono y oírte por sus auriculares.' }),
        el('div.row.end', null,
          el('button.btn', { onclick: close }, 'Ahora no'),
          el('button.btn.primary', { onclick: async () => {
            close();
            const args = { inputDevice: d.name };
            if (d.hasOutput) args.outputDevice = d.name;
            const r = await G.call('setAudioSetup', args);
            if (r && r.ok) { G.audio = r.data; G.toast('Usando ' + d.name); A.refresh(); }
            else G.toast((r && r.error) || 'No se pudo usar ' + d.name, true);
          } }, 'Usar ' + d.name)));
    });
  };

  A.showTuner = function () {
    G.call('setTuner', { on: true });
    let big, detail, needle;
    G.modal('Afinador', (body) => {
      big = el('div.big', { text: '—' });
      detail = el('span.note', { text: 'Toca una cuerda. Usa la entrada de la pista de audio seleccionada.' });
      needle = el('i', { style: { left: '50%' } });
      body.append(el('div.tuner', null, big, el('div.scale', null, needle), detail));
    }, { onClose: () => { G.call('setTuner', { on: false }); A.onTuner = null; } });
    const t = currentTrack();
    if (!t || t.kind !== 'audio') detail.textContent = 'Elige una pista de audio (la de la guitarra o el bajo) para afinar.';
    A.onTuner = (r) => {
      if (!r.active) { big.style.color = ''; return; }
      big.textContent = r.note + r.octave;
      const c = Math.max(-50, Math.min(50, r.cents));
      needle.style.left = 50 + c + '%';
      big.style.color = Math.abs(c) < 4 ? '#4ade80' : '';
      detail.textContent = `${r.freq.toFixed(1)} Hz · ${c > 0 ? '+' : ''}${c.toFixed(0)} cents`;
    };
  };

  A.openSettings = async function (tab) {
    await A.refreshAudio();
    await A.refreshLibrary();
    let current = tab || 'audio';
    const dlg = G.modal('Configuración', (body) => {
      const draw = () => {
        body.innerHTML = '';
        const tabs = el('div.tabs', null, ...[['audio', 'Audio'], ['midi', 'MIDI'], ['rec', 'Grabación'], ['plugins', 'Plugins']]
          .map(([id, label]) => el('button.pill' + (current === id ? '.on' : ''), { onclick: () => { current = id; draw(); } }, label)));
        body.appendChild(tabs);
        const a = G.audio;
        if (current === 'audio') {
          const change = async (args) => {
            const r = await G.call('setAudioSetup', args);
            if (r && r.ok) { G.audio = r.data; G.toast('Audio actualizado'); }
            else G.toast((r && r.error) || 'No se pudo cambiar', true);
            draw(); A.refresh();
          };
          const select = (label, list, value, key, fmt) => el('label.field', null, label,
            el('select.sel', { onchange: (e) => {
              const v = typeof value === 'number' ? +e.target.value : e.target.value;
              const args = { [key]: v };
              // An interface usually is both: choosing it as input also makes it the output (headphones on the interface)
              if (key === 'inputDevice' && (a.outputDevices || []).includes(v) && a.outputDevice !== v) {
                args.outputDevice = v;
                G.toast('También uso ' + v + ' para escuchar (cámbialo en Salida si quieres)');
              }
              change(args);
            } },
              ...(list || []).map((v) => el('option', { value: v, text: fmt ? fmt(v) : v, selected: String(v) === String(value) }))));
          body.append(
            el('div.settings-grid', null,
              select('Sistema de audio', a.types, a.type, 'type'),
              el('div'),
              select('Entrada: ¿por dónde entra el sonido? (elige tu interfaz acá)', a.inputDevices, a.inputDevice, 'inputDevice'),
              select('Salida (parlantes / auriculares)', a.outputDevices, a.outputDevice, 'outputDevice'),
              select('Frecuencia de muestreo', a.sampleRates, a.sampleRate, 'sampleRate', (v) => (v / 1000) + ' kHz'),
              select('Tamaño del búfer (menos = menos demora)', a.bufferSizes, a.bufferSize, 'bufferSize', (v) => `${v} muestras (${(v / (a.sampleRate || 48000) * 1000).toFixed(1)} ms)`)),
            a.deviceOpen ? el('div.info', null,
              el('span', null, 'Demora de entrada ', el('b', { text: a.inputLatencyMs.toFixed(1) + ' ms' })),
              el('span', null, 'Demora de salida ', el('b', { text: a.outputLatencyMs.toFixed(1) + ' ms' })),
              el('span', null, 'Ida y vuelta aprox. ', el('b', { text: a.roundTripMs.toFixed(1) + ' ms' })),
              el('span', null, 'Compensación al grabar ', el('b', { text: a.compensationMs.toFixed(1) + ' ms' })))
              : el('p.note', { text: 'No hay ningún dispositivo de audio abierto.' }),
            el('b', { text: 'Entradas activas' }),
            el('div.chlist', null, ...(a.inputChannels || []).map((name, i) => el('label', null,
              el('input', { type: 'checkbox', checked: !!a.inputActive[i], onchange: (e) => { const list = a.inputActive.slice(); list[i] = e.target.checked; change({ inputActive: list }); } }), name))),
            el('b', { text: 'Salidas activas' }),
            el('div.chlist', null, ...(a.outputChannels || []).map((name, i) => el('label', null,
              el('input', { type: 'checkbox', checked: !!a.outputActive[i], onchange: (e) => { const list = a.outputActive.slice(); list[i] = e.target.checked; change({ outputActive: list }); } }), name))),
            el('p.note', { text: 'Para usar dos interfaces a la vez en Mac, crea un "Dispositivo agregado" en la app Configuración de Audio MIDI y elígelo acá. Para grabar sin demora usa un búfer de 64–128 muestras; si se escuchan cortes, súbelo.' }));
        } else if (current === 'midi') {
          body.append(el('p.note', { text: 'Los teclados y controladores MIDI conectados tocan la pista seleccionada.' }));
          if (!a.midiInputs.length) body.append(el('p.note', { text: 'No hay dispositivos MIDI conectados.' }));
          a.midiInputs.forEach((m) => body.append(el('label', { style: { display: 'flex', gap: '8px', alignItems: 'center' } },
            el('input', { type: 'checkbox', checked: m.enabled, onchange: (e) => G.run('setMidiInput', { id: m.id, enabled: e.target.checked }) }), m.name)));
        } else if (current === 'rec') {
          const off = el('input.num', { type: 'number', step: 0.5, value: a.recordOffsetMs });
          off.addEventListener('change', () => G.run('setPrefs', { recordOffsetMs: +off.value }));
          const met = el('input', { type: 'range', min: 0, max: 1, step: 0.01, value: a.metronomeLevel });
          met.addEventListener('input', () => G.call('setPrefs', { metronomeLevel: +met.value }));
          body.append(
            el('label.field', null, 'Ajuste extra de sincronía al grabar (ms)', off),
            el('p.note', { text: 'Galpon corrige solo la demora que informa tu interfaz. Si las tomas quedan corridas, ajusta este número (positivo = la toma se adelanta).' }),
            el('label.field', null, 'Volumen del metrónomo', met));
        } else {
          const st = G.lib.pluginStatus;
          const list = el('div.plist', null, ...(G.lib.plugins || []).map((p) => el('div', null, el('span', { text: p.name }), el('small', { text: `${p.maker} · ${p.format} · ${p.instrument ? 'instrumento' : 'efecto'}` }))));
          body.append(
            el('div.info', null, el('span', null, 'Formatos: ', el('b', { text: st.formats })), el('span', null, 'Encontrados: ', el('b', { text: String(st.count) })),
              st.scanning ? el('span', null, 'Buscando… ', el('b', { text: Math.round(st.progress * 100) + '% ' + (st.current || '') })) : null),
            el('div.row', null,
              el('button.btn.primary', { disabled: st.scanning, onclick: () => { G.run('scanPlugins', {}); pollScan(); } }, 'Buscar plugins nuevos'),
              el('button.btn', { disabled: st.scanning, onclick: () => { G.run('scanPlugins', { full: true }); pollScan(); } }, 'Volver a buscar todo')),
            el('p.note', { text: 'Si un plugin hace cerrar Galpon mientras se busca, la próxima vez se saltea solo.' }),
            list);
        }
      };
      const pollScan = async () => {
        await A.refreshLibrary();
        if (current === 'plugins') draw();
        if (G.lib.pluginStatus.scanning) setTimeout(pollScan, 400);
      };
      draw();
      if (G.lib.pluginStatus.scanning) pollScan();
    }, { wide: true });
    return dlg;
  };

  // ------------------------------------------------------------------ shortcuts
  function typing(e) { return e.target.closest('input, select, textarea') && e.target.type !== 'range' && e.target.type !== 'checkbox'; }

  function setupKeys() {
    window.addEventListener('keydown', (e) => {
      if ($('.modal-back')) return;
      if (G.Game && G.Game.active) { G.Game.onKey(e); return; }   // the game has its own keys
      if (typing(e)) return;
      const k = e.key.toLowerCase();
      const cmd = e.metaKey || e.ctrlKey;
      if (ui.keys && !cmd && !e.repeat) {
        if (k in TYPE_MAP) { playNote(octave * 12 + TYPE_MAP[k], true); e.preventDefault(); return; }
        if (k === 'z') { octave = Math.max(1, octave - 1); renderKeys(); return; }
        if (k === 'x') { octave = Math.min(8, octave + 1); renderKeys(); return; }
        if (k === 'c') { velocity = Math.max(0.1, velocity - 0.1); renderKeys(); return; }
        if (k === 'v') { velocity = Math.min(1, velocity + 0.1); renderKeys(); return; }
      }
      if (e.code === 'Space') {
        e.preventDefault();
        if (document.activeElement && document.activeElement !== document.body) document.activeElement.blur();
        if (!e.repeat) G.call('togglePlay');
        return;
      }
      if (cmd && k === 'z') { e.preventDefault(); G.run(e.shiftKey ? 'redo' : 'undo'); return; }
      if (cmd && k === 's') { e.preventDefault(); A.save(e.shiftKey); return; }
      if (cmd && k === 'd') { e.preventDefault(); T.duplicate(); return; }
      if (cmd && k === 't') { e.preventDefault(); T.split(); return; }
      if (cmd && k === 'e') { e.preventDefault(); A.exportSong(); return; }
      if (cmd && k === 'k') { e.preventDefault(); $('#btn-keys').click(); return; }
      if (cmd) return;
      if (k === 'backspace' || k === 'delete') { T.deleteSelected(); return; }
      if (k === 'enter' || k === 'home') { G.call('setPosition', { beat: 0 }); return; }
      if (ui.keys) return;   // letters are notes while the keyboard is open
      if (k === 'r') A.record();
      else if (k === 'c') $('#btn-cycle').click();
      else if (k === 'k') $('#btn-metro').click();
      else if (k === 'b') A.toggleBottom('controls');
      else if (k === 'x') A.toggleBottom('mixer');
      else if (k === 'e') $('#btn-editor').click();
      else if (k === 'y') $('#btn-library').click();
      else if (k === 'o') $('#btn-loops').click();
      else if (k === 'arrowup' || k === 'arrowdown') {
        const ts = G.state.tracks, i = ts.findIndex((t) => t.id === G.sel.trackId);
        const next = ts[Math.max(0, Math.min(ts.length - 1, i + (k === 'arrowup' ? -1 : 1)))];
        if (next) { e.preventDefault(); T.select(next.id, 0); }
      }
    });
    window.addEventListener('keyup', (e) => {
      if (e.code === 'Space' && !typing(e)) e.preventDefault();   // stop a focused button from also "clicking"
      const k = e.key.toLowerCase();
      if (k in TYPE_MAP) playNote(octave * 12 + TYPE_MAP[k], false);
    });
    window.addEventListener('blur', () => Array.from(down.keys()).forEach((p) => playNote(p, false)));
  }

  // ------------------------------------------------------------------ boot
  async function boot() {
    setupTopbar();
    setupSplitter();
    T.init();
    setupKeys();
    G.on('tick', onTick);
    G.on('toast', (msg) => G.toast(msg));
    G.on('deviceAdded', (d) => A.offerDevice(d));
    G.on('menu', (action) => {
      const actions = {
        newSong: () => A.showHome(), openSong: () => A.openSong(), saveSong: () => A.save(false), saveSongAs: () => A.save(true),
        importAudio: () => A.importAudio(G.sel.trackId), importMidi: async () => {
          const r = await G.call('choose', { mode: 'open', title: 'Importar MIDI', patterns: '*.mid;*.midi' });
          if (r && r.ok && r.path) G.run('importMidi', { path: r.path });
        },
        export: () => A.exportSong(), undo: () => G.run('undo'), redo: () => G.run('redo'),
        settings: () => A.openSettings('audio'), scanPlugins: () => A.openSettings('plugins'),
        recoverRecordings: async () => {
          const r = await G.call('findLostRecordings');
          if (r && r.ok && r.data.length) A.checkLostRecordings();
          else G.toast('No hay grabaciones sueltas: todas están en alguna pista.');
        },
      };
      actions[action] && actions[action]();
    });
    await Promise.all([A.refreshLibrary(), A.refreshAudio()]);
    await A.refresh();
    if (!G.state.tracks.length) A.showHome();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
