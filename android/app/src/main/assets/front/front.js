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
  'PlayStation': ['PS1', '#8c8c96'],
  'PlayStation Portable': ['PSP', '#3d5a8c'],
  'PlayStation Vita': ['Vita', '#3f6fb0'],
  'PlayStation 2': ['PS2', '#2f4f7a'],
  'Dreamcast': ['DC', '#d9752b'],
};

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
  hasAccess: () => true,
  askAccess() {},
  games: () => JSON.stringify([
    {id: 'ULJM05887', title: 'STEINS;GATE', path: '/g/sg.iso', console: 'psp', system: 'PlayStation Portable', chars: 184220, activeMs: 25400000, last: '2026-11-01'},
    {id: 'ULJS00219', title: '428 〜封鎖された渋谷で〜', path: '/g/428.iso', console: 'psp', system: 'PlayStation Portable', chars: 40212, activeMs: 7400000, last: '2026-10-28'},
    {id: 'ULJM05821', title: 'CHAOS;HEAD らぶChu☆Chu!', path: '/g/ch.iso', console: 'psp', system: 'PlayStation Portable'},
  ]),
  stats: () => JSON.stringify({
    today: {chars: 1186, activeMs: 620000, text: '1,186字 · 6,910/h · 10m'},
    total: {chars: 224432, activeMs: 32800000, text: '224,432字 · 24,630/h · 9h06m'},
    days: Array.from({length: 30}, (_, i) => ({day: 'd' + i, chars: 500 + (i * 7919) % 4000})),
    games: [{title: 'STEINS;GATE', chars: 184220, activeMs: 25400000, text: '184,220字 · 7h03m'}],
  }),
  folders: () => JSON.stringify(['/storage/emulated/0/Games']),
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
  openLog(game) { say('Log of ' + game); },
  actionKeys: () => JSON.stringify([{id: 'pause', label: 'Pause the game', key: ''}, {id: 'card', label: 'Card of the line', key: 'BUTTON_R2'}]),
  twoScreens: () => location.hash.includes('two'),
  // "#kon,0,home" shows the page as a device's home screen has it.
  homeScreen: () => location.hash.includes('home') || location.hash.startsWith('#lower') ? 'on' : 'off',
  setHomeScreen(on) { say(on ? 'Home screen on' : 'Home screen off'); },
  playing: () => location.hash.includes('playing') ? JSON.stringify({path: '/g/sg.iso', console: 'psp', game: 'ULJM05887'}) : '',
  status: () => JSON.stringify({battery: 87, charging: true, wifi: true, online: true, volume: 60, brightness: 70, dim: true,
    storage: [{name: 'Device', free: 81e9, total: 256e9}, {name: 'Card', free: 310e9, total: 512e9}]}),
  volume: () => 60, brightness: () => 70, home() {},
  apps: () => JSON.stringify([{label: 'AnkiDroid', package: 'com.ichi2.anki', activity: 'a'}, {label: 'Firefox', package: 'org.mozilla.firefox', activity: 'a'},
    {label: 'Syncthing-Fork', package: 'com.github.catfriend1.syncthingandroid', activity: 'a'}, {label: 'Tailscale', package: 'com.tailscale.ipn', activity: 'a'}]),
  openApp(name) { say('Open ' + name); }, system(what) { say('Android: ' + what); },
  remote(what) { say(what); },
  services: () => JSON.stringify({tracker: 'x', furochibi: 'x', guides: 'x'}), askServices() {},
  wide: () => JSON.stringify({value: 'off', choices: ['off', '21:9', '32:9'], consoles: ['Switch']}), setWide() {},
  labels: () => JSON.stringify(location.hash.includes('renamed') ? {'Settings/Theme': 'Colours', 'Page/Settings': 'Options'} : {}),
  askLabel(key) { say('Rename ' + key); }, setLabel() {}, copyLabels() {},
  pickActionKeys() { say('Buttons'); },
  update: () => JSON.stringify({mine: '0.3.0', version: '0.3.1', url: 'x', newer: true}), openUpdate() {},
  consoles: () => JSON.stringify([{key: 'psp', short: 'PSP', system: 'PlayStation Portable'}, {key: 'ps2', short: 'PS2', system: 'PlayStation 2'}]),
  layer: (console, game) => JSON.stringify([
    {key: 'picture.resolution', label: 'Internal resolution', kind: 1, value: '2', shown: '2×', set: !!console, from: console ? '' : 'As each console has it', low: 1, high: 8, step: 1, choices: []},
    {key: 'sound.volume', label: 'Volume', kind: 1, value: '100', shown: '100', set: false, from: 'Same as all consoles', low: 0, high: 100, step: 1, choices: []},
    {key: 'picture.aspect', label: 'Shape of the picture', kind: 2, value: 'auto', shown: 'auto', set: false, from: game ? 'Same as its console' : 'The console\'s own', choices: ['auto', '4:3', '16:9', 'stretch']},
    {key: 'picture.vsync', label: 'Vertical sync', kind: 0, value: '1', shown: 'ON', set: false, from: 'The console\'s own', choices: []},
  ]),
  setSetting() {},
  openScripts(console) { say('Scripts of ' + console); },
  addFolder() {}, removeFolder() {}, scan() {}, play(path) { say('Play ' + path); }, open(what) { say('Open ' + what); },
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
  document.documentElement.style.setProperty('--s', Math.max(.45, Math.min(scale, 1.6)).toFixed(3) + 'px');
}

function tick() {
  const now = new Date();
  const time = now.getHours() + ':' + String(now.getMinutes()).padStart(2, '0');
  $('clock').textContent = time;
  if (LOWER) {
    $('away-clock').textContent = time;
    $('away-date').textContent = now.toLocaleDateString('en-GB', {weekday: 'short', day: 'numeric', month: 'short'});
  }
}

// ---- The device, where Furoshiki is its home screen: said in the head, set on the System page ----

const percent = n => n >= 0 ? n + '%' : '—';

/** The battery and the network as the head says them. */
function statusWords(status) {
  if (!status) return '';
  return [
    status.wifi ? 'Wi-Fi' : status.online ? 'Online' : 'No network',
    status.battery >= 0 ? status.battery + '%' + (status.charging ? ' charging' : '') : '',
  ].filter(Boolean).join('  ·  ');
}

function readStatus() {
  if (host.homeScreen?.() !== 'on') {
    state.status = null;
  } else {
    try { state.status = JSON.parse(host.status()); } catch (error) { console.error(error); }
  }
  $('status').textContent = statusWords(state.status);
}

// ---- Figures ----

const number = n => (n || 0).toLocaleString('en-US');

function playTime(ms) {
  const minutes = Math.floor((ms || 0) / 60000);
  return minutes < 60 ? minutes + 'm' : Math.floor(minutes / 60) + 'h' + String(minutes % 60).padStart(2, '0') + 'm';
}

// ---- What a title is: its record, and whose figures are shown ----

const recordOf = game => state.titles[game.id] || null;
const hasJapanese = text => /[\u3040-\u30ff\u3400-\u9fff]/.test(text || '');
const dayText = day => day ? day.replace(/-/g, '/') : '—';

/** What the shelf calls a game: its record's English name, else its own title (the desktop's ShelfName). */
const shelfName = game => recordOf(game)?.english || game.title;

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
    return [['Characters', number(game.chars)], ['Play Time', playTime(game.activeMs)], ['Last Read', dayText(game.last)]];
  }
  const share = tracked.total > 0 ? `  ·  ${Math.round(100 * tracked.read / tracked.total)}%` : '';
  return [['Characters', number(tracked.read) + share], ['Time', playTime(timeOf(game))], ['Last Read', dayText(lastDay(game))]];
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
  if (consoles.size > 1) pages.push({key: 'console:*', tab: 'All', head: 'Library', sub: 'All titles', shelf: true, rows: gameRows(state.games)});
  for (const [system, games] of consoles) {
    const look = LOOKS[system] || [system, ''];
    pages.push({key: 'console:' + system, tab: look[0], head: 'Library', sub: system, shelf: true, rows: gameRows(games)});
  }
  if (pages.length === 0) pages.push(libraryWithoutGames());
  pages.push(statisticsPage());
  pages.push(consolesPage());
  // The device's home screen has the device's own pages too.
  if (host.homeScreen?.() === 'on') pages.push(appsPage(), systemPage());
  pages.push(settingsPage());
  if (host.labels) {
    // The Developer page names the pages before it, so it is made from them.
    state.pages = pages;
    pages.push(developerPage());
    applyLabels(pages);
  }
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
    return at < 0 ? ORDERS[key][0] : `${at + 1}  ·  ${ORDERS[key][0]}${chosen[at][1] ? '  ·  the other way round' : ''}`;
  })];
  pick('Shelf order', labels, 0, at => {
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
  });
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
  return sorted.map(game => ({
    // Its English name, as the desktop's shelf sets it; its own name under it where that is another.
    title: shelfName(game),
    about: shelfName(game) !== game.title ? game.title : '',
    side: [dayText(lastDay(game)), timeOf(game) ? playTime(timeOf(game)) : ''].filter(Boolean).join('\n'),
    file: game.id,
    // Its box's front where the build has it (its console's own), else what the device found.
    cover: recordOf(game)?.cover || game.cover,
    spine: recordOf(game)?.spine || '',
    band: (LOOKS[game.system] || [])[1] || '',
    figures: gameFigures(game),
    // Whose figures they are, said in the card's first line.
    from: trackedOf(game) ? 'Study tracker' : game.chars ? 'This device' : '',
    far: shareOf(game),
    go: state.playing?.path === game.path ? 'Resume' : readOf(game) > 0 ? 'Continue' : 'Start',
    act() { play(game); },
    // A second tap on the spine in hand, or A: the title as a page of its own.
    open() { setStage(true); },
    id: game.path,
    game,
    pin() { host.pin?.(game.path, game.console, game.id, game.title); say('Asked the launcher for a shortcut'); },
    also: 'Its settings',
    alsoAct() {
      setScope({console: game.console, game: game.id, name: 'Only this game: ' + game.title, tag: 'Game'});
      goTo(state.pages.findIndex(one => one.key === 'consoles'));
    },
  }));
}

/** Plays a game. Another in play behind the page (the home screen) is asked about first. */
function play(game) {
  const playing = state.playing;
  if (!playing || playing.path === game.path) {
    host.play(game.path, game.console, game.id);
    return;
  }
  const other = state.games.find(one => one.path === playing.path);
  pick((other?.title || 'Another game') + ' is in play', ['Go back to it', 'End it and start this one'], 0, at => {
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
  if (!rows.length) rows.push({title: 'No other apps', about: 'Apps installed on this device are listed here.', id: 'none'});
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
    dial('Volume', 'volume', 'Left and right turn it down and up.', 'sound', direction => host.volume(direction)),
    status.dim
      ? dial('Brightness', 'brightness', 'Left and right dim and brighten the screen.', 'display', direction => host.brightness(direction))
      : {title: 'Brightness', side: percent(status.brightness), value: percent(status.brightness), about: 'Android asks once before an app may change the brightness.', go: 'Allow', act() { host.system('dim'); }, id: 'brightness'},
    open('Wi-Fi', status.wifi ? 'Connected' : 'Not connected', 'Networks, on Android\'s own page.', 'wifi'),
    open('Bluetooth', '', 'Controllers and headphones.', 'bluetooth'),
    open('Battery', percent(status.battery ?? -1) + (status.charging ? '  ·  charging' : ''), 'What uses it, on Android\'s own page.', 'battery'),
    ...(status.storage || []).map(one => ({
      ...open(one.name + ' storage', size(one.free) + ' free', 'Of ' + size(one.total) + '.', 'storage'), id: 'storage:' + one.name,
    })),
    open('Date and time', new Date().toLocaleDateString('en-GB', {day: 'numeric', month: 'short', year: 'numeric'}), 'The clock and the time zone.', 'date'),
    open('Notifications', '', 'What Android has to tell, pulled down over the page.', 'notifications', 'Show'),
    open('Quick settings', '', 'Android\'s own switches: aeroplane mode, casting, the rest.', 'quick', 'Show'),
    open('Android settings', '', 'Everything else of the device.', 'android'),
  ];
  return {key: 'system', tab: 'System', head: 'System', sub: '', rows};
}

function libraryWithoutGames() {
  const access = host.hasAccess();
  return {
    key: 'library', tab: 'Library', head: 'Library', sub: '', art: '', fill: 8,
    rows: access ? [{
      title: 'Look for games again',
      about: 'Games with a script are listed by themselves, from a folder called Games or Reader Games on the device or its card, or any folder added under Settings.',
      go: 'Look',
      act() { host.scan(); say('Looking through the folders'); },
      id: 'scan',
    }] : [{
      title: 'Allow access to your games',
      about: 'Furoshiki reads games where they are, on the device and its card. Android asks once: allow all files access for Furoshiki.',
      go: 'Allow',
      act() { host.askAccess(); },
      id: 'access',
    }],
  };
}

function statisticsPage() {
  const stats = state.stats || {today: {}, total: {}, days: [], games: []};
  const rows = [
    {title: 'Today', side: stats.today.text || '', file: '統計', figures: figuresOf(stats.today), id: 'today', bars: stats.days},
    {title: 'All games', side: stats.total.text || '', file: '統計', figures: figuresOf(stats.total), id: 'total', bars: stats.days},
    {title: 'Days with reading', side: number(stats.daysRead), value: number(stats.daysRead), file: '統計', id: 'days', about: 'Days on which something was read on this device.'},
    {title: 'Games begun', side: `${(stats.games || []).filter(game => game.chars > 0).length} of ${state.games.length}`,
      value: String((stats.games || []).filter(game => game.chars > 0).length), file: '統計', id: 'begun', about: 'Games with something read, of the games on the shelves.'},
    // The games by what was read of them, the most first.
    ...(stats.games || []).slice().sort((a, b) => (b.chars || 0) - (a.chars || 0))
      .map((game, i) => ({title: game.title, side: playTime(game.activeMs), file: '統計', figures: figuresOf(game), id: 'game' + i})),
    {title: 'Sessions and days', about: 'Every session and day, a session taken out, the statistics written to a file.', file: '統計', go: 'Open', act() { host.open('statistics'); }, id: 'open'},
  ];
  return {key: 'statistics', tab: 'Statistics', head: 'Statistics', sub: '', rows};
}

const figuresOf = c => [['Characters', number(c.chars)], ['Play Time', playTime(c.activeMs)]];

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
    const short = LOOKS[one.system]?.[0] || one.short;
    choices.push({label: 'Only ' + short, scope: {console: one.key, game: '', name: 'Only ' + short, tag: short}});
  }
  const game = state.lastGame;
  if (game) {
    const name = 'Only this game: ' + game.title;
    choices.push({label: name, scope: {console: game.console, game: game.id, name, tag: 'Game'}});
  }
  const now = choices.findIndex(one => one.scope.console === state.scope.console && one.scope.game === state.scope.game);
  pick('Settings for', choices.map(one => one.label), Math.max(now, 0), at => setScope(choices[at].scope));
}

function settingRow(setting) {
  const {console, game} = state.scope;
  const write = value => {
    host.setSetting(setting.key, value, console, game);
    rebuild();
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
    value: setting.shown, go: setting.kind === 0 ? 'Switch' : 'Change', act: change, step, id: setting.key,
  };
  if (setting.set) {
    row.also = game ? 'Use its console\'s value' : console ? 'Use the shared value' : 'Use each console\'s own';
    row.alsoAct = () => write('');
  }
  return row;
}

function consolesPage() {
  const scope = state.scope;
  const rows = [{
    title: 'Settings for', side: scope.game ? 'This game' : scope.name, about: scope.name + '. A game takes these as it starts.', value: scope.tag,
    go: 'Choose', act: pickScope, id: 'scope',
  }];
  let settings = [];
  try {
    settings = JSON.parse(host.layer(scope.console, scope.game));
  } catch (error) {
    console.error(error);
  }
  rows.push(...settings.map(settingRow));
  return {key: 'consoles', tab: 'Consoles', head: 'Consoles', sub: '', rows: rows.map(row => ({file: '本体', ...row}))};
}


// ---- A choice among values, over the page ----

function closePick() {
  state.picking = null;
  $('pick').hidden = true;
}

function drawPick() {
  const list = $('pick-rows');
  list.textContent = '';
  state.picking.labels.forEach((label, index) => {
    const item = document.createElement('li');
    item.textContent = label;
    item.className = index === state.picking.at ? 'on' : '';
    item.onclick = () => {
      const chosen = state.picking.chosen;
      closePick();
      chosen(index);
    };
    list.append(item);
  });
  list.children[state.picking.at]?.scrollIntoView({block: 'nearest'});
}

function pick(title, labels, at, chosen) {
  state.picking = {labels, at: Math.max(at, 0), chosen};
  $('pick-title').textContent = title;
  $('pick').hidden = false;
  drawPick();
}

/** A title looked for by a part of its name: what is typed lists the games that hold it. */
function findTitle() {
  state.picking = {slider: true};
  $('pick-title').textContent = 'Find a title';
  const list = $('pick-rows');
  list.textContent = '';
  const line = document.createElement('li');
  const field = document.createElement('input');
  field.type = 'search';
  field.placeholder = 'A part of its name';
  line.append(field);
  list.append(line);
  const show = () => {
    while (list.children.length > 1) list.lastChild.remove();
    const wanted = field.value.trim().toLowerCase();
    if (!wanted) return;
    for (const game of state.games.filter(one => one.title.toLowerCase().includes(wanted)).slice(0, 12)) {
      const item = document.createElement('li');
      item.textContent = `${game.title}  ·  ${LOOKS[game.system]?.[0] || game.short || ''}`;
      item.onclick = () => {
        closePick();
        const at = state.pages.findIndex(one => one.key === 'console:' + game.system);
        if (at < 0) return;
        state.chosen[state.pages[at].key] = game.path;
        state.lastGame = game;
        goTo(at);
      };
      list.append(item);
    }
  };
  field.oninput = show;
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
  list.append(item);
  $('pick').hidden = false;
}

function actionKeysCount() {
  try { return JSON.parse(host.actionKeys()).filter(one => one.key).length; } catch (e) { return 0; }
}
function actionKeysText() {
  try {
    return JSON.parse(host.actionKeys()).filter(one => one.key).map(one => one.key).join(' ') || 'None';
  } catch (e) { return ''; }
}
/** The OCR engine and the service's address: what a game without a script is read with. */
function ocrRows() {
  if (!host.ocr) return [];
  const ocr = JSON.parse(host.ocr());
  const using = {service: 'OCR service', device: 'This device'}[ocr.using] || 'None';
  const where = ocr.address || ocr.named;
  return [
    {
      title: 'OCR engine', id: 'ocr-engine', go: 'Change', side: (ocr.chosen ? '' : 'Default · ') + using, value: using,
      about: 'A game no script reads is read from its picture. '
        + (ocr.using === 'service' ? 'Its text area is sent to ' + where + '.' : ocr.using === 'device' ? 'On this device: nothing is sent anywhere.' : 'This build has no engine of its own, and no service is known.'),
      act() { host.pickOcrEngine(); },
    },
    {
      title: 'OCR service', id: 'ocr-service', go: 'Type', side: where || 'Not set', value: where ? 'Set' : 'None',
      about: (ocr.address ? 'Typed here' : ocr.named ? 'Named by your home server' : 'Your home server\'s, or the OCR helper on a computer') + (ocr.token ? ', with a token.' : '.'),
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
    about: 'The games that have a recipe drawn wide, never stretched, from their next start. For: ' + wide.consoles.join(', ') + '.',
    act() { pick('Widescreen', wide.choices.map(said), wide.choices.indexOf(wide.value), at => set(wide.choices[at])); },
    step(direction) { set(wide.choices[(wide.choices.indexOf(wide.value) + direction + wide.choices.length) % wide.choices.length]); },
  }];
}

/** Update: this build against the one the home server's Files page offers. */
function updateRow() {
  let update = null;
  try { update = JSON.parse(host.update?.() || 'null'); } catch (e) { /* none */ }
  if (!update) return [];
  const said = !update.version ? 'Not known' : update.newer ? update.version + ' is there' : 'Up to date';
  return [{
    title: 'Update', id: 'update', side: `${update.mine}  ·  ${said}`, value: update.mine, go: update.newer ? 'Get it' : 'Ask again',
    about: update.newer ? 'A newer build is on your home server\'s Files page. The browser fetches it and Android asks before it is installed.'
      : 'This build against the newest on your home server\'s Files page.',
    act() { if (update.newer) host.openUpdate(update.url); else { host.askServices(); say('Asking the home server'); } },
  }];
}

// ---- The Developer page: what a row is called (the desktop's, program/labels.json) ----

const labels = () => { try { return JSON.parse(host.labels?.() || '{}'); } catch (e) { return {}; } };

function developerPage() {
  const renaming = !!state.renaming;
  const count = Object.keys(labels()).length;
  const rows = [
    {
      title: 'Rename labels', id: 'rename', side: renaming ? 'On' : 'Off', value: renaming ? 'On' : 'Off', go: 'Switch',
      about: 'While this is on, the key of any row of any page asks what the row is to be called; this page\'s name row renames a page.',
      act() { state.renaming = !renaming; rebuild(); },
    },
    {
      title: 'Copy my labels', id: 'copy', side: count ? count + ' renamed' : 'None', go: 'Copy',
      about: 'Puts them on the clipboard as the file has them ("Page/Name": "label"), for the next build to carry.',
      act() { host.copyLabels(); say('Copied'); },
    },
    {
      title: 'Put every label back', id: 'clear', go: 'Put back', about: 'Every row is called what it was built as.',
      act() { host.setLabel('', ''); rebuild(); },
    },
    ...(renaming ? state.pages.filter(one => !one.shelf).map(one => ({
      title: 'Page: ' + (one.name || one.head), id: 'page:' + one.key, go: 'Rename', about: 'What this page is called at the foot and the head.',
      act() { host.askLabel('Page/' + (one.name || one.head), one.head); },
    })) : []),
  ];
  return {key: 'developer', tab: 'Developer', head: 'Developer', sub: '', rows: rows.map(row => ({file: '開発', ...row}))};
}

/** The pages' rows called what the user called them, and, while renaming, their key asking for a name. */
function applyLabels(pages) {
  const mine = labels();
  for (const one of pages) {
    if (one.shelf) continue;
    one.name = one.head;
    const called = mine['Page/' + one.name];
    if (called) one.head = one.tab = called;
    if (one.key === 'developer') continue;
    for (const row of one.rows) {
      const key = one.name + '/' + row.title;
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
    side: {on: 'Furoshiki', waiting: 'Not chosen yet', off: 'Off'}[home], value: {on: 'On', waiting: 'Choose', off: 'Off'}[home],
    about: 'Furoshiki as this device\'s home screen, for a handheld: the shelf at the Home key and when the device starts, the other apps and the device\'s settings on pages of their own, a second display always in use.'
      + (home === 'waiting' ? ' Android still has another home app: choose Furoshiki there.' : ''),
    go: {on: 'Switch off', waiting: 'Choose', off: 'Switch on'}[home],
    act() { if (home === 'waiting') host.system('home'); else host.setHomeScreen(home === 'off'); },
    ...(home === 'waiting' ? {also: 'Switch off', alsoAct() { host.setHomeScreen(false); }} : {}),
  }] : [];
  const rows = [
    ...homeRow,
    {
      title: 'Theme', side: theme.name, about: 'Kon, Mono, Paper or Solarized: the themes Furoshiki has on a computer.', value: theme.name, go: 'Change', id: 'look',
      act() {
        const keys = Object.keys(state.themes).sort((a, b) => state.themes[a].order - state.themes[b].order);
        state.theme = keys[(keys.indexOf(state.theme) + 1) % keys.length];
        try { localStorage.setItem('look', state.theme); } catch (e) { /* for this run alone */ }
        applyTheme();
        rebuild();
      },
    },
    {title: 'Icon', side: host.icon(), about: 'The icon on your home screen. Your launcher may take a moment to change it.', value: host.icon(), go: 'Change', act() { host.pickIcon(); }, id: 'icon'},
    // The reader's settings, by the pages Furoshiki has on a computer; each shows its few most
    // changed and keeps the rest behind More.
    open('Reading', 'The game\'s text, the reading panel, the session line.', 'settings:READING'),
    open('Popup', 'The dictionary popup\'s look and size.', 'settings:POPUP'),
    open('Dictionaries', 'Import the dictionaries words are looked up in.', 'dictionaries'),
    open('Cards', 'Anki: where cards go, their picture and audio.', 'settings:CARDS'),
    open('Sources', 'The clipboard, the text server.', 'settings:SOURCES'),
    ...ocrRows(),
    // What each button does, for all consoles or each (map.js); the pointer's speed and the rest from there.
    {title: 'Controller', about: 'What each button does, for all consoles or for each.', go: 'Open', act() { openMap(); }, id: 'controller'},
    ...(host.pickActionKeys ? [{
      title: 'Buttons in a game', id: 'action-keys', go: 'Choose',
      side: actionKeysText(), value: String(actionKeysCount() || 'None'),
      about: 'A button of the controller for pause, the reading timer, a card, the backlog' + (host.services ? ', Explain, the guide' : '') + ' and the menus: the keys of the second screen, without looking down.',
      act() { host.pickActionKeys(); },
    }] : []),
    ...wideRow(),
    open('CRT picture', 'The games shown as on a tube, with its sliders.', 'crt'),
    open('Find a setting', 'Every setting, by its name.', 'settings'),
    ...(host.saveSync?.() ? [open('Save sync', 'Each game\'s save the same on your other devices.', 'settings:SAVE_SYNC')] : []),
    ...(host.trackerAddress ? [{
      title: 'Study tracker', id: 'tracker', go: 'Type', side: host.trackerAddress() || 'Not set', value: state.tracker ? 'On' : 'Off',
      about: 'Where your study tracker is asked what was read of each title on every device. '
        + (state.tracker ? 'It answered ' + new Date(state.tracker.at).toLocaleString('en-GB', {day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit'}) + '.' : 'It has not answered yet.'),
      act() { host.askTrackerAddress(); },
    }] : []),
    ...updateRow(),
    ...(host.services ? [{
      title: 'Home server services', id: 'services', go: 'Ask again', side: servicesText(), value: servicesText() ? 'Known' : 'None',
      about: 'The study tracker, Explain and the guides are found through the home server this device is paired with.',
      act() { host.askServices(); say('Asking the home server'); },
    }] : []),
    {
      title: 'RetroAchievements', side: host.achievementsUser?.() || 'Off', value: host.achievementsUser?.() || 'Off',
      about: 'Your login, for the cores that have achievements (the Dreamcast\'s).', go: 'Log in', id: 'achievements',
      act() { host.achievementsLogin?.(); },
    },
    {
      title: 'Find a title', about: 'Any game on any shelf, by a part of its name.', go: 'Find', id: 'find',
      act() { findTitle(); },
    },
    {
      title: 'Shelf order', side: orderText(), value: ORDERS[orderKeys()[0]?.[0] || 'last'][0], go: 'Change', id: 'order',
      about: 'How every shelf stands: ' + orderText() + '. Several keys may be chosen, the first counting most.',
      act: pickOrder,
    },
    {
      title: 'Games shown', side: host.allGames?.() ? 'All' : 'With a script', value: host.allGames?.() ? 'All' : 'Read',
      about: 'Only the games a script reads, or every game the cores can play (those without a script are read by OCR: Menu, Text area for OCR).', go: 'Switch', id: 'all',
      act() { host.setAllGames?.(!host.allGames?.()); say('Looking through the folders'); rebuild(); },
    },
    {
      title: 'Game scripts', about: 'The scripts games are read with, from each console\'s published list: install, update, your own.',
      go: 'Open', id: 'scripts',
      act() {
        const consoles = JSON.parse(host.consoles());
        if (!consoles.length) { host.open('scripts'); return; }
        const names = consoles.map(one => LOOKS[one.system]?.[0] || one.short);
        pick('Game scripts for', names, 0, at => host.openScripts(consoles[at].key));
      },
    },
    {title: 'Add a game folder', about: 'Games are looked for in this folder too.', go: 'Choose', act() { host.addFolder(); }, id: 'add'},
    ...folders.map(folder => ({
      title: folder, about: 'Games are looked for here. Take it off the list with the button.', go: 'Remove', id: folder,
      act() { host.removeFolder(folder); Front.reload(); },
    })),
    {title: 'Look for games again', about: 'Goes through the folders again.', go: 'Look', act() { host.scan(); say('Looking through the folders'); }, id: 'scan'},
  ];
  return {key: 'settings', tab: 'Settings', head: 'Settings', sub: '', rows: rows.map(row => ({file: '設定', ...row}))};
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

function chosenIndex() {
  const current = page();
  const id = state.chosen[current.key];
  const at = current.rows.findIndex(row => row.id === id);
  return at >= 0 ? at : 0;
}

function drawFoot() {
  const tabs = $('tabs');
  tabs.textContent = '';
  state.pages.forEach((one, index) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = one.tab;
    button.className = index === state.page ? 'on' : '';
    button.onclick = () => goTo(index);
    tabs.append(button);
  });
  tabs.children[state.page]?.scrollIntoView({inline: 'center', block: 'nearest'});
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
  const no = document.createElement('span');
  no.className = 'no';
  no.textContent = String(index + 1).padStart(2, '0');
  const name = document.createElement('span');
  name.className = 'name';
  name.textContent = row.title;
  const side = document.createElement('span');
  side.className = 'side' + (row.same ? ' same' : '');
  side.textContent = (row.side || '').replace(/\n/g, '  ·  ');
  // On a shelf: the console's colour at the spine's foot.
  const band = document.createElement('i');
  band.className = 'band';
  if (row.band) band.style.setProperty('--band', row.band);
  item.append(no, name, side, band);
  if (row.game) {
    // Japanese stands upright in the text face; Latin letters lie along the spine in the display face.
    name.classList.toggle('latin', !hasJapanese(row.title));
    if (row.spine) {
      // Its box's own spine, as wide as that is to its height; the drawn one if the picture fails.
      const picture = document.createElement('img');
      picture.className = 'spine';
      picture.alt = '';
      picture.onload = () => item.classList.add('pictured');
      picture.onerror = () => picture.remove();
      picture.src = pictureSource(row.spine);
      item.append(picture);
    }
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
  where.textContent = current.shelf ? 'Library  ·  ' : 'Furoshiki  ·  ';
  head.append(where, current.shelf ? current.sub : current.head);
  $('file').textContent = current.shelf
    ? `${String(chosenIndex() + 1).padStart(2, '0')}／${String(current.rows.length).padStart(2, '0')}  ·  ${row?.file || ''}`
      + (row?.from ? '  ·  ' + row.from : '') + (row?.game && state.playing?.path === row.game.path ? '  ·  In play' : '')
    : `${String(chosenIndex() + 1).padStart(2, '0')}／${String(current.rows.length).padStart(2, '0')}`;
  $('title').textContent = row?.title || '';
  $('sub').textContent = row?.about || '';
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
  $('stage-where').textContent = `${String(chosenIndex() + 1).padStart(2, '0')} / ${String(current.rows.length).padStart(2, '0')}  ·  ${game.id}`;
  const name = $('stage-name');
  name.textContent = row.title;
  name.classList.toggle('latin', !hasJapanese(row.title));
  $('stage-own').textContent = row.about || '';
  $('stage-go').textContent = row.go;
  const day = lastDay(game);
  $('stage-last').textContent = day ? 'Last read  ' + dayText(day) : 'Not read yet';
  showFar($('stage-far'), shareOf(game));
  $('stage-log').hidden = true;

  const facts = $('stage-facts');
  facts.textContent = '';
  const short = LOOKS[game.system]?.[0] || game.system || '';
  facts.append(fact('Read by', game.script === false ? `Core  ·  ${short}  ·  played, not read` : `Core  ·  ${short}`));
  if (tracked) {
    facts.append(fact('Read', tracked.total > 0
      ? `${number(tracked.read)} of ${number(tracked.total)} characters  ·  ${Math.round(100 * tracked.read / tracked.total)}%`
      : `${number(tracked.read)} characters`, 'Study tracker'));
    if (tracked.hours > 0) {
      facts.append(fact('Time', [playTime(tracked.hours * 3.6e6), tracked.speed > 0 ? number(tracked.speed) + ' an hour' : '',
        tracked.sessions > 0 ? tracked.sessions + ' sessions' : ''].filter(Boolean).join('  ·  ')));
    }
    const jiten = [tracked.difficulty > 0 ? 'Difficulty ' + tracked.difficulty.toFixed(2) : '', tracked.unique > 0 ? number(tracked.unique) + ' different words' : '',
      tracked.coverage > 0 ? Math.round(tracked.coverage) + '% known' : ''].filter(Boolean);
    if (jiten.length) facts.append(fact('Jiten', jiten.join('  ·  ')));
  }
  if (game.chars > 0) {
    const hours = (game.activeMs || 0) / 3.6e6;
    const here = [number(game.chars) + ' characters', playTime(game.activeMs), hours > .05 ? number(Math.round(game.chars / hours)) + ' an hour' : ''].filter(Boolean).join('  ·  ');
    facts.append(fact(tracked ? 'Here' : 'Read', here, 'This device'));
  }
  const other = state.elsewhere?.[game.id];
  if (other && other.day > (game.last || '')) facts.append(fact('Elsewhere', `${other.device}  ·  ${dayText(other.day)}`));
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
    facts.append(fact('30 days', bars, `Most ${number(most)} a day`));
  }
  if (reading.lines > 0) {
    facts.append(fact('Log', `${number(reading.lines)} lines on this device`));
    $('stage-log').hidden = false;
  }
  // Its people: as the game writes them, and how they are said.
  (record?.people || []).forEach((person, index, all) => {
    const who = document.createElement('span');
    who.className = 'person';
    const said = document.createElement('small');
    said.textContent = person.romaji;
    who.append(person.name, said);
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
  drawFoot();
  drawRows();
  drawCard();
  drawStage();
  mirror();
  document.querySelector('.row.on')?.scrollIntoView({block: 'nearest', inline: 'nearest'});
}

/** What the second display's page shows: the row in hand, its keys, the shelves and pages. */
function mirror() {
  if (LOWER || !host.lower) return;
  const current = page();
  const row = current.rows[chosenIndex()];
  const theme = state.themes[state.theme] || {};
  host.lower(JSON.stringify({
    theme: Object.fromEntries(['ground', 'raised', 'ink', 'muted', 'accent', 'rule'].map(key => [key, theme[key]])),
    head: current.shelf ? current.sub : current.head, shelf: !!current.shelf,
    file: $('file').textContent, title: row?.title || '', sub: row?.about || '', value: current.shelf ? '' : row?.value || '',
    figures: row?.figures || [], go: row?.act ? row.go || '' : '', also: row?.also || '', cover: current.shelf ? row?.cover || '' : '',
    tabs: state.pages.map(one => one.tab), page: state.page, steps: !!row?.step,
    at: chosenIndex(), count: current.rows.length, far: current.shelf ? row?.far ?? -1 : -1,
    // A page of settings: its rows too, each a key for a finger on the second display.
    rows: current.shelf ? null : current.rows.map(one => ({title: one.title || '', value: one.value || ''})),
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
  state.page = (index + state.pages.length) % state.pages.length;
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
      state.games = JSON.parse(host.games());
      state.stats = JSON.parse(host.stats());
      if (!Object.keys(state.titles).length) state.titles = JSON.parse(host.titles?.() || '{}');
      state.tracker = JSON.parse(host.tracker?.() || 'null');
      state.elsewhere = JSON.parse(host.elsewhere?.() || 'null');
      state.playing = JSON.parse(host.playing?.() || 'null');
    } catch (error) {
      console.error(error);
    }
    readStatus();
    rebuild();
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
  /** What a finger did on the second display's page. */
  remote(what) {
    const [kind, value] = String(what).split(':');
    const index = chosenIndex();
    if (kind === 'home') { Front.home(); return; }
    if (kind === 'go') page().rows[index]?.act?.();
    else if (kind === 'also') page().rows[index]?.alsoAct?.();
    else if (kind === 'tab') goTo(Number(value));
    else if (kind === 'pick') {
      // A row tapped on the second display: in hand, and done if it was in hand already.
      const to = Number(value);
      if (to === index) (page().rows[to]?.open || page().rows[to]?.act)?.();
      else choose(to, false);
    }
    else if (kind === 'row') choose(Math.max(0, Math.min(page().rows.length - 1, index + Number(value))), false);
    else if (kind === 'step') page().rows[index]?.step?.(Number(value));
  },
  key(name) {
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
        const next = picking.at + (name === 'ArrowUp' ? -1 : 1);
        if (next >= 0 && next < picking.labels.length) {
          picking.at = next;
          drawPick();
        }
      } else if (name === 'Enter' || name === 'Play') {
        closePick();
        picking.chosen(picking.at);
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
      else if (name === 'PageUp' || name === 'PageDown') goTo(state.page + (name === 'PageUp' ? -1 : 1));
      return;
    }
    const step = current.rows[index]?.step;
    if (step && (name === 'ArrowLeft' || name === 'ArrowRight')) {
      step(name === 'ArrowLeft' ? -1 : 1);
      return;
    }
    if (current.shelf && (name === 'ArrowLeft' || name === 'ArrowRight')) {
      // On a shelf, as on the desktop's: left and right along its spines, up and down to another.
      const next = index + (name === 'ArrowLeft' ? -1 : 1);
      if (next >= 0 && next < current.rows.length) choose(next, false);
    } else if (current.shelf && (name === 'ArrowUp' || name === 'ArrowDown')) {
      goTo(state.page + (name === 'ArrowUp' ? -1 : 1));
    } else if (name === 'ArrowUp' || name === 'ArrowDown') {
      const next = index + (name === 'ArrowUp' ? -1 : 1);
      if (next >= 0 && next < current.rows.length) choose(next, false);
    } else if (name === 'ArrowLeft' || name === 'PageUp') {
      goTo(state.page - 1);
    } else if (name === 'ArrowRight' || name === 'PageDown') {
      goTo(state.page + 1);
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
  addEventListener('resize', () => { fit(); tick(); mirror(); });
}
if (!LOWER) {
  const inHand = () => page().rows[chosenIndex()];
  $('stage-back').onclick = () => setStage(false);
  $('stage-go').onclick = () => inHand()?.act?.();
  $('stage-settings').onclick = () => inHand()?.alsoAct?.();
  $('stage-pin').onclick = () => inHand()?.pin?.();
  $('stage-log').onclick = () => host.openLog?.(inHand()?.game?.id || '');
  $('stage-before').onclick = () => Front.key('ArrowLeft');
  $('stage-after').onclick = () => Front.key('ArrowRight');
  $('pin').onclick = () => page().rows[chosenIndex()]?.pin?.();
  $('go').onclick = () => page().rows[chosenIndex()]?.act?.();
  $('also').onclick = () => page().rows[chosenIndex()]?.alsoAct?.();
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
    where.textContent = shown.shelf ? 'Library  ·  ' : 'Furoshiki  ·  ';
    head.append(where, shown.head);
    $('file').textContent = shown.file;
    $('title').textContent = shown.title;
    $('sub').textContent = shown.sub;
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
    $('before').hidden = $('after').hidden = shown.count < 2 && !shown.steps;
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
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = label;
      button.className = index === shown.page ? 'on' : '';
      button.onclick = () => host.remote('tab:' + index);
      tabs.append(button);
    });
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
  for (const id of ['before', 'after']) $(id).onclick = () => host.remote($(id).dataset.what);
  // In a plain browser: something to look at.
  if (!window.FuroshikiHost) {
    Lower.show(JSON.stringify({theme: {}, head: 'PlayStation Portable', shelf: true, file: '01／03  ·  ULJM05887', title: 'STEINS;GATE', sub: '', value: '',
      figures: [['Characters', '184,220'], ['Play Time', '7h03m'], ['Last Read', '2026/11/01']], go: 'Continue', also: 'Its settings', cover: 'covers/steins-gate.psp.jpg', far: .2,
      tabs: ['PSP', 'PS2', 'Statistics', 'Consoles', 'Settings'], page: 0, steps: false, at: 0, count: 3}));
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
// In a plain browser "#look,page,stage" shows the title in hand as its page.
if (!window.FuroshikiHost && location.hash.includes('stage')) setStage(true);
// The clock turns on the second, not somewhere within it.
(function onTheSecond() {
  tick();
  if (new Date().getSeconds() % 20 === 0) readStatus();
  setTimeout(onTheSecond, 1000 - Date.now() % 1000 + 5);
})();
}
