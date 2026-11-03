// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Furoshiki's page on a phone. What it shows comes from HomeActivity (window.FuroshikiHost): the
// games the last look through the folders found, what was read of them, the screens of the
// reader. In a plain browser a made-up host stands in, for looking at the page.

'use strict';

const LOOKS = {
  'Nintendo Switch': ['Switch', 'linear-gradient(135deg,#b8323c,#3a1016)'],
  'Nintendo 3DS': ['3DS', 'linear-gradient(135deg,#8a8f99,#2c2f36)'],
  'PlayStation': ['PS1', 'linear-gradient(135deg,#5b5b66,#16161c)'],
  'PlayStation Portable': ['PSP', 'linear-gradient(135deg,#2d3a52,#0c0f18)'],
  'PlayStation Vita': ['Vita', 'linear-gradient(135deg,#3b4a63,#10141c)'],
  'PlayStation 2': ['PS2', 'radial-gradient(ellipse at 40% 30%,#3c5466,#121a22 75%)'],
  'Dreamcast': ['DC', 'linear-gradient(135deg,#d9752b,#4a2210)'],
};

const made_up = {
  asset(path) {
    const request = new XMLHttpRequest();
    request.open('GET', path, false);
    request.send();
    return request.responseText;
  },
  themes: () => JSON.stringify(['nocturne.json', 'periwinkle.json', 'silver.json', 'slate.json']),
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
  theme: 'slate',
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
};

// ---- The theme: its values become the page's CSS variables, as written ----

function loadThemes() {
  for (const file of JSON.parse(host.themes())) {
    state.themes[file.replace('.json', '')] = JSON.parse(host.asset('themes/' + file));
  }
  try {
    const kept = localStorage.getItem('look');
    if (kept && state.themes[kept]) state.theme = kept;
  } catch (e) { /* no storage here: the first theme */ }
}

function applyTheme() {
  const theme = state.themes[state.theme];
  const root = document.documentElement.style;
  for (const name of Array.from(root)) root.removeProperty(name);
  for (const [key, value] of Object.entries(theme)) {
    if (typeof value === 'string' || typeof value === 'number') root.setProperty('--' + key, value);
  }
  for (const [role, type] of Object.entries(theme.type || {})) {
    root.setProperty('--t-' + role, type.size);
    root.setProperty('--sp-' + role, (type.spacing || 0) + 'em');
  }
  const clock = theme.clock || {};
  for (const key of ['color', 'background', 'border', 'shadow']) {
    if (clock[key]) root.setProperty('--clock-' + key, clock[key]);
  }
  if (clock['text-sh']) root.setProperty('--clock-sh', clock['text-sh']);
  if (clock.padding) root.setProperty('--clock-padding', clock.padding.map(n => n + 'px').join(' '));
  const glow = String(theme['orb-glow'] || '').split(/,(?![^(]*\))/)[0];
  if (glow) root.setProperty('--orb-glow-one', glow);
  root.setProperty('--interlace', (theme.fuzz || {}).interlace || 0);
  fit();
}

/** What one pixel of the desktop's 1280x720 sheet is here. */
function fit() {
  const width = innerWidth, height = innerHeight;
  const scale = width > height ? Math.max(height / 720, Math.min(width / 1280, .62)) : Math.max(width / 640, .5);
  document.documentElement.style.setProperty('--s', Math.min(scale, 1.2).toFixed(3) + 'px');
}

function tick() {
  const format = (state.themes[state.theme].clock || {}).format || 'yyyy/MM/dd H:mm:ss';
  const now = new Date();
  const two = n => String(n).padStart(2, '0');
  // A phone held upright has room for the time alone.
  const short = innerWidth < 500 ? format.replace(/^.*?(?=H)/, '').replace(/:ss/, '') : format;
  $('clock').textContent = short
    .replace('yyyy', now.getFullYear()).replace('MM', two(now.getMonth() + 1)).replace('dd', two(now.getDate()))
    .replace('HH', two(now.getHours())).replace('H', now.getHours()).replace('mm', two(now.getMinutes()))
    .replace('ss', two(now.getSeconds()));
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
    pages.push({key: 'console:' + system, tab: look[0], head: 'Library', sub: system, art: look[1], rows: gameRows(games)});
  }
  if (pages.length === 0) pages.push(libraryWithoutGames());
  pages.push(statisticsPage());
  pages.push(consolesPage());
  pages.push(settingsPage());
  state.pages = pages;
  if (state.page >= pages.length) state.page = 0;
}

function gameRows(games) {
  const sorted = games.slice().sort((a, b) => (b.last || '').localeCompare(a.last || '') || a.title.localeCompare(b.title, 'ja'));
  return sorted.map(game => ({
    title: game.title,
    side: [game.last ? game.last.replace(/-/g, '/') : '', game.activeMs ? playTime(game.activeMs) : ''].filter(Boolean).join('\n'),
    file: game.id,
    cover: game.cover,
    figures: [['Characters', number(game.chars)], ['Play Time', playTime(game.activeMs)], ['Last Read', game.last ? game.last.replace(/-/g, '/') : '—']],
    go: 'はじめる',
    act() { host.play(game.path, game.console, game.id); },
    id: game.path,
    game,
    also: 'このゲームの設定',
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
      about: 'Games with a script are listed by themselves, from a folder called Games or Reader Games on the device or its card, or any folder added under 設定.',
      go: '探す',
      act() { host.scan(); say('Looking through the folders'); },
      id: 'scan',
    }] : [{
      title: 'Allow access to your games',
      about: 'Furoshiki reads games where they are, on the device and its card. Android asks once: allow all files access for Furoshiki.',
      go: '許可する',
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
    {title: 'Reading statistics…', about: 'Sessions, days and the settings of the statistics.', file: '統計', go: '開く', act() { host.open('statistics'); }, id: 'open'},
  ];
  return {key: 'statistics', tab: '統計', head: 'Statistics', sub: '', rows};
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
    value: setting.shown, go: setting.kind === 0 ? '切り替える' : '変える', act: change, step, id: setting.key,
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
    go: '選ぶ', act: pickScope, id: 'scope',
  }];
  let settings = [];
  try {
    settings = JSON.parse(host.layer(scope.console, scope.game));
  } catch (error) {
    console.error(error);
  }
  rows.push(...settings.map(settingRow));
  return {key: 'consoles', tab: '本体', head: 'Consoles', sub: '', rows: rows.map(row => ({file: '本体', ...row}))};
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
  const open = (title, about, what) => ({title, about, go: '開く', act() { host.open(what); }, id: what});
  const folders = JSON.parse(host.folders());
  const rows = [
    {
      title: 'Look', side: theme.name, about: theme.about || '', value: theme.name, go: '変える', id: 'look',
      act() {
        const keys = Object.keys(state.themes).sort((a, b) => state.themes[a].order - state.themes[b].order);
        state.theme = keys[(keys.indexOf(state.theme) + 1) % keys.length];
        try { localStorage.setItem('look', state.theme); } catch (e) { /* for this run alone */ }
        applyTheme();
        rebuild();
      },
    },
    open('Reader settings', 'The popup, the panel, the cursor, cards and the rest of the reader.', 'settings'),
    open('Dictionaries', 'Import the dictionaries words are looked up in.', 'dictionaries'),
    open('Anki', 'Where cards go, and how they are made.', 'anki'),
    open('CRT picture', 'The games shown as on a tube, with its sliders.', 'crt'),
    {
      title: 'Game scripts', about: 'The scripts games are read with, from each console\'s published list: install, update, your own.',
      go: '開く', id: 'scripts',
      act() {
        const consoles = JSON.parse(host.consoles());
        if (!consoles.length) { host.open('scripts'); return; }
        const names = consoles.map(one => LOOKS[one.system]?.[0] || one.short);
        pick('Game scripts for', names, 0, at => host.openScripts(consoles[at].key));
      },
    },
    {title: 'Add a game folder', about: 'Games are looked for in this folder too.', go: '選ぶ', act() { host.addFolder(); }, id: 'add'},
    ...folders.map(folder => ({
      title: folder, about: 'Games are looked for here. Take it off the list with the button.', go: '外す', id: folder,
      act() { host.removeFolder(folder); Front.reload(); },
    })),
    {title: 'Look for games again', about: 'Goes through the folders again.', go: '探す', act() { host.scan(); say('Looking through the folders'); }, id: 'scan'},
  ];
  return {key: 'settings', tab: '設定', head: 'Settings', sub: '', rows: rows.map(row => ({file: '設定', ...row}))};
}

// ---- Drawing ----

const page = () => state.pages[state.page];

/** The card's box art: the frame fitted to the picture within the room the card gives it. */
function fitArt() {
  const art = $('art'), cover = $('cover');
  art.style.width = art.style.height = art.style.justifySelf = art.style.alignSelf = '';
  if (!art.classList.contains('boxed') || !cover.naturalWidth) return;
  const room = art.getBoundingClientRect();
  const edge = art.offsetWidth - art.clientWidth;
  const scale = Math.min((room.width - edge) / cover.naturalWidth, (room.height - edge) / cover.naturalHeight);
  art.style.width = Math.round(cover.naturalWidth * scale + edge) + 'px';
  art.style.height = Math.round(cover.naturalHeight * scale + edge) + 'px';
  art.style.justifySelf = art.style.alignSelf = 'center';
}

function showCover(path) {
  const art = $('art'), cover = $('cover');
  art.classList.toggle('boxed', !!path);
  $('card').classList.toggle('boxed', !!path);
  if (!path) {
    cover.removeAttribute('src');
    fitArt();
    return;
  }
  const source = 'file://' + path.split('/').map(encodeURIComponent).join('/');
  if (cover.getAttribute('src') !== source) {
    cover.onload = fitArt;
    cover.src = source;
  }
  fitArt();
}

addEventListener('resize', fitArt);

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
  const chosen = chosenIndex();
  current.rows.forEach((row, index) => list.append(rowElement(row, index, index === chosen)));
  for (let index = current.rows.length; index < Math.max(8, current.fill || 0); ++index) {
    const empty = document.createElement('li');
    empty.className = 'row empty';
    empty.innerHTML = `<span class="no">${String(index + 1).padStart(3, '0')}</span><span class="name"><span>No File</span></span>`;
    list.append(empty);
  }
}

function rowElement(row, index, on) {
  const item = document.createElement('li');
  item.className = 'row' + (on ? ' on' : '');
  const no = document.createElement('span');
  no.className = 'no';
  no.textContent = String(index + 1).padStart(3, '0');
  const name = document.createElement('span');
  name.className = 'name';
  const text = document.createElement('span');
  text.textContent = row.title;
  name.append(text);
  if (on) name.insertAdjacentHTML('beforeend', '<u class="orb a"><i></i><i></i><i></i><b></b></u><u class="orb b"><i></i><i></i><i></i><b></b></u>');
  const side = document.createElement('span');
  side.className = 'side' + (row.same ? ' same' : '');
  side.innerText = row.side || '';
  item.append(no, name, side);
  item.onclick = () => choose(index, true);
  return item;
}

function drawCard() {
  const current = page();
  const row = current.rows[chosenIndex()];
  const position = `${chosenIndex() + 1}／${current.rows.length}`;
  const theme = state.themes[state.theme];
  $('tab-text').textContent = theme['head-case'] === 'upper' ? current.head.toUpperCase() : current.head;
  $('file').textContent = (theme['file-text'] || 'FILE %1  %2').replace('%1', String(chosenIndex() + 1).padStart(3, '0')).replace('%2', row?.file || '');
  $('title').textContent = row?.title || '';
  $('sub').textContent = row?.about || (current.sub ? `${current.tab}  ${position}` : '');
  const figures = $('figures');
  figures.textContent = '';
  for (const [label, value] of row?.figures || []) {
    const term = document.createElement('dt');
    term.textContent = label;
    const definition = document.createElement('dd');
    definition.textContent = value;
    figures.append(term, definition);
  }
  const art = $('art');
  art.style.background = current.art || '';
  $('value').textContent = row?.value || '';
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
  art.style.display = current.art || row?.value || days?.length ? '' : 'none';
  $('card').style.gridTemplateColumns = art.style.display === 'none' ? '1fr' : '';
  showCover(row?.cover);
  const go = $('go');
  go.hidden = !row?.act;
  go.textContent = row?.go || '';
  const also = $('also');
  also.hidden = !row?.also;
  also.textContent = row?.also || '';
}

function draw() {
  drawFoot();
  drawRows();
  drawCard();
  document.querySelector('.row.on')?.scrollIntoView({block: 'nearest'});
}

function rebuild() {
  makePages();
  draw();
  tick();
}

// ---- Going about ----

function goTo(index) {
  state.page = (index + state.pages.length) % state.pages.length;
  draw();
  $('rows').scrollTop = 0;
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
  state.chosen[current.key] = row.id;
  if (row.game) state.lastGame = row.game;
  drawRows();
  drawCard();
  document.querySelector('.row.on')?.scrollIntoView({block: 'nearest', behavior: 'smooth'});
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
    if (name === 'ArrowUp' || name === 'ArrowDown') {
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

addEventListener('keydown', event => Front.key(event.key));
addEventListener('resize', () => { fit(); tick(); });
$('go').onclick = () => page().rows[chosenIndex()]?.act?.();
$('also').onclick = () => page().rows[chosenIndex()]?.alsoAct?.();
$('pick').onclick = event => { if (event.target === $('pick')) closePick(); };

loadThemes();
// In a plain browser "#look,page" shows that theme and page, for pictures of them.
const [look, startPage] = window.FuroshikiHost ? [] : location.hash.slice(1).split(',');
if (state.themes[look]) state.theme = look;
applyTheme();
document.fonts?.load('20px DotGothic16');
swipes();
Front.reload();
if (startPage) goTo(Number(startPage));
setInterval(tick, 1000);
