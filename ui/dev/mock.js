/* A fake engine, only used when the interface is opened in a normal browser
   (to work on the design without building the app). No sound. */
(function () {
  'use strict';
  const G = window.G;
  let recStart = 0, version = 1, nextId = 1, playing = false, recording = false, beat = 0, lastT = performance.now(), selected = 0;
  const song = { limiter: true, name: 'Canción de prueba', bpm: 120, sigNum: 4, sigDen: 4, key: 'C', minor: false, cycle: false, cycleStart: 0, cycleEnd: 16, metronome: true, countIn: true, masterDb: 0 };
  const tracks = [];
  const lib = {
    sounds: [
      { id: 'splendid-piano', name: 'Piano de cola real', group: 'Teclados', description: 'Un Steinway grabado nota por nota.', author: 'AKAI', licence: 'Dominio público', size: '~150 MB', drums: false, installed: true, downloading: false },
      { id: 'virtuosity-drums', name: 'Batería acústica real', group: 'Batería', description: 'Batería GM.', author: 'Versilian', licence: 'CC0 (libre)', size: 'grande', drums: true, installed: false, downloading: false },
      { id: 'black-blue-bass', name: 'Bajo eléctrico real', group: 'Bajos', description: 'Bajo.', author: 'Karoryfer', licence: 'Gratis', size: 'grande', drums: false, installed: false, downloading: false },
    ],
    presets: [
      { name: 'Piano de cola', type: 'piano', group: 'Teclados' }, { name: 'Piano eléctrico', type: 'epiano', group: 'Teclados' },
      { name: 'Órgano', type: 'organ', group: 'Teclados' }, { name: 'Lead brillante', type: 'synth', group: 'Sintes' },
      { name: 'Pad nocturno', type: 'synth', group: 'Sintes' }, { name: 'Bajo eléctrico', type: 'synth', group: 'Bajos' },
      { name: 'Batería acústica', type: 'drums', group: 'Batería' }],
    effects: [['eq', 'Ecualizador'], ['comp', 'Compresor'], ['gate', 'Puerta de ruido'], ['drive', 'Distorsión'], ['amp', 'Amplificador'], ['nam', 'Amp NAM (Tone3000)'], ['cab', 'Gabinete (IR)'], ['chorus', 'Chorus'], ['delay', 'Delay'], ['reverb', 'Reverb']].map(([type, name]) => ({ type, name })),
    drummerStyles: [['rock', 'Rock de garage', 'Tomás', 'Directo y fuerte'], ['pop', 'Pop', 'Lucía', 'Prolijo y bailable'], ['funk', 'Funk', 'Nico', 'Mucho groove'], ['cuarteto', 'Cuarteto', 'Ramiro', 'Tunga-tunga cordobés'], ['lofi', 'Lo-fi', 'Sol', 'Relajado'], ['reggaeton', 'Reggaetón', 'Fede', 'Dembow'], ['jazz', 'Jazz suave', 'Ana', 'Escobillas'], ['electronic', 'Electrónica', 'Max', 'Caja de ritmos']].map(([id, name, who, description]) => ({ id, name, who, description })),
    loops: [['drums-funk', 'Groove funky', 'Batería'], ['bass-walk', 'Bajo caminante', 'Bajo'], ['keys-pop', 'Acordes pop', 'Teclados'], ['pad-night', 'Pad nocturno', 'Teclados'], ['synth-arp', 'Arpegio brillante', 'Sintes']].map(([id, name, category]) => ({ id, name, category, length: 8, colour: '#60A5FA' })),
    plugins: [{ id: 'au1', name: 'Neural Amp Modeler', maker: 'Steven Atkinson', format: 'AudioUnit', instrument: false }, { id: 'au2', name: 'Vital', maker: 'Vital Audio', format: 'VST3', instrument: true }],
    pluginStatus: { scanning: false, progress: 1, current: '', count: 2, formats: 'AudioUnit, VST3' },
  };
  const palette = ['#60A5FA', '#4ADE80', '#F472B6', '#C084FC'];
  const drumNotes = (len, seed) => {
    const n = [];
    let s = seed;
    const rnd = () => ((s = (s * 9301 + 49297) % 233280) / 233280);
    for (let b = 0; b < len; b += 0.5) {
      if (b % 2 === 0 || rnd() < 0.2) n.push([b, 0.1, 36, 0.9]);
      if (b % 2 === 1) n.push([b, 0.1, 38, 0.9]);
      n.push([b, 0.1, 42, 0.6]);
    }
    return n;
  };
  const params = {};

  let chords = [];
  let sections = [{ beat: 0, length: 16, kind: 'intro' }, { beat: 16, length: 32, kind: 'verse' }];
  const sectionKinds = [['intro', 'Intro'], ['verse', 'Estrofa'], ['prechorus', 'Pre-estribillo'], ['chorus', 'Estribillo'], ['bridge', 'Puente'], ['solo', 'Solo'], ['outro', 'Final']];
  function track(id) { return tracks.find((t) => t.id === id); }
  function addTrack(kind, preset) {
    const t = { id: nextId++, kind, name: kind === 'drummer' ? 'Baterista IA' : kind === 'audio' ? 'Audio ' + (tracks.length + 1) : kind === 'bus' ? (preset === 'delay' ? 'Delay' : 'Reverb') : preset || 'Piano de cola',
      colour: kind === 'drummer' ? '#E6A84A' : kind === 'bus' ? '#94A3B8' : palette[tracks.length % 4], sends: [], output: 0, volumeDb: 0, pan: 0, mute: false, solo: false, armed: false, monitor: false,
      inputChannel: 0, stereoInput: false, inserts: [], clips: [],
      instrument: kind === 'audio' || kind === 'bus' ? undefined : { kind: 'builtin', name: kind === 'drummer' ? 'Batería acústica' : preset || 'Piano de cola', type: 'piano', bypass: false, missing: false, hasEditor: false },
      drummer: { style: 'rock', complexity: 0.4, loudness: 0.6, fills: 0.35, swing: 0, humanize: 0.5, kickSnare: true, cymbals: true, toms: true, percussion: false, kickSnareLevel: 5, cymbalLevel: 3, percussionLevel: 3, followTrack: 0 } };
    if (kind === 'drummer') t.clips.push({ id: nextId++, name: 'Verso', start: 0, length: 32, midi: true, loopLength: 32, seed: 1, locked: false, fill: true, notes: drumNotes(32, 3) });
    tracks.push(t);
    selected = t.id;
    return t;
  }

  // demo song
  addTrack('drummer');
  const bass = addTrack('instrument', 'Bajo eléctrico');
  bass.clips.push({ id: nextId++, name: 'Bajo', start: 0, length: 16, midi: true, loopLength: 8, seed: 1, locked: false, fill: false, notes: [[0, 0.9, 36, 0.9], [1, 0.4, 36, 0.7], [1.5, 0.4, 43, 0.7], [2, 0.9, 41, 0.8], [3, 0.9, 43, 0.8], [4, 0.9, 45, 0.9], [6, 1.5, 43, 0.8]] });
  const keys = addTrack('instrument', 'Piano eléctrico');
  keys.clips.push({ id: nextId++, name: 'Acordes', start: 8, length: 16, midi: true, loopLength: 16, seed: 1, locked: false, fill: false, notes: [[0, 3.5, 60, 0.7], [0, 3.5, 64, 0.7], [0, 3.5, 67, 0.7], [4, 3.5, 57, 0.7], [4, 3.5, 60, 0.7], [4, 3.5, 64, 0.7]] });
  const voz = addTrack('audio');
  voz.name = 'Voz';
  voz.clips.push({ id: nextId++, name: 'Tomas (3)', start: 16, length: 12, midi: false, audioFile: 'voz.wav', offsetSamples: 0, gainDb: 0, audioMissing: false,
    takes: [{ audioFile: 'voz.wav', offsetSamples: 0 }, { audioFile: 'voz.wav', offsetSamples: 1 }, { audioFile: 'voz.wav', offsetSamples: 2 }], activeTake: 1 });
  voz.inputGainDb = 6;

  function state() {
    let length = 0;
    tracks.forEach((t) => t.clips.forEach((c) => { length = Math.max(length, c.start + c.length); }));
    const copy = JSON.parse(JSON.stringify(tracks));
    const audio = copy.find((t) => t.kind === 'audio');
    copy.forEach((t) => { if (t.kind === 'drummer' && audio && t.drummer.followTrack !== 0) t.listensTo = [audio.id, audio.name, true]; });
    return { song: Object.assign({}, song, { length }), tracks: copy, chords: JSON.parse(JSON.stringify(chords)), sections: JSON.parse(JSON.stringify(sections)), sectionKinds, master: [], selected, folder: '/Users/demo/Music/Galpon/Prueba.galpon', dirty: true, sampleRate: 48000, version };
  }

  const ok = (data) => ({ ok: true, data });
  const changed = (data) => { version++; return ok(data); };

  // ---------------------------------------------------------------- game mode (fake song)
  const gm = { open: false, pos: 0, playing: false, ended: false, speed: 1, loop: null, length: 64,
    stems: [{ id: 'full', name: 'full', enabled: false, full: true }, { id: 'guitar', name: 'Guitar', enabled: true, full: false }, { id: 'drums', name: 'drums', enabled: true, full: false }, { id: 'vocals', name: 'vocals', enabled: true, full: false }, { id: 'click', name: 'Click', enabled: false, full: false }] };
  const fakeSong = (title, artist, bassToo) => ({ path: '/Music/Galpon/Juego/' + title + '.feedpak', title, artist, album: 'Demo', year: 2026, duration: 64,
    arrangements: [{ id: 'lead', name: 'Lead', type: 'lead', tuning: [0, 0, 0, 0, 0, 0], capo: 0, playable: true }].concat(bassToo ? [{ id: 'bass', name: 'Bass', type: 'bass', tuning: [-2, 0, 0, 0], capo: 0, playable: true }] : []),
    stems: ['full', 'guitar', 'drums', 'vocals', 'click'] });
  const gameSongs = [fakeSong('Ruta 9', 'Los del Galpón', true), fakeSong('Siesta eléctrica', 'Banda de prueba', false), fakeSong('Viento norte', 'Example Artist', true),
    { path: '/Music/Galpon/Juego/roto.feedpak', title: 'roto', error: 'Falta manifest.yaml: no parece un .feedpak' }];
  function gameData() {
    const beats = [], notes = [], chords = [], anchors = [], lyrics = [];
    for (let i = 0; i < 64 * 2; i++) beats.push({ time: i * 0.5, measure: i % 4 === 0 ? i / 4 + 1 : -1 });
    const riff = [[0, 0], [0, 3], [1, 2], [1, 0], [2, 2], [1, 3], [0, 5], [0, 3]];
    for (let b = 4; b < 120; b++) {
      const t = b * 0.5;
      if (b >= 64 && b < 80 && b % 4 === 0) { chords.push({ t, id: (b / 4) % 2, notes: [{ s: 0, f: 0 }, { s: 1, f: 2 }, { s: 2, f: 2 }, { s: 3, f: 0 }, { s: 4, f: 0 }, { s: 5, f: 0 }].map((n) => (b / 4) % 2 ? { s: n.s, f: [3, 2, 0, 0, 0, 3][n.s] } : n) }); continue; }
      const [s, f] = riff[b % riff.length];
      const n = { t, s: b >= 88 ? s + 3 : s, f: b >= 88 ? f + 5 : f, sus: b % 8 === 7 ? 0.9 : 0 };
      if (b % 16 === 5) n.ho = true;
      if (b % 16 === 9) n.bn = 1;
      if (b % 16 === 13) { n.sl = n.f + 2; n.sus = 0.4; }
      if (b % 12 === 2) n.pm = true;
      notes.push(n);
    }
    anchors.push({ time: 0, fret: 1, width: 4 }, { time: 44, fret: 5, width: 4 });
    ['Ba-', 'jo el', 'sol+', 'de la', 'ru-', 'ta nue-', 've+'].forEach((w, i) => lyrics.push({ t: 8 + i * 0.7 + (i > 2 ? 1 : 0), d: 0.5, w }));
    const lead = { name: 'Lead', type: 'lead', tuning: [0, 0, 0, 0, 0, 0], capo: 0, notes, chords, anchors, templates: [{ name: 'E', displayName: 'Mi' }, { name: 'G', displayName: 'Sol' }] };
    const bass = { name: 'Bass', type: 'bass', tuning: [-2, 0, 0, 0], capo: 0, notes: notes.filter((n, i) => i % 2 === 0).map((n) => ({ t: n.t, s: n.s % 4, f: n.f, sus: 0.3 })), chords: [], anchors };
    return { summary: gameSongs[0], arrangements: { lead, bass }, beats, sections: [{ name: 'intro', number: 1, time: 0 }, { name: 'verse', number: 1, time: 8 }, { name: 'chorus', number: 1, time: 32 }, { name: 'solo', number: 1, time: 44 }, { name: 'outro', number: 1, time: 56 }],
      lyrics, stems: gm.stems, length: gm.length, problems: '' };
  }
  function gameMock(cmd, a) {
    switch (cmd) {
      case 'gameLibrary': return ok({ songs: gameSongs, folders: ['/Users/demo/Music/Galpon/Juego'] });
      case 'gameImport': return ok({ count: 0, error: '' });
      case 'gameAddFolder': return ok();
      case 'gameOpen': gm.open = true; gm.pos = 0; gm.playing = false; gm.ended = false; return ok(gameData());
      case 'gameClose': gm.open = false; gm.playing = false; return ok();
      case 'gamePlay': gm.playing = true; gm.ended = false; playing = false; return ok();
      case 'gamePause': gm.playing = false; return ok();
      case 'gameToggle': gm.playing = !gm.playing; if (gm.playing) { playing = false; if (gm.ended) gm.pos = 0; gm.ended = false; } return ok();
      case 'gameSeek': gm.pos = a.time; gm.ended = false; if (gm.events) { gm.next = gm.events.findIndex((e) => e.t >= a.time); if (gm.next < 0) gm.next = gm.events.length; } return ok();
      case 'gameSpeed': gm.speed = a.speed; return ok();
      case 'gameLoop': gm.loop = a.on ? [a.a, a.b] : null; return ok();
      case 'gameStem': { const s = gm.stems.find((x) => x.id === a.id); if (!s) return { ok: false, error: 'Ese audio no existe' }; s.enabled = a.on; return ok(gm.stems); }
      case 'gameGuitar': gm.guitar = gm.guitar || !!a.create; return gm.guitar ? ok({ trackId: 99, name: 'Guitarra (juego)', input: 0, sound: 'Puerta de ruido · Amplificador · Gabinete · Reverb', volumeDb: 0 }) : ok(null);
      case 'gameNotes': gm.events = a.events || []; gm.next = gm.events.findIndex((e) => e.t >= gm.pos); if (gm.next < 0) gm.next = gm.events.length; return ok({ listening: !!gm.guitar });
      case 'gameDetectOffset': case 'gameVolume': case 'gameStemVolume': return ok();
      case 'gameTone': gm.tone = a.on; return a.on ? ok({ trackId: 100, tones: [{ key: 'clean', name: 'Clean Rhythm', chain: 'Amplificador · Chorus · Reverb' }, { key: 'lead', name: 'Lead Drive', chain: 'Distorsión · Amplificador · Delay · Reverb' }], changes: [[0, 'Clean Rhythm'], [32, 'Lead Drive']] }) : ok();
      case 't3Status': return ok(gm.t3 || { hasKey: false, connected: false, tones: [], files: {}, downloading: {} });
      case 't3Connect': gm.t3 = { hasKey: true, connected: true, user: 'demo', tones: [{ id: '1', title: 'Marshall JCM800 Crunch', favorite: true, gear: 'amp' }, { id: '2', title: 'Fender Twin Clean', favorite: true, gear: 'full-rig' }], files: { 1: '/x/1.nam' }, downloading: {} }; return ok();
      case 't3Disconnect': gm.t3 = null; return ok();
      case 't3Refresh': case 't3Download': return ok();
      default: return { ok: false, error: 'mock: ' + cmd };
    }
  }

  G.mock = {
    call(cmd, a) {
      if (cmd.startsWith('game') || cmd.startsWith('t3')) return gameMock(cmd, a);
      const t = track(a.trackId || a.id);
      const c = t && t.clips.find((x) => x.id === a.clipId);
      switch (cmd) {
        case 'getState': return ok(state());
        case 'getLibrary': return ok(lib);
        case 'getAudioSetup': return ok({ types: ['CoreAudio'], type: 'CoreAudio', inputDevices: ['MacBook Air Microphone', 'Scarlett 2i2'], outputDevices: ['MacBook Air Speakers', 'Scarlett 2i2'],
          inputDevice: 'Scarlett 2i2', outputDevice: 'Scarlett 2i2', sampleRate: 48000, bufferSize: 128, sampleRates: [44100, 48000, 96000], bufferSizes: [32, 64, 128, 256, 512],
          inputChannels: ['Entrada 1', 'Entrada 2'], outputChannels: ['Salida 1', 'Salida 2'], inputActive: [true, true], outputActive: [true, true], sources: ['Entrada 1', 'Entrada 2'],
          inputLatencyMs: 3.2, outputLatencyMs: 3.9, bufferMs: 2.7, roundTripMs: 12.4, deviceOpen: true, recordOffsetMs: 0, metronomeLevel: 0.6, compensationMs: 7.1, midiInputs: [{ id: 'm1', name: 'Arturia KeyStep', enabled: true }] });
        case 'recentSongs': return ok([{ path: '/a', name: 'Tema nuevo', modified: Date.now() - 3600e3 }, { path: '/b', name: 'Idea suelta', modified: Date.now() - 86400e3 }]);
        case 'flexAnalyze': {
          const notes = [[0.2, 1.6, 57.4], [2, 1.5, 59.7], [4, 0.8, 62.2], [5, 2.5, 64.1]].map(([st, l, p], i) => ({ key: i * 0.5, start: st, length: l, pitch: p, correction: 0 }));
          const curve = []; notes.forEach((n) => { for (let b = n.start; b < n.start + n.length; b += 0.05) curve.push([b, n.pitch + 0.15 * Math.sin(b * 20)]); curve.push([n.start + n.length, null]); });
          return ok({ notes, curve, hits: [0.2, 2, 4, 5], flex: {} });
        }
        case 'flexApply': if (c) c.flex = Object.assign({ on: true }, a.settings); return changed();
        case 'flexReset': if (c) c.flex = { on: false }; return changed();
        case 'getWaveform': return ok({ peaks: Array.from({ length: 400 }, (_, i) => Math.abs(Math.sin(i / 9)) * (0.3 + 0.5 * Math.abs(Math.sin(i / 53)))), samples: 288000, sampleRate: 48000 });
        case 'setAutomation': {
          t.automation = (t.automation || []).filter((l) => l.target !== a.target);
          t.automation.push({ target: a.target, on: a.on !== false, points: a.points });
          return a.drag ? ok() : changed();
        }
        case 'removeAutomation': t.automation = (t.automation || []).filter((l) => l.target !== a.target); return changed();
        case 'getParams': {
          const key = (a.trackId || 0) + ':' + a.slot;
          params[key] = params[key] || ['Brillo', 'Calidez', 'Trémolo', 'Reverb', 'Volumen'].map((name, index) => ({ index, name, value: 0.5, text: '0.50', label: '', steps: 0 }));
          return ok({ params: params[key], total: params[key].length, slot: {} });
        }
        case 'setParam': return ok(a.value.toFixed(2));
        case 'togglePlay': playing = !playing; if (!playing) recording = false; return ok();
        case 'play': playing = true; return ok();
        case 'stop': playing = false; recording = false; return ok();
        case 'record': recording = !recording; playing = recording; recStart = beat; return ok();
        case 'setPosition': beat = a.beat; return ok();
        case 'selectTrack': selected = a.id; return ok();
        case 'setSong': Object.assign(song, a); return changed();
        case 'detectChords': {
          const N = ['Do', 'Lam', 'Fa', 'Sol7'], E = ['C', 'Am', 'F', 'G7'];
          chords = [0, 1, 2, 3, 4, 5, 6, 7].map((i) => ({ beat: i * 4, length: 4, root: [0, 9, 5, 7][i % 4], quality: [0, 1, 0, 2][i % 4], name: N[i % 4], en: E[i % 4] }));
          return changed(chords.length);
        }
        case 'setChord': if (a.remove) chords.splice(a.index, 1); else Object.assign(chords[a.index], { root: a.root, quality: a.quality, name: ['Do','Do#','Re','Mib','Mi','Fa','Fa#','Sol','Lab','La','Sib','Si'][a.root] + ['', 'm', '7', 'm7'][a.quality] }); return changed();
        case 'clearChords': chords = []; return changed();
        case 'setSections': {
          sections = (a.sections || []).map((x) => ({ beat: Math.max(0, x.beat), length: Math.max(0.25, x.length), kind: x.kind })).sort((x, y) => x.beat - y.beat);
          for (let i = 0; i + 1 < sections.length; i++) sections[i].length = Math.min(sections[i].length, sections[i + 1].beat - sections[i].beat);
          sections = sections.filter((x) => x.length >= 0.25);
          return changed();
        }
        case 'downloadSound': return ok();
        case 'findLostRecordings': return ok([]);
        case 'recoverRecordings': return ok(0);
        case 'useSound': { const dest = t && t.kind !== 'audio' ? t : addTrack('instrument', 'Piano de cola real'); dest.instrument = { kind: 'builtin', type: 'sampler', name: 'Piano de cola real', bypass: false, missing: false, hasEditor: false, extra: { loadable: 'sampler', status: 'Splendid Grand Piano · 360 muestras' } }; return changed(dest.id); }
        case 'addTrack': return changed(addTrack(a.kind, a.preset).id);
        case 'deleteTrack': tracks.splice(tracks.indexOf(t), 1); return changed();
        case 'moveTrack': tracks.splice(tracks.indexOf(t), 1); tracks.splice(a.index, 0, t); return changed();
        case 'setTrack': Object.keys(a).forEach((k) => { if (k !== 'trackId') t[k] = a[k]; }); return changed();
        case 'setSend': {
          let snd = t.sends.find((x) => x.busId === a.busId);
          if (a.remove) { t.sends = t.sends.filter((x) => x !== snd); return changed(); }
          if (!snd) { snd = { busId: a.busId, levelDb: -12, pre: false }; t.sends.push(snd); }
          if ('levelDb' in a) snd.levelDb = a.levelDb;
          if ('pre' in a) snd.pre = a.pre;
          return changed();
        }
        case 'setInstrument': t.instrument.name = a.preset || a.id; if (a.rename !== false && t.kind === 'instrument' && a.preset) t.name = a.preset; return changed();
        case 'moveInsert': { const [x] = t.inserts.splice(a.from, 1); t.inserts.splice(a.to, 0, x); return changed(); }
        case 'addInsert': (a.trackId ? t.inserts : []).splice(a.index >= 0 ? a.index : 1e9, 0, { kind: a.kind, type: a.id, name: (lib.effects.find((e) => e.type === a.id) || { name: a.id }).name, bypass: false, missing: false, hasEditor: a.kind === 'plugin', extra: a.id === 'nam' ? { loadable: 'nam', status: 'Sin modelo: elige un archivo .nam' } : undefined }); return changed();
        case 'removeInsert': t.inserts.splice(a.index, 1); return changed();
        case 'setBypass': t.inserts[a.slot].bypass = a.bypass; return changed();
        case 'addClip': { const clip = { id: nextId++, name: t.kind === 'drummer' ? 'Sección' : 'Región MIDI', start: a.start, length: t.kind === 'drummer' ? 32 : 16, midi: true, loopLength: 16, seed: 1, fill: true, notes: t.kind === 'drummer' ? drumNotes(32, nextId) : [] }; t.clips.push(clip); return changed(clip.id); }
        case 'setClip': if (a.start !== undefined) c.start = a.start; if (a.length !== undefined) { c.length = a.length; if (!a.loop) c.loopLength = Math.max(c.loopLength, a.length); }
          ['name', 'gainDb', 'fill'].forEach((k) => { if (a[k] !== undefined) c[k] = a[k]; });
          if (a.toTrackId) { t.clips.splice(t.clips.indexOf(c), 1); track(a.toTrackId).clips.push(c); } return changed();
        case 'setTake': c.activeTake = a.take; return changed();
        case 'deleteClip': t.clips = t.clips.filter((x) => x.id !== a.clipId); return changed();
        case 'duplicateClip': { const d = JSON.parse(JSON.stringify(c)); d.id = nextId++; d.start = c.start + c.length; t.clips.push(d); return changed(d.id); }
        case 'splitClip': { const r = JSON.parse(JSON.stringify(c)); r.id = nextId++; const cut = a.beat - c.start; r.start = a.beat; r.length = c.length - cut; c.length = cut; t.clips.push(r); return changed(); }
        case 'setNotes': c.notes = a.notes; return changed();
        case 'setDrummer': Object.assign(t.drummer, a.settings); t.clips.forEach((x) => { x.notes = drumNotes(x.length, Math.round(t.drummer.complexity * 100)); }); return changed();
        case 'varyDrummer': t.clips.forEach((x) => { x.notes = drumNotes(x.length, Math.floor(Math.random() * 1000)); }); return changed();
        case 'addLoop': { const dest = t || addTrack('instrument', 'Piano de cola'); dest.clips.push({ id: nextId++, name: a.loopId, start: a.beat, length: 8, midi: true, loopLength: 8, notes: [[0, 1, 60, 0.8], [2, 1, 64, 0.8], [4, 1, 67, 0.8]] }); return changed(); }
        case 'choose': return { ok: false };
        default: return changed();
      }
    },
  };

  setInterval(() => {
    const now = performance.now();
    if (playing) {
      beat += (now - lastT) / 1000 * song.bpm / 60;
      if (song.cycle && !recording && beat >= song.cycleEnd) beat = song.cycleStart;
    }
    lastT = now;
    const meters = tracks.map(() => (playing ? [Math.random() * 0.6, Math.random() * 0.6] : [0, 0]));
    const inputs = tracks.map((t) => (t.kind === 'audio' ? 0.2 + Math.random() * 0.4 : 0));
    let game;
    if (gm.open) {
      if (gm.playing) {
        gm.pos += 0.033 * gm.speed;
        if (gm.loop && gm.pos >= gm.loop[1]) gm.pos = gm.loop[0];
        if (gm.pos >= gm.length) { gm.pos = gm.length; gm.playing = false; gm.ended = true; }
      }
      game = { pos: gm.pos, playing: gm.playing, ended: gm.ended, speed: gm.speed, latency: 0.01, now, level: gm.guitar && gm.playing ? Math.random() * 0.2 : 0, listening: !!gm.guitar };
      if (gm.tone) game.tone = gm.pos >= 32 ? 'Lead Drive' : 'Clean Rhythm';
      // pretend to listen: most notes hit, some early/late, a few missed
      if (gm.guitar && gm.events && gm.playing) {
        const js = [];
        while (gm.next < gm.events.length && gm.events[gm.next].t + 0.3 < gm.pos) {
          const e = gm.events[gm.next], r = Math.random();
          js.push([gm.next, 0, 0, r > 0.15, (Math.random() - 0.4) * 90, r > 0.15 ? 1 : 0.3]);
          if (e.sus > 0.4 && r > 0.15) js.push([gm.next, 0, 1, Math.random() > 0.3, 0, 0.8]);
          gm.next++;
        }
        if (js.length) game.judgments = js;
      }
    }
    const rec = recording ? { start: recStart, tracks: tracks.filter((t) => t.kind === 'audio' && (t.armed || t.id === selected)).map((t) => t.id) } : undefined;
    G.emitLocal('tick', { beat, playing, recording, rec, countIn: false, cpu: 0.05, version, meters, inputs, master: playing ? [0.5, 0.5] : [0, 0], game });
  }, 33);
})();
