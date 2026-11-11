// Made by tools/popup.py from shared/popup/. Do not edit.
static const char* const PopupPage = R"popup(<!doctype html>
<!-- SPDX-FileCopyrightText: Readers contributors
     SPDX-License-Identifier: GPL-2.0-or-later
     The dictionary popup as a page: README.md here says how it is drawn and what each class is.
     tools/popup.py puts base.css and popup.js into this page for the desktop (popup_page.inc);
     the Android app loads the three files from its assets. -->
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   The popup's layout, and the "console" theme: today's popup in the console's own colours, which
   the app hands over as the variables below (Style:: on the desktop, ReaderStyle on Android).
   A theme (themes/*.css) comes after this, the user's own CSS after that. */
:root {
  --text: #ffffff;
  --dim: #b4b4c0;
  --accent: #9fd0ff;
  --ground: rgba(20, 20, 28, 0.94);
  --edge: #8888a0;
  --box: #2f5bc8;
  --box-pressed: #1e3c8a;
  --sign: #ffffff;
  --book: #ffffff;
  --scale: 1;
  /* The look's settings (Reader.show's model.look): how solid the ground is (1 as the theme
     has it), and the corners. A theme sets --ground and --radius; .popup uses them through
     these, so the user's sliders work in every theme. */
  --opacity: 1;
  --radius: 4px;
  --font: "Noto Sans JP", "Noto Sans CJK JP", "Yu Gothic UI", "Meiryo", sans-serif;
}
html, body { margin: 0; padding: 0; background: transparent; overflow: hidden; }
body { font-family: var(--font); color: var(--text); -webkit-user-select: none; user-select: none; }
.popup {
  box-sizing: border-box;
  max-height: 100vh;
  overflow-y: auto;
  background: color-mix(in srgb, var(--ground) calc(var(--opacity) * 100%), transparent);
  border: 1px solid var(--edge);
  border-radius: var(--radius);
  padding: calc(5px * var(--scale)) calc(5px * var(--scale)) calc(4px * var(--scale)) calc(7px * var(--scale));
  font-size: calc(13px * var(--scale));
  line-height: 1.35;
}
/* The scroll bar is part of the popup, not a thing beside it: no track, no buttons, a thin
   rounded thumb in the text's own colour, faint until the pointer is over the popup. (No
   scrollbar-width or scrollbar-color here: with either, Chromium draws its own bar with a track
   and passes these rules over.) */
.popup::-webkit-scrollbar { width: 7px; height: 7px; background: transparent; }
.popup::-webkit-scrollbar-track, .popup::-webkit-scrollbar-corner { background: transparent; }
.popup::-webkit-scrollbar-button { display: none; width: 0; height: 0; }
.popup::-webkit-scrollbar-thumb {
  background: color-mix(in srgb, var(--text) 16%, transparent);
  background-clip: padding-box;
  border: 2px solid transparent;
  border-radius: 4px;
}
.popup:hover::-webkit-scrollbar-thumb { background-color: color-mix(in srgb, var(--text) 34%, transparent); }
.popup::-webkit-scrollbar-thumb:active { background-color: color-mix(in srgb, var(--text) 52%, transparent); }
.entry { position: relative; padding: 3px 0 4px; }
.entry + .entry { border-top: 1px solid var(--edge); }
.head { display: flex; flex-wrap: wrap; align-items: baseline; gap: 0 0.6em; }
.headword { font-size: calc(17px * var(--scale)); font-weight: 600; ruby-position: over; ruby-align: center; }
.headword rt { display: none; font-size: 0.62em; font-weight: 400; color: var(--dim); }
.reading, .frequency, .lookups, .number, .notes, .dictionary-name, .pitch { color: var(--dim); font-size: calc(11px * var(--scale)); }
.reasons, .pitch { color: var(--accent); font-size: calc(11px * var(--scale)); }
.entry.unknown .headword {
  background: color-mix(in srgb, var(--accent) 19%, transparent);
  box-shadow: inset 0 -2px 0 color-mix(in srgb, var(--accent) 63%, transparent);
  border-radius: 4px;
  padding: 0 3px;
}
.popup.no-mark .entry.unknown .headword { background: none; box-shadow: none; padding: 0; }
.tags { display: inline-flex; flex-wrap: wrap; gap: 4px; }
.tag { font-size: calc(11px * var(--scale)); color: var(--dim); }
.tag-name::after { content: " "; }
/* A name in the running game (its names dictionary): the accent's colour, in every theme. */
.tag.name { color: var(--accent); border: 1px solid color-mix(in srgb, var(--accent) 55%, transparent); border-radius: 3px; padding: 0 4px; }
.senses { list-style: none; margin: 0; padding: 0; }
.sense .tag { margin-right: 0.3em; }
.number { margin-right: 0.3em; }
.glosses .gloss + .gloss::before { content: " | "; color: var(--dim); }
.forms .form + .form::before { content: "、"; color: var(--dim); }
.forms .tag { margin-right: 0.4em; }
.card-buttons {
  display: flex; gap: calc(6px * var(--scale)); justify-content: flex-start;
  margin-top: 2px;
  pointer-events: none;
}
.popup[data-corner$="right"] .card-buttons { justify-content: flex-end; }
.popup[data-corner^="top"] .entry { display: flex; flex-direction: column; }
.popup[data-corner^="top"] .card-buttons { order: -1; position: static; margin: 0 0 2px; }
.card {
  pointer-events: auto;
  width: calc(24px * var(--scale)); height: calc(24px * var(--scale));
  padding: 0; border-radius: 4px; cursor: pointer;
  border: 1px solid color-mix(in srgb, var(--sign) 63%, transparent);
  background: var(--box); color: var(--sign);
  display: grid; place-items: center; position: relative;
}
.card.book { color: var(--book); border-color: color-mix(in srgb, var(--book) 63%, transparent); }
.card:active, .card.pressed { background: var(--box-pressed); }
.card svg { width: 60%; height: 60%; display: block; }
.card[data-state="working"] { opacity: 0.4; }
.card[data-state="checking"] svg { animation: reader-turn 0.9s linear infinite; }
.card[data-state="away"] svg { opacity: 0.6; }
.card[data-state="away"]::after {
  content: ""; position: absolute; right: -3px; top: -3px; width: 30%; height: 30%;
  border-radius: 50%; background: #8a8aa0; border: 1px solid var(--box);
}
@keyframes reader-turn { to { transform: rotate(360deg); } }
@media (prefers-reduced-motion: reduce) { .card[data-state="checking"] svg { animation: none; } }
.card-buttons.ask .card { width: calc(30px * var(--scale)); }
.card-buttons.ask .ask-keep { border-color: var(--accent); }
.message { font-size: calc(13px * var(--scale)); padding: 2px 0; }
.link { color: var(--accent); cursor: pointer; text-decoration: underline; }

/* ---- A name of the running game as a card (entry.card): picture, name, part, other names ---- */
.name-card { display: flex; gap: calc(10px * var(--scale)); align-items: flex-start; margin: calc(4px * var(--scale)) 0 calc(3px * var(--scale)); }
.name-card .portrait {
  flex: none; width: calc(54px * var(--scale)); aspect-ratio: 3 / 4; object-fit: cover; object-position: 50% 18%;
  border-radius: calc(6px * var(--scale)); border: 1px solid color-mix(in srgb, var(--edge) 70%, transparent);
  background: color-mix(in srgb, var(--text) 8%, transparent);
}
.popup[data-portrait="large"] .name-card .portrait { width: calc(84px * var(--scale)); }
.name-card .who { min-width: 0; flex: 1; }
.who-name { font-size: calc(18px * var(--scale)); font-weight: 600; line-height: 1.9; margin-top: calc(-2px * var(--scale)); }
.who-name ruby { ruby-position: over; ruby-align: center; margin-right: 0.3em; }
.who-name rt { font-size: 0.55em; font-weight: 400; color: var(--dim); letter-spacing: 0.02em; }
.who-line { display: flex; flex-wrap: wrap; align-items: center; gap: 0.2em 0.6em; margin-top: calc(-1px * var(--scale)); }
.romaji { color: var(--dim); font-size: calc(11px * var(--scale)); letter-spacing: 0.02em; }
.tag.role { color: var(--accent); }
.also { margin-top: calc(3px * var(--scale)); display: flex; flex-wrap: wrap; gap: calc(3px * var(--scale)) calc(5px * var(--scale)); }
.alias {
  font-size: calc(11.5px * var(--scale)); padding: 0 calc(6px * var(--scale)); border-radius: 999px;
  background: color-mix(in srgb, var(--text) 9%, transparent); border: 1px solid color-mix(in srgb, var(--text) 16%, transparent);
}
.about { margin-top: calc(4px * var(--scale)); color: color-mix(in srgb, var(--text) 86%, transparent); font-size: calc(12px * var(--scale)); line-height: 1.45; }
/* ---- Compact definitions (model.look.compact) and no part-of-speech tags (hideTags) ---- */
.sense.later { display: none; }
.entry.all .sense.later { display: list-item; }
.entry.all .more { display: none; }
.more { font-size: calc(11px * var(--scale)); text-decoration: none; opacity: 0.9; }
.popup.no-tags .sense .tag[data-category="part-of-speech"] { display: none; }
</style>
<style id="console"></style>
<style id="theme"></style>
<style id="user"></style>
</head>
<body>
<div class="popup" id="popup"></div>
<script>
// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Draws the dictionary popup from what the app hands over (Reader.show), and tells the app what
// it needs to know back: how tall the popup is, where its card buttons are (for the
// controller's cursor), what it says, and which button was pressed. Every word comes in as
// text and is put into the page as text: nothing a dictionary holds is read as markup.
//
// The same file on the desktop (Qt WebEngine on Linux, WebView2 on Windows) and on Android
// (WebView); README.md here has the model and the class names.
"use strict";

(function () {
  const popup = document.getElementById("popup");
  let model = { entries: [] };

  /** A message to the app, by whichever way this engine has. */
  function post(message) {
    const text = JSON.stringify(message);
    if (window.readerPreview) {
      window.readerPreview(message); // a page that shows the popup by itself (a theme's picture)
    } else if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(text); // WebView2
    } else if (window.readerAndroid) {
      window.readerAndroid.post(text); // Android's JavascriptInterface
    } else {
      console.log("reader:" + text); // Qt WebEngine reads the console
    }
  }

  function element(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
  }

  const SVG = "http://www.w3.org/2000/svg";
  /** The signs of the card buttons, drawn in the button's colour (currentColor). */
  function sign(kind) {
    const svg = document.createElementNS(SVG, "svg");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("aria-hidden", "true");
    const path = document.createElementNS(SVG, "path");
    path.setAttribute("fill", "none");
    path.setAttribute("stroke", "currentColor");
    path.setAttribute("stroke-width", "2.6");
    path.setAttribute("stroke-linecap", "round");
    path.setAttribute("stroke-linejoin", "round");
    if (kind === "play") {
      path.setAttribute("fill", "currentColor");
      path.setAttribute("d", "M8 5v14l11-7z");
    } else if (kind === "stop") {
      path.setAttribute("fill", "currentColor");
      path.setAttribute("d", "M7 7h10v10H7z");
    } else if (kind === "keep" || kind === "drop") {
      path.setAttribute("stroke-width", "2.2");
      path.setAttribute("d", "M2 9h4l5-4v14l-5-4H2z" + (kind === "keep" ? "M14 12.5l2.5 2.5 5-6" : "M15 9l6 6M21 9l-6 6"));
    } else if (kind === "book") {
      path.setAttribute("d", "M3 6c3-1.5 6-1.5 9 1 3-2.5 6-2.5 9-1v12c-3-1.5-6-1.5-9 1-3-2.5-6-2.5-9-1zM12 7v12");
      path.setAttribute("stroke-width", "1.8");
    } else if (kind === "ring") {
      path.setAttribute("d", "M12 3a9 9 0 1 1-9 9");
    } else {
      path.setAttribute("d", "M12 4v16M4 12h16");
    }
    svg.appendChild(path);
    return svg;
  }

  function button(kind, index, state, label) {
    const node = element("button", "card " + kind);
    node.type = "button";
    node.dataset.kind = kind;
    node.dataset.index = String(index);
    node.dataset.state = state;
    node.title = label || "";
    node.setAttribute("aria-label", label || "");
    node.appendChild(sign(kind === "book" ? "book" : state === "checking" ? "ring" : "plus"));
    node.addEventListener("click", (event) => {
      event.stopPropagation();
      if (node.dataset.state === "working") return;
      post({ type: kind, index: index });
    });
    return node;
  }

  /** The buttons an entry offers in its state: the plus, the book, or both. */
  function buttons(entry, index) {
    const box = element("div", "card-buttons");
    const labels = model.labels || {};
    const inDeck = entry.state === "inDeck";
    // The plus is the one nearer the corner, the book beside it.
    const left = !model.corner || /left$/.test(model.corner);
    const parts = [];
    if (!inDeck || model.duplicates) parts.push(button("add", index, inDeck ? "add" : entry.state, labels.add));
    if (inDeck) parts.push(button("book", index, "inDeck", labels.book));
    (left ? parts : parts.reverse()).forEach((part) => box.appendChild(part));
    return box;
  }

  function headline(entry) {
    const head = element("div", "head");
    const word = element("ruby", "headword");
    word.appendChild(document.createTextNode(entry.expression));
    if (entry.reading) word.appendChild(element("rt", null, entry.reading));
    head.appendChild(word);
    if (entry.reading) head.appendChild(element("span", "reading", entry.reading));
    if (entry.name) {
      // A name in this game, from its names dictionary.
      const tag = element("span", "tag name", (model.labels && model.labels.name) || "名前");
      tag.dataset.category = "name";
      head.appendChild(tag);
    }
    if (entry.reasons && entry.reasons.length) head.appendChild(element("span", "reasons", "« " + entry.reasons.join(" « ")));
    (entry.pitches || []).forEach((pitch) => head.appendChild(element("span", "pitch", pitch)));
    if (entry.frequencies && entry.frequencies.length) {
      const tags = element("span", "tags frequencies");
      entry.frequencies.forEach((frequency) => {
        const tag = element("span", "tag frequency");
        tag.dataset.category = "frequency";
        tag.dataset.dictionary = frequency.dictionary;
        tag.appendChild(element("span", "tag-name", frequency.dictionary));
        tag.appendChild(element("span", "tag-value", frequency.value));
        tags.appendChild(tag);
      });
      head.appendChild(tags);
    }
    return head;
  }

  /** A name written with its reading over it, part by part where the two have as many parts. */
  function nameRuby(name, reading) {
    const out = element("div", "who-name");
    const parts = name.split(/\s+/).filter(Boolean);
    const readings = (reading || "").split(/\s+/).filter(Boolean);
    const paired = readings.length === parts.length;
    (paired ? parts : [name]).forEach((part, index) => {
      const ruby = element("ruby", null);
      ruby.appendChild(document.createTextNode(part));
      const over = paired ? readings[index] : reading;
      if (over && over !== part) ruby.appendChild(element("rt", null, over));
      out.appendChild(ruby);
    });
    return out;
  }

  /**
   * Who or what a name of the running game is (entry.card, from the game's names dictionary):
   * the picture, the full name under its reading, what part it has, its other names and a
   * sentence about it. Drawn in place of the name's senses.
   */
  function nameCard(card) {
    const look = model.look || {};
    const out = element("div", "name-card");
    out.dataset.kind = card.kind || "person";
    if (card.image && look.portrait !== "off") {
      const picture = element("img", "portrait");
      picture.alt = "";
      picture.decoding = "async";
      picture.addEventListener("load", reportSoon);
      picture.addEventListener("error", () => { picture.remove(); out.classList.remove("pictured"); reportSoon(); });
      picture.src = card.image;
      out.classList.add("pictured");
      out.appendChild(picture);
    }
    const who = element("div", "who");
    who.appendChild(nameRuby(card.name, card.reading));
    const line = element("div", "who-line");
    if (card.romaji) line.appendChild(element("span", "romaji", card.romaji));
    if (card.role) {
      const role = element("span", "tag role", card.role);
      role.dataset.category = "role";
      line.appendChild(role);
    }
    if (line.childNodes.length) who.appendChild(line);
    if (card.also && card.also.length) {
      const also = element("div", "also");
      card.also.forEach((alias) => also.appendChild(element("span", "alias", alias)));
      who.appendChild(also);
    }
    if (card.about) who.appendChild(element("div", "about", card.about));
    out.appendChild(who);
    return out;
  }

  function senses(entry) {
    const out = document.createDocumentFragment();
    let list = null;
    const look = model.look || {};
    // With its card drawn, a name's own senses (the card's words again, as plain lines) are left out.
    const carded = entry.card && look.cards !== false;
    const shown = (entry.senses || []).filter((sense) => !(carded && sense.name));
    const most = look.compact > 0 ? look.compact : 0;
    let count = 0;
    shown.forEach((sense) => {
      if (sense.heading) {
        const name = element("div", "dictionary-name", sense.heading);
        name.dataset.dictionary = sense.heading;
        out.appendChild(name);
        list = null;
      }
      if (!list) {
        list = element("ol", "senses");
        out.appendChild(list);
      }
      const item = element("li", "sense");
      if (sense.dictionary) item.dataset.dictionary = sense.dictionary;
      if (sense.name) item.classList.add("name");
      item.appendChild(element("span", "number", String(sense.number)));
      (sense.tags || []).forEach((tag) => {
        const node = element("span", "tag", tag);
        node.dataset.category = "part-of-speech";
        item.appendChild(node);
      });
      const glosses = element("span", "glosses");
      (sense.glosses || []).forEach((gloss) => glosses.appendChild(element("span", "gloss", gloss)));
      item.appendChild(glosses);
      if (sense.notes) item.appendChild(element("span", "notes", " (" + sense.notes + ")"));
      // Compact: the first few senses, the rest behind "more".
      if (most && ++count > most) item.classList.add("later");
      list.appendChild(item);
    });
    if (most && count > most) {
      const more = element("div", "link more", "+" + (count - most) + " " + ((model.labels && model.labels.more) || "more"));
      more.addEventListener("click", (event) => {
        event.stopPropagation();
        more.closest(".entry").classList.add("all");
        reportSoon();
      });
      out.appendChild(more);
    }
    return out;
  }

  function forms(entry) {
    const out = element("div", "forms");
    const tag = element("span", "tag", (model.labels && model.labels.forms) || "forms");
    tag.dataset.category = "forms";
    out.appendChild(tag);
    entry.forms.forEach((form) => out.appendChild(element("span", "form", form)));
    return out;
  }

  function draw() {
    popup.textContent = "";
    popup.dataset.corner = model.corner || "bottom_left";
    popup.dataset.console = model.console || "";
    popup.dataset.game = model.game || "";
    popup.classList.toggle("no-mark", model.markUnknown === false);
    const root = document.documentElement.style;
    root.setProperty("--scale", String((model.scale || 100) / 100));
    // The look's own settings (model.look), as variables every theme reads: how solid the
    // ground is, how round the corners, and what is left out.
    const look = model.look || {};
    const share = (value, low, high) => Math.min(high, Math.max(low, Number(value)));
    if (look.opacity > 0) root.setProperty("--opacity", String(share(look.opacity, 20, 100) / 100));
    else root.removeProperty("--opacity");
    if (look.radius >= 0 && look.radius !== null && look.radius !== undefined && look.radius !== "") root.setProperty("--radius", share(look.radius, 0, 28) + "px");
    else root.removeProperty("--radius");
    if (look.accent && /^#[0-9a-fA-F]{6}$/.test(look.accent)) root.setProperty("--accent", look.accent);
    else root.removeProperty("--accent");
    popup.classList.toggle("no-tags", !!look.hideTags);
    popup.dataset.portrait = look.portrait || "small";
    if (model.message) {
      popup.appendChild(element("div", "message", model.message.text));
      if (model.message.dictionaries) {
        const link = element("div", "link", (model.labels && model.labels.dictionaries) || "Open the dictionaries");
        link.addEventListener("click", () => post({ type: "dictionaries" }));
        popup.appendChild(link);
      }
    }
    (model.entries || []).forEach((entry, index) => {
      const section = element("section", "entry");
      section.dataset.index = String(index);
      section.dataset.state = entry.state || "add";
      section.classList.toggle("unknown", !!entry.unknown);
      section.appendChild(headline(entry));
      if (entry.history) section.appendChild(element("div", "lookups", entry.history));
      // look.cards false: an app that shows who a name is in a place of its own (Furochibi's pane).
      if (entry.card && (model.look || {}).cards !== false) section.appendChild(nameCard(entry.card));
      section.appendChild(senses(entry));
      if (entry.forms && entry.forms.length) section.appendChild(forms(entry));
      section.appendChild(buttons(entry, index));
      popup.appendChild(section);
    });
    popup.scrollTop = 0;
    report();
    if (design) mark(picked);
  }

  /** How tall the popup is, where its buttons are and what it says, for the app. */
  function report() {
    const boxes = [];
    popup.querySelectorAll(".card").forEach((node) => {
      const r = node.getBoundingClientRect();
      boxes.push({ index: Number(node.dataset.index), kind: node.dataset.kind, x: r.left, y: r.top, w: r.width, h: r.height });
    });
    const content = popup.scrollHeight + (popup.offsetHeight - popup.clientHeight);
    post({ type: "size", width: popup.offsetWidth, height: content, scrolled: popup.scrollTop, boxes: boxes, text: popup.innerText });
  }

  let pending = false;
  function reportSoon() {
    if (pending) return;
    pending = true;
    requestAnimationFrame(() => { pending = false; report(); });
  }

  popup.addEventListener("scroll", reportSoon);
  window.addEventListener("resize", reportSoon);
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(reportSoon);
  // Clicks between the buttons answer nothing, and never reach the game.
  document.addEventListener("mousedown", (event) => { if (!event.target.closest(".card, .link")) event.preventDefault(); });

  // The question about a card's audio, in the place of the entry's buttons: listen (stop while
  // it plays), with the audio, without. The press that asked for the card is not an answer.
  let asking = -1;
  let askedAt = 0;
  function askButton(kind, label) {
    const node = element("button", "card ask-" + kind);
    node.type = "button";
    node.title = label || "";
    node.setAttribute("aria-label", label || "");
    node.appendChild(sign(kind));
    node.addEventListener("click", (event) => {
      event.stopPropagation();
      if (Date.now() - askedAt < 300) return;
      post({ type: kind === "stop" ? "listen" : kind });
    });
    return node;
  }
  function drawAsk(playing) {
    const section = popup.querySelector('.entry[data-index="' + asking + '"]');
    if (!section) return;
    const labels = model.labels || {};
    const box = element("div", "card-buttons ask");
    box.appendChild(askButton(playing ? "stop" : "play", playing ? labels.stop : labels.listen));
    box.appendChild(askButton("keep", labels.keep));
    box.appendChild(askButton("drop", labels.drop));
    section.replaceChild(box, section.querySelector(".card-buttons"));
    reportSoon();
  }


  // ---- The customiser's design mode (Furoshiki's popup customiser) ----
  // In its preview, a click picks the element under it (the customiser shows its settings), a
  // block of an entry or a part of its headline is dragged to another place, and the picked
  // element's corner is dragged to size it. Nothing of it runs in a popup over a game.
  let design = null;
  let picked = "";
  let handle = null, marker = null;
  function designStyle() {
    if (document.getElementById("design-mode")) return;
    const style = element("style", null,
      ".designing .design-picked { outline: 1px dashed #4aa3ff !important; outline-offset: 2px; }" +
      ".designing .entry > *, .designing .head > * { cursor: grab; }" +
      ".designing .design-dragging { opacity: 0.35; }" +
      "#design-handle { position: fixed; width: 10px; height: 10px; background: #4aa3ff; border: 1px solid #fff; border-radius: 2px; cursor: nwse-resize; z-index: 10; }" +
      "#design-marker { position: fixed; height: 2px; background: #4aa3ff; z-index: 10; pointer-events: none; }" +
      "#design-marker.across { width: 2px; }");
    style.id = "design-mode";
    document.head.appendChild(style);
  }
  function pickOf(node) {
    for (let at = node; at && at !== document.body; at = at.parentElement) {
      if (!(at instanceof Element)) continue;
      for (const [id, selector] of design.pick) if (at.matches(selector)) return id;
    }
    return "";
  }
  function selectorOf(id) {
    const found = (design.pick || []).find((one) => one[0] === id);
    return found ? found[1] : "";
  }
  function mark(id) {
    picked = id || "";
    document.querySelectorAll(".design-picked").forEach((node) => node.classList.remove("design-picked"));
    const selector = selectorOf(picked);
    if (selector) popup.querySelectorAll(selector).forEach((node) => node.classList.add("design-picked"));
    placeHandle();
  }
  function sizeOf(id) {
    return (design && design.sizes || []).find((one) => one.element === id);
  }
  function scale() {
    return Number(getComputedStyle(document.documentElement).getPropertyValue("--scale")) || 1;
  }
  function placeHandle() {
    if (handle) { handle.remove(); handle = null; }
    if (!design || !picked) return;
    const size = sizeOf(picked);
    const node = popup.querySelector(".design-picked");
    if (!size || !node) return;
    const box = node.getBoundingClientRect();
    handle = element("div");
    handle.id = "design-handle";
    handle.style.left = Math.round(Math.min(box.right, innerWidth - 12) - 5) + "px";
    handle.style.top = Math.round(Math.min(box.bottom, innerHeight - 12) - 5) + "px";
    handle.addEventListener("pointerdown", (event) => {
      event.preventDefault();
      event.stopPropagation();
      handle.setPointerCapture(event.pointerId);
      const start = size.width ? node.getBoundingClientRect().width / scale() : parseFloat(getComputedStyle(node).fontSize) / scale();
      const x = event.clientX, y = event.clientY;
      const move = (next) => {
        const along = size.width ? next.clientX - x : ((next.clientX - x) + (next.clientY - y)) / 3;
        const value = Math.min(size.high, Math.max(size.low, Math.round((start + along) * 2) / 2));
        post({ type: "value", key: size.key, value: value });
      };
      const done = () => {
        handle.removeEventListener("pointermove", move);
        handle.removeEventListener("pointerup", done);
        post({ type: "valueDone", key: size.key });
      };
      handle.addEventListener("pointermove", move);
      handle.addEventListener("pointerup", done);
    });
    document.body.appendChild(handle);
  }
  /** What a node of an entry or of its headline is as a block or a part: [which, id, parent]. */
  function movable(node) {
    for (let at = node; at && at !== popup; at = at.parentElement) {
      const parent = at.parentElement;
      if (!parent) break;
      const list = parent.classList.contains("entry") ? ["entry", design.blocks] : parent.classList.contains("head") ? ["head", design.parts] : null;
      if (!list) continue;
      for (const [id, selector] of list[1]) if (at.matches(selector)) return [list[0], id, parent];
    }
    return null;
  }
  /** The blocks or parts of a parent in the order they stand, each with the box of its first node. */
  function placesIn(parent, which) {
    const list = which === "entry" ? design.blocks : design.parts;
    const seen = new Map();
    Array.from(parent.children).forEach((child) => {
      for (const [id, selector] of list) {
        if (child.matches(selector)) {
          const box = child.getBoundingClientRect();
          const was = seen.get(id);
          seen.set(id, was ? { top: Math.min(was.top, box.top), bottom: Math.max(was.bottom, box.bottom), left: Math.min(was.left, box.left), right: Math.max(was.right, box.right) }
                           : { top: box.top, bottom: box.bottom, left: box.left, right: box.right });
        }
      }
    });
    return Array.from(seen.entries()).sort((a, b) => which === "entry" ? a[1].top - b[1].top : (a[1].top - b[1].top) || (a[1].left - b[1].left));
  }
  document.addEventListener("click", (event) => {
    if (!design) return;
    event.preventDefault();
    event.stopPropagation();
    const id = pickOf(event.target);
    mark(id);
    post({ type: "pick", element: id });
  }, true);
  document.addEventListener("pointerdown", (event) => {
    if (!design || event.target === handle) return;
    const found = movable(event.target);
    if (!found) return;
    const [which, id, parent] = found;
    const x = event.clientX, y = event.clientY;
    let dragging = false, before = null;
    const nodes = () => Array.from(parent.children).filter((child) => (which === "entry" ? design.blocks : design.parts).some(([one, selector]) => one === id && child.matches(selector)));
    const move = (next) => {
      if (!dragging && Math.hypot(next.clientX - x, next.clientY - y) < 6) return;
      if (!dragging) {
        dragging = true;
        nodes().forEach((node) => node.classList.add("design-dragging"));
        marker = element("div");
        marker.id = "design-marker";
        if (which === "head") marker.classList.add("across");
        document.body.appendChild(marker);
      }
      const places = placesIn(parent, which).filter(([one]) => one !== id);
      before = null;
      for (const [one, box] of places) {
        const middle = which === "entry" ? (box.top + box.bottom) / 2 : (box.left + box.right) / 2;
        if ((which === "entry" ? next.clientY : next.clientX) < middle) { before = [one, box]; break; }
      }
      const whole = parent.getBoundingClientRect();
      if (which === "entry") {
        const top = before ? before[1].top - 2 : whole.bottom;
        Object.assign(marker.style, { left: whole.left + "px", width: whole.width + "px", top: top + "px" });
      } else {
        const last = places.length ? places[places.length - 1][1] : whole;
        const left = before ? before[1].left - 3 : last.right + 2;
        const box = before ? before[1] : last;
        Object.assign(marker.style, { left: left + "px", top: box.top + "px", height: (box.bottom - box.top) + "px" });
      }
    };
    const done = () => {
      document.removeEventListener("pointermove", move);
      document.removeEventListener("pointerup", done);
      if (!dragging) return;
      nodes().forEach((node) => node.classList.remove("design-dragging"));
      if (marker) { marker.remove(); marker = null; }
      post({ type: "move", which: which, id: id, before: before ? before[0] : "" });
    };
    document.addEventListener("pointermove", move);
    document.addEventListener("pointerup", done);
  }, true);
  window.Reader = {
    ask(index) {
      asking = index;
      askedAt = Date.now();
      drawAsk(false);
    },
    askPlaying(on) {
      if (asking >= 0) drawAsk(!!on);
    },
    askDone() {
      const index = asking;
      asking = -1;
      const entry = (model.entries || [])[index];
      const section = popup.querySelector('.entry[data-index="' + index + '"]');
      if (entry && section) section.replaceChild(buttons(entry, index), section.querySelector(".card-buttons"));
      reportSoon();
    },
    show(next) {
      asking = -1;
      model = next || { entries: [] };
      draw();
    },
    setBox(index, state) {
      const entry = (model.entries || [])[index];
      if (!entry || entry.state === state) return;
      entry.state = state;
      const section = popup.querySelector('.entry[data-index="' + index + '"]');
      if (!section) return;
      section.dataset.state = state;
      section.replaceChild(buttons(entry, index), section.querySelector(".card-buttons"));
      reportSoon();
    },
    setUnknown(index, unknown) {
      const entry = (model.entries || [])[index];
      if (entry) entry.unknown = unknown;
      const section = popup.querySelector('.entry[data-index="' + index + '"]');
      if (section) section.classList.toggle("unknown", !!unknown);
    },
    setStyle(id, css) {
      const node = document.getElementById(id);
      if (node) node.textContent = css || "";
      reportSoon();
      if (design) requestAnimationFrame(placeHandle);
    },
    scrollTo(y) {
      popup.scrollTop = y;
      reportSoon();
    },
    /** The customiser's preview: its elements, blocks and sizes (config), or off (null). */
    design(config) {
      design = config || null;
      document.documentElement.classList.toggle("designing", !!design);
      if (design) designStyle();
      mark(design ? picked : "");
    },
    /** The element the customiser shows the settings of, marked in the preview. */
    pick(id) {
      if (design) mark(id);
    },
  };
  post({ type: "ready" });
})();
</script>
</body>
</html>
)popup";
struct PopupTheme {
    const char* name;
    const char* css;
};
static const PopupTheme PopupThemes[] = {
    {"dense", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Dense: as much as fits, for a small popup and a quick glance (after Nazeka): the word, its
   reading and its senses run on in lines, no boxes round anything, the first sense's glosses
   brightest. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: rgba(17, 17, 20, 0.95); --text: #e8e8ea; --dim: #9a9aa6; --accent: #ffcf70; --edge: #3a3a44; --radius: 3px;
  --box: transparent; --box-pressed: #33333c; --sign: #7fd08a; --book: #8fb8ff;
}
.popup { border: 1px solid #3a3a44; padding: calc(4px * var(--scale)) calc(7px * var(--scale)); font-size: calc(12px * var(--scale)); line-height: 1.3; }
.entry { padding: 2px calc(26px * var(--scale)) 2px 0; }
.entry + .entry { border-top: 1px dotted #3a3a44; }
.head { gap: 0 0.45em; }
.headword { font-size: calc(14px * var(--scale)); font-weight: 700; color: #fff; }
.reading { color: var(--accent); font-size: calc(12px * var(--scale)); }
.reading::before { content: "【"; color: var(--dim); }
.reading::after { content: "】"; color: var(--dim); }
.tag { font-size: calc(10px * var(--scale)); }
.tag.frequency .tag-name { display: none; }
.tag.frequency .tag-value::before { content: "#"; }
.lookups { display: none; }
.senses { display: inline; }
.sense { display: inline; }
.sense + .sense::before { content: " "; }
.number { margin-right: 0.15em; color: var(--accent); }
.number::after { content: "."; }
.sense:first-child .glosses { color: #fff; }
.glosses .gloss + .gloss::before { content: "; "; }
.dictionary-name { display: inline; margin-right: 0.4em; }
.senses + .dictionary-name::before { content: " "; white-space: pre; }
.dictionary-name::after { content: ":"; }
.forms { display: none; }
.entry.unknown .headword { background: none; box-shadow: none; padding: 0; color: var(--accent); }
.card-buttons { position: absolute; right: 0; top: 2px; margin: 0; }
.popup[data-corner^="top"] .entry { display: block; }
.popup[data-corner^="top"] .card-buttons { position: absolute; }
.card { width: calc(20px * var(--scale)); height: calc(20px * var(--scale)); border: none; }
.entry.all .sense.later { display: inline; }
.name-card { gap: calc(7px * var(--scale)); margin: 2px 0; }
.name-card .portrait { width: calc(38px * var(--scale)); border-radius: 3px; }
.popup[data-portrait="large"] .name-card .portrait { width: calc(58px * var(--scale)); }
.who-name { font-size: calc(14px * var(--scale)); line-height: 1.75; }
.about { font-size: calc(11px * var(--scale)); margin-top: 2px; line-height: 1.35; }
.alias { border: none; background: none; padding: 0; color: var(--dim); }
.alias + .alias::before { content: "・"; }
.also { gap: 0; margin-top: 1px; }
)popup"},
    {"furoshiki", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Furoshiki glass: a translucent indigo panel with a lavender double border and rounded white
   gothic text with a soft glow, as Furoshiki's own menus (and Never7's). */
:root { --font: "M PLUS Rounded 1c", "Hiragino Maru Gothic ProN", "Noto Sans JP", sans-serif; --accent: #7ff0c8; --dim: #cfc9f5; --ground: rgba(52, 40, 130, 0.86); --radius: 14px; }
.popup {
  border: 3px double #b9b0f2;
  padding: 10px 14px;
  color: #fff;
}
.headword { text-shadow: 0 0 10px rgba(255, 255, 255, 0.5); }
.entry + .entry { border-top-color: rgba(185, 176, 242, 0.45); }
.tag { background: rgba(255, 255, 255, 0.12); border: 1px solid rgba(185, 176, 242, 0.6); border-radius: 999px; padding: 0 8px; color: #e9e5ff; }
.tag.frequency { border-color: #7ff0c8; color: #c4ffe9; }
.card, .card.book { background: rgba(255, 255, 255, 0.14); border: 1px solid #b9b0f2; border-radius: 8px; color: #fff; }
.card:active, .card.pressed { background: rgba(224, 79, 168, 0.6); }
)popup"},
    {"glass", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Glass: a pane of smoked glass over the game, made for the opacity slider (two thirds solid
   by itself). White type with a soft shadow so it reads on any scene, hairlines of light, and
   no boxes inside it: labels are small tracked capitals, not chips. --ground, --text, --dim,
   --accent and --rule are the whole palette. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: rgba(14, 16, 22, 0.66); --text: #ffffff; --dim: rgba(255, 255, 255, 0.68); --accent: #9ad4ff;
  --rule: rgba(255, 255, 255, 0.18); --edge: var(--rule); --radius: 14px;
  --box: transparent; --box-pressed: rgba(255, 255, 255, 0.22); --sign: #ffffff; --book: var(--accent);
}
.popup {
  border: 1px solid var(--rule); border-top-color: rgba(255, 255, 255, 0.4);
  padding: calc(11px * var(--scale)) calc(15px * var(--scale));
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.7), 0 0 10px rgba(0, 0, 0, 0.35);
  -webkit-backdrop-filter: blur(14px) saturate(1.15); backdrop-filter: blur(14px) saturate(1.15);
  font-feature-settings: "tnum" 1;
}
.entry { padding: 2px 0 8px; }
.entry + .entry { border-top-color: var(--rule); padding-top: 8px; }
.head { gap: 0 0.8em; }
.headword { font-weight: 600; font-size: calc(22px * var(--scale)); letter-spacing: 0.02em; }
.headword rt { display: ruby-text; color: var(--dim); letter-spacing: 0.1em; }
.reading { display: none; }
.reasons, .pitch, .tag, .dictionary-name, .lookups, .romaji { font-size: calc(10.5px * var(--scale)); letter-spacing: 0.08em; color: var(--dim); }
.tag, .dictionary-name { text-transform: uppercase; }
.tags { gap: 0 0.9em; }
.tag { background: none; border: none; padding: 0; }
.tag.frequency .tag-value { color: var(--text); }
.tag.name { border: none; padding: 0; color: var(--accent); }
.tag.role { color: var(--accent); }
.dictionary-name { margin-top: 6px; }
.sense .tag[data-category="part-of-speech"] { color: var(--accent); }
.glosses .gloss + .gloss::before { content: " / "; color: var(--dim); }
.entry.unknown .headword { background: none; box-shadow: none; padding: 0; text-decoration: underline; text-decoration-color: var(--accent); text-decoration-thickness: 1.5px; text-underline-offset: 6px; }
.name-card .portrait { border: 1px solid rgba(255, 255, 255, 0.35); border-radius: calc(8px * var(--scale)); }
.alias { background: none; border: none; padding: 0; font-size: calc(12px * var(--scale)); }
.alias + .alias::before { content: "/ "; color: var(--dim); }
.also { gap: 0 0.8em; }
.card { border-radius: 999px; border-color: rgba(255, 255, 255, 0.4); text-shadow: none; }
)popup"},
    {"kon", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Kon (紺, deep indigo): Furoshiki 3's Kon theme as a popup. Cream type on indigo, labels in
   small tracked capitals in the indigo's own pale blue, hairlines in cream, and one vermilion:
   a rule along the top, and the word not yet known. Every colour is a variable. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Hiragino Sans", "Meiryo", sans-serif;
  --ground: #101a3a; --raised: #18244d; --text: #ece5d3; --dim: #8a93b5; --accent: #d9472b;
  --rule: rgba(236, 229, 211, 0.22);
  --edge: var(--rule); --radius: 2px;
  --box: transparent; --box-pressed: var(--raised); --sign: var(--text); --book: var(--dim);
}
.popup {
  border: 1px solid var(--rule); border-top: 3px solid var(--accent);
  padding: calc(11px * var(--scale)) calc(16px * var(--scale)) calc(10px * var(--scale));
  font-size: calc(13.5px * var(--scale)); line-height: 1.5; font-feature-settings: "tnum" 1, "palt" 1;
}
.entry { padding: 0 0 calc(10px * var(--scale)); }
.entry + .entry { border-top: 1px solid var(--rule); padding-top: calc(10px * var(--scale)); }
.head { gap: 0 0.9em; align-items: baseline; }
.headword { font-size: calc(25px * var(--scale)); font-weight: 600; letter-spacing: 0.02em; line-height: 1.7; }
.headword rt { display: ruby-text; font-size: 0.42em; color: var(--dim); letter-spacing: 0.1em; }
.reading { display: none; }
.reasons, .pitch, .lookups, .tag, .dictionary-name, .romaji, .more {
  font-size: calc(10px * var(--scale)); letter-spacing: 0.12em; text-transform: uppercase; color: var(--dim);
}
.lookups { margin: calc(-2px * var(--scale)) 0 calc(4px * var(--scale)); text-transform: none; letter-spacing: 0.04em; }
.tags { gap: calc(4px * var(--scale)) 0.6em; }
.tag { background: none; border: 1px solid var(--rule); border-radius: 2px; padding: 0 5px; }
.tag.frequency { background: var(--raised); border-color: transparent; }
.tag.frequency .tag-value { color: var(--text); letter-spacing: 0.04em; }
.tag.name { color: var(--accent); border-color: var(--accent); }
.tag.role { color: var(--text); }
.dictionary-name { margin-top: calc(8px * var(--scale)); padding-bottom: 2px; border-bottom: 1px solid var(--rule); }
.senses { margin-top: calc(3px * var(--scale)); }
.sense { padding-left: 1.6em; margin: 1px 0; }
.sense > .number { display: inline-block; width: 1.6em; margin: 0 0 0 -1.6em; color: var(--dim); font-size: 0.9em; }
.sense .tag { margin-right: 0.6em; }
.glosses .gloss + .gloss::before { content: " · "; color: var(--dim); }
.notes { color: var(--dim); font-size: 0.9em; }
.forms { margin-top: calc(4px * var(--scale)); }
.entry.unknown .headword { background: none; box-shadow: none; padding: 0; border-radius: 0; color: var(--accent); }
.name-card { gap: calc(14px * var(--scale)); margin: calc(6px * var(--scale)) 0 calc(4px * var(--scale)); }
.name-card .portrait { width: calc(60px * var(--scale)); border-radius: 2px; border: 1px solid var(--rule); }
.popup[data-portrait="large"] .name-card .portrait { width: calc(92px * var(--scale)); }
.who-name { font-size: calc(19px * var(--scale)); font-weight: 600; letter-spacing: 0.04em; }
.who-line { gap: 0 1em; }
.alias { background: var(--raised); border: none; border-radius: 2px; font-size: calc(12px * var(--scale)); }
.about { color: var(--text); opacity: 0.85; margin-top: calc(6px * var(--scale)); max-width: 46em; }
.card-buttons { margin-top: calc(6px * var(--scale)); }
.card { border: 1px solid var(--rule); border-radius: 2px; width: calc(26px * var(--scale)); height: calc(26px * var(--scale)); }
.card:hover { border-color: var(--text); }
.card svg { width: 46%; height: 46%; }
.link { color: var(--text); text-decoration: none; border-bottom: 1px solid var(--accent); }
.more { margin-top: 2px; display: inline-block; }
)popup"},
    {"kumihan", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Kumihan (組版, typesetting): type and rules, nothing else. One ink on one ground, a single
   accent used once (the word not yet known), labels in small tracked capitals, figures that
   line up, hairlines where a box would be. Every colour is a variable: set --ground, --text,
   --dim, --accent and --rule in your own CSS and the whole look follows. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Hiragino Sans", "Meiryo", sans-serif;
  --ground: #101012; --text: #ecebe6; --dim: #8d8c86; --accent: #e8502a; --rule: rgba(236, 235, 230, 0.16);
  --edge: var(--rule); --radius: 0px;
  --box: transparent; --box-pressed: color-mix(in srgb, var(--text) 14%, transparent); --sign: var(--text); --book: var(--dim);
  --label: 0.62em;
}
.popup {
  border: 1px solid var(--rule); border-top: 2px solid var(--text);
  padding: calc(12px * var(--scale)) calc(16px * var(--scale)) calc(10px * var(--scale));
  font-size: calc(13.5px * var(--scale)); line-height: 1.5; font-feature-settings: "tnum" 1, "palt" 1;
}
.entry { padding: 0 0 calc(10px * var(--scale)); }
.entry + .entry { border-top: 1px solid var(--rule); padding-top: calc(10px * var(--scale)); }
.head { gap: 0 0.9em; align-items: baseline; }
.headword { font-size: calc(26px * var(--scale)); font-weight: 500; letter-spacing: 0.02em; line-height: 1.7; }
.headword rt { display: ruby-text; font-size: 0.4em; color: var(--dim); letter-spacing: 0.12em; }
.reading { display: none; }
.reasons, .pitch, .lookups, .tag, .dictionary-name, .romaji, .more {
  font-size: calc(10px * var(--scale)); letter-spacing: 0.1em; text-transform: uppercase; color: var(--dim);
}
.reasons { color: var(--dim); }
.pitch { color: var(--dim); }
.lookups { margin: calc(-2px * var(--scale)) 0 calc(4px * var(--scale)); text-transform: none; letter-spacing: 0.04em; }
.tags { gap: 0 1em; }
.tag { background: none; border: none; padding: 0; }
.tag.frequency .tag-value { color: var(--text); letter-spacing: 0.04em; }
.tag.name { border: none; padding: 0; color: var(--text); border-bottom: 1px solid var(--text); border-radius: 0; }
.tag.role { color: var(--text); }
.dictionary-name { margin-top: calc(8px * var(--scale)); padding-bottom: 2px; border-bottom: 1px solid var(--rule); }
.senses { margin-top: calc(3px * var(--scale)); }
.sense { padding-left: 1.6em; margin: 1px 0; }
.sense > .number { display: inline-block; width: 1.6em; margin: 0 0 0 -1.6em; color: var(--dim); font-size: 1em; text-align: left; }
.sense .tag { margin-right: 0.7em; }
.glosses .gloss + .gloss::before { content: " / "; color: var(--dim); }
.notes { color: var(--dim); font-size: 0.9em; }
.forms { margin-top: calc(4px * var(--scale)); }
.entry.unknown .headword { background: none; box-shadow: none; padding: 0; border-radius: 0; color: var(--accent); }
.name-card { gap: calc(14px * var(--scale)); margin: calc(6px * var(--scale)) 0 calc(4px * var(--scale)); }
.name-card .portrait { width: calc(60px * var(--scale)); border-radius: 0; border: none; filter: grayscale(1) contrast(1.05); }
.popup[data-portrait="large"] .name-card .portrait { width: calc(92px * var(--scale)); }
.who-name { font-size: calc(19px * var(--scale)); font-weight: 500; letter-spacing: 0.04em; }
.who-name rt { letter-spacing: 0.12em; }
.who-line { gap: 0 1em; }
.also { gap: 0 0.9em; margin-top: calc(2px * var(--scale)); }
.alias { background: none; border: none; padding: 0; border-radius: 0; font-size: calc(12px * var(--scale)); }
.alias + .alias::before { content: "/ "; color: var(--dim); }
.about { color: var(--text); opacity: 0.82; margin-top: calc(6px * var(--scale)); max-width: 46em; }
.card-buttons { margin-top: calc(6px * var(--scale)); }
.card { border: 1px solid var(--rule); border-radius: 0; width: calc(26px * var(--scale)); height: calc(26px * var(--scale)); }
.card svg { width: 46%; height: 46%; }
.link { color: var(--text); text-decoration: none; border-bottom: 1px solid var(--rule); }
.more { margin-top: 2px; display: inline-block; }
)popup"},
    {"kuro", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Kuro: a black sheet, each dictionary a block of its own under a thin rule (after JL): Meiryo,
   the reading beside the word in its own colour, frequencies as plain numbers, most at home at
   85 % solid. */
:root {
  --font: "Meiryo", "Yu Gothic UI", "Noto Sans JP", sans-serif;
  --ground: rgba(0, 0, 0, 0.85); --text: #ffffff; --dim: #a9a9a9; --accent: #7bc8ff; --edge: #5a5a5a; --radius: 0px;
  --box: #000; --box-pressed: #2b2b2b; --sign: #ffffff; --book: #ffd75e;
}
.popup { border: 1px solid #5a5a5a; padding: calc(6px * var(--scale)) calc(9px * var(--scale)); }
.entry + .entry { border-top: 2px solid #3d3d3d; }
.headword { font-size: calc(21px * var(--scale)); font-weight: 400; color: #7bc8ff; }
.reading { color: #ffd75e; font-size: calc(15px * var(--scale)); }
.reasons { color: #c9a0ff; }
.pitch { color: #9be29b; }
.tag { color: #a9a9a9; }
.tag.frequency .tag-name { color: #ff9ecb; }
.tag.frequency .tag-value { color: #ff9ecb; }
.tag.name { border-radius: 0; }
.lookups { color: #7f7f7f; }
.dictionary-name { color: #e8c98b; border-top: 1px solid #3d3d3d; margin-top: 4px; padding-top: 3px; font-size: calc(11px * var(--scale)); }
.dictionary-name:first-of-type { border-top: none; }
.senses { font-size: calc(14px * var(--scale)); }
.sense .tag[data-category="part-of-speech"] { color: #9be29b; }
.sense .tag[data-category="part-of-speech"]::before { content: "("; }
.sense .tag[data-category="part-of-speech"]::after { content: ")"; }
.number { color: #fff; }
.number::after { content: "."; }
.glosses .gloss + .gloss::before { content: "; "; color: #a9a9a9; }
.entry.unknown .headword { background: none; box-shadow: none; padding: 0; border-bottom: 1px dashed #7bc8ff; border-radius: 0; }
.who-name { color: #fff; font-weight: 400; }
.name-card .portrait { border-radius: 0; border-color: #5a5a5a; }
.alias { border-radius: 0; background: none; border-color: #3d3d3d; color: #ffd75e; }
.card { border-radius: 0; border-color: #5a5a5a; }
)popup"},
    {"ookii", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Ookii (大きい): for reading from the sofa. Large type on a generous scale, one sense to a
   line with room between them, the reading always over the word; no chips or circles, only
   size, weight and one accent. --ground, --text, --dim, --accent and --rule are the palette. */
:root {
  --font: "Noto Sans JP", "Hiragino Sans", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: rgba(22, 24, 30, 0.97); --text: #f4f3ee; --dim: #9c9fa8; --accent: #f2c14e; --rule: rgba(244, 243, 238, 0.14);
  --edge: var(--rule); --radius: 10px;
  --box: transparent; --box-pressed: rgba(244, 243, 238, 0.16); --sign: var(--text); --book: var(--accent);
}
.popup { border: 1px solid var(--rule); padding: calc(16px * var(--scale)) calc(20px * var(--scale)); font-size: calc(17px * var(--scale)); line-height: 1.55; box-shadow: 0 10px 34px rgba(0, 0, 0, 0.5); font-feature-settings: "tnum" 1, "palt" 1; }
.entry { padding: 2px 0 10px; }
.entry + .entry { border-top: 1px solid var(--rule); padding-top: 12px; }
.head { gap: 0 1em; }
.headword { font-size: calc(36px * var(--scale)); font-weight: 700; letter-spacing: 0.02em; line-height: 1.6; }
.headword rt { display: ruby-text; font-size: 0.38em; color: var(--dim); font-weight: 400; letter-spacing: 0.12em; }
.reading { display: none; }
.reasons, .pitch, .lookups, .tag, .notes, .dictionary-name, .romaji, .more { font-size: calc(12px * var(--scale)); letter-spacing: 0.08em; color: var(--dim); }
.tag, .dictionary-name { text-transform: uppercase; }
.tags { gap: 0 1em; }
.tag { background: none; border: none; padding: 0; }
.tag.frequency .tag-value { color: var(--text); }
.tag.name, .tag.role { border: none; padding: 0; color: var(--accent); }
.sense { padding-left: 1.4em; margin: 6px 0; }
.sense > .number { display: inline-block; width: 1.4em; margin: 0 0 0 -1.4em; font-size: 1em; color: var(--accent); font-weight: 700; }
.glosses .gloss + .gloss::before { content: " / "; color: var(--dim); }
.dictionary-name { margin-top: 10px; padding-bottom: 3px; border-bottom: 1px solid var(--rule); }
.entry.unknown .headword { background: none; box-shadow: inset 0 -0.12em 0 var(--accent); padding: 0; border-radius: 0; }
.name-card { gap: calc(16px * var(--scale)); }
.name-card .portrait { width: calc(80px * var(--scale)); border-radius: calc(6px * var(--scale)); border: 1px solid var(--rule); }
.popup[data-portrait="large"] .name-card .portrait { width: calc(116px * var(--scale)); }
.who-name { font-size: calc(26px * var(--scale)); font-weight: 700; }
.alias { background: none; border: none; padding: 0; font-size: calc(14px * var(--scale)); }
.alias + .alias::before { content: "/ "; color: var(--dim); }
.also { gap: 0 0.8em; }
.about { font-size: calc(15px * var(--scale)); line-height: 1.6; margin-top: 8px; }
.card { width: calc(36px * var(--scale)); height: calc(36px * var(--scale)); border-radius: var(--radius); border: 1px solid var(--rule); }
)popup"},
    {"paper-dark", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Paper dark: Paper's quiet serif on dark brown, cream ink. */
:root { --font: "Noto Serif JP", "Yu Mincho", serif; --dim: #b8a88a; --accent: #d9a55a; --ground: #2b2119; --radius: 4px; --text: #efe4cf; }
.popup { border: 1px solid #5a4632; color: #efe4cf; box-shadow: 0 4px 18px rgba(0, 0, 0, 0.6); }
.entry + .entry { border-top-color: #4a3a2a; }
.tag { font-family: "Noto Sans JP", sans-serif; color: #e3cfa6; background: #3e3023; border-radius: 3px; padding: 0 5px; }
.tag.frequency { background: #2f3a26; color: #bcd6a8; }
.entry.unknown .headword { background: rgba(217, 165, 90, 0.28); box-shadow: none; }
.card, .card.book { background: #3e3023; border-color: #8a7050; color: #efd9ae; }
.card:active, .card.pressed { background: #57432f; }
)popup"},
    {"paper", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Paper: light, quiet, a serif. */
:root { --font: "Noto Serif JP", "Yu Mincho", serif; --dim: #7a6e5a; --accent: #b0782a; --ground: #fbf8f1; --radius: 4px; --text: #2a2620; }
.popup { border: 1px solid #d9d0bd; color: #2a2620; box-shadow: 0 4px 18px rgba(0, 0, 0, 0.5); }
.entry + .entry { border-top-color: #e2d9c6; }
.tag { font-family: "Noto Sans JP", sans-serif; color: #6b5a3a; background: #efe6d2; border-radius: 3px; padding: 0 5px; }
.tag.frequency { background: #e3eedc; color: #3e6a35; }
.entry.unknown .headword { background: #fff1b8; box-shadow: none; }
.card, .card.book { background: #efe6d2; border-color: #a8956c; color: #5a4524; }
.card:active, .card.pressed { background: #e2d4b3; }
)popup"},
    {"shoin", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Shoin (書院): a page from a well-set dictionary. Ivory paper inside one ruled frame, a mincho
   headword with its reading over it, senses numbered in the margin in vermilion, the
   dictionary named in small tracked capitals over a hairline. --ground, --text, --dim,
   --accent (the vermilion) and --rule are the palette. */
:root {
  --font: "Noto Serif JP", "Yu Mincho", "Hiragino Mincho ProN", serif;
  --ground: #f5efe1; --text: #26211a; --dim: #807259; --accent: #a8321f; --rule: #c9bb99; --edge: var(--rule); --radius: 0px;
  --box: transparent; --box-pressed: #e6dcc3; --sign: var(--accent); --book: var(--text);
  --sans: "Noto Sans JP", "Yu Gothic UI", sans-serif;
}
.popup {
  border: 1px solid var(--text); outline: 1px solid var(--rule); outline-offset: -5px;
  padding: calc(14px * var(--scale)) calc(17px * var(--scale)) calc(12px * var(--scale));
  box-shadow: 0 6px 20px rgba(0, 0, 0, 0.45); font-feature-settings: "palt" 1;
}
.entry + .entry { border-top: 1px solid var(--rule); margin-top: 6px; padding-top: 8px; }
.head { gap: 0 0.9em; }
.headword { font-size: calc(26px * var(--scale)); font-weight: 600; letter-spacing: 0.05em; }
.headword rt { display: ruby-text; color: var(--dim); letter-spacing: 0.14em; font-size: 0.4em; }
.reading { display: none; }
.reasons { color: var(--dim); font-style: italic; }
.pitch { color: var(--dim); }
.tag, .dictionary-name, .lookups, .romaji, .more { font-family: var(--sans); font-size: calc(10px * var(--scale)); letter-spacing: 0.1em; color: var(--dim); }
.tag, .dictionary-name { text-transform: uppercase; }
.tags { gap: 0 0.9em; }
.tag { border: none; background: none; padding: 0; }
.tag.frequency .tag-value { color: var(--text); font-weight: 700; }
.tag.name { color: var(--accent); border: none; padding: 0; }
.tag.role { color: var(--accent); }
.dictionary-name { margin: 8px 0 3px; padding-bottom: 2px; border-bottom: 1px solid var(--rule); }
.sense { padding-left: 1.5em; margin: 2px 0; font-size: calc(14px * var(--scale)); line-height: 1.55; }
.sense > .number { display: inline-block; width: 1.5em; margin: 0 0 0 -1.5em; color: var(--accent); font-size: 1em; font-feature-settings: "tnum" 1; }
.glosses .gloss + .gloss::before { content: " ・ "; color: var(--dim); }
.notes { font-style: italic; color: var(--dim); }
.entry.unknown .headword { background: none; box-shadow: none; padding: 0; text-decoration: underline; text-decoration-color: var(--accent); text-decoration-thickness: 1px; text-underline-offset: 7px; }
.name-card .portrait { border: 1px solid var(--text); border-radius: 0; }
.who-name { font-size: calc(20px * var(--scale)); letter-spacing: 0.05em; }
.alias { border: none; background: none; padding: 0; border-radius: 0; font-size: calc(12.5px * var(--scale)); }
.alias + .alias::before { content: "・"; color: var(--rule); }
.also { gap: 0 0.2em; }
.about { font-size: calc(12.5px * var(--scale)); line-height: 1.65; }
.card { border-radius: 0; border-color: var(--text); }
.link { color: var(--text); }
)popup"},
    {"solarized-dark", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Solarized Dark (Ethan Schoonover's palette): base03 ground, base02 panels, base1 text, the
   yellow as the accent; labels as coloured text, no boxes; the palette's names are in the rules below, the main ones variables. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: #002b36; --text: #93a1a1; --dim: #839496; --accent: #b58900; --edge: #586e75; --radius: 8px;
  --box: #073642; --box-pressed: #0a4a5a; --sign: #b58900; --book: #2aa198;
}
.popup { border: 1px solid #586e75; padding: calc(9px * var(--scale)) calc(12px * var(--scale)); box-shadow: 0 6px 22px rgba(0, 0, 0, 0.45); }
.entry + .entry { border-top-color: #073642; }
.headword { color: #eee8d5; font-weight: 700; font-size: calc(20px * var(--scale)); }
.headword rt { display: ruby-text; color: #839496; }
.reading { display: none; }
.reasons { color: #cb4b16; }
.pitch { color: #6c71c4; }
.tag { background: none; padding: 0; color: #93a1a1; letter-spacing: 0.04em; }
.tags { gap: 0 0.9em; }
.tag.frequency .tag-name { color: #2aa198; }
.tag.frequency .tag-value { color: #eee8d5; }
.tag.name { background: none; color: var(--accent); border: none; padding: 0; font-weight: 700; }
.tag.role { background: none; border: none; color: var(--accent); }
.sense .tag[data-category="part-of-speech"] { color: #268bd2; }
.number { color: #586e75; }
.glosses { color: #93a1a1; }
.glosses .gloss + .gloss::before { color: #586e75; }
.dictionary-name { color: #2aa198; letter-spacing: 0.06em; text-transform: uppercase; font-size: calc(9.5px * var(--scale)); margin-top: 3px; }
.entry.unknown .headword { background: none; box-shadow: inset 0 -2px 0 #b58900; border-radius: 0; padding: 0; }
.who-name { color: #eee8d5; }
.alias { background: none; border: none; padding: 0; }
.alias + .alias::before { content: "/ "; color: var(--dim); }
.also { gap: 0 0.7em; }
.name-card .portrait { border-color: #586e75; }
.card { border-color: #586e75; border-radius: 6px; }
.link { color: #268bd2; }
)popup"},
    {"solarized-light", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Solarized Light: the same palette turned over, base3 ground and base00 text. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: #fdf6e3; --text: #657b83; --dim: #93a1a1; --accent: #b58900; --edge: #d3cbb7; --radius: 8px;
  --box: #eee8d5; --box-pressed: #e0d9c2; --sign: #b58900; --book: #2aa198;
}
.popup { border: 1px solid #d3cbb7; padding: calc(9px * var(--scale)) calc(12px * var(--scale)); box-shadow: 0 6px 22px rgba(0, 0, 0, 0.35); }
.entry + .entry { border-top-color: #eee8d5; }
.headword { color: #073642; font-weight: 700; font-size: calc(20px * var(--scale)); }
.headword rt { display: ruby-text; color: #93a1a1; }
.reading { display: none; }
.reasons { color: #cb4b16; }
.pitch { color: #6c71c4; }
.tag { background: none; padding: 0; color: #657b83; letter-spacing: 0.04em; }
.tags { gap: 0 0.9em; }
.tag.frequency .tag-name { color: #2aa198; }
.tag.frequency .tag-value { color: #073642; }
.tag.name { background: none; color: var(--accent); border: none; padding: 0; font-weight: 700; }
.tag.role { background: none; border: none; color: var(--accent); }
.sense .tag[data-category="part-of-speech"] { color: #268bd2; }
.dictionary-name { color: #2aa198; letter-spacing: 0.06em; text-transform: uppercase; font-size: calc(9.5px * var(--scale)); margin-top: 3px; }
.entry.unknown .headword { background: none; box-shadow: inset 0 -2px 0 #b58900; border-radius: 0; padding: 0; }
.who-name { color: #073642; }
.alias { background: none; border: none; padding: 0; }
.alias + .alias::before { content: "/ "; color: var(--dim); }
.also { gap: 0 0.7em; }
.card { border-color: #93a1a1; border-radius: 6px; }
.link { color: #268bd2; }
)popup"},
    {"yomitan-dark", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Yomitan dark, after the maintainer's screenshot: black, serif glosses, glowing tag boxes,
   furigana over the headword, a blue triangle beside it. */
:root { --font: "Noto Serif JP", "Yu Mincho", serif; --dim: #c8c8d8; --ground: #0b0b0b; --radius: 8px; --text: #f2f2f2; --accent: #6aa0ff; }
.popup { border: 1px solid #555; padding: 10px 14px 10px 18px; color: #f2f2f2; }
.entry::before { content: ""; position: absolute; left: -15px; top: calc(20px * var(--scale)); border: 7px solid transparent; border-left: 8px solid #3b8cf0; }
.headword { font-weight: 400; font-size: calc(26px * var(--scale)); }
.headword rt { display: ruby-text; }
.reading { display: none; }
.tags { gap: 6px; }
.tag {
  font-family: "Noto Sans JP", sans-serif; font-weight: 700; font-size: calc(12.5px * var(--scale));
  background: #000; color: #e6e6ff; border: 1px solid #c9c9ee; border-radius: 5px; padding: 1px 6px;
  box-shadow: 0 0 6px rgba(200, 200, 255, 0.7);
}
/* The frequency as one small box: its source and number on a line, no bigger than a tag. */
.tag.frequency { font-size: calc(10.5px * var(--scale)); padding: 0 5px; }
.tag.frequency .tag-name { color: #bfe6c8; }
.tag.frequency .tag-value { font-weight: 400; color: #fff; }
.glosses, .forms .form { font-size: calc(16px * var(--scale)); }
.number { display: none; }
.entry.unknown .headword { background: linear-gradient(transparent 62%, rgba(59, 140, 240, 0.35) 62%); box-shadow: none; }
.card { background: transparent; border: none; color: #5cbf5c; }
.card.book { color: #6aa0ff; }
)popup"},
};
