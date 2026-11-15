// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The controller mapping screen (Settings, Controller): what each button does, for all consoles
// or for one, with that console's controller drawn from shared/controllers/controllers.json (the
// desktop program draws the same). Two lists: the console's own buttons, each with the button of
// the controller in the hand that presses it, then the shortcuts, each with the button it is on.
// The model is ButtonMap (the reader's shared Kotlin): the page asks the host to assign, clear
// and reset, and shows what it answers. Loaded after front.js, whose host, state and helpers it uses.

'use strict';

/** The actions the desktop has and this app has not (no card is taken back, no line explained). */
const NOT_HERE = new Set(['reader_undo_card', 'reader_explain']);

/** The buttons the pointer can be held on (ButtonMap.POINTER_BUTTONS). */
const POINTER_BUTTONS = new Set([102, 103, 104, 105, 106, 107, 108, 109]);

/** Android's key codes the screen reads (KeyEvent.KEYCODE_...). */
const CODE = {back: 4, up: 19, down: 20, left: 21, right: 22, a: 96, b: 97, x: 99, y: 100, l1: 102, r1: 103};

/** A console's tab, by its key (Console.kt's), when the host has no name for it. */
const CONSOLE_TABS = {psp: 'PSP', dc: 'DC', '3ds': '3DS', nds: 'DS', ps1: 'PS1', ps2: 'PS2', psvita: 'Vita', switch: 'Switch'};

/** All consoles on Android: a controller as Android names its buttons (A at the bottom). */
const ANDROID_NAMES = {
  up: '↑', down: '↓', left: '←', right: '→', south: 'A', east: 'B', west: 'X', north: 'Y',
  l1: 'L1', r1: 'R1', l2: 'L2', r2: 'R2', l3: 'L3', r3: 'R3', start: 'Start', select: 'Select',
};

/** The console's buttons in the order they are listed (ButtonMap.PAD_BUTTONS). */
const PAD_ORDER = ['up', 'down', 'left', 'right', 'south', 'east', 'west', 'north', 'l1', 'r1', 'l2', 'r2', 'l3', 'r3', 'select', 'start'];

/** The buttons a console's controller has no letter for. */
const PAD_WORDS = {up: 'Up', down: 'Down', left: 'Left', right: 'Right', l3: 'Left stick, pressed', r3: 'Right stick, pressed'};

const LEARN_MS = 6000;
const B_TWICE_MS = 600;
/** The input wizard: a button not pressed in this time is passed over. */
const WIZARD_MS = 10000;

const map = {
  open: false,
  data: null,
  tabs: [],
  tab: 0,
  at: 0,
  shown: {},
  pads: {},
  own: false,
  learning: null,
  wizard: null,
};

function mapData() {
  if (!map.data) map.data = JSON.parse(host.asset('controllers.json'));
  return map.data;
}

/** The input's id (south, l2...) for a key code, and back. */
// The grips and "···" have no number of the desktop's: on Android they are buttons 1 to 5
// (the Steam Controller as Furoshiki reads it, steam/SteamInput.kt).
const codeOf = one => one.number ?? one.android;
const inputOf = code => mapData().inputs.find(one => codeOf(one) === code);

/** The pad a console's tab draws, with its buttons by id. */
function padOf(console) {
  const data = mapData();
  if (!console) {
    // Not the Steam Controller here: a controller of Android's, the Pro Controller's shape.
    const pro = data.pads.pro;
    return {
      name: 'Any controller',
      draw: pro.draw.map(item => item.k === 'key' ? {...item, label: ANDROID_NAMES[item.id] || item.label} : item),
    };
  }
  let pad = data.pads[data.consoles[console]] || data.pads.pro;
  if (pad.same_as) pad = {...data.pads[pad.same_as], name: pad.name};
  return pad;
}

/** A button's name as the console has it ("△", "ZL"), else Android's. */
function buttonName(code, pad) {
  const input = inputOf(code);
  if (!input) return '#' + code;
  const button = pad.draw.find(one => one.k === 'key' && one.id === input.id);
  return (button && button.label) || ANDROID_NAMES[input.id] || input.label || input.id;
}

function actions() {
  // The guide's come with the home server's services (host.services), which a published app has not.
  const guides = !!host.services;
  return mapData().actions.filter(one => !one.desktop && !NOT_HERE.has(one.id) && (guides || !one.id.startsWith('furoshiki_guide')));
}

/** The console's buttons a tab lists: those its controller has, each once. */
function gameButtons(pad) {
  const has = new Set(pad.draw.filter(one => one.k === 'key').map(one => one.id));
  return PAD_ORDER.filter(id => has.has(id));
}

/** A console's button as the console names it ("△", "L1", "Select"). */
function gameButtonName(id, pad) {
  if (PAD_WORDS[id]) return PAD_WORDS[id];
  const label = pad.draw.find(one => one.k === 'key' && one.id === id)?.label || ANDROID_NAMES[id] || id;
  return /^[A-Z]{3,}$/.test(label) ? label[0] + label.slice(1).toLowerCase() : label;
}

/** The rows a controller walks, in order: the tab's game buttons, the shortcuts, the other settings. */
function mapRows() {
  return [
    ...gameButtons(padOf(consoleKey())).map(id => ({pad: id})),
    ...actions().map(action => ({action})),
    {more: true},
  ];
}

/** The controller's button that presses a console's button on this tab: {source, own, changed}. */
function padShown(id) {
  return map.pads[id] || {source: id, own: !consoleKey(), changed: false};
}

function consoleKey() {
  return map.tabs[map.tab]?.key || '';
}

function mapTabs() {
  const order = Object.keys(mapData().consoles).filter(key => key !== 'all');
  const have = new Map();
  try {
    for (const one of JSON.parse(host.consoles())) have.set(one.key, LOOKS[one.system]?.[0] || one.short);
  } catch (error) { console.error(error); }
  for (const game of state.games) if (game.console && !have.has(game.console)) have.set(game.console, LOOKS[game.system]?.[0] || '');
  const tabs = [{key: '', name: 'All consoles'}];
  for (const key of order) if (have.has(key)) tabs.push({key, name: have.get(key) || CONSOLE_TABS[key] || key});
  return tabs;
}

/** What the host says each action is on, as the tab's layer has it. */
function readMap() {
  try {
    const answer = JSON.parse(host.buttonMap(consoleKey(), JSON.stringify(actions().map(one => one.id))));
    map.shown = answer.actions || {};
    map.pads = answer.pad || {};
    map.own = !!answer.own;
  } catch (error) {
    console.error(error);
    map.shown = {};
    map.pads = {};
    map.own = false;
  }
}

// ---- Opening and closing ----

function openMap() {
  map.open = true;
  map.tabs = mapTabs();
  map.tab = Math.min(map.tab, map.tabs.length - 1);
  map.at = 0;
  host.listenRaw?.(true);
  $('map').hidden = false;
  readMap();
  drawMap();
  fresh($('map'), 'fresh-page');
}

function closeMap() {
  if (map.wizard) finishWizard(false);
  stopLearning();
  map.open = false;
  host.listenRaw?.(false);
  $('map').hidden = true;
}

function goToTab(index) {
  if (map.wizard) return;
  stopLearning();
  // The same row on the next tab where it has it (a console has fewer buttons than another).
  const was = mapRows()[map.at];
  map.tab = (index + map.tabs.length) % map.tabs.length;
  const rows = mapRows();
  const same = rows.findIndex(row => was && (was.more ? row.more : was.pad ? row.pad === was.pad : row.action?.id === was.action.id));
  map.at = same >= 0 ? same : Math.min(map.at, rows.length - 1);
  readMap();
  drawMap();
}

// ---- Drawing ----

const SVG = 'http://www.w3.org/2000/svg';

function shape(spec, cls) {
  let element;
  if (spec.d) {
    element = document.createElementNS(SVG, 'path');
    element.setAttribute('d', spec.d);
  } else {
    element = document.createElementNS(SVG, 'circle');
    element.setAttribute('cx', spec.cx);
    element.setAttribute('cy', spec.cy);
    element.setAttribute('r', spec.r);
  }
  element.setAttribute('class', cls);
  return element;
}

/** The pad as it is drawn (shared/controllers/draw.py): its items in the order they are painted. */
function drawPad(pad, chosen) {
  const svg = $('map-svg');
  svg.textContent = '';
  const bound = new Set(Object.values(map.shown).map(one => one.button).filter(code => code >= 0).map(code => inputOf(code)?.id));
  for (const spec of pad.draw) {
    if (spec.k !== 'key') {
      svg.append(shape(spec, 'pad-' + spec.k + (spec.strong ? ' strong' : '') + (spec.fill ? ' sunk' : '')));
      continue;
    }
    const group = document.createElementNS(SVG, 'g');
    group.dataset.input = spec.id;
    const asked = map.wizard && spec.id === chosen;
    const given = map.wizard && spec.id in map.wizard.got;
    group.setAttribute('class', 'pad-button' + (bound.has(spec.id) ? ' bound' : '') + (spec.id === chosen ? ' chosen' : '') +
      (asked ? ' asked' : '') + (given ? ' given' : ''));
    group.append(shape(spec, 'pad-key' + (spec.edge === false ? ' edgeless' : '')));
    if (spec.glyph) {
      const glyph = document.createElementNS(SVG, 'path');
      glyph.setAttribute('d', spec.glyph);
      glyph.setAttribute('class', 'pad-glyph');
      group.append(glyph);
    } else if (!spec.quiet && spec.label) {
      const text = document.createElementNS(SVG, 'text');
      text.setAttribute('x', spec.lx);
      text.setAttribute('y', spec.ly);
      text.setAttribute('class', 'pad-label');
      text.setAttribute('font-size', spec.ls);
      text.textContent = spec.label;
      group.append(text);
    }
    group.onclick = () => {
      // A finger on a button of the drawing: the row of its shortcut, else the console's button itself.
      const found = mapData().inputs.find(one => one.id === spec.id);
      const code = found ? codeOf(found) : undefined;
      const rows = mapRows();
      const shortcut = rows.findIndex(row => row.action && map.shown[row.action.id]?.button === code);
      const at = shortcut >= 0 ? shortcut : rows.findIndex(row => row.pad === spec.id);
      if (at >= 0) chooseRow(at);
    };
    svg.append(group);
  }
}

function drawMap() {
  const console = consoleKey();
  const pad = padOf(console);
  // The tabs, L and R at their ends as on the foot of the page.
  const tabs = $('map-tabs');
  tabs.textContent = '';
  map.tabs.forEach((tab, index) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = tab.name;
    button.className = index === map.tab ? 'on' : '';
    button.onclick = () => goToTab(index);
    tabs.append(button);
  });
  tabs.children[map.tab]?.scrollIntoView({inline: 'nearest', block: 'nearest'});
  $('map-pad-name').textContent = pad.name;
  // On a console's tab: what comes from all consoles is dimmer, and the tab can go back to them.
  const layer = $('map-layer');
  layer.hidden = !console;
  $('map-same').disabled = !map.own;
  $('map-layer-text').textContent = map.own ? map.tabs[map.tab].name + ' has its own' : 'As all consoles';

  // The input wizard has the side to itself: one button asked for at a time.
  const wizard = map.wizard;
  $('map-wizard').hidden = !wizard;
  $('map-rows').hidden = !!wizard;
  $('map-setup').hidden = !!wizard;
  $('map').classList.toggle('wizard', !!wizard);
  if (wizard) {
    layer.hidden = true;
    const id = wizard.steps[wizard.at];
    $('wiz-count').textContent = `${wizard.at + 1} / ${wizard.steps.length}  ·  ${map.tabs[map.tab].name}`;
    $('wiz-name').textContent = gameButtonName(id, pad);
    $('wiz-note').textContent = wizard.note;
    $('wiz-back').disabled = wizard.at === 0;
    // The time left to press it, run down anew for each button.
    const bar = $('wiz-time').firstElementChild;
    bar.style.animation = 'none';
    void bar.offsetWidth;
    bar.style.animation = `wiz-run ${WIZARD_MS}ms linear forwards`;
    drawPad(pad, id);
    drawFootLine();
    return;
  }
  const rows = $('map-rows');
  const scrolled = rows.scrollTop;
  rows.textContent = '';
  const all = mapRows();
  const head = text => {
    const item = document.createElement('li');
    item.className = 'map-head';
    item.textContent = text;
    rows.append(item);
    return item;
  };
  /** One row: a name, where it comes from, the button on it, Clear and Learn. */
  const row = (index, {cls, name, where, chip, clearable, learning}) => {
    const item = document.createElement('li');
    item.className = 'map-row' + (index === map.at ? ' on' : '') + cls + (learning ? ' learning' : '');
    const dot = document.createElement('i');
    dot.className = 'dot';
    const label = document.createElement('span');
    label.className = 'name';
    label.textContent = name;
    const from = document.createElement('span');
    from.className = 'where';
    from.textContent = learning ? (map.learning.note || 'Press a button') : where;
    const on = document.createElement('span');
    on.className = 'chip';
    on.textContent = learning ? '…' : chip;
    const clear = document.createElement('button');
    clear.type = 'button';
    clear.className = 'clear';
    clear.textContent = 'Clear';
    clear.hidden = !clearable || learning;
    clear.onclick = event => { event.stopPropagation(); map.at = index; clearRow(); };
    const learn = document.createElement('button');
    learn.type = 'button';
    learn.className = 'learn';
    learn.textContent = learning ? 'Cancel' : 'Learn';
    learn.onclick = event => {
      event.stopPropagation();
      map.at = index;
      if (learning) stopLearning(); else startLearning();
      drawMap();
    };
    item.append(dot, label, from, on, clear, learn);
    if (learning) {
      const left = document.createElement('b');
      left.className = 'wait';
      left.style.animationDuration = LEARN_MS + 'ms';
      item.append(left);
    }
    item.onclick = () => chooseRow(index);
    rows.append(item);
  };
  all.forEach((one, index) => {
    if (one.pad) {
      // The console's own buttons: which button of the controller in the hand presses each.
      if (!index) head(console ? map.tabs[map.tab].name + '’s buttons' : 'Game buttons');
      const shown = padShown(one.pad);
      row(index, {
        cls: ' game' + (shown.changed ? ' bound changed' : '') + (!shown.own && console && shown.changed ? ' inherited' : ''),
        name: gameButtonName(one.pad, pad),
        where: !shown.own && console && map.own && shown.changed ? 'All consoles' : '',
        chip: shown.source === 'none' ? '—' : ANDROID_NAMES[shown.source] || shown.source,
        clearable: shown.changed,
        learning: map.learning?.pad === one.pad,
      });
    } else if (one.action) {
      if (!index || !all[index - 1].action) head('Shortcuts');
      const shown = map.shown[one.action.id] || {button: -1, own: true};
      const bound = shown.button >= 0;
      row(index, {
        cls: (bound ? ' bound' : '') + (!shown.own && console ? ' inherited' : ''),
        name: one.action.label,
        // Said where the tab has its own: else the whole tab is all consoles', and the bar says so.
        where: !shown.own && console && map.own ? 'All consoles' : '',
        chip: bound ? buttonName(shown.button, pad) : '—',
        clearable: bound,
        learning: map.learning?.action === one.action.id,
      });
    } else {
      // The settings of the controller that are no button's: the pointer's speed, touch play...
      const more = document.createElement('li');
      more.className = 'map-row more' + (map.at === index ? ' on' : '');
      more.innerHTML = '<i class="dot"></i><span class="name">Pointer speed, touch controls, second display</span><span class="where"></span><span class="chip"></span><span></span>';
      const open = document.createElement('button');
      open.type = 'button';
      open.className = 'learn';
      open.textContent = 'Open';
      open.onclick = event => { event.stopPropagation(); host.open('settings:CONTROLLER'); };
      more.append(open);
      more.onclick = () => chooseRow(index);
      rows.append(more);
    }
  });
  // The list scrolls: where it was, then the chosen row in sight, with its head when it is a list's first.
  rows.scrollTop = scrolled;
  const on = rows.querySelector('.map-row.on');
  if (on?.previousElementSibling?.classList.contains('map-head')) on.previousElementSibling.scrollIntoView({block: 'nearest'});
  on?.scrollIntoView({block: 'nearest'});

  // On the drawing: a game button's row fills that button of the console, a shortcut's the button it is on.
  const chosen = all[map.at];
  drawPad(pad, chosen?.pad || (chosen?.action ? inputOf(map.shown[chosen.action.id]?.button)?.id : undefined));
  drawFootLine();
}

function drawFootLine() {
  const foot = $('map-foot');
  if (map.wizard) {
    foot.textContent = 'Every button is the wizard\'s until it ends  ·  Wait, or Skip, to pass one over';
    return;
  }
  if (map.learning) {
    foot.textContent = 'Press the button  ·  B twice, or wait, to cancel';
    return;
  }
  foot.textContent = ['Start  Set up every button', 'A  Learn', 'X  Clear', consoleKey() ? 'Y  Same as all consoles' : '', 'L R  Console', 'B  Back']
    .filter(Boolean).join('    ');
}

function chooseRow(index) {
  if (map.learning) return;
  map.at = Math.max(0, Math.min(mapRows().length - 1, index));
  drawMap();
}

// ---- Learning a button ----

function startLearning() {
  const row = mapRows()[map.at];
  if (!row || row.more) {
    host.open('settings:CONTROLLER');
    return;
  }
  stopLearning();
  map.learning = {action: row.action?.id, pad: row.pad, note: '', timer: 0, b: 0};
  armLearning();
}

/** The wait starts (again): nothing pressed in time, nothing learnt. */
function armLearning() {
  clearTimeout(map.learning.timer);
  map.learning.timer = setTimeout(() => {
    stopLearning();
    drawMap();
  }, LEARN_MS);
}

function stopLearning() {
  if (!map.learning) return;
  clearTimeout(map.learning.timer);
  clearTimeout(map.learning.b);
  map.learning = null;
}

/** A button pressed while one is wanted. B is learnt only when it is not pressed again at once. */
function learnPress(code) {
  const learning = map.learning;
  if (code === CODE.back) {
    stopLearning();
    drawMap();
    return;
  }
  if (code === CODE.b) {
    if (learning.b) {
      stopLearning();
      drawMap();
      return;
    }
    learning.b = setTimeout(() => {
      learning.b = 0;
      if (map.learning === learning) learnt(CODE.b);
    }, B_TWICE_MS);
    return;
  }
  clearTimeout(learning.b);
  learning.b = 0;
  learnt(code);
}

function learnt(code) {
  const learning = map.learning;
  const refuse = note => {
    learning.note = note;
    armLearning();
    drawMap();
  };
  if (!inputOf(code)) return refuse('Not a controller button');
  if (learning.pad) {
    // A game button: the controller's button that presses it from now on (the two swap).
    if (!host.assignPad(learning.pad, code, consoleKey())) return refuse('Not this button');
    stopLearning();
    readMap();
    drawMap();
    fresh($('map-rows').querySelector('.map-row.on'), 'fresh-card');
    return;
  }
  if (learning.action === 'pointer' && !POINTER_BUTTONS.has(code)) return refuse('The pointer needs a shoulder, trigger, stick, Select or Start');
  if (!host.assignButton(learning.action, code, consoleKey())) return refuse('Not this button');
  stopLearning();
  readMap();
  drawMap();
  fresh($('map-rows').querySelector('.map-row.on'), 'fresh-card');
}

// ---- The input wizard: every button of the console asked for in turn ----

/**
 * Asks for each of the tab's console buttons, lit on the drawing: the controller's button
 * pressed next is its. While it runs every button comes here and goes nowhere else: the page
 * is not moved about, a tab not changed. Nothing is written until the last one is done.
 */
function startWizard() {
  stopLearning();
  map.wizard = {steps: gameButtons(padOf(consoleKey())), at: 0, got: {}, held: new Set(), note: '', timer: 0};
  armWizard();
  drawMap();
}

function armWizard() {
  clearTimeout(map.wizard.timer);
  map.wizard.timer = setTimeout(() => wizardStep(null), WIZARD_MS);
}

/** The button asked for is `code`'s, or passed over (null); then the next, or the end. */
function wizardStep(code) {
  const wizard = map.wizard;
  if (!wizard) return;
  const id = wizard.steps[wizard.at];
  if (code === null) delete wizard.got[id];
  else wizard.got[id] = code;
  wizard.note = '';
  wizard.at++;
  if (wizard.at >= wizard.steps.length) {
    finishWizard(true);
    return;
  }
  armWizard();
  drawMap();
}

function wizardBack() {
  const wizard = map.wizard;
  if (!wizard || wizard.at === 0) return;
  wizard.at--;
  delete wizard.got[wizard.steps[wizard.at]];
  wizard.note = '';
  armWizard();
  drawMap();
}

function finishWizard(keep) {
  const wizard = map.wizard;
  if (!wizard) return;
  clearTimeout(wizard.timer);
  map.wizard = null;
  const count = Object.keys(wizard.got).length;
  if (keep && count) {
    host.setPads(JSON.stringify(wizard.got), consoleKey());
    say(`${count} of ${wizard.steps.length} buttons set for ${map.tabs[map.tab].name.toLowerCase()}`);
  } else {
    say('Nothing changed');
  }
  readMap();
  drawMap();
}

function wizardPress(code, down) {
  const wizard = map.wizard;
  if (!down) {
    wizard.held.delete(code);
    return;
  }
  // The system's own Back key ends it; a controller's B is a button like any other here.
  if (code === CODE.back) {
    finishWizard(false);
    return;
  }
  // One at a time: a button that goes down while another is held is not an answer.
  const busy = wizard.held.size > 0;
  wizard.held.add(code);
  if (busy) return;
  if (!inputOf(code)) {
    wizard.note = 'Not a button a game can have';
    armWizard();
    drawMap();
    return;
  }
  wizardStep(code);
}

// ---- Clear and reset ----

function clearRow() {
  const row = mapRows()[map.at];
  if (!row || row.more) return;
  if (row.pad) host.clearPad(row.pad, consoleKey());
  else host.clearButton(row.action.id, consoleKey());
  readMap();
  drawMap();
}

function sameAsAll() {
  const console = consoleKey();
  if (!console || !map.own) return;
  host.sameAsAll(console);
  readMap();
  drawMap();
  say(map.tabs[map.tab].name + ': same as all consoles');
}

// ---- The controller, its key codes as they come (HomeActivity hands them over while this is open) ----

/** The console's button or buttons a button of the controller presses, lit for a moment while it is held. */
function light(code, down) {
  const input = inputOf(code)?.id;
  if (!input) return;
  for (const id of PAD_ORDER.filter(one => padShown(one).source === input)) {
    const group = document.querySelector(`#map-svg [data-input="${id}"]`);
    if (!group) continue;
    clearTimeout(light[id]);
    if (down) group.classList.add('lit');
    else light[id] = setTimeout(() => group.classList.remove('lit'), 140);
  }
}

function mapPad(code, down) {
  if (!map.open) return;
  if (map.wizard) {
    wizardPress(code, down);
    return;
  }
  light(code, down);
  if (!down) return;
  if (map.learning) {
    learnPress(code);
    return;
  }
  const count = mapRows().length;
  switch (code) {
    case CODE.up: if (map.at > 0) chooseRow(map.at - 1); break;
    case CODE.down: if (map.at < count - 1) chooseRow(map.at + 1); break;
    case CODE.l1: case CODE.left: goToTab(map.tab - 1); break;
    case CODE.r1: case CODE.right: goToTab(map.tab + 1); break;
    case CODE.a: startLearning(); drawMap(); break;
    case CODE.x: clearRow(); break;
    case CODE.y: sameAsAll(); break;
    case 108: startWizard(); break;
    case CODE.b: case CODE.back: closeMap(); break;
    default: break;
  }
}

/** The page's own key names (a keyboard in a browser, the host before raw keys), as key codes. */
const NAMED = {
  ArrowUp: CODE.up, ArrowDown: CODE.down, ArrowLeft: CODE.left, ArrowRight: CODE.right,
  Enter: CODE.a, Escape: CODE.b, Other: CODE.y, x: CODE.x, y: CODE.y, PageUp: CODE.l1, PageDown: CODE.r1, Backspace: CODE.back,
  // In a browser, buttons to learn: 1 to 8 the shoulders, triggers, sticks, Select and Start.
  1: 102, 2: 103, 3: 104, 4: 105, 5: 106, 6: 107, 7: 108, 8: 109,
};

window.MapScreen = {
  get open() { return map.open; },
  key(name) {
    const code = NAMED[name];
    if (code === undefined) return;
    mapPad(code, true);
    mapPad(code, false);
  },
};
Front.pad = (code, down) => mapPad(Number(code), !!down);
const reloadPage = Front.reload;
Front.reload = () => {
  reloadPage();
  if (map.open && !map.learning) {
    readMap();
    drawMap();
  }
};

// In a plain browser: ButtonMap as it is in Kotlin, over a made-up store, for looking at the screen.
if (!window.FuroshikiHost) {
  const values = {reader_stats_button: '105', reader_button_action_106: 'reader_backlog', reader_button_action_107: 'reader_sentence_card',
    'reader_cursor_button@ps2': '102', 'pad_button_south@ps2': 'east', 'pad_button_east@ps2': 'south', 'reader_button_action_100@ps2': 'reader_word_card', 'reader_button_action_106@ps2': 'none'};
  const SETTINGS = {pointer: 'reader_cursor_button', reader_menu: 'reader_menu_button', reader_pause_stats: 'reader_stats_button', reader_next_choice: 'reader_next_choice_button'};
  const PREFIX = 'reader_button_action_';
  const key = (base, c) => c ? base + '@' + c : base;
  const setting = (name, c) => Number(c && values[key(name, c)] !== undefined ? values[key(name, c)] : values[name] ?? (name === 'reader_cursor_button' ? 104 : -1));
  const action = (button, c) => { const v = (c ? values[key(PREFIX + button, c)] : undefined) ?? values[PREFIX + button]; return v && v !== 'none' ? v : null; };
  const buttons = () => [...new Set(Object.keys(values).filter(k => k.startsWith(PREFIX)).map(k => Number(k.slice(PREFIX.length).split('@')[0])))].sort((a, b) => a - b);
  const clear = (id, c) => {
    if (SETTINGS[id]) { values[key(SETTINGS[id], c)] = '-1'; return; }
    for (const b of buttons()) if (action(b, c) === id) values[key(PREFIX + b, c)] = 'none';
  };
  const PAD = 'pad_button_';
  const source = (id, c) => (c ? values[key(PAD + id, c)] : undefined) ?? values[PAD + id] ?? id;
  const putPad = (id, to, c) => {
    if (to === id && (!c || source(id, '') === id)) delete values[key(PAD + id, c)]; else values[key(PAD + id, c)] = to;
  };
  Object.assign(host, {
    assignPad(id, code, c) {
      const input = inputOf(code)?.id;
      if (!input || !PAD_ORDER.includes(input)) return false;
      const before = source(id, c);
      if (before === input) return true;
      for (const other of PAD_ORDER.filter(one => one !== id && source(one, c) === input)) putPad(other, before, c);
      putPad(id, input, c);
      return true;
    },
    clearPad(id, c) { putPad(id, id, c); },
    setPads(json, c) {
      const asked = Object.fromEntries(Object.entries(JSON.parse(json)).map(([id, code]) => [id, inputOf(code)?.id]).filter(([, to]) => to));
      const before = Object.fromEntries(PAD_ORDER.map(id => [id, source(id, c)]));
      for (const [id, to] of Object.entries(asked)) putPad(id, to, c);
      for (const id of PAD_ORDER) if (!(id in asked) && Object.values(asked).includes(before[id])) values[key(PAD + id, c)] = 'none';
    },
    listenRaw() {},
    buttonMap(c, ids) {
      const out = {};
      for (const id of JSON.parse(ids)) {
        if (SETTINGS[id]) {
          const own = !c || values[key(SETTINGS[id], c)] !== undefined;
          out[id] = {button: setting(SETTINGS[id], own ? c : ''), own};
        } else {
          const found = buttons().find(b => action(b, c) === id) ?? -1;
          out[id] = {button: found, own: !c || (found >= 0 && values[key(PREFIX + found, c)] !== undefined)};
        }
      }
      const pad = {};
      for (const id of PAD_ORDER) pad[id] = {source: source(id, c), own: !c || values[key(PAD + id, c)] !== undefined, changed: source(id, c) !== id};
      return JSON.stringify({actions: out, pad, own: !!c && Object.keys(values).some(k => k.endsWith('@' + c))});
    },
    assignButton(id, button, c) {
      if (id === 'pointer' && !POINTER_BUTTONS.has(button)) return false;
      clear(id, c);
      for (const [other, name] of Object.entries(SETTINGS)) if (other !== id && setting(name, c) === button) values[key(name, c)] = '-1';
      if (action(button, c)) values[key(PREFIX + button, c)] = 'none';
      if (SETTINGS[id]) values[key(SETTINGS[id], c)] = String(button); else values[key(PREFIX + button, c)] = id;
      return true;
    },
    clearButton: clear,
    sameAsAll(c) { for (const k of Object.keys(values)) if (k.endsWith('@' + c)) delete values[k]; },
  });
}

// The list scrolls: when its room changes (the device turned, the page's sizes settled) the chosen row stays in sight.
if (window.ResizeObserver) {
  new ResizeObserver(() => {
    if (map.open) $('map-rows').querySelector('.map-row.on')?.scrollIntoView({block: 'nearest'});
  }).observe($('map-rows'));
}

$('map-back').onclick = closeMap;
$('map-same').onclick = sameAsAll;
$('map-setup').onclick = startWizard;
$('wiz-skip').onclick = () => wizardStep(null);
$('wiz-back').onclick = wizardBack;
$('wiz-cancel').onclick = () => finishWizard(false);

// In a plain browser "#kon,map" opens it (",map,ps2" at a console's tab), for pictures of it.
if (!window.FuroshikiHost && /(^|,)map\b/.test(location.hash.slice(1))) {
  const parts = location.hash.slice(1).split(',');
  openMap();
  const tab = map.tabs.findIndex(one => one.key === parts[parts.indexOf('map') + 1]);
  if (tab > 0) goToTab(tab);
  const row = Number(parts[parts.indexOf('map') + 2]);
  if (row) chooseRow(row);
  if (parts.includes('learn')) { startLearning(); drawMap(); }
}
