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
  twoScreens: () => location.hash.includes('two'),
  remote(what) { say(what); },
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
  order: 'last',
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
    if (order && ORDERS[order]) state.order = order;
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
const LOWER = location.hash === '#lower';

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
  $('clock').textContent = now.getHours() + ':' + String(now.getMinutes()).padStart(2, '0');
}

// ---- Figures ----

const number = n => (n || 0).toLocaleString('en-US');

function playTime(ms) {
  const minutes = Math.floor((ms || 0) / 60000);
  return minutes < 60 ? minutes + 'm' : Math.floor(minutes / 60) + 'h' + String(minutes % 60).padStart(2, '0') + 'm';
}

// ---- The pages along the foot ----

function makePages() {
  const pages = [];
  const consoles = new Map();
  for (const game of state.games) {
    if (!consoles.has(game.system)) consoles.set(game.system, []);
    consoles.get(game.system).push(game);
  }
  for (const [system, games] of consoles) {
    const look = LOOKS[system] || [system, ''];
    pages.push({key: 'console:' + system, tab: look[0], head: 'Library', sub: system, band: look[1], shelf: true, rows: gameRows(games)});
  }
  if (pages.length === 0) pages.push(libraryWithoutGames());
  pages.push(statisticsPage());
  pages.push(consolesPage());
  pages.push(settingsPage());
  state.pages = pages;
  if (state.page >= pages.length) state.page = 0;
}

// The orders a shelf may stand in (the Settings page's row), last read at first.
const ORDERS = {
  last: ['Last read', (a, b) => (b.last || '').localeCompare(a.last || '')],
  title: ['Title', () => 0],
  chars: ['Characters read', (a, b) => (b.chars || 0) - (a.chars || 0)],
  time: ['Play time', (a, b) => (b.activeMs || 0) - (a.activeMs || 0)],
};

function gameRows(games) {
  const by = ORDERS[state.order] || ORDERS.last;
  const sorted = games.slice().sort((a, b) => by[1](a, b) || a.title.localeCompare(b.title, 'ja'));
  return sorted.map(game => ({
    title: game.title,
    side: [game.last ? game.last.replace(/-/g, '/') : '', game.activeMs ? playTime(game.activeMs) : ''].filter(Boolean).join('\n'),
    file: game.id,
    cover: game.cover,
    figures: [['Characters', number(game.chars)], ['Play Time', playTime(game.activeMs)], ['Last Read', game.last ? game.last.replace(/-/g, '/') : '—']],
    go: 'Continue',
    act() { host.play(game.path, game.console, game.id); },
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
    ...(stats.games || []).map((game, i) => ({title: game.title, side: playTime(game.activeMs), file: '統計', figures: figuresOf(game), id: 'game' + i})),
    {title: 'Reading statistics…', about: 'Sessions, days and the settings of the statistics.', file: '統計', go: 'Open', act() { host.open('statistics'); }, id: 'open'},
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

function settingsPage() {
  const theme = state.themes[state.theme];
  const open = (title, about, what) => ({title, about, go: 'Open', act() { host.open(what); }, id: what});
  const folders = JSON.parse(host.folders());
  const rows = [
    {
      title: 'Look', side: theme.name, about: 'Kon, Mono, Paper or Solarized: the looks Furoshiki has on a computer.', value: theme.name, go: 'Change', id: 'look',
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
    open('Controller', 'The pointer and the reader\'s buttons.', 'settings:CONTROLLER'),
    open('CRT picture', 'The games shown as on a tube, with its sliders.', 'crt'),
    open('Find a setting', 'Every setting, by its name.', 'settings'),
    ...(host.saveSync?.() ? [open('Save sync', 'Each game\'s save the same on your other devices.', 'settings:SAVE_SYNC')] : []),
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
      title: 'Shelf order', side: ORDERS[state.order][0], value: ORDERS[state.order][0], about: 'How each console\'s shelf stands.', go: 'Change', id: 'order',
      act() {
        const keys = Object.keys(ORDERS);
        pick('Shelf order', keys.map(key => ORDERS[key][0]), keys.indexOf(state.order), at => {
          state.order = keys[at];
          try { localStorage.setItem('order', state.order); } catch (e) { /* for this run alone */ }
          rebuild();
        });
      },
    },
    {
      title: 'Games shown', side: host.allGames?.() ? 'All' : 'With a script', value: host.allGames?.() ? 'All' : 'Read',
      about: 'Only the games a script reads, or every game the cores can play (those are played, not read).', go: 'Switch', id: 'all',
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
  const source = 'file://' + path.split('/').map(encodeURIComponent).join('/');
  if (cover.getAttribute('src') !== source) {
    // Another picture: unseen until it is read, then it fades in.
    cover.classList.remove('ready');
    cover.onload = () => cover.classList.add('ready');
    cover.onerror = () => { cover.classList.remove('ready'); art.classList.remove('boxed'); $('card').classList.remove('boxed'); };
    cover.src = source;
  }
}

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
  current.rows.forEach((row, index) => list.append(rowElement(row, index, index === chosen)));
}

/** The mark on another row of the list as it stands: the rows are not made again. */
function moveCursor(index) {
  const rows = $('rows').children;
  const next = rows[index];
  if (!next) return false;
  for (const row of rows) row.classList.toggle('on', row === next);
  return true;
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
  if (page().band) band.style.setProperty('--band', page().band);
  item.append(no, name, side, band);
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

function draw() {
  drawFoot();
  drawRows();
  drawCard();
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
    at: chosenIndex(), count: current.rows.length,
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
  state.page = (index + state.pages.length) % state.pages.length;
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
    row.act?.();
    return;
  }
  const moved = state.chosen[current.key] !== row.id && index !== chosenIndex();
  state.chosen[current.key] = row.id;
  if (row.game) state.lastGame = row.game;
  if (!moveCursor(index)) drawRows();
  drawCard();
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
  reload() {
    try {
      state.games = JSON.parse(host.games());
      state.stats = JSON.parse(host.stats());
    } catch (error) {
      console.error(error);
    }
    rebuild();
  },
  /** What a finger did on the second display's page. */
  remote(what) {
    const [kind, value] = String(what).split(':');
    const index = chosenIndex();
    if (kind === 'go') page().rows[index]?.act?.();
    else if (kind === 'also') page().rows[index]?.alsoAct?.();
    else if (kind === 'tab') goTo(Number(value));
    else if (kind === 'row') choose(Math.max(0, Math.min(page().rows.length - 1, index + Number(value))), false);
    else if (kind === 'step') page().rows[index]?.step?.(Number(value));
  },
  key(name) {
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
      } else if (name === 'Enter') {
        closePick();
        picking.chosen(picking.at);
      }
      return;
    }
    const current = page();
    const index = chosenIndex();
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
      current.rows[index]?.act?.();
    }
  },
};
window.Front = Front;

if (!LOWER) {
  addEventListener('keydown', event => Front.key(event.key));
  addEventListener('resize', () => { fit(); tick(); mirror(); });
}
if (!LOWER) {
  $('pin').onclick = () => page().rows[chosenIndex()]?.pin?.();
  $('go').onclick = () => page().rows[chosenIndex()]?.act?.();
  $('also').onclick = () => page().rows[chosenIndex()]?.alsoAct?.();
}
$('pick').onclick = event => { if (event.target === $('pick')) closePick(); };

// ---- The second display's page: shown what the first tells it, its keys told back ----

const Lower = {
  show(json) {
    const shown = JSON.parse(json);
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
    $('value').textContent = shown.value;
    const figures = $('figures');
    figures.textContent = '';
    for (const [label, value] of shown.figures) {
      const term = document.createElement('dt');
      term.textContent = label;
      const definition = document.createElement('dd');
      definition.textContent = value;
      figures.append(term, definition);
    }
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
  setInterval(tick, 20000);
  $('go').onclick = () => host.remote('go');
  $('also').onclick = () => host.remote('also');
  for (const id of ['before', 'after']) $(id).onclick = () => host.remote($(id).dataset.what);
  // In a plain browser: something to look at.
  if (!window.FuroshikiHost) {
    Lower.show(JSON.stringify({theme: {}, head: 'PlayStation Portable', shelf: true, file: '01／03  ·  ULJM05887', title: 'STEINS;GATE', sub: '', value: '',
      figures: [['Characters', '184,220'], ['Play Time', '7h03m'], ['Last Read', '2026/11/01']], go: 'Continue', also: 'Its settings', cover: '',
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
// The clock turns on the second, not somewhere within it.
(function onTheSecond() {
  tick();
  setTimeout(onTheSecond, 1000 - Date.now() % 1000 + 5);
})();
}
