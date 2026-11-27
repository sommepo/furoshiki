// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Furoshiki's page on a phone. What it shows comes from HomeActivity (window.FuroshikiHost): the
// games the last look through the folders found, what was read of them, the screens of the
// reader. In a plain browser a made-up host stands in, for looking at the page.

'use strict';

// A console's short name, and the colour of its spines' band (colour and type only, nothing of a
// console maker's: the desktop's SpineStyleOf).
const LOOKS = {
  'Nintendo Switch': ['Switch', '#c8323c'],
  'Nintendo 3DS': ['3DS', '#b03a48'],
  'Nintendo DS': ['DS', '#7a8a99'],
  'PlayStation': ['PS1', '#8c8c96'],
  'PlayStation Portable': ['PSP', '#3d5a8c'],
  'PlayStation Vita': ['PS Vita', '#3f6fb0'],
  'PlayStation 2': ['PS2', '#2f4f7a'],
  'Dreamcast': ['Dreamcast', '#d9752b'],
  // A title another app plays (Add game, read as its companion): the system's key stays
  // "Another app" (AppTitles.SYSTEM); the third is the name the page shows for it.
  'Another app': ['Other apps', '#5d6b5a', 'Other apps'],
  // GameNative's games on a shelf of their own, not among the other apps' (the maintainer, 2026-11-19).
  'GameNative': ['GameNative', '#4a6b7a', 'GameNative'],
};

/**
 * What each console is called, by its key (Console.kt's; Places.NAMES in Kotlin has the same): on
 * a tab or in a short place, then in a sentence. Never the key itself ("PSVITA", "NDS") nor a
 * shortening of ours ("DC", "Vita").
 */
const CONSOLE_NAMES = {
  psp: ['PSP', 'PSP'], dc: ['Dreamcast', 'Dreamcast'], '3ds': ['3DS', 'Nintendo 3DS'], nds: ['DS', 'Nintendo DS'],
  ps1: ['PS1', 'PlayStation'], ps2: ['PS2', 'PlayStation 2'], psvita: ['PS Vita', 'PS Vita'], switch: ['Switch', 'Nintendo Switch'],
};
/** A console's name on a tab: by its key, else by its system's name, else what its emulator calls it. */
const tabName = (key, system, short) => CONSOLE_NAMES[key]?.[0] || LOOKS[system]?.[0] || short || system || '';
/** A console's name in a sentence or a header. */
const fullName = (key, system) => CONSOLE_NAMES[key]?.[1] || LOOKS[system]?.[2] || system || '';
/** The colour of a console's band on a spine. */
const bandOf = game => (LOOKS[game.system] || LOOKS[fullName(game.console, game.system)] || [])[1] || '';

const made_up = {
  asset(path) {
    const request = new XMLHttpRequest();
    request.open('GET', path, false);
    request.send();
    return request.responseText;
  },
  themes: () => JSON.stringify(['kon.json', 'mono.json', 'paper.json', 'solarized.json']),
  icon: () => '30 Solarized',
  pickIcon() { say('Choose an icon'); },
  // "#…,noaccess" shows the library before file access is allowed; "#…,empty" a library with no games.
  hasAccess: () => !location.hash.includes('noaccess'),
  askAccess() {},
  games: () => JSON.stringify(location.hash.includes('empty') || location.hash.includes('noaccess') ? [] : [
    {id: 'ULJM05887', title: 'STEINS;GATE', path: '/g/sg.iso', console: 'psp', system: 'PlayStation Portable', script: true, chars: 184220, activeMs: 25400000, last: '2026-11-01'},
    {id: 'ULJS00219', title: '428 〜封鎖された渋谷で〜', path: '/g/428.iso', console: 'psp', system: 'PlayStation Portable', script: true, chars: 40212, activeMs: 7400000, last: '2026-10-28'},
    {id: 'ULJM05821', title: 'CHAOS;HEAD らぶChu☆Chu!', path: '/g/ch.iso', console: 'psp', system: 'PlayStation Portable', script: true},
    // Titles from file names, a game with no ID a user knows, and consoles named by their keys.
    {id: 'SLUS-20395', title: 'CHULIP (USA) (1)', path: '/g/chulip.iso', console: 'ps2', system: 'PlayStation 2', script: false},
    {id: 'PCSG00488', title: 'Sora no Kiseki FC Evolution (Japan) [!]', path: '/g/sora', console: 'psvita', system: 'PlayStation Vita', script: false},
    {id: 'T-1234', title: 'July', path: '/g/july.gdi', console: 'dc', system: 'Dreamcast', script: true},
    {id: 'APP-PARANORMASIGHT', title: 'PARANORMASIGHT_ THE SEVEN MYSTERIES OF HONJO', path: 'app:app.gamenative/a#1', console: 'app', system: 'Another app', via: 'GameNative', chars: 5120, activeMs: 1900000, last: '2026-11-18'},
  ]),
  ocrSet: () => JSON.stringify(['PCSG00488']),
  setupSteps: () => JSON.stringify(['folders', 'dictionaries', 'anki', 'gamenative']),
  hasGameNative: () => true,
  hasController: () => location.hash.includes('pad'),
  version: () => '0.5.9',
  addTitle(name, activity, label) { say('Add game from ' + label); },
  stats: () => JSON.stringify({
    today: {chars: 1186, activeMs: 620000, text: '1,186字 · 6,910/h · 10m'},
    total: {chars: 224432, activeMs: 32800000, text: '224,432字 · 24,630/h · 9h 06m'},
    days: Array.from({length: 30}, (_, i) => ({day: 'd' + i, chars: 500 + (i * 7919) % 4000})),
    games: [{id: 'ULJM05887', title: 'STEINS;GATE', chars: 184220, activeMs: 25400000, text: '184,220字 · 7h 03m'},
      {id: 'APP-EVER17', title: 'ever 17 (Japan)', chars: 900, activeMs: 400000, text: '900字 · 6m'}],
    daysRead: 212,
    // A year of days for the Statistics page, the newest first.
    year: Array.from({length: 380}, (_, i) => { const d = new Date(); d.setDate(d.getDate() - i); const v = (i * 7919 + 13) % 23;
      return {day: `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`, chars: v < 5 || i > 330 ? 0 : 900 + v * 430, ms: v < 5 || i > 330 ? 0 : 600000 + v * 90000}; }),
  }),
  statsFolder: () => JSON.stringify({path: '/storage/emulated/0/Furoshiki/Stats', devices: 2, access: true}),
  askStatsFolder() { say('Statistics folder'); },
  goal: kind => { try { return Number(localStorage.getItem('goal-' + kind) ?? {daily: 9000, yearly: 3000000}[kind] ?? 0); } catch (e) { return 0; } },
  setGoal(kind, value) { try { localStorage.setItem('goal-' + kind, String(value)); } catch (e) { /* this run */ } },
  folders: () => JSON.stringify(location.hash.includes('empty') ? [] : ['/storage/emulated/0/Games', '/storage/1A2B-3C4D/Games/PSP']),
  ocr: () => JSON.stringify({chosen: '', using: 'device', address: '', named: '', token: false, device: true}),
  pickOcrEngine() { say('OCR engine'); }, typeOcrService() { say('OCR helper'); },
  allGames: () => true, setAllGames() {},
  // The build's own records where the page was laid out with them, else two made up.
  titles() {
    const request = new XMLHttpRequest();
    request.open('GET', 'titles.json', false);
    try { request.send(); } catch (e) { return made_up.someTitles(); }
    return request.status === 200 ? request.responseText : made_up.someTitles();
  },
  someTitles: () => JSON.stringify({
    ULJM05887: {key: 'steins-gate', title: 'STEINS;GATE', english: 'STEINS;GATE', jiten: 283, tracker_title: '', guide: 'steins-gate', more: 7,
      people: [{name: '岡部 倫太郎', romaji: 'Okabe Rintarou'}, {name: '牧瀬 紅莉栖', romaji: 'Makise Kurisu'}, {name: '椎名 まゆり', romaji: 'Shiina Mayuri'}]},
    ULJS00219: {key: '428-shibuya', title: '428 〜封鎖された渋谷で〜', english: '428: Shibuya Scramble', jiten: 0, tracker_title: '', guide: '', people: [], more: 0},
  }),
  tracker: () => location.hash.includes('untracked') ? '' : JSON.stringify({at: Date.now(), jiten: {283: {read: 138093, total: 695712, unique: 16181, speed: 9000,
    sessions: 26, difficulty: 2.93, coverage: 84.23, hours: 15.35, last: '2026-11-10'}}, title: {}}),
  trackerAddress: () => 'https://tracker.example:8788',
  askTrackerAddress() { say('Type the study tracker\'s address'); },
  reading: () => JSON.stringify({days: Array.from({length: 30}, (_, i) => ({day: 'd' + i, chars: (i * 7919) % 4000})), lines: 2140}),
  openLog(game) { say('Backlog of ' + game); },
  haptics: () => true, setHaptics() {}, hasVibrator: () => true,
  settingsIndex: () => JSON.stringify([{section: 'READING', page: 'Reading', key: 'reader_furigana', title: 'Furigana'},
    {section: 'POPUP', page: 'Popup', key: 'reader_popup_theme', title: 'Popup theme'}, {section: 'CARDS', page: 'Cards', key: 'reader_open_anki', title: 'Anki setup'}]),
  openSetting(section, key) { say('Open ' + section + ' at ' + key); },
  twoScreens: () => location.hash.includes('two'),
  // "#kon,0,home" shows the page as a device's home screen has it.
  homeScreen: () => location.hash.includes('home') || location.hash.startsWith('#lower') ? 'on' : 'off',
  setHomeScreen(on) { say(on ? 'Home screen on' : 'Home screen off'); },
  playing: () => location.hash.includes('playing') ? JSON.stringify({path: '/g/sg.iso', console: 'psp', game: 'ULJM05887'}) : '',
  status: () => JSON.stringify({battery: 87, charging: true, wifi: true, online: true, volume: 60, brightness: 70, dim: true,
    storage: [{name: 'Device', free: 81e9, total: 256e9}, {name: 'Card', free: 310e9, total: 512e9}]}),
  volume: () => 60, brightness: () => 70, home() {},
  apps: () => JSON.stringify([{label: 'AnkiDroid', package: 'com.ichi2.anki', activity: 'a'}, {label: 'Calendar', package: 'com.android.calendar', activity: 'a'},
    {label: 'Firefox', package: 'org.mozilla.firefox', activity: 'a'}, {label: 'GameNative', package: 'app.gamenative', activity: 'a'},
    {label: 'Minecraft', package: 'com.mojang.minecraftpe', activity: 'a', game: true},
    {label: 'Syncthing-Fork', package: 'com.github.catfriend1.syncthingandroid', activity: 'a'}, {label: 'Tailscale', package: 'com.tailscale.ipn', activity: 'a'}]),
  openApp(name) { say('Open ' + name); }, system(what) { say('Android: ' + what); },
  remote(what) { say(what); },
  services: () => JSON.stringify({tracker: 'x', furochibi: 'x', guides: 'x'}), askServices() {},
  wide: () => JSON.stringify({value: 'off', choices: ['off', '21:9', '32:9'], consoles: ['Switch']}), setWide() {},
  labels: () => JSON.stringify(location.hash.includes('renamed') ? {'Settings/Theme': 'Colours', 'Page/Settings': 'Options'} : {}),
  askLabel(key) { say('Rename ' + key); }, setLabel() {}, copyLabels() {},
  // `#…,published` shows a published build's Update row (GitHub's releases) in place of the dev line's.
  update: () => location.hash.includes('published') ? '' : JSON.stringify({mine: '0.3.0', version: '0.3.1', url: 'x', newer: true}), openUpdate() {},
  release: () => JSON.stringify({mine: '0.2.5', version: '0.2.6', newer: true, checked: 1}), checkRelease() {}, installRelease() {},
  consoles: () => JSON.stringify([{key: 'psp', short: 'PSP', system: 'PlayStation Portable'}, {key: 'ps2', short: 'PS2', system: 'PlayStation 2'},
    {key: 'psvita', short: 'VITA', system: 'PlayStation Vita'}, {key: 'dc', short: 'DC', system: 'Dreamcast'}, {key: 'nds', short: 'NDS', system: 'Nintendo DS'}]),
  layer: (console, game) => JSON.stringify([
    {key: 'picture.resolution', label: 'Internal resolution', kind: 1, value: '2', shown: '2×', set: !!console, from: console ? '' : 'Per console', low: 1, high: 8, step: 1, choices: []},
    {key: 'sound.volume', label: 'Volume', kind: 1, value: '100', shown: '100', set: false, from: 'All consoles', low: 0, high: 100, step: 1, choices: []},
    {key: 'picture.aspect', label: 'Aspect ratio', kind: 2, value: 'auto', shown: 'auto', set: false, from: game ? 'Console setting' : 'Default', choices: ['auto', '4:3', '16:9', 'stretch']},
    {key: 'picture.vsync', label: 'Vertical sync', kind: 0, value: '1', shown: 'On', set: false, from: 'Default', choices: []},
    {key: 'picture.frame_limit', label: 'Limit frame rate', kind: 0, value: '1', shown: 'On', set: false, from: 'Default', choices: []},
  ]),
  setSetting() {},
  openScripts(console) { say('Scripts of ' + console); },
  addFolder() { say('Choose a folder'); }, removeFolder() {}, play(path) { say('Play ' + path); }, open(what) { say('Open ' + what); },
  // A scan as the app tells it: two consoles looked through, then over.
  scan() { Front.scanning('PSP · 1 of 2'); setTimeout(() => Front.scanning('PS Vita · 2 of 2'), 300); setTimeout(() => Front.scanning(''), 600); },
};

const host = window.FuroshikiHost || made_up;
const $ = id => document.getElementById(id);

const state = {
  themes: {},
  theme: 'kon',
  games: [],
  stats: null,
  pages: [],
  page: 0,
  chosen: {},
  // Whose settings the Consoles page shows: all consoles, one, or one game.
  scope: {console: '', game: '', name: 'All consoles', tag: 'All'},
  // The last game chosen on a console's page: "Only this game" among the choices.
  lastGame: null,
  picking: null,
  // The shelf's order: keys in priority, "last,title" (a "!" after one turns it round), the desktop's shelf/sort.
  order: 'last',
  // The game in play behind the page ({path, console, game}), where the page is the home screen.
  playing: null,
  // The device: battery, network, volume, brightness (host.status).
  status: null,
  // The title records by game ID (host.titles): English names, people, box pictures.
  titles: {},
  // What the study tracker said last (host.tracker), or null.
  tracker: null,
  // The title in hand as a page of its own, over its shelf.
  stage: false,
};

// ---- The theme: the desktop's shelf themes (home/shelf), six colours as the page's variables ----

function loadThemes() {
  for (const file of JSON.parse(host.themes())) {
    try {
      state.themes[file.replace('.json', '')] = JSON.parse(host.asset('themes/' + file));
    } catch (error) {
      console.error(error);
    }
  }
  // Kon unless another was chosen (a look of the page before this one is no longer one).
  if (!state.themes[state.theme]) state.theme = Object.keys(state.themes)[0];
  try {
    const order = localStorage.getItem('order');
    if (order && orderKeys(order).length) state.order = order;
  } catch (e) { /* no storage here */ }
  try {
    const kept = localStorage.getItem('look');
    if (kept && state.themes[kept]) state.theme = kept;
  } catch (e) { /* no storage here: the first theme */ }
}

function applyTheme() {
  const theme = state.themes[state.theme] || {};
  const root = document.documentElement.style;
  for (const key of ['ground', 'raised', 'ink', 'muted', 'accent', 'rule']) {
    if (theme[key]) root.setProperty('--' + key, theme[key]);
  }
  fit();
}

/** Whether this is the page of a device's second display (index.html#lower): the title in hand
 *  and the keys, told what to show by the page on the first (Lower, at the end). */
const LOWER = location.hash.startsWith('#lower');

/**
 * What one pixel of the desktop's 1280x720 sheet is here, and which shape the page takes:
 * wide (a handheld's 16:9 screen: the row in hand beside the rows), square (an open Fold: the
 * title in hand above its shelf), tall (a phone, a closed Fold). With a second display (the
 * Thor's lower screen) the first is all shelf and the second has the title and the keys.
 */
/** Text sizes offered, and the one chosen (kept in the page's storage; the second display is told it). */
const TEXT_SIZES = [['Small', .9], ['Medium', 1.1], ['Large', 1.3], ['Extra large', 1.5]];
function textSizeIndex() {
  if (LOWER && window.lowerTextSize !== undefined) return window.lowerTextSize;
  let kept = 1;
  try { const said = localStorage.getItem('text-size'); if (said !== null) kept = Number(said); } catch (e) { /* medium */ }
  return kept >= 0 && kept < TEXT_SIZES.length ? kept : 1;
}
const textSize = () => TEXT_SIZES[textSizeIndex()][1];
function setTextSize(index) {
  const next = Math.max(0, Math.min(TEXT_SIZES.length - 1, index));
  try { localStorage.setItem('text-size', String(next)); } catch (e) { /* this run */ }
  fit();
  rebuild();
}

function fit() {
  const width = innerWidth, height = innerHeight, shape = width / height;
  const body = document.body.classList;
  body.toggle('lower', LOWER);
  body.toggle('two', !LOWER && !!host.twoScreens?.());
  body.toggle('square', shape >= .8 && shape < 1.45);
  body.toggle('tall', shape < .8);
  let scale;
  if (LOWER) scale = Math.min(width / 600, height / 520);
  else if (shape >= 1.45) scale = Math.min(height / 720, width / 1180);
  else if (shape >= .8) scale = Math.min(width / 860, height / 760);
  else scale = width / 560;
  // The user's text size (Settings › Display), and a little more on a top display read from a
  // hand's length (a new user, 2026-11-19: "all of the text is too small ... top screen especially").
  scale *= textSize() * (!LOWER && host.twoScreens?.() ? 1.12 : 1);
  document.documentElement.style.setProperty('--s', Math.max(.45, Math.min(scale, 2.6)).toFixed(3) + 'px');
}

/** A date as 2026/11/19, the one form everywhere; with a time, 2026/11/19 22:14. */
const two = n => String(n).padStart(2, '0');
const ymd = date => `${date.getFullYear()}/${two(date.getMonth() + 1)}/${two(date.getDate())}`;
const ymdTime = date => `${ymd(date)} ${two(date.getHours())}:${two(date.getMinutes())}`;
/** A place in a list as "01 / 73". */
const counter = (at, count) => `${two(at)} / ${two(count)}`;

function tick() {
  const now = new Date();
  const time = now.getHours() + ':' + String(now.getMinutes()).padStart(2, '0');
  $('clock').textContent = time;
  if (LOWER) {
    $('away-clock').textContent = time;
    $('away-date').textContent = ymd(now);
  }
}

// ---- The device, where Furoshiki is its home screen: said in the head, set on the System page ----

const percent = n => n >= 0 ? n + '%' : 'Not set';

/** The battery and the network as the head says them. */
function statusWords(status) {
  if (!status) return '';
  // A phone's header has one line: the battery alone (the system's own bar has the rest).
  if (document.body.classList.contains('tall')) return status.battery >= 0 ? status.battery + '%' : '';
  return [
    status.wifi ? 'Wi-Fi' : status.online ? 'Online' : 'No network',
    status.battery >= 0 ? status.battery + '%' + (status.charging ? ' charging' : '') : '',
  ].filter(Boolean).join(' · ');
}

function readStatus() {
  // The battery and the network in the head, as the home screen or not.
  try { state.status = host.status ? JSON.parse(host.status()) : null; } catch (error) { console.error(error); }
  $('status').textContent = statusWords(state.status);
}

// ---- Figures ----

const number = n => (n || 0).toLocaleString('en-US');

function playTime(ms) {
  const minutes = Math.floor((ms || 0) / 60000);
  return minutes < 60 ? minutes + 'm' : Math.floor(minutes / 60) + 'h ' + String(minutes % 60).padStart(2, '0') + 'm';
}

// ---- What a title is: its record, and whose figures are shown ----

const recordOf = game => state.titles[game.id] || null;
const hasJapanese = text => /[\u3040-\u30ff\u3400-\u9fff]/.test(text || '');
/**
 * Text that may be Japanese (a game's name, a person's) put in an element: the page is English
 * (lang="en"), and what is Japanese says so, for a screen reader's voice and the glyphs' forms.
 */
function putText(element, text) {
  element.textContent = text || '';
  if (hasJapanese(text)) element.lang = 'ja'; else element.removeAttribute('lang');
}
const dayText = day => day ? day.replace(/-/g, '/') : 'Not yet';

/**
 * A title taken from a file's name, as it is shown (the file is never renamed): the dump's tags at
 * its end go ("CHULIP (USA) (1)", "Game (Japan) [!]"), and the "_ " a file name has in the place
 * of ": " comes back ("PARANORMASIGHT_ THE SEVEN MYSTERIES"). A disc's number stays.
 */
const DUMP_TAG = /^(USA|Japan|Europe|World|Asia|Korea|China|Taiwan|Hong Kong|Australia|Brazil|Canada|France|Germany|Italy|Spain|Netherlands|Sweden|Russia|UK|NTSC(-[UJ])?|PAL|En|Ja|Fr|De|Es|It|Nl|Pt|Sv|Ko|Zh|Ru|Rev ?[0-9A-Z.]+|v?[0-9][0-9.]*|Beta ?[0-9]*|Demo|Proto(type)?|Unl|Alt ?[0-9]*|Sample)$/i;
function cleanTitle(title) {
  let text = String(title || '');
  for (;;) {
    // Square brackets at the end always; round ones only where every part is a dump's tag
    // (a game script's "作り話 (A Made-up Game)" is a name). TitleText.kt has the same rule.
    const square = /\s*\[[^\[\]]*\]\s*$/.exec(text);
    const round = /\s*\(([^()]*)\)\s*$/.exec(text);
    let cut;
    if (square) cut = text.slice(0, square.index);
    else if (round && round[1].split(',').every(part => DUMP_TAG.test(part.trim()))) cut = text.slice(0, round.index);
    else break;
    if (!cut.trim()) break;
    text = cut;
  }
  return text.replace(/_ /g, ': ').replace(/\s{2,}/g, ' ').trim() || String(title || '');
}

/** What the shelf calls a game: its record's English name, else its own title tidied (the desktop's ShelfName). */
const shelfName = game => recordOf(game)?.english || cleanTitle(game.title);

/** A game's ID where it is one a user knows (a disc's serial), never one of ours ("APP-EVER17"). */
const shownId = game => /^APP-/i.test(String(game?.id || '')) ? '' : String(game?.id || '');

/** How a game's text is read, in words: its mark on the spine says the same. */
const READ_BY = {script: 'Game script', ocr: 'OCR', unset: 'OCR, default text area'};

/** The study tracker's figures of a game's title (by its Jiten deck, else the tracker's own title), or null. */
function trackedOf(game) {
  const record = recordOf(game), kept = state.tracker;
  if (!record || !kept) return null;
  return (record.jiten && kept.jiten?.[record.jiten]) || (record.tracker_title && kept.title?.[record.tracker_title]) || null;
}

/** The later of the two last days: the tracker counts every device's reading. */
const lastDay = game => [trackedOf(game)?.last || '', game.last || ''].sort().pop();
const readOf = game => trackedOf(game)?.read ?? (game.chars || 0);
const timeOf = game => { const tracked = trackedOf(game); return tracked?.hours > 0 ? tracked.hours * 3.6e6 : game.activeMs || 0; };

/** The open spine's figures: the tracker's where it has the title, else this device's own. */
function gameFigures(game) {
  const tracked = trackedOf(game);
  if (!tracked) {
    return [['Characters', number(game.chars)], ['Play time', playTime(game.activeMs)], ['Last read', dayText(game.last)]];
  }
  const share = tracked.total > 0 ? ` · ${Math.round(100 * tracked.read / tracked.total)}%` : '';
  return [['Characters', number(tracked.read) + share], ['Play time', playTime(timeOf(game))], ['Last read', dayText(lastDay(game))]];
}

// ---- The pages along the foot ----

function makePages() {
  const pages = [];
  const consoles = new Map();
  for (const game of state.games) {
    if (!consoles.has(game.system)) consoles.set(game.system, []);
    consoles.get(game.system).push(game);
  }
  // Every title on one shelf first, then each console's (the desktop's floors).
  if (consoles.size > 1) pages.push({key: 'console:*', tab: 'All games', head: 'Library', sub: 'All games', shelf: true, rows: gameRows(state.games)});
  for (const [system, games] of consoles) {
    const key = games[0]?.console, short = games[0]?.short;
    pages.push({key: 'console:' + system, tab: tabName(key, system, short), head: 'Library', sub: fullName(key, system), shelf: true, rows: gameRows(games)});
  }
  if (pages.length === 0) pages.push(libraryWithoutGames());
  pages.push(statisticsPage());
  pages.push(consolesPage());
  // The device's home screen has the device's own pages too.
  if (host.homeScreen?.() === 'on') pages.push(appsPage(), systemPage());
  pages.push(settingsPage());
  // The names given to rows (program/labels.json, the desktop's Developer page) still apply; the
  // Developer page itself is not on Android (the maintainer, 2026-11-17: "remove developer settings").
  if (host.labels) applyLabels(pages);
  state.pages = pages;
  if (state.page >= pages.length) state.page = 0;
}

// The orders a shelf may stand in (the Settings page's row), last read at first.
const ORDERS = {
  last: ['Last read', (a, b) => lastDay(b).localeCompare(lastDay(a))],
  title: ['Title', () => 0],
  console: ['Console', (a, b) => (a.system || '').localeCompare(b.system || '')],
  chars: ['Characters read', (a, b) => readOf(b) - readOf(a)],
  time: ['Play time', (a, b) => timeOf(b) - timeOf(a)],
  progress: ['Progress', (a, b) => shareOf(b) - shareOf(a)],
  length: ['Length', (a, b) => (trackedOf(b)?.total || 0) - (trackedOf(a)?.total || 0)],
};
/** How far through a title the tracker says it is, 0 to 1; a title it does not have goes after those it has. */
const shareOf = game => { const tracked = trackedOf(game); return tracked?.total > 0 ? tracked.read / tracked.total : -1; };

/** The order's keys as `[key, turned]`, those that are none of ours left out. */
function orderKeys(order = state.order) {
  return String(order).split(',').map(one => [one.replace('!', ''), one.endsWith('!')]).filter(([key]) => ORDERS[key]);
}

/** The order said: "Last read, then Title". */
const orderText = () => orderKeys().map(([key, turned]) => ORDERS[key][0] + (turned ? ' ↑' : '')).join(', then ') || ORDERS.last[0];

/** The shelf's order chosen: a key is added after the others, a second tap turns it round, a third takes it off. */
function pickOrder() {
  const keys = Object.keys(ORDERS);
  const chosen = orderKeys();
  const labels = ['Done', ...keys.map(key => {
    const at = chosen.findIndex(([one]) => one === key);
    return at < 0 ? ORDERS[key][0] : `${at + 1} · ${ORDERS[key][0]}${chosen[at][1] ? ' · Reversed' : ''}`;
  })];
  pick('Sort by', labels, 0, at => {
    if (at === 0) return;
    const key = keys[at - 1];
    const index = chosen.findIndex(([one]) => one === key);
    if (index < 0) chosen.push([key, false]);
    else if (!chosen[index][1]) chosen[index][1] = true;
    else chosen.splice(index, 1);
    state.order = chosen.map(([one, turned]) => one + (turned ? '!' : '')).join(',') || 'last';
    try { localStorage.setItem('order', state.order); } catch (e) { /* for this run alone */ }
    rebuild();
    pickOrder();
  }, '', {cancel: false});
}

function gameRows(games) {
  const chosen = orderKeys();
  const by = (a, b) => {
    for (const [key, turned] of chosen) {
      const way = ORDERS[key][1](a, b);
      if (way) return turned ? -way : way;
    }
    return 0;
  };
  const sorted = games.slice().sort((a, b) => by(a, b) || shelfName(a).localeCompare(shelfName(b), 'ja'));
  // How each is read (the maintainer, 2026-11-17): a script (a tick), OCR with an area set
  // (glasses), or nothing yet (a question mark: OCR to set up).
  let ocrSet = new Set();
  try { ocrSet = new Set(JSON.parse(host.ocrSet?.() || '[]')); } catch (e) { /* none */ }
  const readBy = game => game.script === true ? 'script' : ocrSet.has(String(game.id).toUpperCase()) ? 'ocr' : 'unset';
  return sorted.map(game => ({
    read: readBy(game),
    // Its English name, as the desktop's shelf sets it; its own name under it where that is another.
    title: shelfName(game),
    about: recordOf(game)?.english && shelfName(game) !== cleanTitle(game.title) ? cleanTitle(game.title) : '',
    side: [lastDay(game) ? dayText(lastDay(game)) : '', timeOf(game) ? playTime(timeOf(game)) : ''].filter(Boolean).join('\n'),
    file: shownId(game),
    // Its box's front where the build has it (its console's own), else what the device found.
    cover: recordOf(game)?.cover || game.cover,
    // Its box's spine: built in, else found (VNDB, ScreenScraper), else drawn in its console's look
    // (Spines.kt, as the desktop draws one).
    spine: recordOf(game)?.spine || game.spine || game.drawn || '',
    band: bandOf(game),
    // How its text is read, in words: the tick, the glasses and the "?" on its spine say no more.
    figures: [...gameFigures(game), ['Read by', READ_BY[readBy(game)]]],
    // Whose figures they are, said in the card's first line.
    from: trackedOf(game) ? 'Study tracker' : game.chars ? 'This device' : '',
    far: shareOf(game),
    // Start (never played), Continue (played before); Resume only for the game held behind the home screen.
    go: state.playing?.path === game.path ? 'Resume' : readOf(game) > 0 || timeOf(game) > 0 ? 'Continue' : 'Start',
    act() { play(game); },
    // A second tap on the spine in hand, or A: the title as a page of its own.
    open() { setStage(true); },
    id: game.path,
    game,
    pin() { host.pin?.(game.path, game.console, game.id, shelfName(game)); say('Adding to the Home screen'); },
    also: 'Game settings',
    alsoAct() {
      setScope({console: game.console, game: game.id, name: shelfName(game), tag: 'Game'});
      goTo(state.pages.findIndex(one => one.key === 'consoles'));
    },
    // A title another app plays: which app, and a way off the shelf instead of a console's settings.
    ...(game.console === 'app' ? (state.playing?.path === game.path && host.quitPlaying ? {
      // In play: the clean way out, the companion ended (another app's game has no menu of ours).
      about: 'Plays in ' + (game.via || 'another app'),
      also: 'Quit game',
      alsoAct() { host.quitPlaying(); say('Quitting ' + shelfName(game) + '…'); },
    } : {
      about: 'Plays in ' + (game.via || 'another app'),
      also: 'Remove from library',
      alsoAct() {
        pick('Remove ' + shelfName(game) + ' from library?', ['Remove', 'Cancel'], 1, at => {
          if (at === 0) host.forgetTitle?.(game.id);
        }, '', {cancel: false});
      },
    }) : {}),
  }));
}

/**
 * Add game: the three ways a game comes to the library, asked first (a user, 2026-11-19: the
 * button opened a list of every installed app at once, Calendar and Clock among them).
 */
function addGame() {
  const ways = [['Add games folder', () => host.addFolder()]];
  if (host.addTitle) ways.push(['Add game from another app', addTitle]);
  ways.push(['Scan for games', () => scanForGames()]);
  pick('Add game', ways.map(one => one[0]), 0, at => ways[at]?.[1]());
}

/** A scan the user asked for: what it found is said once it is over (Front.scanning). */
function scanForGames() {
  state.scanAsked = true;
  host.scan();
}

/**
 * Add game from another app: a game another app plays (GameNative, Winlator…). Furoshiki stands
 * on the second display beside it, reading the screen by OCR, as with a console's game that has no
 * game script. The apps Android itself marks as games first, the PC game apps at their head, then
 * every other app under "Other apps".
 */
function addTitle() {
  if (!host.addTitle) return;
  let apps = [];
  try { apps = JSON.parse(host.apps()); } catch (error) { console.error(error); }
  if (!apps.length) { say('No apps found'); return; }
  const pcGames = app => /gamenative|winlator|winnative|mobox|gamehub|steam/i.test(app.package + ' ' + app.label);
  const games = apps.filter(app => app.game || pcGames(app)).sort((a, b) => pcGames(b) - pcGames(a));
  const others = apps.filter(app => !games.includes(app));
  const listed = [...games, ...others];
  // A heading only where there are both kinds: the games need none.
  const labels = games.length && others.length
    ? [...games.map(app => app.label), {head: 'Other apps'}, ...others.map(app => app.label)] : listed.map(app => app.label);
  pick('Add game from another app', labels, 0, at => {
    const app = listed[at > games.length && games.length && others.length ? at - 1 : at];
    if (app) host.addTitle(app.package, app.activity, app.label);
  });
}

/** The row that adds one. */
function addTitleRows() {
  if (!host.addTitle) return [];
  return [{
    title: 'Add game from another app', id: 'add-title', go: 'Add',
    about: 'For example GameNative',
    act() { addTitle(); },
  }];
}

/** Plays a game. Another in play behind the page (the home screen) is asked about first. */
function play(game) {
  const playing = state.playing;
  if (!playing || playing.path === game.path) {
    host.play(game.path, game.console, game.id);
    return;
  }
  const other = state.games.find(one => one.path === playing.path);
  const running = other ? shelfName(other) : 'Another game';
  pick(running + ' is running', ['Resume ' + running, 'Quit and start ' + shelfName(game)], 0, at => {
    if (at === 0) host.play(playing.path, playing.console, playing.game);
    else host.play(game.path, game.console, game.id);
  });
}

// ---- The device's own pages, while Furoshiki is its home screen ----

function appsPage() {
  let apps = [];
  try { apps = JSON.parse(host.apps()); } catch (error) { console.error(error); }
  const rows = apps.map(app => ({
    title: app.label, about: app.package, go: 'Open', id: app.package + '/' + app.activity,
    act() { host.openApp(app.package, app.activity); },
    also: 'App info', alsoAct() { host.system('app:' + app.package); },
  }));
  if (!rows.length) rows.push({title: 'No apps', about: '', id: 'none'});
  return {key: 'apps', tab: 'Apps', head: 'Apps', sub: '', rows};
}

function systemPage() {
  const status = state.status || {};
  const size = bytes => (bytes / 1e9).toFixed(bytes < 1e10 ? 1 : 0) + ' GB';
  const open = (title, side, about, what, go = 'Open') => ({title, side, value: side, about, go, act() { host.system(what); }, id: what});
  const dial = (title, key, about, screen, change) => ({
    title, side: percent(status[key]), value: percent(status[key]), about, go: 'More', id: key,
    act() { host.system(screen); },
    step(direction) { change(direction); readStatus(); rebuild(); },
  });
  const rows = [
    dial('Volume', 'volume', '', 'sound', direction => host.volume(direction)),
    status.dim
      ? dial('Brightness', 'brightness', '', 'display', direction => host.brightness(direction))
      : {title: 'Brightness', side: percent(status.brightness), value: percent(status.brightness), about: 'Allow Furoshiki to change system settings', go: 'Allow', act() { host.system('dim'); }, id: 'brightness'},
    open('Wi-Fi', status.wifi ? 'Connected' : 'Not connected', '', 'wifi'),
    open('Bluetooth', '', '', 'bluetooth'),
    open('Battery', percent(status.battery ?? -1) + (status.charging ? ' · Charging' : ''), '', 'battery'),
    ...(status.storage || []).map(one => ({
      ...open(one.name + ' storage', size(one.free) + ' free', size(one.total) + ' total', 'storage'), id: 'storage:' + one.name,
    })),
    open('Date & time', ymd(new Date()), '', 'date'),
    open('Notifications', '', '', 'notifications', 'Show'),
    open('Quick settings', '', '', 'quick', 'Show'),
    open('Android settings', '', '', 'android'),
  ];
  return {key: 'system', tab: 'System', head: 'System', sub: '', rows};
}

/** Where a scan looks, in words: the user's folders, and Games on the device and its SD card. */
function lookedIn() {
  let folders = [];
  try { folders = JSON.parse(host.folders?.() || '[]'); } catch (e) { /* none */ }
  const own = folders.map(shortPath);
  return [...own, 'Games on internal storage and the SD card'].join(', ');
}

/**
 * A folder as a row says it: without the device's own root ("Games/PSP" for
 * /storage/emulated/0/Games/PSP), "SD card: Games/PSP" on a card, its last two parts where it is deep.
 */
function shortPath(path) {
  const text = String(path);
  const inside = /^\/storage\/emulated\/\d+\/?(.*)$/.exec(text) || /^\/sdcard\/?(.*)$/.exec(text);
  const card = inside ? null : /^\/storage\/[^/]+\/?(.*)$/.exec(text);
  const rest = (inside || card)?.[1] ?? text;
  const parts = rest.split('/').filter(Boolean);
  const shown = parts.slice(-2).join('/') || (card ? '' : 'Internal storage');
  return card ? ('SD card' + (shown ? ': ' + shown : '')) : shown;
}

function libraryWithoutGames() {
  const access = host.hasAccess();
  // A new library: asked once where the games are, not left to guess what a scan looks through
  // (a new user, 2026-11-19: "what exactly is it scanning? shouldn't we ask for a folder first?").
  return {
    key: 'library', tab: 'Library', head: 'Library', sub: '', art: '', fill: 8,
    rows: access ? [{
      title: 'Add games folder',
      about: 'The folder your games are in',
      go: 'Add',
      act() { host.addFolder(); },
      id: 'add',
    }, {
      title: 'Scan for games',
      // After a scan that found nothing: said here, where it stays, and where it looked.
      about: state.scanFoundNone ? 'No games found in ' + lookedIn() + '. Add the folder your games are in.'
        : 'Looks in ' + lookedIn(),
      go: 'Scan',
      act() { scanForGames(); },
      id: 'scan',
    }, ...addTitleRows()] : [{
      title: 'Allow file access',
      about: 'Needed to find your games',
      go: 'Allow',
      act() { host.askAccess(); },
      id: 'access',
    }],
  };
}

/** A game of the statistics by the library's name for it, where the library has it; else its own title tidied. */
function statsName(counted) {
  const game = counted.id ? state.games.find(one => String(one.id) === String(counted.id)) : null;
  return game ? shelfName(game) : (counted.id && state.titles[counted.id]?.english) || cleanTitle(counted.title);
}

function statisticsPage() {
  const stats = state.stats || {today: {}, total: {}, days: [], games: []};
  const rows = [
    {title: 'Today', side: stats.today.text || '', file: '統計', figures: figuresOf(stats.today), id: 'today', bars: stats.days},
    {title: 'All games', side: stats.total.text || '', file: '統計', figures: figuresOf(stats.total), id: 'total', bars: stats.days},
    {title: 'Reading days', side: number(stats.daysRead), value: number(stats.daysRead), file: '統計', id: 'days', about: ''},
    {title: 'Games started', side: `${(stats.games || []).filter(game => game.chars > 0).length} of ${state.games.length}`,
      value: String((stats.games || []).filter(game => game.chars > 0).length), file: '統計', id: 'begun', about: ''},
    // The games by what was read of them, the most first.
    ...(stats.games || []).slice().sort((a, b) => (b.chars || 0) - (a.chars || 0))
      // Called what the library calls it (its record's name, its file's name tidied), by its ID.
      .map((game, i) => ({title: statsName(game), side: playTime(game.activeMs), file: '統計', figures: figuresOf(game), id: 'game' + i})),
    {title: 'Full statistics', about: 'Sessions, export', file: '統計', go: 'Open', act() { host.open('statistics'); }, id: 'open'},
  ];
  return {key: 'statistics', tab: 'Statistics', head: 'Statistics', sub: '', view: 'stats', rows};
}

const figuresOf = c => [['Characters', number(c.chars)], ['Play time', playTime(c.activeMs)]];

// ---- The Statistics page: the desktop's, drawn by itself (the maintainer, 2026-11-27) ----
// The period is chosen at the top left, as on the desktop (WEEK, MONTH, YEAR, ALL TIME before
// CHARACTERS); a goal is set on tumbler wheels, a digit a wheel ("your initial abacus
// implementation on android was too big, maybe go with simple tumbler locks").

const PERIODS = ['Week', 'Month', 'Year', 'All time'];
/** A goal's kind: what one step of its last wheel is, and how many wheels it has. */
const GOALS = {daily: [100, 3], weekly: [1000, 3], monthly: [1000, 4], yearly: [10000, 4]};
const PERIOD_GOAL = ['weekly', 'monthly', 'yearly', ''];
const goalOf = kind => { try { return Number(host.goal?.(kind)) || 0; } catch (e) { return 0; } };
const compact = n => n >= 1e6 ? (n / 1e6).toFixed(2) + 'M' : n >= 1e4 ? (n / 1e3).toFixed(n >= 1e5 ? 0 : 1).replace(/\.0$/, '') + 'K' : number(n);
const hoursText = ms => ((ms || 0) / 3.6e6).toFixed(ms >= 3.6e7 ? 0 : 1) + 'H';
const dayKey = date => `${date.getFullYear()}-${two(date.getMonth() + 1)}-${two(date.getDate())}`;
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

state.statsAt = {range: 2, focus: 0, open: '', kind: '', digits: [], wheel: 0};
try { const kept = localStorage.getItem('stats-range'); if (kept !== null && Number(kept) >= 0 && Number(kept) < 4) state.statsAt.range = Number(kept); } catch (e) { /* the year */ }

/** Every figure the page shows, from what the app counted (host.stats: `year` is a day a row, the newest first). */
function statsFigures() {
  const stats = state.stats || {};
  const total = stats.total || {}, today = stats.today || {};
  const byDay = new Map((stats.year || stats.days || []).map(one => [one.day, one]));
  const now = /^\d{4}-\d\d-\d\d$/.test(stats.day || '') ? new Date(stats.day + 'T12:00:00') : new Date();
  const todayKey = dayKey(now);
  // The last 53 weeks (26 on an upright phone), a column a week, Monday at the top; the last column is this week.
  const weeks = document.body.classList.contains('tall') ? 26 : 53;
  const monday = new Date(now); monday.setDate(now.getDate() - (now.getDay() + 6) % 7);
  const first = new Date(monday); first.setDate(monday.getDate() - (weeks - 1) * 7);
  const grid = [];
  for (let i = 0; i < weeks * 7; i++) {
    const date = new Date(first); date.setDate(first.getDate() + i);
    const key = dayKey(date), one = byDay.get(key);
    grid.push({key, date, chars: one?.chars || 0, ms: one?.ms || 0, later: key > todayKey});
  }
  const range = state.statsAt.range;
  const inPeriod = [
    key => key >= dayKey(monday),
    key => key.slice(0, 7) === todayKey.slice(0, 7),
    key => key.slice(0, 4) === todayKey.slice(0, 4),
    () => true,
  ][range];
  let chars = 0, ms = 0, active = 0;
  for (const [key, one] of byDay) if (inPeriod(key) && key <= todayKey) { chars += one.chars || 0; ms += one.ms || 0; active += one.chars > 0 ? 1 : 0; }
  if (range === 3) { chars = total.chars || 0; ms = total.activeMs || 0; active = stats.daysRead || active; }
  // Days in a row, up to today (or yesterday while today has nothing yet).
  let streak = 0;
  const walk = new Date(now);
  if (!(byDay.get(todayKey)?.chars > 0)) walk.setDate(walk.getDate() - 1);
  while (byDay.get(dayKey(walk))?.chars > 0) { streak++; walk.setDate(walk.getDate() - 1); }
  const daily = goalOf('daily');
  const kind = PERIOD_GOAL[range];
  const own = kind ? goalOf(kind) : 0;
  const goal = own || (kind ? daily * [7, 30, 365][range] : 0);
  // The last thirty days' pace says when the next milestone comes.
  let month = 0;
  for (let i = 0; i < 30; i++) { const date = new Date(now); date.setDate(now.getDate() - i); month += byDay.get(dayKey(date))?.chars || 0; }
  const ladder = [5e5, 1e6, 2e6, 5e6, 8e6, 15e6, 30e6];
  const all = total.chars || 0;
  const next = ladder.find(step => step > all) || 0;
  let when = '';
  if (next && month > 0) {
    const date = new Date(now); date.setDate(now.getDate() + Math.ceil((next - all) / (month / 30)));
    when = `${date.getDate()} ${MONTHS[date.getMonth()]} ${date.getFullYear()}`;
  }
  // How far along the ladder: each step the same width, as the desktop's ruler.
  let along = 0;
  if (all > 0) {
    const at = ladder.findIndex(step => step > all);
    const upper = at < 0 ? ladder.length : at, low = upper > 0 ? ladder[upper - 1] : 0, high = ladder[upper] || low;
    along = Math.min(1, (upper + (high > low ? (all - low) / (high - low) : 0)) / ladder.length);
  }
  const peaks = [...byDay.values()].filter(one => one.chars > 0).sort((a, b) => b.chars - a.chars).slice(0, 3);
  // The chart under the year: the period in its parts (days of a week or month, months of a year).
  const parts = [];
  if (range <= 1) {
    const from = range === 0 ? new Date(monday) : new Date(now.getFullYear(), now.getMonth(), 1);
    const count = range === 0 ? 7 : new Date(now.getFullYear(), now.getMonth() + 1, 0).getDate();
    for (let i = 0; i < count; i++) {
      const date = new Date(from); date.setDate(from.getDate() + i);
      parts.push({label: range === 0 ? ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'][i] : String(i + 1), chars: byDay.get(dayKey(date))?.chars || 0});
    }
  } else {
    const sums = new Map();
    for (const [key, one] of byDay) sums.set(key.slice(0, 7), (sums.get(key.slice(0, 7)) || 0) + (one.chars || 0));
    for (let i = 11; i >= 0; i--) {
      const date = new Date(now.getFullYear(), now.getMonth() - i, 1);
      parts.push({label: MONTHS[date.getMonth()], chars: sums.get(`${date.getFullYear()}-${two(date.getMonth() + 1)}`) || 0});
    }
  }
  return {
    range, chars, ms, active, goal, kind, daily, streak, grid, todayKey, peaks, parts, all, next, when, along, ladder,
    today: {chars: today.chars || 0, ms: today.activeMs || 0},
    daysRead: stats.daysRead || 0,
    titles: (stats.games || []).filter(game => game.chars > 0).length,
    perDay: active > 0 ? Math.round(chars / active) : 0,
    hoursDay: active > 0 ? ms / 3.6e6 / active : 0,
    speed: ms > 0 ? Math.round(chars / (ms / 3.6e6)) : 0,
  };
}

/** A goal's value as the wheels' digits, the largest first. */
function goalDigits(kind, value) {
  const [step, wheels] = GOALS[kind];
  return String(Math.max(0, Math.min(10 ** wheels - 1, Math.round(value / step)))).padStart(wheels, '0').split('').map(Number);
}
const goalValue = (kind, digits) => Number(digits.join('')) * GOALS[kind][0];

function openGoal(kind) {
  if (!GOALS[kind]) return;
  const at = state.statsAt;
  Object.assign(at, {open: 'goal', kind, digits: goalDigits(kind, goalOf(kind)), wheel: 1});
  at.wheel = Math.max(at.digits.findIndex(digit => digit > 0), 0);
  drawStats();
}
function turnWheel(wheel, by) {
  const at = state.statsAt;
  at.wheel = wheel;
  at.digits[wheel] = (at.digits[wheel] + by + 10) % 10;
  drawStats();
}
function closeGoal(keep) {
  const at = state.statsAt;
  if (keep) {
    try { host.setGoal?.(at.kind, goalValue(at.kind, at.digits)); } catch (e) { /* as it was */ }
    say(goalValue(at.kind, at.digits) > 0 ? 'Goal set' : 'Goal off');
  }
  at.open = '';
  drawStats();
}
function setRange(range) {
  state.statsAt.range = (range + PERIODS.length) % PERIODS.length;
  try { localStorage.setItem('stats-range', String(state.statsAt.range)); } catch (e) { /* this run */ }
  drawStats();
}

/** The wheels of the goal being set: a digit a wheel, the one above it larger, the fixed zeros after. */
function tumblerHtml() {
  const at = state.statsAt, [step] = GOALS[at.kind];
  const zeros = String(step).length - 1, count = at.digits.length + zeros;
  let wheels = '';
  at.digits.forEach((digit, i) => {
    if (i > 0 && (count - i) % 3 === 0) wheels += '<span class="tsep">,</span>';
    wheels += `<span class="tw${i === at.wheel ? ' on' : ''}" data-wheel="${i}"><button type="button" data-turn="1" aria-label="Higher">${(digit + 1) % 10}</button><b>${digit}</b><button type="button" data-turn="-1" aria-label="Lower">${(digit + 9) % 10}</button></span>`;
  });
  for (let i = 0; i < zeros; i++) {
    if ((zeros - i) % 3 === 0) wheels += '<span class="tsep">,</span>';
    wheels += '<span class="tw fixed"><b>0</b></span>';
  }
  return `<div class="tumbler"><div class="wheels">${wheels}</div><div class="tkeys"><button type="button" data-goal-keep>Keep</button><button type="button" data-goal-cancel>Cancel</button></div></div>`;
}

function drawStats() {
  const box = $('stats');
  const shown = page()?.view === 'stats' && !LOWER;
  document.body.classList.toggle('stats', shown);
  if (!shown || !box) return;
  const f = statsFigures(), at = state.statsAt;
  const focus = which => state.inList && !at.open && at.focus === which ? ' focus' : '';
  const share = (n, of) => of > 0 ? Math.min(999, Math.round(n * 100 / of)) + '%' : '';
  const dots = (n, of) => {
    const lit = of > 0 ? Math.min(24, Math.round(24 * n / of)) : 0;
    return '<div class="sdots">' + Array.from({length: 24}, (_, i) => `<i class="${i < lit ? 'lit' : ''}"></i>`).join('') + '</div>';
  };
  const setting = kind => at.open === 'goal' && at.kind === kind;
  // A period's goal not set by itself follows the daily one, and says so by its figure.
  const goalShown = kind => kind === 'daily' ? f.daily : f.goal;
  const rung = n => n >= 1e6 ? n / 1e6 + 'M' : n / 1e3 + 'K';
  const goalKey = (kind, label, which) => kind
    ? `<button type="button" class="sgoal${focus(which)}" data-goal="${kind}">${label} ${goalShown(kind) > 0 ? compact(goalShown(kind)) : 'off'}</button>` : '<span></span>';
  const period = at.open === 'period'
    ? '<span class="drum open">' + PERIODS.map((name, i) => `<button type="button" class="${i === at.range ? 'on' : ''}" data-range="${i}">${name}</button>`).join('') + '</span>'
    : `<button type="button" class="drum${focus(0)}" data-period>${PERIODS[at.range]}</button>`;
  const level = one => one.later ? 'later' : one.chars <= 0 ? '' : f.daily > 0 && one.chars >= f.daily ? 'met' : one.chars >= (f.daily || 4000) / 2 ? 'much' : 'some';
  const months = [];
  f.grid.forEach((one, i) => { if (i % 7 === 0 && one.date.getDate() <= 7) months.push(`<span style="grid-column:${i / 7 + 1}">${MONTHS[one.date.getMonth()]}</span>`); });
  const most = Math.max(1, ...f.grid.map(one => one.chars));
  const spark = f.grid.filter(one => !one.later).map((one, i, list) => `${(i / Math.max(1, list.length - 1) * 100).toFixed(2)},${(100 - one.chars / most * 100).toFixed(1)}`).join(' ');
  const top = Math.max(1, ...f.parts.map(one => one.chars));
  const every = Math.ceil(f.parts.length / 12);
  const peak = f.peaks[0]?.chars || 1;
  const dayName = key => { const date = new Date(key + 'T12:00:00'); return `${date.getDate()} ${MONTHS[date.getMonth()]}`; };
  box.innerHTML = `
    <div class="stiles">
      <div class="stile${setting(f.kind) ? ' setting' : ''}">
        <div class="shead"><span>${period}<span class="scap">Characters</span></span><em>${share(f.chars, f.goal)}</em></div>
        ${setting(f.kind) ? tumblerHtml() : `<div class="snum">${compact(f.chars)}<small>${hoursText(f.ms)}</small></div>${dots(f.chars, f.goal)}
        <div class="sfoot">${goalKey(f.kind, 'Goal', 1)}<span>${number(f.active)} active</span></div>`}
      </div>
      <div class="stile${setting('daily') ? ' setting' : ''}">
        <div class="shead"><span>Today</span><em>${share(f.today.chars, f.daily)}</em></div>
        ${setting('daily') ? tumblerHtml() : `<div class="snum">${number(f.today.chars)}<small>${hoursText(f.today.ms)}</small></div>${dots(f.today.chars, f.daily)}
        <div class="sfoot">${goalKey('daily', 'Daily goal', 2)}<span></span></div>`}
      </div>
      <div class="stile narrow">
        <div class="shead"><span>Streak</span><span>${number(f.daysRead)} days</span></div>
        <div class="snum">${number(f.streak)}<small>Days</small></div>
        <div class="sfoot bottom"><span>${number(f.titles)} titles</span></div>
      </div>
      <div class="stile">
        <div class="shead"><span>Pace</span><span>Speed</span></div>
        <div class="snum">${number(f.perDay)}<small>/ D</small></div>
        <div class="sfoot bottom"><span>${f.hoursDay.toFixed(1)}H / day</span><span>${compact(f.speed)} / H</span></div>
      </div>
    </div>
    <div class="smile">
      <div><span class="scap">Milestone</span><b>${number(f.all)} ch</b></div>
      <div class="sruler"><i style="width:${(f.along * 100).toFixed(1)}%"></i>${f.ladder.map((step, i) => `<span class="${step === f.next ? 'next' : ''}" style="left:${((i + 1) / f.ladder.length * 100).toFixed(2)}%">${rung(step)}</span>`).join('')}</div>
      <div class="swhen"><b>${f.when || ''}</b><span class="scap">${f.next ? 'Next · ' + number(f.next) : ''}</span></div>
    </div>
    <div class="syear" style="--weeks:${f.grid.length / 7}">
      <div class="smonths">${months.join('')}</div>
      <div class="sgrid">${f.grid.map(one => `<i class="${level(one)}${one.key === f.todayKey ? ' now' : ''}"></i>`).join('')}</div>
      <svg class="sspark" viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true"><polyline points="${spark}"/></svg>
    </div>
    <div class="slow">
      <div class="sarc">
        <div class="shead"><span>Characters</span><span>${PERIODS[at.range === 3 ? 2 : at.range]}</span></div>
        <div class="sbars">${f.parts.map(one => `<i style="height:${Math.max(2, one.chars / top * 100).toFixed(1)}%"></i>`).join('')}</div>
        <div class="slabels">${f.parts.map((one, i) => `<span>${i % every === 0 ? one.label : ''}</span>`).join('')}</div>
      </div>
      <div class="speak">
        <div class="shead"><span>Peak days</span><span>All time</span></div>
        ${f.peaks.map((one, i) => `<div class="sp"><div><span>${two(i + 1)} · ${dayName(one.day)}</span><span>${number(one.chars)} ch${one.ms ? ' · ' + hoursText(one.ms).toLowerCase() : ''}</span></div><i class="${i === 0 ? 'first' : ''}" style="width:${(one.chars / peak * 100).toFixed(1)}%"></i></div>`).join('') || '<p class="scap">No reading yet</p>'}
      </div>
    </div>`;
  // A finger: the period's word, a goal, the wheels.
  box.querySelector('[data-period]')?.addEventListener('click', () => { at.focus = 0; at.open = 'period'; setInList(true); drawStats(); });
  box.querySelectorAll('[data-range]').forEach(one => one.addEventListener('click', () => { at.open = ''; setRange(Number(one.dataset.range)); }));
  box.querySelectorAll('[data-goal]').forEach(one => one.addEventListener('click', () => { at.focus = one.dataset.goal === 'daily' ? 2 : 1; setInList(true); openGoal(one.dataset.goal); }));
  box.querySelector('[data-goal-keep]')?.addEventListener('click', () => closeGoal(true));
  // The second display shows the period and the wheels too, for a thumb.
  if (!LOWER && host.lower) mirror();
  box.querySelector('[data-goal-cancel]')?.addEventListener('click', () => closeGoal(false));
  box.querySelectorAll('.tw[data-wheel]').forEach(wheel => {
    const index = Number(wheel.dataset.wheel);
    wheel.querySelectorAll('[data-turn]').forEach(one => one.addEventListener('click', event => { event.stopPropagation(); turnWheel(index, Number(one.dataset.turn)); }));
    // A stroke down a wheel turns it: down brings the digit above (the larger) into the window.
    let from = null;
    wheel.addEventListener('pointerdown', event => { from = event.clientY; });
    wheel.addEventListener('pointerup', event => {
      if (from === null) return;
      const steps = Math.round((event.clientY - from) / (wheel.offsetHeight / 3));
      from = null;
      if (steps) turnWheel(index, steps);
      else if (event.target.tagName === 'B') { at.wheel = index; drawStats(); }
    });
  });
}

/** A key on the Statistics page; false where the page has no use for it (the rail's keys, L and R). */
function statsKey(name) {
  const at = state.statsAt;
  if (at.open === 'goal') {
    if (name === 'ArrowLeft' || name === 'ArrowRight') { at.wheel = Math.max(0, Math.min(at.digits.length - 1, at.wheel + (name === 'ArrowLeft' ? -1 : 1))); drawStats(); }
    else if (name === 'ArrowUp' || name === 'ArrowDown') turnWheel(at.wheel, name === 'ArrowUp' ? 1 : -1);
    else if (name === 'Enter' || name === 'Play') closeGoal(true);
    else if (name === 'Escape') closeGoal(false);
    return true;
  }
  if (at.open === 'period') {
    if (name === 'ArrowUp' || name === 'ArrowDown') setRange(at.range + (name === 'ArrowUp' ? -1 : 1));
    else if (name === 'Enter' || name === 'Escape' || name === 'ArrowLeft' || name === 'ArrowRight') { at.open = ''; drawStats(); }
    return true;
  }
  if (!state.inList) return false;
  if (name === 'PageUp' || name === 'PageDown') return false;
  const last = PERIOD_GOAL[at.range] ? 2 : 1;
  const order = PERIOD_GOAL[at.range] ? [0, 1, 2] : [0, 2];
  const here = Math.max(order.indexOf(at.focus), 0);
  if (name === 'ArrowLeft') { if (here > 0) at.focus = order[here - 1]; else setInList(false); }
  else if (name === 'ArrowRight') at.focus = order[Math.min(order.length - 1, here + 1)];
  else if (name === 'Escape') setInList(false);
  else if (name === 'Enter') {
    if (order[here] === 0) at.open = 'period';
    else { openGoal(order[here] === 1 ? PERIOD_GOAL[at.range] : 'daily'); return true; }
  }
  drawStats();
  return true;
}


// ---- The Consoles page: the settings in their layers, as the desktop's ----

function setScope(scope) {
  state.scope = scope;
  delete state.chosen.consoles;
  rebuild();
}

function pickScope() {
  const consoles = JSON.parse(host.consoles());
  const choices = [{label: 'All consoles', scope: {console: '', game: '', name: 'All consoles', tag: 'All'}}];
  for (const one of consoles) {
    const short = tabName(one.key, one.system, one.short);
    choices.push({label: short, scope: {console: one.key, game: '', name: short, tag: short}});
  }
  const game = state.lastGame;
  if (game) {
    choices.push({label: shelfName(game), scope: {console: game.console, game: game.id, name: shelfName(game), tag: 'Game'}});
  }
  const now = choices.findIndex(one => one.scope.console === state.scope.console && one.scope.game === state.scope.game);
  pick('Settings for', choices.map(one => one.label), Math.max(now, 0), at => setScope(choices[at].scope));
}

function settingRow(setting) {
  const {console, game} = state.scope;
  const write = value => {
    host.setSetting(setting.key, value, console, game);
    rebuild();
    // A renderer is taken as a game starts: said when it is changed, not in its label.
    if (setting.key === 'picture.renderer') say('Applies from the next game you start');
  };
  const steps = setting.kind === 1 ? Math.round((setting.high - setting.low) / (setting.step || 1)) : 0;
  const numbers = Array.from({length: steps + 1}, (_, at) => String(setting.low + at * (setting.step || 1)));
  const shown = value => setting.key === 'picture.resolution' ? value + '×' : value;
  const change = () => {
    if (setting.kind === 0) {
      write(setting.value === '1' ? '0' : '1');
    } else if (setting.kind === 2) {
      pick(setting.label, setting.choices, setting.choices.indexOf(setting.value), at => write(setting.choices[at]));
    } else if (steps <= 16) {
      pick(setting.label, numbers.map(shown), numbers.indexOf(setting.value), at => write(numbers[at]));
    } else {
      pickNumber(setting, write);
    }
  };
  const step = direction => {
    if (setting.kind === 0) {
      write(setting.value === '1' ? '0' : '1');
    } else if (setting.kind === 2) {
      const count = setting.choices.length;
      write(setting.choices[(setting.choices.indexOf(setting.value) + direction + count) % count]);
    } else {
      const next = Number(setting.value) + direction * (setting.step || 1);
      write(String(Math.min(setting.high, Math.max(setting.low, next))));
    }
  };
  const row = {
    title: setting.label, side: setting.shown, same: !setting.set, about: setting.from || (game ? 'This game' : state.scope.name),
    value: setting.shown, go: setting.kind === 0 ? (setting.value === '1' ? 'Turn off' : 'Turn on') : 'Change', act: change, step, id: setting.key,
  };
  if (setting.set) {
    row.also = game ? 'Use console setting' : 'Reset to default';
    row.alsoAct = () => write('');
  }
  return row;
}

function consolesPage() {
  const scope = state.scope;
  const rows = [{
    title: 'Settings for', side: scope.name, about: '', value: scope.tag,
    go: 'Choose', act: pickScope, id: 'scope',
  }];
  let settings = [];
  try {
    settings = JSON.parse(host.layer(scope.console, scope.game));
  } catch (error) {
    console.error(error);
  }
  rows.push(...settings.map(settingRow));
  // Whose settings these are, in the header: a game's ("Game settings" on its card comes here), a console's, or all.
  const head = scope.game ? 'Game settings · ' + scope.name : scope.console ? 'Consoles · ' + scope.name : 'Consoles';
  return {key: 'consoles', tab: 'Consoles', head, name: 'Consoles', sub: '', hidden: true, rows: rows.map(row => ({file: '本体', ...row}))};
}


// ---- A choice among values, over the page ----

/**
 * Setup, asked once, one sequence with a true count (the maintainer, 2026-11-19: "ask the user for
 * their game folders as a part of the setup, also their gamenative folder and prompt them on how
 * to setup frontend sync ... prompt for existing users on update from old build"): where the
 * games are, a dictionary (needed for lookups), Anki (optional), then GameNative's exported games
 * where its app is installed. The host says which steps this device has (host.setupSteps); the
 * steps done are kept in the page's storage, so a build that has a step for the first time asks
 * it, new library or old. A step put away with Back is asked again at the next start; a folder
 * chosen or a screen left comes back through Front.reload, which goes on.
 */
const SETUP_NAMES = {folders: 'Game folders', dictionaries: 'Dictionaries', anki: 'Anki', gamenative: 'GameNative'};
function setupSteps() {
  try { return JSON.parse(host.setupSteps?.() || '["folders"]').filter(step => SETUP_NAMES[step]); } catch (e) { return ['folders']; }
}
function setupDone() {
  let done = [];
  if (!window.FuroshikiHost) {
    const from = /setup:(\w+)/.exec(location.hash);
    return from ? setupSteps().slice(0, Math.max(setupSteps().indexOf(from[1]), 0)) : [];
  }
  try {
    done = JSON.parse(localStorage.getItem('setup-done') || 'null');
    if (!done) {
      // The setup before this one kept a number: 1 once the folders were asked, 2 once GameNative was.
      const before = Number(localStorage.getItem('setup-step') || 0);
      done = [...(before >= 1 ? ['folders'] : []), ...(before >= 2 ? ['gamenative'] : [])];
    }
  } catch (e) { return null; }
  return done;
}
function runSetup() {
  if (LOWER || state.picking || !host.hasAccess?.() || !host.addFolder) return;
  // In a plain browser only when asked for ("#…,setup", "#…,setup:anki" for a later step).
  if (!window.FuroshikiHost && !location.hash.includes('setup')) return;
  const steps = setupSteps(), done = setupDone();
  // No storage: nothing can be kept, so nothing is asked (it would be asked for ever).
  if (!done) return;
  const step = steps.find(one => !done.includes(one));
  // A step put away with Back is asked again at the next start, not at the next scan's news.
  if (!step || state.setupAsked === step) return;
  state.setupAsked = step;
  const title = `Setup · ${steps.indexOf(step) + 1} of ${steps.length} · ${SETUP_NAMES[step]}`;
  const next = () => {
    try { localStorage.setItem('setup-done', JSON.stringify([...done, step])); } catch (e) { /* this run */ }
  };
  if (step === 'folders') {
    // A folder for each console is as good as one for all (the maintainer, 2026-11-19: "if their
    // game folders sit at root per console"; the system's chooser refuses a card's root): after a
    // folder, the folders so far and the question again, until Done.
    const folders = JSON.parse(host.folders?.() || '[]');
    const adding = !!state.setupAdding;
    const labels = adding ? ['Add another folder', 'Done']
      : ['Add games folder', folders.length ? 'Keep my folders' : 'Use the Games folder', 'Skip'];
    const note = adding ? folders.join('\n')
      : 'Furoshiki looks in the folders you add, and in a folder called Games on internal storage and on the SD card. If each console has its own folder, add them one after another.';
    pick(title, labels, adding ? 1 : 0, at => {
      if (at === 0) { state.setupAdding = true; state.setupAsked = null; host.addFolder(); return; }
      next();
      if (!adding && at === 1) scanForGames();
      setTimeout(runSetup, 350);
    }, note, {cancel: false});
  } else if (step === 'dictionaries') {
    pick(title, ['Import dictionaries', 'Skip'], 0, at => {
      next();
      // Its screen comes over the page (or on the second display): the next step once it is left.
      if (at === 0) host.open('dictionaries'); else setTimeout(runSetup, 350);
    }, 'A dictionary is needed to look up words. Download a Yomitan dictionary (.zip), such as Jitendex, then import the file.', {cancel: false});
  } else if (step === 'anki') {
    pick(title, ['Set up Anki', 'Skip'], 0, at => {
      next();
      if (at === 0) host.open('anki'); else setTimeout(runSetup, 350);
    }, 'Optional. Add words to Anki as cards with the line, a screenshot and audio.', {cancel: false});
  } else {
    pick(title, ['Add GameNative folder', 'Skip'], 0, at => {
      next();
      if (at === 0) host.addFolder();
    }, 'To list your GameNative games here and start them directly: in GameNative, open Settings › Interface › Frontend Sync, turn it on and choose a folder (a new one such as Games/GameNative). Then add that same folder here.', {cancel: false});
  }
}

function closePick() {
  state.picking = null;
  $('pick').hidden = true;
}

/** A row that only shuts the list: every list has a way out a finger can see. */
function cancelRow() {
  const item = document.createElement('li');
  item.textContent = 'Cancel';
  item.className = 'cancel';
  item.setAttribute('role', 'button');
  item.onclick = closePick;
  return item;
}

function drawPick() {
  const list = $('pick-rows');
  list.textContent = '';
  const picking = state.picking;
  picking.labels.forEach((label, index) => {
    const item = document.createElement('li');
    if (label && label.head) {
      // A heading among the rows ("Other apps"): never chosen.
      item.textContent = label.head;
      item.className = 'head';
    } else {
      putText(item, label);
      item.className = index === picking.at ? 'on' : '';
      item.setAttribute('role', 'button');
      item.onclick = () => {
        const chosen = state.picking.chosen;
        closePick();
        chosen(index);
      };
    }
    list.append(item);
  });
  if (picking.cancel) {
    const item = cancelRow();
    if (picking.at === picking.labels.length) item.classList.add('on');
    list.append(item);
  }
  list.children[picking.at]?.scrollIntoView({block: 'nearest'});
}

/**
 * A list over the page: `labels` (a string a row, `{head}` a heading), the row marked at first,
 * what a choice does, a note above the rows. It ends with a Cancel row unless `cancel: false`
 * says its own rows have one (Skip, Done, Cancel).
 */
function pick(title, labels, at, chosen, note, options = {}) {
  let first = Math.max(at, 0);
  while (labels[first]?.head && first < labels.length - 1) first++;
  state.picking = {labels, at: first, chosen, cancel: options.cancel !== false};
  $('pick-title').textContent = title;
  $('pick-note').textContent = note || '';
  $('pick').hidden = false;
  drawPick();
}

/** A title looked for by a part of its name: what is typed lists the games that hold it. */
function findTitle() {
  state.picking = {slider: true};
  $('pick-title').textContent = 'Search games';
  const list = $('pick-rows');
  list.textContent = '';
  const line = document.createElement('li');
  const field = document.createElement('input');
  field.type = 'search';
  field.placeholder = 'Game name';
  line.append(field);
  list.append(line);
  const show = () => {
    // Between the field and Cancel: the games found.
    while (list.children.length > 2) list.children[1].remove();
    const wanted = field.value.trim().toLowerCase();
    if (!wanted) return;
    const holds = game => (shelfName(game) + '\n' + game.title).toLowerCase().includes(wanted);
    for (const game of state.games.filter(holds).slice(0, 12)) {
      const item = document.createElement('li');
      item.setAttribute('role', 'button');
      item.textContent = `${shelfName(game)} · ${tabName(game.console, game.system, game.short)}`;
      item.onclick = () => {
        closePick();
        const at = state.pages.findIndex(one => one.key === 'console:' + game.system);
        if (at < 0) return;
        state.chosen[state.pages[at].key] = game.path;
        state.lastGame = game;
        goTo(at);
      };
      list.lastChild.before(item);
    }
  };
  field.oninput = show;
  $('pick-note').textContent = '';
  list.append(cancelRow());
  $('pick').hidden = false;
  field.focus();
}

/** A number with too many values to list: a slider, written as a finger lets go. */
function pickNumber(setting, write) {
  state.picking = {slider: true};
  $('pick-title').textContent = `${setting.label}  ${setting.value}`;
  const list = $('pick-rows');
  list.textContent = '';
  const item = document.createElement('li');
  const slider = document.createElement('input');
  slider.type = 'range';
  slider.min = setting.low;
  slider.max = setting.high;
  slider.step = setting.step || 1;
  slider.value = setting.value;
  slider.oninput = () => { $('pick-title').textContent = `${setting.label}  ${slider.value}`; };
  slider.onchange = () => {
    closePick();
    write(slider.value);
  };
  item.append(slider);
  list.append(item, cancelRow());
  $('pick-note').textContent = '';
  $('pick').hidden = false;
}

/** The OCR engine and the service's address: what a game without a script is read with. */
function ocrRows() {
  if (!host.ocr) return [];
  const ocr = JSON.parse(host.ocr());
  const using = {service: 'OCR helper', device: 'On this device', lens: 'Google Lens'}[ocr.using] || 'Not set';
  const where = ocr.address || ocr.named;
  return [
    {
      title: 'OCR engine', id: 'ocr-engine', go: 'Change', side: using + (ocr.chosen ? '' : ' (default)'), value: using,
      about: ocr.using === 'service' ? where || '' : '',
      act() { host.pickOcrEngine(); },
    },
    {
      title: 'OCR helper', id: 'ocr-service', go: 'Edit', side: where || 'Not set', value: where ? 'Set' : 'Not set',
      about: !ocr.address && ocr.named ? 'From home server' : '',
      act() { host.typeOcrService(); },
    },
  ];
}

function servicesText() {
  try { return Object.keys(JSON.parse(host.services() || '{}')).join(' · '); } catch (e) { return ''; }
}

/** Widescreen: one row for every console whose core has a wide picture (the desktop's WideStep). */
function wideRow() {
  let wide = null;
  try { wide = JSON.parse(host.wide?.() || 'null'); } catch (e) { /* none */ }
  if (!wide || !wide.choices?.length) return [];
  const said = value => value === 'off' ? 'Off' : value;
  const set = value => { host.setWide(value); rebuild(); };
  return [{
    title: 'Widescreen', id: 'wide', go: 'Change', side: said(wide.value), value: said(wide.value),
    about: wide.consoles.join(', '),
    act() { pick('Widescreen', wide.choices.map(said), wide.choices.indexOf(wide.value), at => set(wide.choices[at])); },
    step(direction) { set(wide.choices[(wide.choices.indexOf(wide.value) + direction + wide.choices.length) % wide.choices.length]); },
  }];
}

/**
 * Update: a dev build against the one the home server's Files page offers (host.update, dev only);
 * a published build against the latest release on GitHub (host.release, Updater.kt), installed here.
 */
function updateRow() {
  let update = null;
  try { update = JSON.parse(host.update?.() || 'null'); } catch (e) { /* none */ }
  if (!update) return releaseRow();
  const said = !update.version ? 'Not checked yet' : update.newer ? update.version + ' available' : 'Up to date';
  return [{
    title: 'Update', id: 'update', side: said, value: update.newer ? update.version : said, go: update.newer ? 'Download' : 'Check now',
    about: '',
    act() { if (update.newer) host.openUpdate(update.url); else { host.askServices(); say('Checking for updates…'); } },
  }];
}

function releaseRow() {
  let release = null;
  try { release = JSON.parse(host.release?.() || 'null'); } catch (e) { /* none */ }
  if (!release) return [];
  const said = release.newer ? release.version + ' available' : release.checked ? 'Up to date' : 'Not checked yet';
  return [{
    title: 'Update', id: 'update', side: said, value: release.newer ? release.version : said, go: release.newer ? 'Install' : 'Check now',
    about: '',
    act() {
      if (release.newer) { host.installRelease(); say('Downloading update…'); } else { host.checkRelease(); say('Checking for updates…'); }
    },
  }];
}

// ---- The Developer page: what a row is called (the desktop's, program/labels.json) ----

const labels = () => { try { return JSON.parse(host.labels?.() || '{}'); } catch (e) { return {}; } };

function developerPage() {
  const renaming = !!state.renaming;
  const count = Object.keys(labels()).length;
  const rows = [
    {
      title: 'Rename labels', id: 'rename', side: renaming ? 'On' : 'Off', value: renaming ? 'On' : 'Off', go: renaming ? 'Turn off' : 'Turn on',
      about: '',
      act() { state.renaming = !renaming; rebuild(); },
    },
    {
      title: 'Copy labels', id: 'copy', side: count ? count + ' renamed' : 'None', go: 'Copy',
      about: '',
      act() { host.copyLabels(); say('Copied'); },
    },
    {
      title: 'Reset labels', id: 'clear', go: 'Reset', about: '',
      act() { host.setLabel('', ''); rebuild(); },
    },
    ...(renaming ? state.pages.filter(one => !one.shelf).map(one => ({
      title: 'Page: ' + (one.name || one.head), id: 'page:' + one.key, go: 'Rename', about: '',
      act() { host.askLabel('Page/' + (one.name || one.head), one.head); },
    })) : []),
  ];
  return {key: 'developer', tab: 'Developer', head: 'Developer', sub: '', rows: rows.map(row => ({file: '開発', ...row}))};
}

/**
 * Rows whose built-in title changed (2026-11-15, plain app wording), by their new title: a label
 * saved under the old "Page/Title" key still applies. A label set from now on is kept under the new one.
 */
const FORMER_TITLES = {
  'Scan for games': 'Look for games again',
  'Allow file access': 'Allow access to your games',
  'Add game from another app': 'Add game',
  'Reading days': 'Days with reading',
  'Games started': 'Games begun',
  'Full statistics': 'History',
  'Date & time': 'Date and time',
  'No apps': 'No other apps',
  'Shortcut buttons': 'Buttons in a game',
  'CRT filter': 'CRT picture',
  'Search settings': 'Find a setting',
  'Home server': 'Home server services',
  'Search games': 'Find a title',
  'Sort by': 'Shelf order',
  'Show games': 'Games shown',
  'Add games folder': 'Add folder',
  'Reading settings': 'Reading',
  'OCR helper': 'OCR service',
};

/** The pages' rows called what the user called them, and, while renaming, their key asking for a name. */
function applyLabels(pages) {
  const mine = labels();
  for (const one of pages) {
    if (one.shelf) continue;
    one.name = one.name || one.head;
    const called = mine['Page/' + one.name];
    if (called) one.head = one.tab = called;
    if (one.key === 'developer') continue;
    for (const row of one.rows) {
      const key = one.name + '/' + row.title;
      const former = FORMER_TITLES[row.title];
      if (former && !mine[key] && mine[one.name + '/' + former]) mine[key] = mine[one.name + '/' + former];
      if (state.renaming) {
        const now = mine[key] || row.title;
        row.go = 'Rename';
        row.act = () => host.askLabel(key, now);
        delete row.step;
      }
      if (mine[key]) row.title = mine[key];
    }
  }
}

function settingsPage() {
  const theme = state.themes[state.theme];
  const open = (title, about, what) => ({title, about, go: 'Open', act() { host.open(what); }, id: what});
  const folders = JSON.parse(host.folders());
  const home = host.homeScreen?.();
  const homeRow = home ? [{
    title: 'Home screen', id: 'home',
    side: {on: 'On', waiting: 'Not set', off: 'Off'}[home], value: {on: 'On', waiting: 'Choose', off: 'Off'}[home],
    about: home === 'waiting' ? 'Choose Furoshiki as your home app' : '',
    go: {on: 'Turn off', waiting: 'Choose', off: 'Turn on'}[home],
    act() { if (home === 'waiting') host.system('home'); else host.setHomeScreen(home === 'off'); },
    ...(home === 'waiting' ? {also: 'Turn off', alsoAct() { host.setHomeScreen(false); }} : {}),
  }] : [];
  // The Sources page is the text server's: the dev line's alone ("Copy lines to clipboard" is on Reading).
  let sources = [];
  const user = host.achievementsUser?.() || '';
  const rows = [
    ...homeRow,
    {title: 'Add games folder', about: '', go: 'Add', act() { host.addFolder(); }, id: 'add'},
    ...addTitleRows(),
    {
      title: 'Theme', side: theme.name, about: '', value: theme.name, go: 'Change', id: 'look',
      act() {
        const keys = Object.keys(state.themes).sort((a, b) => state.themes[a].order - state.themes[b].order);
        state.theme = keys[(keys.indexOf(state.theme) + 1) % keys.length];
        try { localStorage.setItem('look', state.theme); } catch (e) { /* for this run alone */ }
        applyTheme();
        rebuild();
      },
    },
    {title: 'Icon', side: iconName(), about: '', value: iconName(), go: 'Change', act() { host.pickIcon(); }, id: 'icon'},
    {
      title: 'Text size', side: TEXT_SIZES[textSizeIndex()][0], about: '', value: TEXT_SIZES[textSizeIndex()][0], go: 'Change', id: 'text-size',
      act() { pick('Text size', TEXT_SIZES.map(one => one[0]), textSizeIndex(), at => setTextSize(at)); },
      step(direction) { setTextSize(textSizeIndex() + direction); },
    },
    // The reading layer's settings, by the pages Furoshiki has on a computer; each shows its few most
    // changed and keeps the rest behind More.
    open('Reading settings', 'Text panel, furigana, statistics in game, clipboard', 'settings:READING'),
    open('Popup', 'Popup theme, size', 'settings:POPUP'),
    open('Dictionaries', 'Yomitan dictionaries', 'dictionaries'),
    open('Cards', 'Anki, screenshot, audio', 'settings:CARDS'),
    ...sources,
    ...ocrRows(),
    // What each button does, for all consoles or each (map.js); the pointer's speed and the rest from there.
    // Game buttons and shortcuts, in the one place a shortcut is given a button.
    {title: 'Controller', about: 'Game buttons, shortcuts', go: 'Open', act() { openMap(); }, id: 'controller'},
    ...(host.haptics && host.hasVibrator?.() ? [{
      title: 'Haptics', id: 'haptics', side: host.haptics() ? 'On' : 'Off', value: host.haptics() ? 'On' : 'Off',
      about: '', go: host.haptics() ? 'Turn off' : 'Turn on',
      act() { host.setHaptics(!host.haptics()); rebuild(); },
    }] : []),
    ...wideRow(),
    open('CRT filter', '', 'crt'),
    // The search itself, not the page it is on: the row does what it says.
    {title: 'Search settings', about: '', go: 'Search', act() { findSetting(); }, id: 'settings'},
    // The Consoles page's own row among the groups: every console's settings, or one's.
    {
      title: 'Consoles', id: 'consoles-page', about: 'Resolution, aspect ratio, volume', go: 'Open',
      act() { setScope({console: '', game: '', name: 'All consoles', tag: 'All'}); goTo(state.pages.findIndex(one => one.key === 'consoles')); },
    },
    ...(host.saveSync?.() ? [open('Save sync', '', 'settings:SAVE_SYNC')] : []),
    // The folder each device writes its reading to and reads every other device's from (SharedStats.kt).
    ...(host.statsFolder ? [(() => {
      let folder = {};
      try { folder = JSON.parse(host.statsFolder()); } catch (e) { /* none */ }
      const devices = folder.devices || 0;
      return {title: 'Statistics folder', id: 'stats-folder', go: 'Edit', side: folder.path || '',
        about: !folder.access ? 'Needs access to all files' : devices ? `${devices} other device${devices === 1 ? '' : 's'}` : 'No other devices yet',
        act() { host.askStatsFolder(); }};
    })()] : []),
    ...(host.trackerAddress ? [{
      title: 'Study tracker', id: 'tracker', go: 'Edit', side: host.trackerAddress() || 'Not set', value: state.tracker ? 'On' : 'Off',
      about: state.tracker ? 'Last synced ' + ymdTime(new Date(state.tracker.at))
        : host.trackerAddress() ? 'Not connected' : '',
      act() { host.askTrackerAddress(); },
    }] : []),
    {title: 'Version', id: 'version', side: appVersion(), value: appVersion(), about: ''},
    ...updateRow(),
    {title: 'Open-source licences', id: 'licences', go: 'Open', about: '', act() { host.open('licences'); }},
    ...(host.services ? [{
      title: 'Home server', id: 'services', go: 'Refresh', side: servicesText(), value: servicesText() ? 'Connected' : 'Not connected',
      about: '',
      act() { host.askServices(); say('Checking home server…'); },
    }] : []),
    {
      title: 'RetroAchievements', side: user || 'Not logged in', value: user || 'Off',
      // Logged in: the same box logs out.
      about: '', go: user ? 'Account' : 'Log in', id: 'achievements',
      act() { host.achievementsLogin?.(); },
    },
    {
      title: 'Search games', about: '', go: 'Search', id: 'find',
      act() { findTitle(); },
    },
    {
      title: 'Sort by', side: orderText(), value: ORDERS[orderKeys()[0]?.[0] || 'last'][0], go: 'Change', id: 'order',
      about: '',
      act: pickOrder,
    },
    {
      title: 'Show games', side: host.allGames?.() ? 'All games' : 'With a game script only', value: host.allGames?.() ? 'All' : 'With script',
      about: '', go: 'Change', id: 'all',
      act() { host.setAllGames?.(!host.allGames?.()); rebuild(); },
    },
    {
      title: 'Game scripts', about: 'Scripts, names and box art for each game',
      go: 'Open', id: 'scripts',
      // Every console's games in one list (GameScriptsActivity), its consoles a filter there, as the
      // desktop's page.
      act() { host.openScripts(''); },
    },
    // GameNative's Frontend Sync (and Winlator's shortcuts) leave a file a game in a folder: that
    // folder added, each game is in the library and Start plays it at once (companion/FrontendExports).
    // Offered where GameNative is installed.
    ...(host.addTitle && host.hasGameNative?.() ? [{
      title: 'Add GameNative folder', about: 'GameNative\'s Frontend Sync folder', go: 'Add',
      act() { host.addFolder(); }, id: 'add-exports',
    }] : []),
    // A folder by what it is, with enough of its path to tell it from the others; the whole path on its card.
    ...folders.map(folder => ({
      title: 'Games folder', side: shortPath(folder), value: shortPath(folder), about: folder, go: 'Remove', id: folder, folder: true,
      act() {
        pick('Remove this folder from the library?', ['Remove', 'Cancel'], 1, at => {
          if (at === 0) { host.removeFolder(folder); Front.reload(); }
        }, folder + '\nThe games stay on the device.', {cancel: false});
      },
    })),
    {title: 'Scan for games', about: '', go: 'Scan', act() { scanForGames(); }, id: 'scan'},
  ];
  return groupedSettings(rows.map(row => ({file: '設定', ...row})));
}

/**
 * Search settings: every setting by its name, wherever it is. The page's own rows (each group's,
 * and the Consoles page's), and every row of the reading layer's pages (host.settingsIndex:
 * Reading, Popup, Cards, Controller). A row found is opened where it is: a page's own is done
 * as its key does, another's opens its page at it.
 */
function findSetting() {
  // A row of the Settings page shown: its group opened, the row in hand, for the user to change.
  const showRow = (group, id) => {
    goTo(state.pages.findIndex(one => one.key === 'settings'));
    if (group) { openSettingsGroup(group, id); return; }
    state.chosen.settings = id;
    setInList(true);
    draw();
  };
  // The page's own rows, by what they are called now, each with the group it is in.
  const own = [];
  const before = state.settingsGroup;
  state.settingsGroup = null;
  for (const group of SETTING_GROUPS) {
    state.settingsGroup = group.key;
    const page = settingsPage();
    // A group of one row is not opened by the page (it is that row): its row is among the groups.
    for (const row of page.rows) {
      if (row.id === 'group-back' || String(row.id).startsWith('group:') || row.id === 'settings' || !row.act) continue;
      if (!own.some(one => one.id === row.id)) own.push({id: row.id, title: row.title, where: 'Settings · ' + group.title, act() { showRow(group.key, row.id); }});
    }
  }
  state.settingsGroup = null;
  for (const row of settingsPage().rows) {
    if (String(row.id).startsWith('group:') && row.act && !own.some(one => one.title === row.title)) {
      own.push({id: row.id, title: row.title, where: 'Settings', act() { showRow('', row.id); }});
    }
  }
  state.settingsGroup = before;
  const scope = state.scope;
  state.scope = {console: '', game: '', name: 'All consoles', tag: 'All'};
  for (const row of consolesPage().rows.slice(1)) {
    own.push({id: 'consoles:' + row.id, title: row.title, where: 'Consoles', act() {
      setScope({console: '', game: '', name: 'All consoles', tag: 'All'});
      state.chosen.consoles = row.id;
      goTo(state.pages.findIndex(one => one.key === 'consoles'));
      setInList(true);
      draw();
    }});
  }
  state.scope = scope;
  let theirs = [];
  try { theirs = JSON.parse(host.settingsIndex?.() || '[]'); } catch (e) { /* the page's own alone */ }
  const all = [...own, ...theirs.map(one => ({title: one.title, where: one.page, act() { host.openSetting(one.section, one.key); }}))];

  state.picking = {slider: true};
  $('pick-title').textContent = 'Search settings';
  $('pick-note').textContent = '';
  const list = $('pick-rows');
  list.textContent = '';
  const line = document.createElement('li');
  const field = document.createElement('input');
  field.type = 'search';
  field.placeholder = 'Setting name';
  line.append(field);
  list.append(line, cancelRow());
  field.oninput = () => {
    while (list.children.length > 2) list.children[1].remove();
    const wanted = field.value.trim().toLowerCase();
    if (!wanted) return;
    // A word that begins so first, then any that holds it.
    const begins = all.filter(one => one.title.toLowerCase().split(' ').some(word => word.startsWith(wanted)));
    const holds = all.filter(one => !begins.includes(one) && (one.title + ' ' + one.where).toLowerCase().includes(wanted));
    for (const one of [...begins, ...holds].slice(0, 14)) {
      const item = document.createElement('li');
      item.setAttribute('role', 'button');
      item.textContent = `${one.title} · ${one.where}`;
      item.onclick = () => { closePick(); one.act(); };
      list.lastChild.before(item);
    }
  };
  $('pick').hidden = false;
  field.focus();
}

/** The icon's name without its number ("Solarized", not "30 Solarized"). */
const iconName = () => String(host.icon?.() || '').replace(/^\d+\s+/, '');

/** This app's version, for About. */
function appVersion() {
  try { return host.version?.() || JSON.parse(host.release?.() || host.update?.() || '{}').mine || ''; } catch (e) { return ''; }
}

/**
 * The settings by kind rather than one long list (the maintainer, 2026-11-17: "categorised
 * instead of one big list"): the page lists Search settings and the groups, a group opens its
 * rows, and its first row or B goes back. A row is put in its group by its id. A group of one row
 * is that row: it opens that row's page at once (no "Cards › Cards").
 */
// The same groups in the same order as the desktop program's sections (2026-11-20), Dictionaries and
// Game scripts among them as there (the maintainer, 2026-11-24: "i would like the main settings
// structure to match pc, with scripts and dictionaries on the main page"). A group of one row opens
// that row's page at once. The desktop's Keys (a keyboard's) has no group here.
const SETTING_GROUPS = [
  {key: 'library', title: 'Library', ids: ['add', 'add-title', 'find', 'order', 'all', 'add-exports', 'scan']},
  {key: 'text-ocr', title: 'Text & OCR', ids: ['settings:READING', 'ocr-engine', 'ocr-service', 'settings:SOURCES']},
  {key: 'dictionaries', title: 'Dictionaries', ids: ['dictionaries']},
  {key: 'scripts', title: 'Game scripts', ids: ['scripts']},
  {key: 'popup', title: 'Popup', ids: ['settings:POPUP']},
  {key: 'anki', title: 'Anki', ids: ['settings:CARDS']},
  {key: 'consoles', title: 'Consoles', ids: ['consoles-page']},
  {key: 'controls', title: 'Controls', ids: ['controller', 'haptics']},
  {key: 'display', title: 'Display', ids: ['look', 'icon', 'text-size', 'wide', 'crt', 'home']},
  {key: 'about', title: 'Sync & About', ids: ['settings:SAVE_SYNC', 'stats-folder', 'tracker', 'services', 'achievements', 'version', 'update', 'licences']},
];

function groupedSettings(rows) {
  const groupOf = row => row.folder ? 'library'
    : SETTING_GROUPS.find(group => group.ids.includes(row.id))?.key || (row.id === 'settings' ? '' : 'library');
  const open = SETTING_GROUPS.find(group => group.key === state.settingsGroup);
  const insideOf = group => rows.filter(row => groupOf(row) === group.key);
  if (open && insideOf(open).length > 1) {
    const back = {title: '‹ Settings', id: 'group-back', about: '', go: 'Back', file: '設定', act() { closeSettingsGroup(); }};
    return {key: 'settings', tab: 'Settings', head: 'Settings · ' + open.title, sub: '',
      rows: [back, ...insideOf(open)]};
  }
  const top = rows.filter(row => groupOf(row) === '');
  const groups = SETTING_GROUPS.map(group => {
    const inside = insideOf(group);
    if (!inside.length) return null;
    // One row: the group is that row, under the group's name, with what the row says and does.
    if (inside.length === 1) {
      const only = inside[0];
      return {...only, title: group.title, id: 'group:' + group.key, about: only.about || (only.title !== group.title ? only.title : '')};
    }
    return {
      title: group.title, id: 'group:' + group.key, file: '設定', go: 'Open',
      side: String(inside.length), about: [...new Set(inside.map(row => row.title))].join(', '),
      act() { openSettingsGroup(group.key, inside[0]?.id); },
    };
  }).filter(Boolean);
  return {key: 'settings', tab: 'Settings', head: 'Settings', sub: '', rows: [...top, ...groups]};
}

function openSettingsGroup(key, first) {
  state.settingsGroup = key;
  state.chosen.settings = first;
  setInList(true);
  state.arriving = true;
  rebuild();
}

function closeSettingsGroup() {
  const was = state.settingsGroup;
  state.settingsGroup = null;
  state.chosen.settings = 'group:' + was;
  state.arriving = true;
  rebuild();
}

// ---- Drawing ----

const page = () => state.pages[state.page];

function showCover(path) {
  const art = $('art'), cover = $('cover');
  art.classList.toggle('boxed', !!path);
  $('card').classList.toggle('boxed', !!path);
  if (!path) {
    cover.removeAttribute('src');
    cover.classList.remove('ready');
    return;
  }
  const source = pictureSource(path);
  if (cover.getAttribute('src') !== source) {
    // Another picture: unseen until it is read, then it fades in.
    cover.classList.remove('ready');
    cover.onload = () => cover.classList.add('ready');
    cover.onerror = () => { cover.classList.remove('ready'); art.classList.remove('boxed'); $('card').classList.remove('boxed'); };
    cover.src = source;
  }
}

/** A picture's address: a file on the device by its path, or one the build carries (covers/…, spines/…). */
const pictureSource = path => path.startsWith('/') ? 'file://' + path.split('/').map(encodeURIComponent).join('/') : path;

/** Whether a page of rows has its rows in hand (else the pages' rail): body.rail marks the latter. */
function setInList(on) {
  state.inList = !!on;
  if (!LOWER) document.body.classList.toggle('rail', !on && !page()?.shelf);
}

function chosenIndex() {
  const current = page();
  const id = state.chosen[current.key];
  const at = current.rows.findIndex(row => row.id === id);
  return at >= 0 ? at : 0;
}

/** A page of the library: a shelf, or the page an empty library has in their place. */
const inLibrary = one => !!one?.shelf || one?.key === 'library';

/**
 * The places down the left edge, as Furochibi has them (the maintainer, 2026-11-27: "bring the
 * library, settings, stats tabs to furoshiki ... furochibi's layout with the michroma sidetabs"):
 * Library (every shelf), then each page that is not a shelf. The Consoles page is under Settings.
 */
function places() {
  const current = page();
  const shelf = state.pages.findIndex(one => one.key === state.lastShelf);
  const list = [{name: 'Library', at: shelf >= 0 ? shelf : Math.max(state.pages.findIndex(inLibrary), 0), on: inLibrary(current)}];
  state.pages.forEach((one, index) => {
    if (inLibrary(one) || one.hidden) return;
    list.push({name: one.tab, at: index, on: index === state.page || (!!current?.hidden && one.key === 'settings')});
  });
  return list;
}

function drawRail() {
  const rail = $('rail');
  if (!rail || LOWER) return;
  rail.textContent = '';
  const mark = letter => Object.assign(document.createElement('span'), {className: 'lr', textContent: letter});
  rail.append(mark('L'));
  for (const place of places()) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = place.on ? 'on' : '';
    button.setAttribute('aria-current', place.on ? 'page' : 'false');
    button.append(Object.assign(document.createElement('b'), {textContent: place.name}));
    button.onclick = () => goTo(place.at);
    rail.append(button);
  }
  rail.append(mark('R'));
}

/** The next place down the rail (1) or the one before (-1), round the ends. */
function goPlace(by) {
  const list = places();
  const at = Math.max(list.findIndex(one => one.on), 0);
  goTo(list[(at + by + list.length) % list.length].at);
}

/**
 * The console in hand on the first of two displays, as a heading with the one before and after it
 * beside it (the maintainer, 2026-11-27: "B is good"): left and right go along the consoles, and
 * this is what they go along. One display keeps the words along the foot.
 */
function drawConsoleHead() {
  const box = $('conhead');
  if (!box) return;
  const shown = !LOWER && document.body.classList.contains('two') && inLibrary(page());
  box.hidden = !shown;
  if (!shown) return;
  const shelves = state.pages.map((one, index) => ({one, index})).filter(({one}) => inLibrary(one));
  const at = shelves.findIndex(({index}) => index === state.page);
  const before = shelves[at - 1], after = shelves[at + 1];
  const current = page();
  const count = current.rows?.filter(row => row.game).length || 0;
  box.innerHTML = `<button type="button" class="cn before"${before ? '' : ' disabled'}><span class="key">←</span><span>${before ? before.one.tab : ''}</span></button>
    <div class="cname"><b>${current.tab}</b><span>${current.sub && current.sub !== current.tab ? current.sub + ' · ' : ''}${count} game${count === 1 ? '' : 's'}</span></div>
    <button type="button" class="cn after"${after ? '' : ' disabled'}><span>${after ? after.one.tab : ''}</span><span class="key">→</span></button>
    <div class="pips">${shelves.map((_, i) => `<i class="${i === at ? 'on' : ''}"></i>`).join('')}</div>`;
  if (before) box.querySelector('.before').onclick = () => goTo(before.index);
  if (after) box.querySelector('.after').onclick = () => goTo(after.index);
}

function drawFoot() {
  drawRail();
  drawConsoleHead();
  const tabs = $('tabs');
  tabs.textContent = '';
  // The foot is the library's: its shelves as words. A page of the rail has none.
  document.body.classList.toggle('nofoot', !inLibrary(page()));
  state.pages.forEach((one, index) => {
    if (!inLibrary(one)) return;
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = one.tab;
    button.className = index === state.page ? 'on' : '';
    button.onclick = () => goTo(index);
    tabs.append(button);
  });
  // The chosen tab in sight, and again once the type is loaded, which makes the tabs wider.
  const inSight = () => tabs.querySelector('.on')?.scrollIntoView({inline: 'center', block: 'nearest'});
  inSight();
  document.fonts?.ready.then(inSight);
}

function drawRows() {
  const current = page();
  const list = $('rows');
  list.textContent = '';
  $('main').classList.toggle('shelf', !!current.shelf);
  const chosen = chosenIndex();
  current.rows.forEach((row, index) => {
    const item = rowElement(row, index, index === chosen);
    // A page that has just come: its rows arrive one after the other, the first dozen or so.
    if (state.arriving) {
      item.classList.add('arrive');
      item.style.animationDelay = Math.min(index, 16) * 16 + 'ms';
    }
    list.append(item);
    if (index === chosen) openSpine(item, row);
  });
  state.arriving = false;
}

/** The mark on another row of the list as it stands: the rows are not made again. */
function moveCursor(index) {
  const rows = $('rows').children;
  const next = rows[index];
  if (!next) return false;
  for (const row of rows) row.classList.toggle('on', row === next);
  openSpine(next, page().rows[index]);
  return true;
}

/**
 * The spine in hand opens: its box's front beside it, as tall as the shelf and as wide as its own
 * shape makes it, the spines after it gliding aside (the desktop's open spine). Drawn where the
 * shelf has the whole display (two displays: the title's words and keys are on the second).
 */
function openSpine(item, row) {
  if (!item || !row?.game || !row.cover) return;
  let plate = item.querySelector('img.plate');
  if (!plate) {
    plate = document.createElement('img');
    plate.className = 'plate';
    plate.alt = '';
    const fit = () => {
      if (!plate.naturalHeight) return;
      // A picture wider than tall (a game's own art, no box) lies across: as wide as a box stands tall, no more.
      plate.classList.toggle('lying', plate.naturalWidth > plate.naturalHeight * 1.1);
      // As wide as the picture stands at the shelf's height, now and whenever that changes.
      const measure = () => {
        if (!plate.offsetWidth) return;
        item.style.setProperty('--plate', plate.offsetWidth + 'px');
        item.classList.add('plated');
      };
      new ResizeObserver(measure).observe(plate);
      measure();
    };
    plate.onload = fit;
    plate.onerror = () => plate.remove();
    plate.src = pictureSource(row.cover);
    item.append(plate);
    if (plate.complete) fit();
  }
}

/** Plays an element's arriving again (the card's and the page's fades). */
function fresh(element, name) {
  element.classList.remove(name);
  void element.offsetWidth;
  element.classList.add(name);
}

function rowElement(row, index, on) {
  const item = document.createElement('li');
  item.className = 'row' + (on ? ' on' : '');
  item.setAttribute('role', 'button');
  const no = document.createElement('span');
  no.className = 'no';
  no.textContent = String(index + 1).padStart(2, '0');
  const name = document.createElement('span');
  name.className = 'name';
  putText(name, row.title);
  const side = document.createElement('span');
  side.className = 'side' + (row.same ? ' same' : '');
  side.textContent = (row.side || '').replace(/\n/g, ' · ');
  // On a shelf: the console's colour at the spine's foot.
  const band = document.createElement('i');
  band.className = 'band';
  if (row.band) band.style.setProperty('--band', row.band);
  item.append(no, name, side, band);
  if (row.read) {
    const mark = document.createElement('i');
    mark.className = 'mark ' + row.read;
    // In words on the game's card too (its "Read by" line): a finger never sees a title.
    mark.title = 'Read by: ' + READ_BY[row.read];
    mark.setAttribute('aria-label', mark.title);
    mark.innerHTML = row.read === 'ocr'
      ? '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6"><circle cx="6.5" cy="14" r="3.6"/><circle cx="17.5" cy="14" r="3.6"/><path d="M10.1 14h3.8M2.9 14 4.4 8M21.1 14 19.6 8"/></svg>'
      : row.read === 'script' ? '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"><path d="M4.5 12.5 10 18 19.5 6.5"/></svg>' : '?';
    item.append(mark);
  }
  if (row.game) {
    // Uniform per-console generated horizontal game cases (Iteration A: Authentic Studio Cases).
    const key = (row.game.console || 'other').toLowerCase();
    const shortCap = {
      psp: 'PSP', ps2: 'PS2', ps1: 'PS1', ds: 'DS', nds: 'DS', '3ds': '3DS',
      switch: 'SWI', swi: 'SWI', eden: 'SWI', dc: 'DC', dreamcast: 'DC',
      psvita: 'VITA', vita: 'VITA', app: 'APP',
    }[key] || tabName(row.game.console, row.game.system).slice(0, 4).toUpperCase();
    const box = document.createElement('span');
    box.className = 'gen-spine';
    box.dataset.console = key;
    const capL = document.createElement('span');
    capL.className = 'cap-l';
    capL.textContent = shortCap;
    const body = document.createElement('span');
    body.className = 'spine-body';
    putText(body, row.title);
    box.append(capL, body);
    const id = (row.game.id || '').trim();
    if (id && id.length <= 12 && !/^(APP|HASH|FILE|CUSTOM)-/i.test(id)) {
      const capR = document.createElement('span');
      capR.className = 'cap-r';
      capR.textContent = id;
      box.append(capR);
    }
    item.classList.add('cased');
    item.append(box);
  }
  if (row.game && state.playing?.path === row.game.path) item.classList.add('playing');
  item.onclick = () => choose(index, true);
  return item;
}

function drawCard() {
  const current = page();
  const row = current.rows[chosenIndex()];
  // Where this is: the library's shelf of a console, or a page.
  const head = $('tab-text');
  head.textContent = '';
  const where = document.createElement('i');
  where.textContent = current.shelf ? 'Library · ' : 'Furoshiki · ';
  head.append(where, current.shelf ? current.sub : current.head);
  // "01 / 73", then a game's own ID where it has one a user knows, whose figures, and whether it is in play.
  $('file').textContent = current.shelf
    ? [counter(chosenIndex() + 1, current.rows.length), row?.file, row?.from,
      row?.game && state.playing?.path === row.game.path ? 'Now playing' : ''].filter(Boolean).join(' · ')
    : counter(chosenIndex() + 1, current.rows.length);
  putText($('title'), row?.title || '');
  putText($('sub'), row?.about || '');
  const figures = $('figures');
  figures.textContent = '';
  for (const [label, value] of row?.figures || []) {
    const term = document.createElement('dt');
    term.textContent = label;
    const definition = document.createElement('dd');
    definition.textContent = value;
    figures.append(term, definition);
  }
  $('value').textContent = current.shelf ? '' : row?.value || '';
  showFar($('far'), current.shelf ? row?.far : -1);
  const bars = $('bars');
  bars.textContent = '';
  const days = row?.bars;
  if (days && days.length) {
    const most = Math.max(...days.map(day => day.chars), 1);
    for (const day of days.slice().reverse()) {
      const bar = document.createElement('i');
      bar.style.height = Math.round(day.chars * 100 / most) + '%';
      bars.append(bar);
    }
  }
  const go = $('go');
  go.hidden = !row?.act;
  go.textContent = row?.go || '';
  const also = $('also');
  also.hidden = !row?.also;
  also.textContent = row?.also || '';
  $('pin').hidden = !row?.pin;
  // Last: the cover is fitted in the room the keys leave it.
  showCover(current.shelf ? row?.cover : '');
}

/** How far through a title: a hairline filled to what was read, where the tracker knows its length. */
function showFar(line, share) {
  line.hidden = !(share >= 0);
  line.firstElementChild.style.width = Math.round(Math.max(0, Math.min(1, share || 0)) * 100) + '%';
}

// ---- The stage: the title in hand as a page of its own (the desktop's PaintStage) ----

function setStage(on) {
  const row = page().rows[chosenIndex()];
  state.stage = !!on && !!page().shelf && !!row?.game;
  drawStage();
  if (state.stage) fresh($('stage'), 'fresh-page');
}

function fact(label, value, from) {
  const line = document.createElement('div');
  line.className = 'fact';
  const term = document.createElement('dt');
  term.textContent = label;
  const said = document.createElement('dd');
  if (value instanceof Node) said.append(value); else said.textContent = value;
  line.append(term, said);
  if (from) {
    const whose = document.createElement('span');
    whose.className = 'from';
    whose.textContent = from;
    line.append(whose);
  }
  return line;
}

function drawStage() {
  document.body.classList.toggle('staged', state.stage);
  if (!state.stage) return;
  const current = page();
  const row = current.rows[chosenIndex()];
  const game = row?.game;
  if (!game) { state.stage = false; document.body.classList.remove('staged'); return; }
  const record = recordOf(game), tracked = trackedOf(game);
  $('stage-where').textContent = [counter(chosenIndex() + 1, current.rows.length), shownId(game)].filter(Boolean).join(' · ');
  const name = $('stage-name');
  putText(name, row.title);
  name.classList.toggle('latin', !hasJapanese(row.title));
  putText($('stage-own'), row.about || '');
  $('stage-go').textContent = row.go;
  // The second key says what it does for this game ("Game settings"; "Remove from library" for another app's).
  $('stage-settings').textContent = row.also || 'Game settings';
  $('stage-settings').hidden = !row.alsoAct;
  const day = lastDay(game);
  $('stage-last').textContent = day ? 'Last read ' + dayText(day) : 'Not started';
  showFar($('stage-far'), shareOf(game));
  $('stage-log').hidden = true;

  const facts = $('stage-facts');
  facts.textContent = '';
  // A console's game says its console; another app's game its app.
  if (game.console === 'app') facts.append(fact('App', game.via || 'Another app'));
  else facts.append(fact('Console', fullName(game.console, game.system)));
  facts.append(fact('Read by', READ_BY[row.read] || READ_BY.unset));
  if (tracked) {
    facts.append(fact('Read', tracked.total > 0
      ? `${number(tracked.read)} of ${number(tracked.total)} characters · ${Math.round(100 * tracked.read / tracked.total)}%`
      : `${number(tracked.read)} characters`, 'Study tracker'));
    if (tracked.hours > 0) {
      facts.append(fact('Time', [playTime(tracked.hours * 3.6e6), tracked.speed > 0 ? number(tracked.speed) + '/h' : '',
        tracked.sessions > 0 ? tracked.sessions + ' sessions' : ''].filter(Boolean).join(' · ')));
    }
    const jiten = [tracked.difficulty > 0 ? 'Difficulty ' + tracked.difficulty.toFixed(2) : '', tracked.unique > 0 ? number(tracked.unique) + ' unique words' : '',
      tracked.coverage > 0 ? Math.round(tracked.coverage) + '% known' : ''].filter(Boolean);
    if (jiten.length) facts.append(fact('Jiten', jiten.join(' · ')));
  }
  if (game.chars > 0) {
    const hours = (game.activeMs || 0) / 3.6e6;
    const here = [number(game.chars) + ' characters', playTime(game.activeMs), hours > .05 ? number(Math.round(game.chars / hours)) + '/h' : ''].filter(Boolean).join(' · ');
    facts.append(fact(tracked ? 'This device' : 'Read', here, tracked ? '' : 'This device'));
  }
  const other = state.elsewhere?.[game.id];
  if (other && other.day > (game.last || '')) facts.append(fact('Last played on', `${other.device} · ${dayText(other.day)}`));
  // The guides are the dev line's: they come with the home server's services (host.services).
  if (record?.guide && host.services) facts.append(fact('Guide', record.guide.replace(/-/g, ' ')));
  let reading = {days: [], lines: 0};
  try { reading = JSON.parse(host.reading?.(game.id) || '{}'); } catch (error) { console.error(error); }
  if (reading.days?.length) {
    // What was read of it here on each of its last days, the oldest at the left: bars to scale.
    const most = Math.max(...reading.days.map(one => one.chars), 1);
    const bars = document.createElement('span');
    bars.className = 'days';
    for (const one of reading.days.slice().reverse()) {
      const bar = document.createElement('i');
      bar.style.height = Math.max(6, Math.round(one.chars * 100 / most)) + '%';
      bars.append(bar);
    }
    facts.append(fact('30 days', bars, `Best day ${number(most)}`));
  }
  if (reading.lines > 0) {
    facts.append(fact('Backlog', `${number(reading.lines)} lines`));
    $('stage-log').hidden = false;
  }
  // Its people: as the game writes them, and how they are said.
  (record?.people || []).forEach((person, index, all) => {
    const who = document.createElement('span');
    who.className = 'person';
    const said = document.createElement('small');
    said.textContent = person.romaji;
    who.append(person.name, said);
    who.lang = 'ja';
    said.lang = 'en';
    facts.append(fact(index === 0 ? 'Names' : '', who, index === all.length - 1 && record.more > 0 ? `+ ${record.more} more` : ''));
  });

  const art = $('stage-art'), cover = $('stage-cover');
  art.classList.remove('boxed');
  if (row.cover) {
    cover.onload = () => art.classList.add('boxed');
    cover.onerror = () => art.classList.remove('boxed');
    const source = pictureSource(row.cover);
    if (cover.getAttribute('src') !== source) cover.src = source; else if (cover.complete && cover.naturalWidth) art.classList.add('boxed');
  } else {
    cover.removeAttribute('src');
  }
}

function draw() {
  if (!LOWER) document.body.classList.toggle('rail', !state.inList && !page().shelf);
  drawFoot();
  drawRows();
  drawCard();
  drawStage();
  drawStats();
  mirror();
  document.querySelector('.row.on')?.scrollIntoView({block: 'nearest', inline: 'nearest'});
}

/**
 * What the second display's own pages need (Lower.page): the game in hand with its shelf's
 * neighbours, the Statistics page's period and goal, or the sections of Settings. Null for the
 * other pages, which keep the card.
 */
function lowerPlace(current, row) {
  if (inLibrary(current) && current.shelf) {
    const shelves = state.pages.map((one, index) => ({tab: one.tab, index, on: index === state.page})).filter(one => inLibrary(state.pages[one.index]));
    const at = shelves.findIndex(one => one.on);
    let reading = {};
    if (row?.game) { try { reading = JSON.parse(host.reading?.(row.game.id) || '{}'); } catch (e) { /* none */ } }
    const record = row?.game ? recordOf(row.game) : null;
    return {kind: 'library', before: shelves[at - 1] || null, after: shelves[at + 1] || null, shelf: current.tab,
      title: row?.title || '', own: record?.title && record.title !== row?.title ? record.title : '',
      file: $('file').textContent, figures: row?.figures || [], far: row?.far ?? -1, cover: row?.cover || '',
      last: reading.last || '', days: (reading.days || []).slice(0, 14).reverse().map(one => one.chars),
      go: row?.go || '', also: row?.also || '', backlog: (reading.lines || 0) > 0};
  }
  if (current.view === 'stats') {
    const at = state.statsAt;
    return {kind: 'stats', range: at.range, periods: PERIODS, goal: at.open === 'goal' ? at.kind : '',
      digits: at.open === 'goal' ? at.digits : [], wheel: at.wheel, step: at.open === 'goal' ? GOALS[at.kind][0] : 0,
      goals: Object.fromEntries(Object.keys(GOALS).map(kind => [kind, goalOf(kind)]))};
  }
  if (current.key === 'settings') {
    const groups = SETTING_GROUPS.map(group => {
      const row = state.settingsGroup ? null : current.rows.find(one => one.id === 'group:' + group.key);
      return {key: group.key, title: group.title, side: row ? (row.value || row.side || '') : '', on: state.settingsGroup === group.key};
    });
    return {kind: 'settings', groups, open: state.settingsGroup || ''};
  }
  return null;
}

/** What the second display's page shows: the row in hand, its keys, the shelves and pages. */
function mirror() {
  if (LOWER || !host.lower) return;
  const current = page();
  const row = current.rows[chosenIndex()];
  const theme = state.themes[state.theme] || {};
  host.lower(JSON.stringify({
    size: textSizeIndex(),
    theme: Object.fromEntries(['ground', 'raised', 'ink', 'muted', 'accent', 'rule'].map(key => [key, theme[key]])),
    head: current.shelf ? current.sub : current.head, shelf: !!current.shelf,
    file: $('file').textContent, title: row?.title || '', sub: row?.about || '', value: current.shelf ? '' : row?.value || '',
    figures: row?.figures || [], go: row?.act ? row.go || '' : '', also: row?.also || '', cover: current.shelf ? row?.cover || '' : '',
    tabs: state.pages.map(one => one.tab), shelves: state.pages.map(one => !!one.shelf), page: state.page, steps: !!row?.step,
    at: chosenIndex(), count: current.rows.length, far: current.shelf ? row?.far ?? -1 : -1,
    // A page of settings: its rows too, each a key for a finger on the second display.
    rows: current.shelf ? null : current.rows.map(one => ({title: one.title || '', value: one.value || ''})),
    place: lowerPlace(current, row),
  }));
}

function rebuild() {
  makePages();
  draw();
  tick();
}

// ---- Going about ----

function goTo(index) {
  const before = state.page;
  state.stage = false;
  // A page of rows comes with the pages' rail in hand: up and down go on along it (the maintainer,
  // 2026-11-17: down at Statistics "just moves down through the stats page"). A or left takes the
  // rows in hand, B gives them back.
  setInList(false);
  state.page = (index + state.pages.length) % state.pages.length;
  // Another page: the settings' group it had open is shut.
  if (state.page !== before && state.settingsGroup) {
    state.settingsGroup = null;
    makePages();
  }
  if (page().shelf) state.lastShelf = page().key;
  state.arriving = state.page !== before;
  document.documentElement.style.setProperty('--dir', index >= before ? '1' : '-1');
  draw();
  $('rows').scrollTop = 0;
  document.querySelector('.row.on')?.scrollIntoView({block: 'nearest'});
  if (state.page !== before) fresh($('main'), 'fresh-page');
}

/** A row chosen by a finger: the card shows it. A second tap on it does what its button does. */
function choose(index, tapped) {
  const current = page();
  const row = current.rows[index];
  if (!row) return;
  if (tapped && index === chosenIndex() && state.chosen[current.key] === row.id) {
    // The spine in hand tapped again: its stage. Any other row: what its key does.
    (row.open || row.act)?.();
    return;
  }
  if (!current.shelf) setInList(true);
  const moved = state.chosen[current.key] !== row.id && index !== chosenIndex();
  document.documentElement.style.setProperty('--dir', index >= chosenIndex() ? '1' : '-1');
  state.chosen[current.key] = row.id;
  if (row.game) state.lastGame = row.game;
  if (!moveCursor(index)) drawRows();
  drawCard();
  drawStage();
  mirror();
  if (moved) fresh($('card'), 'fresh-card');
  document.querySelector('.row.on')?.scrollIntoView({block: 'nearest', inline: 'nearest', behavior: 'smooth'});
}

function say(text) {
  const box = $('say');
  box.textContent = text;
  box.classList.add('on');
  clearTimeout(say.timer);
  say.timer = setTimeout(() => box.classList.remove('on'), 3000);
}

function swipes() {
  let start = null;
  const area = $('main');
  area.addEventListener('pointerdown', event => { start = [event.clientX, event.clientY]; });
  area.addEventListener('pointerup', event => {
    if (!start) return;
    const across = event.clientX - start[0], down = event.clientY - start[1];
    start = null;
    if (page().shelf) return; // a shelf's spines scroll sideways themselves
    if (Math.abs(across) > 70 && Math.abs(down) < 50) goTo(state.page + (across < 0 ? 1 : -1));
  });
}

const Front = {
  /** A line from the app, said along the foot. */
  said(text) { say(text); },
  reload() {
    try {
      state.gamesText = host.games();
      state.games = JSON.parse(state.gamesText);
      for (const game of state.games) if (game.console === 'app' && /gamenative/i.test(game.path + ' ' + (game.via || ''))) game.system = 'GameNative';
      state.stats = JSON.parse(host.stats());
      if (!Object.keys(state.titles).length) state.titles = JSON.parse(host.titles?.() || '{}');
      state.tracker = JSON.parse(host.tracker?.() || 'null');
      state.elsewhere = JSON.parse(host.elsewhere?.() || 'null');
      state.playing = JSON.parse(host.playing?.() || 'null');
    } catch (error) {
      console.error(error);
    }
    readStatus();
    setTimeout(runSetup, 600);
    rebuild();
  },
  /**
   * A console looked at: the page made again only where the games it lists changed. Each console's
   * look made it again twice, and the shelf shook under the user's hand (the maintainer, 2026-11-24:
   * "seems to cause jitter on the main menu as the scan happens").
   */
  listed() {
    let text = '';
    try { text = host.games(); } catch (e) { /* as it was */ }
    if (text === state.gamesText) return;
    Front.reload();
  },
  /** A title just added (Add a title): on its shelf, in hand. */
  added(path) {
    Front.reload();
    const game = state.games.find(one => one.path === path);
    if (!game) return;
    const at = state.pages.findIndex(one => one.key === 'console:' + game.system);
    if (at < 0) return;
    state.chosen[state.pages[at].key] = game.path;
    goTo(at);
    say(shelfName(game) + ' added');
  },
  /** The Home key with the page in front: the shelf, at the game in play or at its first. */
  home() {
    closePick();
    state.stage = false;
    const game = state.playing && state.games.find(one => one.path === state.playing.path);
    if (game) {
      const at = state.pages.findIndex(one => one.key === 'console:' + game.system);
      state.chosen[state.pages[at]?.key] = game.path;
      goTo(Math.max(at, 0));
    } else if (!page().shelf) {
      // No game in play: the shelf that was looked at last.
      goTo(Math.max(state.pages.findIndex(one => one.key === state.lastShelf), 0));
    }
  },
  /**
   * Android's Back: a list or box open over the page shuts first, then the page goes up one
   * level (a game's page to its shelf, a group of settings to Settings, a page to the library).
   * False from the top level: only there does Back leave the app (HomeActivity).
   */
  back() {
    if (window.MapScreen?.open) { MapScreen.key('Backspace'); return true; }
    if (state.picking) { closePick(); return true; }
    if (state.stage) { setStage(false); return true; }
    if (page().key === 'settings' && state.settingsGroup) { closeSettingsGroup(); return true; }
    if (page().hidden) { goTo(state.pages.findIndex(one => one.key === 'settings')); return true; }
    if (!page().shelf) {
      const to = Math.max(state.pages.findIndex(one => one.key === state.lastShelf), 0);
      if (to === state.page) return false;
      goTo(to);
      return true;
    }
    return false;
  },
  /** A scan under way: what it is looking through now ("PSP · 2 of 9"), or '' once it is done. */
  scanning(text) {
    const was = !!$('scan').textContent;
    $('scan').textContent = text ? 'Scanning · ' + text : '';
    // A scan is over: with nothing found, said where it stays (the empty library's row) and once
    // along the foot when the user asked for the scan.
    if (was && !text) {
      setTimeout(() => {
        let games = [];
        try { games = JSON.parse(host.games()); } catch (e) { /* as it was */ }
        const none = games.length === 0;
        if (none && state.scanAsked) say('No games found. Add the folder your games are in.');
        else if (state.scanAsked) say(games.length === 1 ? '1 game in the library' : games.length + ' games in the library');
        state.scanAsked = false;
        if (none !== !!state.scanFoundNone) { state.scanFoundNone = none; rebuild(); }
      }, 700);
    }
  },
  /** What a finger did on the second display's page. */
  remote(what) {
    const [kind, value] = String(what).split(':');
    const index = chosenIndex();
    if (kind === 'home') { Front.home(); return; }
    if (kind === 'go') page().rows[index]?.act?.();
    else if (kind === 'also') page().rows[index]?.alsoAct?.();
    else if (kind === 'add') addGame();
    else if (kind === 'askalso') {
      // A long press on the title in hand (the second display): its second key, asked first.
      const row = page().rows[index];
      if (row?.alsoAct) pick(row.title, [row.also, 'Cancel'], 1, at => { if (at === 0) row.alsoAct(); }, '', {cancel: false});
    }
    else if (kind === 'tab') goTo(Number(value));
    else if (kind === 'pick') {
      // A row tapped on the second display: in hand, and done if it was in hand already.
      const to = Number(value);
      if (to === index) (page().rows[to]?.open || page().rows[to]?.act)?.();
      else choose(to, false);
    }
    else if (kind === 'row') choose(Math.max(0, Math.min(page().rows.length - 1, index + Number(value))), false);
    else if (kind === 'step') page().rows[index]?.step?.(Number(value));
    // The second display's own pages (Lower.page).
    else if (kind === 'backlog') host.openLog?.(page().rows[index]?.game?.id || '');
    else if (kind === 'range') { state.statsAt.open = ''; setRange(Number(value)); }
    else if (kind === 'goal') { setInList(true); state.statsAt.focus = value === 'daily' ? 2 : 1; openGoal(value); }
    else if (kind === 'wheel') { const [wheel, by] = value.split('/').map(Number); turnWheel(wheel, by); }
    else if (kind === 'keep') closeGoal(true);
    else if (kind === 'cancel') closeGoal(false);
    else if (kind === 'section') {
      const at = state.pages.findIndex(one => one.key === 'settings');
      if (state.page !== at) goTo(at);
      if (state.settingsGroup) { state.settingsGroup = null; makePages(); }
      const first = page().rows.find(one => one.id === 'group:' + value);
      if (first?.act) first.act();
      else { const group = SETTING_GROUPS.find(one => one.key === value); if (group) openSettingsGroup(value, null); }
    }
    else if (kind === 'find') findSetting();
  },
  key(name, byFinger) {
    // A controller is in the hand: its hints (L, R, the buttons' letters) are shown from here on.
    if (!byFinger) document.body.classList.add('pad');
    // The controller mapping screen over the page takes the keys (map.js).
    if (window.MapScreen?.open) {
      MapScreen.key(name);
      return;
    }
    if (state.picking) {
      const picking = state.picking;
      if (name === 'Escape' || picking.slider) {
        if (name === 'Escape') closePick();
      } else if (name === 'ArrowUp' || name === 'ArrowDown') {
        const by = name === 'ArrowUp' ? -1 : 1;
        const last = picking.labels.length - (picking.cancel ? 0 : 1);
        let next = picking.at + by;
        while (picking.labels[next]?.head) next += by;
        if (next >= 0 && next <= last) {
          picking.at = next;
          drawPick();
        }
      } else if (name === 'Enter' || name === 'Play') {
        closePick();
        if (picking.at < picking.labels.length) picking.chosen(picking.at);
      }
      return;
    }
    const current = page();
    const index = chosenIndex();
    if (name === 'Play') {
      // Start on a controller: the title in hand at once, from its shelf or its stage.
      current.rows[index]?.act?.();
      return;
    }
    if (state.stage) {
      // The stage's keys, as the desktop's: A plays, left and right another title, up or B the shelf, down the Log.
      const row = current.rows[index];
      if (name === 'Enter') row?.act?.();
      else if (name === 'Other') row?.alsoAct?.();
      else if (name === 'ArrowLeft' || name === 'ArrowRight') {
        const next = index + (name === 'ArrowLeft' ? -1 : 1);
        if (next >= 0 && next < current.rows.length) choose(next, false);
      } else if (name === 'ArrowUp' || name === 'Escape') setStage(false);
      else if (name === 'ArrowDown' && !$('stage-log').hidden) host.openLog?.(row.game.id);
      else if (name === 'PageUp' || name === 'PageDown') goPlace(name === 'PageUp' ? -1 : 1);
      return;
    }
    if (current.view === 'stats' && statsKey(name)) return;
    if (!current.shelf && !state.inList) {
      // The rail in hand: along the pages, or into this one's rows.
      if (name === 'ArrowUp' || name === 'ArrowDown' || name === 'PageUp' || name === 'PageDown') {
        goPlace(name === 'ArrowUp' || name === 'PageUp' ? -1 : 1);
      } else if ((name === 'Enter' || name === 'ArrowRight') && current.rows.length) {
        // Into the page, which lies to the right of the rail.
        setInList(true);
        if (current.view === 'stats') drawStats();
      } else if (name === 'Escape') {
        if (host.homeScreen?.() === 'on') Front.home();
        else Front.back();
      }
      return;
    }
    if (current.key === 'settings' && state.settingsGroup && name === 'Escape') {
      // B in a group of settings: back to the groups.
      closeSettingsGroup();
      return;
    }
    if (!current.shelf && name === 'Escape') {
      setInList(false);
      return;
    }
    const step = current.rows[index]?.step;
    if (step && (name === 'ArrowLeft' || name === 'ArrowRight')) {
      step(name === 'ArrowLeft' ? -1 : 1);
      return;
    }
    if (current.shelf && (name === 'ArrowUp' || name === 'ArrowDown')) {
      // Option A editorial list: up and down move through titles, left and right switch console tabs.
      const next = index + (name === 'ArrowUp' ? -1 : 1);
      if (next >= 0 && next < current.rows.length) choose(next, false);
    } else if (current.shelf && (name === 'ArrowLeft' || name === 'ArrowRight')) {
      // Along the shelves only: the other places are the rail's (L and R).
      const next = state.page + (name === 'ArrowLeft' ? -1 : 1);
      if (inLibrary(state.pages[next])) goTo(next);
    } else if (name === 'ArrowUp' || name === 'ArrowDown') {
      const next = index + (name === 'ArrowUp' ? -1 : 1);
      if (next >= 0 && next < current.rows.length) choose(next, false);
    } else if (name === 'PageUp' || name === 'PageDown') {
      goPlace(name === 'PageUp' ? -1 : 1);
    } else if (name === 'ArrowLeft' && !current.shelf) {
      // Back to the rail, which lies to the left.
      setInList(false);
    } else if (name === 'Enter') {
      const row = current.rows[index];
      (row?.open || row?.act)?.();
    } else if (name === 'Other') {
      current.rows[index]?.alsoAct?.();
    } else if (name === 'Escape' && !current.shelf && host.homeScreen?.() === 'on') {
      // A console's way back: from any page to the shelf.
      Front.home();
    }
  },
};
window.Front = Front;

if (!LOWER) {
  addEventListener('keydown', event => Front.key(event.key));
  // A controller the device has or that is plugged in: its hints from the start.
  try { if (host.hasController?.()) document.body.classList.add('pad'); } catch (e) { /* none */ }
  addEventListener('resize', () => { fit(); tick(); mirror(); });
}
if (!LOWER) {
  const inHand = () => page().rows[chosenIndex()];
  $('stage-back').onclick = () => setStage(false);
  $('stage-go').onclick = () => inHand()?.act?.();
  $('stage-settings').onclick = () => inHand()?.alsoAct?.();
  $('stage-pin').onclick = () => inHand()?.pin?.();
  $('stage-log').onclick = () => host.openLog?.(inHand()?.game?.id || '');
  $('stage-before').onclick = () => Front.key('ArrowLeft', true);
  $('stage-after').onclick = () => Front.key('ArrowRight', true);
  $('pin').onclick = () => page().rows[chosenIndex()]?.pin?.();
  $('go').onclick = () => page().rows[chosenIndex()]?.act?.();
  $('also').onclick = () => page().rows[chosenIndex()]?.alsoAct?.();
  // One display: "+ Add game" at the end of the tabs (two displays have it on the second).
  $('add-game').hidden = false;
  $('add-game').onclick = () => addGame();
}
$('pick').onclick = event => { if (event.target === $('pick')) closePick(); };

// ---- The second display's page: shown what the first tells it, its keys told back ----

const Lower = {
  /** What the first display's page shows, and whether it is in front: with anything else there
   *  (another app, a game that leaves this display alone) this is the clock and the device's keys. */
  tell(json, away) {
    if (json) Lower.show(json);
    document.body.classList.toggle('away', !!away);
    Lower.status();
  },
  status() {
    // The second display's page beside the shelf (a Presentation) has no device figures to give.
    if (!host.status) return;
    let status = null;
    try { status = JSON.parse(host.status()); } catch (error) { console.error(error); }
    if (!status) return;
    $('status').textContent = $('away-status').textContent = statusWords(status);
    $('away-volume').textContent = percent(status.volume);
    $('away-brightness').textContent = percent(status.brightness);
    $('away-dim').hidden = !status.dim;
  },
  show(json) {
    const shown = JSON.parse(json);
    if (shown.size !== undefined && shown.size !== window.lowerTextSize) {
      window.lowerTextSize = shown.size;
      fit();
    }
    // Another title or row on the first display: this card comes in from the side it lies on.
    const moved = Lower.was && (Lower.was.title !== shown.title || Lower.was.page !== shown.page);
    if (moved) {
      document.documentElement.style.setProperty('--dir', shown.page !== Lower.was.page || shown.at >= Lower.was.at ? '1' : '-1');
      fresh($('card'), 'fresh-card');
    }
    Lower.was = {title: shown.title, page: shown.page, at: shown.at};
    const root = document.documentElement.style;
    for (const [key, value] of Object.entries(shown.theme || {})) if (value) root.setProperty('--' + key, value);
    $('main').classList.toggle('shelf', shown.shelf);
    const head = $('tab-text');
    head.textContent = '';
    const where = document.createElement('i');
    where.textContent = shown.shelf ? 'Library · ' : 'Furoshiki · ';
    head.append(where, shown.head);
    $('file').textContent = shown.file;
    putText($('title'), shown.title);
    putText($('sub'), shown.sub);
    $('value').textContent = '';
    $('card-text').dataset.value = shown.value || '';
    const figures = $('figures');
    figures.textContent = '';
    for (const [label, value] of shown.figures) {
      const term = document.createElement('dt');
      term.textContent = label;
      const definition = document.createElement('dd');
      definition.textContent = value;
      figures.append(term, definition);
    }
    showFar($('far'), shown.far ?? -1);
    const go = $('go');
    go.hidden = !shown.go;
    go.textContent = shown.go;
    const also = $('also');
    also.hidden = !shown.also;
    also.textContent = shown.also;
    // Before and after, for a finger: along the shelf, or a setting's value down and up.
    // Only a setting's value down and up: a title is chosen on the first display (or its tabs here).
    $('before').hidden = $('after').hidden = !shown.steps;
    $('before').dataset.what = shown.steps ? 'step:-1' : 'row:-1';
    $('after').dataset.what = shown.steps ? 'step:1' : 'row:1';
    showCover(shown.cover);
    // A page of settings: every row a key (a tap takes it in hand, another does it).
    const list = $('rows');
    const scrolled = list.scrollTop;
    list.textContent = '';
    document.body.classList.toggle('listed', !!shown.rows);
    (shown.rows || []).forEach((row, index) => {
      const item = document.createElement('li');
      item.className = 'lrow' + (index === shown.at ? ' on' : '');
      const name = document.createElement('span');
      name.textContent = row.title;
      const value = document.createElement('b');
      value.textContent = row.value;
      item.append(name, value);
      item.onclick = () => host.remote('pick:' + index);
      list.append(item);
    });
    list.scrollTop = scrolled;
    list.querySelector('.on')?.scrollIntoView({block: 'nearest'});
    const tabs = $('tabs');
    tabs.textContent = '';
    shown.tabs.forEach((label, index) => {
      if (index > 0 && shown.shelves && !shown.shelves[index] && shown.shelves[index - 1]) tabs.append(Object.assign(document.createElement('i'), {className: 'sep'}));
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = label;
      button.className = index === shown.page ? 'on' : '';
      button.onclick = () => host.remote('tab:' + index);
      tabs.append(button);
    });
    tabs.querySelector('.on')?.scrollIntoView({block: 'nearest'});
    // "+ Add game" asks which way first (a folder, another app's game, a scan): always offered.
    $('add-game').hidden = false;
    // Removing a title is kept behind a long press on it, not a key beside Continue.
    Lower.removeBehindPress = shown.shelf && /remove/i.test(shown.also || '');
    if (Lower.removeBehindPress) also.hidden = true;
    Lower.page(shown.place);
  },
  /**
   * The second display's own pages (the maintainer, 2026-11-27): the game in hand as a page with
   * its cover, last line and keys; the Statistics page's period and goal wheels for a thumb; the
   * sections of Settings as keys. Other pages keep the card above.
   */
  page(place) {
    const box = $('lowpage');
    document.body.classList.toggle('lowpage', !!place);
    box.hidden = !place;
    if (!place) return;
    const esc = text => String(text ?? '').replace(/[&<>"]/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'})[c]);
    const head = `<header class="lhead"><span>${place.kind === 'library' ? esc(place.file) : place.kind === 'stats' ? 'Statistics' : 'Settings'}</span><span class="lclock">${esc($('clock').textContent)}</span></header>`;
    if (place.kind === 'library') {
      const most = Math.max(1, ...place.days);
      const days = place.days.length ? `<div class="ldays"><span>${place.days.length} days</span>${place.days.map(n => `<i class="${n <= 0 ? '' : n >= most ? 'hot' : 'on'}"></i>`).join('')}</div>` : '';
      box.innerHTML = `${head}
        <div class="lgame">
          ${place.cover ? `<img class="lcover" src="${esc(pictureSource(place.cover))}" alt="">` : ''}
          <div class="ltext">
            <h1>${esc(place.title)}</h1>${place.own ? `<div class="lown">${esc(place.own)}</div>` : ''}
            <dl class="lfig">${place.figures.map(([label, value]) => `<div><dt>${esc(label)}</dt><dd>${esc(value)}</dd></div>`).join('')}</dl>
            ${place.far >= 0 ? `<div class="lfar"><i style="width:${Math.round(place.far * 100)}%"></i></div>` : ''}
            ${place.last ? `<div class="llast"><span>Last line</span><p lang="ja">${esc(place.last)}</p></div>` : ''}
            ${days}
          </div>
        </div>
        <div class="lkeys">
          ${place.go ? `<button type="button" class="lgo" data-r="go">${esc(place.go)}</button>` : ''}
          ${place.backlog ? '<button type="button" data-r="backlog">Backlog</button>' : ''}
          ${place.also ? `<button type="button" data-r="also">${esc(place.also)}</button>` : ''}
        </div>
        <nav class="lshelves">
          <button type="button" data-r="${place.before ? 'tab:' + place.before.index : ''}"${place.before ? '' : ' disabled'}>‹ ${esc(place.before?.tab || '')}</button>
          <b>${esc(place.shelf)}</b>
          <button type="button" data-r="${place.after ? 'tab:' + place.after.index : ''}"${place.after ? '' : ' disabled'}>${esc(place.after?.tab || '')} ›</button>
          <button type="button" class="ladd" data-r="add">+ Add game</button>
        </nav>`;
    } else if (place.kind === 'stats') {
      const kinds = [['daily', 'Daily'], ['weekly', 'Weekly'], ['monthly', 'Monthly'], ['yearly', 'Yearly']];
      let wheels = '';
      if (place.goal) {
        const zeros = String(place.step).length - 1, count = place.digits.length + zeros;
        place.digits.forEach((digit, i) => {
          if (i > 0 && (count - i) % 3 === 0) wheels += '<span class="lsep">,</span>';
          wheels += `<span class="lw${i === place.wheel ? ' on' : ''}"><button type="button" data-r="wheel:${i}/1">${(digit + 1) % 10}</button><b data-r="wheel:${i}/0">${digit}</b><button type="button" data-r="wheel:${i}/-1">${(digit + 9) % 10}</button></span>`;
        });
        for (let i = 0; i < zeros; i++) {
          if ((zeros - i) % 3 === 0) wheels += '<span class="lsep">,</span>';
          wheels += '<span class="lw fixed"><b>0</b></span>';
        }
      }
      box.innerHTML = `${head}
        <div class="lcap">Period</div>
        <div class="lgrid">${place.periods.map((name, i) => `<button type="button" class="${i === place.range ? 'on' : ''}" data-r="range:${i}">${esc(name)}</button>`).join('')}</div>
        <div class="lcap">Goal</div>
        <div class="lgrid">${kinds.map(([kind, name]) => `<button type="button" class="${place.goal === kind ? 'on' : ''}" data-r="goal:${kind}">${name}<small>${place.goals[kind] > 0 ? number(place.goals[kind]) : 'Off'}</small></button>`).join('')}</div>
        ${place.goal ? `<div class="lwheels">${wheels}</div>
        <div class="lkeys"><button type="button" class="lgo" data-r="keep">Keep</button><button type="button" data-r="cancel">Cancel</button></div>` : ''}`;
    } else if (place.kind === 'settings') {
      box.innerHTML = `${head}
        <div class="lsections">${place.groups.map(group => `<button type="button" class="${group.on ? 'on' : ''}" data-r="section:${group.key}"><b>${esc(group.title)}</b><span>${esc(group.side)}</span></button>`).join('')}</div>
        <div class="lkeys"><button type="button" data-r="find">Find a setting</button></div>`;
    }
    for (const key of box.querySelectorAll('[data-r]')) {
      const what = key.dataset.r;
      if (what && !what.endsWith('/0')) key.onclick = () => host.remote(what);
    }
  },
};
window.Lower = Lower;

if (LOWER) {
  fit();
  tick();
  addEventListener('resize', fit);
  setInterval(tick, 5000);
  setInterval(Lower.status, 15000);
  $('away-home').onclick = () => host.home();
  for (const key of document.querySelectorAll('#away [data-dial]')) {
    key.onclick = () => {
      host[key.dataset.dial](Number(key.dataset.by));
      Lower.status();
    };
  }
  if (!window.FuroshikiHost) Lower.tell(null, location.hash.includes('away'));
  $('go').onclick = () => host.remote('go');
  $('also').onclick = () => host.remote('also');
  $('add-game').onclick = () => host.remote('add');
  let pressed = 0;
  $('card').addEventListener('pointerdown', () => { pressed = setTimeout(() => { if (Lower.removeBehindPress) host.remote('askalso'); }, 650); });
  for (const end of ['pointerup', 'pointercancel', 'pointerleave']) $('card').addEventListener(end, () => clearTimeout(pressed));
  for (const id of ['before', 'after']) $(id).onclick = () => host.remote($(id).dataset.what);
  // In a plain browser: something to look at.
  if (!window.FuroshikiHost) {
    Lower.show(JSON.stringify({theme: {}, head: 'PSP', shelf: true, file: '01 / 03 · ULJM05887', title: 'STEINS;GATE', sub: '', value: '',
      figures: [['Characters', '184,220'], ['Play time', '7h 03m'], ['Last read', '2026/11/01'], ['Read by', 'Game script']], go: 'Continue', also: 'Game settings', cover: 'covers/steins-gate.psp.jpg', far: .2,
      tabs: ['PSP', 'PS2', 'Statistics', 'Consoles', 'Settings'], page: 0, steps: false, at: 0, count: 3,
      // "#lower,library", "#lower,stats", "#lower,settings": that page of the second display.
      place: location.hash.includes('library') ? {kind: 'library', before: {tab: 'All games', index: 0}, after: {tab: 'PS2', index: 2}, shelf: 'PSP',
        title: 'STEINS;GATE', own: '', file: '01 / 03 · ULJM05887', figures: [['Characters', '184,220'], ['Play time', '15h 21m'], ['Last read', '2026/11/10'], ['Read by', 'Game script']],
        far: .31, cover: 'covers/steins-gate.psp.jpg', last: '今日の天気は晴れ、ところにより一時雨。', days: [0, 0, 1200, 3000, 0, 900, 0, 0, 4000, 2500, 3100, 0, 5200, 6100],
        go: 'Continue', also: 'Game settings', backlog: true}
        : location.hash.includes('stats') ? {kind: 'stats', range: 2, periods: ['Week', 'Month', 'Year', 'All time'], goal: 'daily', digits: [0, 9, 0], wheel: 1, step: 100,
          goals: {daily: 9000, weekly: 0, monthly: 0, yearly: 3000000}}
        : location.hash.includes('settings') ? {kind: 'settings', open: 'popup', groups: ['Library', 'Text & OCR', 'Dictionaries', 'Game scripts', 'Popup', 'Anki', 'Consoles', 'Controls', 'Display', 'Sync & About']
          .map((title, i) => ({key: title === 'Popup' ? 'popup' : 'g' + i, title, side: ['68 games', 'Game script', '46 · 40 on', '85', 'Kon · 80%', 'AnkiDroid', '8', 'Thor', 'Two screens', '0.7.39'][i], on: title === 'Popup'}))}
        : null}));
  }
} else {
loadThemes();
// In a plain browser "#look,page" shows that theme and page, for pictures of them.
const [look, startPage] = window.FuroshikiHost ? [] : location.hash.slice(1).split(',');
if (state.themes[look]) state.theme = look;
applyTheme();
document.fonts?.load('20px Machine');
document.fonts?.load('700 20px Text');
swipes();
Front.reload();
if (startPage) goTo(Number(startPage));
// In a plain browser "#look,page,stage" shows the title in hand as its page; "group:library" a
// group of the Settings page, "addgame" and "addapp" the two lists of Add game, "gamesettings"
// the settings of the game in hand.
if (!window.FuroshikiHost && location.hash.includes('stage')) setStage(true);
// "keys=Enter+ArrowRight" presses those keys once the page is drawn (pictures of a state a key leads to).
if (!window.FuroshikiHost) (location.hash.match(/keys=([\w+]+)/)?.[1] || '').split('+').filter(Boolean).forEach(name => Front.key(name));
if (!window.FuroshikiHost) {
  const group = /group:(\w+)/.exec(location.hash);
  if (group) openSettingsGroup(group[1]);
  if (location.hash.includes('addgame')) addGame();
  if (location.hash.includes('addapp')) addTitle();
  if (location.hash.includes('gamesettings')) page().rows[chosenIndex()]?.alsoAct?.();
  // "search:the" shows Search settings with that typed.
  const search = /search:(\w+)/.exec(location.hash);
  if (search) { findSetting(); const field = document.querySelector('#pick-rows input'); field.value = search[1]; field.oninput(); }
}
// The clock turns on the second, not somewhere within it.
(function onTheSecond() {
  tick();
  if (new Date().getSeconds() % 20 === 0) readStatus();
  setTimeout(onTheSecond, 1000 - Date.now() % 1000 + 5);
})();
}
