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
/* An entry taller than the popup keeps its headline in sight: it stays at the top (under the tab
   row where that stays too) while the meanings go on under it. What has gone under it is cut
   away by the script (pin(), a clip-path on each part), so that nothing shows through a popup
   that is not solid, whatever the theme's ground is. */
.entry > .head { position: sticky; top: var(--pin, 0px); z-index: 2; }
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
/* How often the word was looked up: one line under the definition, cut with an ellipsis. */
.lookups { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; margin-top: calc(3px * var(--scale)); }
/* A dictionary's name: a slim label with its rule running on to the right. */
.dictionary-name { display: flex; align-items: center; gap: 0.7em; }
.dictionary-name::after { content: ""; flex: 1; height: 1px; background: var(--edge); opacity: 0.8; }
.sense .tag { margin-right: 0.3em; }
/* A sense's tags: one quiet run, small, the ones past the second behind a count. */
.sense-tags { display: inline; margin-right: 0.15em; }
.sense-tags .tag { font-size: calc(9.5px * var(--scale)); opacity: 0.85; vertical-align: 0.08em; }
.sense-tags .tag.extra { display: none; }
.sense-tags.all .tag.extra { display: inline; }
.sense-tags.all .more-tags { display: none; }
.sense-tags .more-tags { cursor: pointer; }
.popup.no-tags .sense-tags { display: none; }
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
/* The card with the line's audio: the same button and the same plus, with a small speaker in its
   upper corner, clear of the plus's arms. */
.card .with-audio {
  position: absolute; top: 5%; right: 5%; width: 38%; height: 38%; display: block; opacity: 0.9;
  animation: none !important;
}
.card[data-state="working"] { opacity: 0.4; }
.card.making { opacity: 1; }
.card.making svg { animation: reader-turn 0.7s linear infinite; width: 52%; height: 52%; }
/* A press: the button gives and springs back, and a ring goes out from it. */
.card.pressing { animation: reader-press 0.26s cubic-bezier(0.2, 0.9, 0.3, 1.2); }
.card.pressing::before, .card.landing::before {
  content: ""; position: absolute; inset: -1px; border-radius: inherit; pointer-events: none;
  border: 1px solid var(--accent, currentColor); animation: reader-ring 0.36s ease-out forwards;
}
/* The card made: the book lands where the plus was. */
.card.landing { animation: reader-land 0.42s cubic-bezier(0.2, 0.9, 0.25, 1.25); }
.card.landing svg { animation: reader-land-sign 0.42s cubic-bezier(0.2, 0.9, 0.25, 1.25); }
@keyframes reader-press { 0% { transform: scale(1); } 35% { transform: scale(0.86); } 100% { transform: scale(1); } }
@keyframes reader-ring { from { opacity: 0.9; transform: scale(1); } to { opacity: 0; transform: scale(1.55); } }
@keyframes reader-land { 0% { transform: scale(0.8); } 100% { transform: scale(1); } }
@keyframes reader-land-sign { 0% { transform: scale(0.4) rotate(-12deg); opacity: 0; } 60% { opacity: 1; } 100% { transform: none; } }
/* The d-pad's step to another entry: it arrives from the side it came from, very quickly. */
.entry.arriving { animation: reader-arrive 0.13s cubic-bezier(0.2, 0.8, 0.2, 1); }
.entry.arriving::before { animation: reader-mark 0.16s cubic-bezier(0.2, 0.8, 0.2, 1); transform-origin: 50% 50%; }
@keyframes reader-arrive { from { transform: translateY(var(--from, 6px)); opacity: 0.45; } to { transform: none; opacity: 1; } }
@keyframes reader-mark { from { transform: scaleY(0.2); opacity: 0.2; } to { transform: none; opacity: 1; } }
.card[data-state="checking"] svg { animation: reader-turn 0.9s linear infinite; }
.card[data-state="away"] svg { opacity: 0.6; }
.card[data-state="away"]::after {
  content: ""; position: absolute; right: -3px; top: -3px; width: 30%; height: 30%;
  border-radius: 50%; background: #8a8aa0; border: 1px solid var(--box);
}
@keyframes reader-turn { to { transform: rotate(360deg); } }
@media (prefers-reduced-motion: reduce) {
  .card[data-state="checking"] svg, .card.pressing, .card.landing, .card.landing svg, .card.pressing::before,
  .card.landing::before, .entry.arriving, .entry.arriving::before { animation: none; }
}
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

/* ---- Tabs by kind (words, grammar, kanji, names), each with its count; none for one kind ---- */
.popup.tabbed { padding-top: calc(4px * var(--scale)); }
.tabs {
  display: flex; align-items: stretch; gap: 0; line-height: 1.25;
  margin: 0 0 calc(3px * var(--scale));
  border-bottom: 1px solid var(--edge);
  position: relative;
}
.tab {
  cursor: pointer; color: var(--dim);
  font-size: calc(10.5px * var(--scale)); letter-spacing: 0.12em; text-transform: uppercase;
  padding: calc(3px * var(--scale)) calc(8px * var(--scale)) calc(4px * var(--scale));
  white-space: nowrap;
}
.tab .tab-count { margin-left: 0.5em; font-feature-settings: "tnum" 1; opacity: 0.8; }
.tab.on { color: var(--text); box-shadow: inset 0 -2px 0 var(--accent); }
/* ---- The controller's buttons, marked like the legend on a key cap: only with a controller ---- */
.key-mark { display: none; }
.popup.pad .key-mark {
  display: grid; place-items: center; pointer-events: none;
  /* A small filled cap in the popup's own text colour, the legend cut out in its ground: seen at
     a glance on any theme, but quieter than the buttons it sits on. */
  min-width: calc(14px * var(--scale)); height: calc(14px * var(--scale)); padding: 0 calc(3px * var(--scale));
  border-radius: calc(7px * var(--scale)); box-sizing: border-box;
  background: var(--text); color: var(--ground, #000);
  font: 700 calc(9.5px * var(--scale)) / 1 var(--font); letter-spacing: 0; text-transform: none;
  opacity: 0.88;
}
.popup.pad .tabs .key-mark { align-self: center; margin: 0 calc(4px * var(--scale)); }
.popup.pad .tabs .key-mark.right { margin-left: auto; }
.popup.pad .card { overflow: visible; }
.popup.pad .card .key-mark {
  position: absolute; right: calc(-6px * var(--scale)); bottom: calc(-6px * var(--scale));
  box-shadow: 0 0 0 calc(1.5px * var(--scale)) var(--ground, #000);
}
/* The entry the d-pad is on (Reader.entry): a hairline in the accent down its side, and only its
   card buttons marked, for they are the ones the controller presses. */
.popup.pad.choosing .entry:not(.chosen) .card .key-mark { display: none; }
.popup.pad.choosing .entry.chosen::before {
  content: ""; position: absolute; left: calc(-5px * var(--scale)); top: calc(4px * var(--scale));
  bottom: calc(5px * var(--scale)); width: calc(2px * var(--scale)); border-radius: 1px;
  background: var(--accent, currentColor); pointer-events: none;
}
/* ---- A dictionary's own layout (grammar, characters): as it set it, in the popup's colours ---- */
.sense.laid-out { list-style: none; }
.sense.unnumbered { padding-left: 0; }
.sc { white-space: pre-line; margin: calc(2px * var(--scale)) 0 0; }
.sc-definition + .sc-definition { margin-top: calc(6px * var(--scale)); }
.sc .sc-ul, .sc .sc-ol { margin: 0.15em 0; padding-left: 1.4em; white-space: normal; }
.sc .sc-li { white-space: pre-line; }
.sc .sc-table { border-collapse: collapse; margin: 0.3em 0; white-space: normal; font-size: 0.95em; }
.sc .sc-td, .sc .sc-th { border: 1px solid var(--edge); padding: 0.1em 0.45em; vertical-align: top; text-align: left; }
.sc .sc-th { color: var(--dim); font-weight: 600; }
.sc .sc-rt { color: var(--dim); }
.sc .sc-summary { cursor: pointer; color: var(--dim); }
.sc .sc-img { color: var(--dim); }
/* What grammar dictionaries mark (DOJG, Bunpro, 日本語文型辞典 and others): their formation boxes
   and example sentences. */
.sc [data-sc-content="formation"], .sc [data-sc-content="structure"], .sc [data-sc-class="formation"] {
  border: 1px solid var(--edge); border-radius: 3px; padding: 0.2em 0.5em; margin: 0.3em 0; display: block;
}
.sc [data-sc-content="example-sentence"], .sc [data-sc-content="examples"], .sc [data-sc-content="example"] {
  border-left: 2px solid var(--edge); padding-left: 0.6em; margin: 0.25em 0; display: block; color: color-mix(in srgb, var(--text) 88%, transparent);
}
.sc [data-sc-content="example-sentence-b"], .sc [data-sc-content="translation"] { color: var(--dim); }
/* ---- A character, as a character dictionary sets it out ---- */
.kanji { display: grid; grid-template-columns: auto 1fr; gap: 0 calc(12px * var(--scale)); align-items: start; margin: calc(2px * var(--scale)) 0; }
.kanji .glyph { font-size: calc(54px * var(--scale)); line-height: 1.05; font-weight: 400; }
.kanji-rows { display: grid; grid-template-columns: auto 1fr; gap: calc(1px * var(--scale)) calc(10px * var(--scale)); margin: calc(2px * var(--scale)) 0 0; }
.kanji-rows dt { color: var(--dim); font-size: calc(9.5px * var(--scale)); letter-spacing: 0.12em; text-transform: uppercase; padding-top: 0.25em; }
.kanji-rows dd { margin: 0; }
.kanji-side { min-width: 0; }

/* Where color-mix is not known (the Thor's WebView 109): a color-mix with var() in it is only found
   wrong as it is used, and then counts as unset (no ground at all), so the plain colours are given
   here instead of before it. */
@supports not (color: color-mix(in srgb, red 50%, blue)) {
  .popup { background: var(--ground); }
  .entry.unknown .headword { background: rgba(127, 127, 127, 0.25); box-shadow: none; }
  .tag.name { border-color: var(--accent); }
  .card { border-color: var(--edge); }
  .about, .sc [data-sc-content="example-sentence"] { color: var(--text); }
}
</style>
<style id="console"></style>
<style id="theme"></style>
<style id="device"></style>
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
    } else if (kind === "done") {
      path.setAttribute("d", "M5 12.5l4.5 4.5L19 7.5");
    } else {
      path.setAttribute("d", "M12 4v16M4 12h16");
    }
    svg.appendChild(path);
    return svg;
  }

  /**
   * The card with the line's audio: the same plus as the plain card's, and a small speaker in the
   * button's upper corner (a speaker alone read as "play the audio"; the maintainer, 2026-11-20:
   * both buttons one size, the plus most of the button, the speaker smaller).
   */
  function speaker() {
    const svg = document.createElementNS(SVG, "svg");
    svg.setAttribute("viewBox", "0 0 12 12");
    svg.setAttribute("aria-hidden", "true");
    svg.setAttribute("class", "with-audio");
    const path = document.createElementNS(SVG, "path");
    path.setAttribute("fill", "none");
    path.setAttribute("stroke", "currentColor");
    path.setAttribute("stroke-width", "1.25");
    path.setAttribute("stroke-linecap", "round");
    path.setAttribute("stroke-linejoin", "round");
    path.setAttribute("d", "M1.4 4.6h2l2.5-2.1v7L3.4 7.4h-2zM8 4.4a2.5 2.5 0 0 1 0 3.2M9.9 2.9a4.9 4.9 0 0 1 0 6.2");
    svg.appendChild(path);
    return svg;
  }

  /**
   * A controller's button as a small mark in a corner, like the legend on a key cap: shown only
   * while a controller is the input (.popup.pad, Reader.setPad), never on touch or a mouse.
   */
  function keyMark(text, className) {
    const mark = element("span", "key-mark" + (className ? " " + className : ""), text);
    mark.setAttribute("aria-hidden", "true");
    return mark;
  }

  /**
   * The controller's button for each card button (PopupKeys on Android): north, east. An app
   * names them as its controller prints them (model.keys: Furoshiki on a computer says Y and B,
   * the Steam Controller's letters); the tab row's L and R likewise (keys.left, keys.right).
   */
  const KEYS = { add: "△", addAudio: "○" };
  function keyOf(kind) {
    const own = model.keys || {};
    return own[kind] || KEYS[kind] || "";
  }

  /** The card button last pressed (by a finger, the pointer or the controller). */
  let pressed = { index: -1, kind: "" };

  /** Plays an animation class on a node once, from its start even when it was just played. */
  function play(node, name) {
    if (!node) return;
    node.classList.remove(name);
    void node.offsetWidth;
    node.classList.add(name);
    node.addEventListener("animationend", () => node.classList.remove(name), { once: true });
  }
  /** A button gives under its press and a ring goes out from it. */
  function press(node) { play(node, "pressing"); }

  function button(kind, index, state, label) {
    const node = element("button", "card " + kind);
    node.type = "button";
    node.dataset.kind = kind;
    node.dataset.index = String(index);
    node.dataset.state = state;
    node.title = label || "";
    node.setAttribute("aria-label", label || "");
    // The one pressed turns while its card is made; the other waits, dimmed.
    const making = state === "working" && pressed.index === index && pressed.kind === kind;
    if (making) node.classList.add("making");
    node.appendChild(sign(kind === "book" ? "book" : state === "checking" || making ? "ring" : "plus"));
    if (kind === "addAudio") node.appendChild(speaker());
    if (keyOf(kind)) node.appendChild(keyMark(keyOf(kind)));
    node.addEventListener("click", (event) => {
      event.stopPropagation();
      if (node.dataset.state === "working") return;
      pressed = { index: index, kind: kind };
      press(node);
      // Both pluses say "add"; "audio" says which (an app from before them reads only the index).
      if (kind === "book") post({ type: "book", index: index });
      else post({ type: "add", index: index, audio: kind === "addAudio" });
    });
    return node;
  }

  /**
   * The buttons an entry offers in its state: the card without the line's audio and the card
   * with it (the controller's △ and ○), the book, or all three.
   */
  function buttons(entry, index) {
    const box = element("div", "card-buttons");
    const labels = model.labels || {};
    const inDeck = entry.state === "inDeck";
    // The pluses are the ones nearer the corner, the book beside them.
    const left = !model.corner || /left$/.test(model.corner);
    const parts = [];
    if (!inDeck || model.duplicates) {
      const state = inDeck ? "add" : entry.state;
      parts.push(button("add", index, state, labels.add));
      parts.push(button("addAudio", index, state, labels.addAudio || labels.add));
    }
    if (inDeck) parts.push(button("book", index, "inDeck", labels.book));
    (left ? parts : parts.reverse()).forEach((part) => box.appendChild(part));
    return box;
  }

  // ---- Kinds: words, grammar, kanji, names (a tab each) ----
  const KINDS = ["word", "grammar", "kanji", "name"];
  const KIND_LABELS = { word: "Words", grammar: "Grammar", kanji: "Kanji", name: "Names" };

  /** What a sense is: its dictionary's kind (sense.kind, made by the app), a name of the game's. */
  function senseKind(sense) {
    if (sense.name) return "name";
    return KINDS.indexOf(sense.kind) >= 0 ? sense.kind : "word";
  }

  /** The kinds an entry has senses of, in the order of its senses. */
  function kindsOf(entry) {
    const out = [];
    const add = (kind) => { if (out.indexOf(kind) < 0) out.push(kind); };
    if (entry.kanji) add("kanji");
    if (entry.card) add("name");
    (entry.senses || []).forEach((sense) => add(senseKind(sense)));
    if (!out.length) add(entry.name ? "name" : "word"); // a word with only its forms
    return out;
  }

  /**
   * The tabs for these entries: each kind that has results, in the fixed order, with how many
   * entries it has; and the kind the popup opens on, the best match's (the first entry's first).
   */
  function tabsOf(entries) {
    const counts = {};
    (entries || []).forEach((entry) => kindsOf(entry).forEach((kind) => { counts[kind] = (counts[kind] || 0) + 1; }));
    const tabs = KINDS.filter((kind) => counts[kind]).map((kind) => ({ kind: kind, count: counts[kind] }));
    const first = entries && entries.length ? kindsOf(entries[0])[0] : (tabs[0] ? tabs[0].kind : "word");
    return { tabs: tabs, first: first };
  }

  let tabs = [];
  let tab = "word";
  /**
   * The entry the controller is on (model.choose: the d-pad goes from one to the next, its card
   * buttons are the ones marked and pressed: Reader.entry, Reader.addChosen), or -1.
   */
  let chosen = -1;

  /** Marks entry [index] as the chosen one and brings it into view ([scroll]). */
  function chooseEntry(index, scroll, step) {
    chosen = index;
    let section = null;
    popup.querySelectorAll(".entry").forEach((node) => {
      const on = Number(node.dataset.index) === index;
      node.classList.toggle("chosen", on);
      if (on) section = node;
    });
    if (!section || !scroll) return;
    const top = section.getBoundingClientRect().top - popup.getBoundingClientRect().top + popup.scrollTop;
    const row = popup.querySelector(".tabs");
    // Under the tab row where it stays at the top; the first entry from the very top.
    const above = row && getComputedStyle(row).position === "sticky" ? row.offsetHeight : 0;
    const first = popup.querySelector(".entry") === section;
    glide(first ? 0 : Math.max(0, top - above - 2));
    // A very short move from the side it came from, so the eye sees which way it went.
    if (step) {
      section.style.setProperty("--from", (step < 0 ? -1 : 1) * 7 + "px");
      play(section, "arriving");
    }
  }

  /** The popup's scroll taken to [to] in about a tenth of a second (at once where motion is unwanted). */
  let gliding = 0;
  const still = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  function glide(to) {
    cancelAnimationFrame(gliding);
    const from = popup.scrollTop;
    if (still || Math.abs(to - from) < 2) { popup.scrollTop = to; return; }
    const began = performance.now(), time = 110;
    const frame = (now) => {
      const part = Math.min(1, (now - began) / time);
      popup.scrollTop = from + (to - from) * (1 - Math.pow(1 - part, 3));
      if (part < 1) gliding = requestAnimationFrame(frame);
      else reportSoon();
    };
    gliding = requestAnimationFrame(frame);
  }
  /** A controller is the input (Reader.setPad, model.pad). */
  let pad = false;

  /** The row of tabs, with the controller's L and R at its ends (shown only with a controller). */
  function tabRow() {
    const row = element("div", "tabs");
    row.setAttribute("role", "tablist");
    row.appendChild(keyMark(keyOf("left") || "L", "left"));
    const labels = model.labels || {};
    tabs.forEach((one) => {
      const node = element("span", "tab" + (one.kind === tab ? " on" : ""));
      node.dataset.kind = one.kind;
      node.setAttribute("role", "tab");
      node.appendChild(element("span", "tab-name", labels[one.kind + "s"] || labels[one.kind] || KIND_LABELS[one.kind]));
      node.appendChild(element("span", "tab-count", String(one.count)));
      node.addEventListener("click", (event) => {
        event.stopPropagation();
        choose(one.kind);
      });
      row.appendChild(node);
    });
    row.appendChild(keyMark(keyOf("right") || "R", "right"));
    return row;
  }

  /** Shows another kind's tab: [kind], or a step along the row (Reader.tab). */
  function choose(kind) {
    if (asking >= 0 || design || kind === tab || !tabs.some((one) => one.kind === kind)) return;
    tab = kind;
    draw(false);
    post({ type: "tab", kind: kind });
  }

  // ---- A dictionary's own layout: Yomitan's structured content, as the dictionary sets it ----
  // Built element by element from a list of what may be there: a dictionary's text is never read
  // as markup, its links lead nowhere, and a style cannot fetch anything.
  const CONTENT_TAGS = new Set(["br", "ruby", "rt", "rp", "table", "thead", "tbody", "tfoot", "tr", "td", "th",
    "span", "div", "ol", "ul", "li", "details", "summary", "a", "img", "b", "i", "em", "strong", "sub", "sup", "p"]);
  const CONTENT_STYLES = ["fontStyle", "fontWeight", "fontSize", "color", "backgroundColor", "textDecorationLine",
    "textDecorationStyle", "textDecorationColor", "borderColor", "borderStyle", "borderRadius", "borderWidth",
    "verticalAlign", "textAlign", "textEmphasis", "margin", "marginTop", "marginLeft", "marginRight", "marginBottom",
    "padding", "paddingTop", "paddingLeft", "paddingRight", "paddingBottom", "wordBreak", "whiteSpace", "listStyleType"];

  function content(node, out) {
    if (node === null || node === undefined) return;
    if (typeof node === "string" || typeof node === "number") {
      out.appendChild(document.createTextNode(String(node)));
      return;
    }
    if (Array.isArray(node)) {
      node.forEach((one) => content(one, out));
      return;
    }
    if (typeof node !== "object") return;
    const tag = String(node.tag || "span");
    if (!CONTENT_TAGS.has(tag)) {
      content(node.content, out);
      return;
    }
    if (tag === "img") {
      // No pictures from a dictionary's zip here; what it stands for, when it says.
      if (node.alt || node.title) out.appendChild(element("span", "sc-img", node.alt || node.title));
      return;
    }
    const made = element(tag === "a" ? "span" : tag, "sc-" + tag);
    if (node.data && typeof node.data === "object") {
      Object.keys(node.data).forEach((key) => {
        if (/^[A-Za-z0-9_-]+$/.test(key)) made.setAttribute("data-sc-" + key.replace(/[A-Z]/g, (c) => "-" + c.toLowerCase()), String(node.data[key]));
      });
    }
    if (node.style && typeof node.style === "object") {
      CONTENT_STYLES.forEach((key) => {
        const value = node.style[key];
        if (value === undefined || value === null) return;
        const text = String(value);
        if (/url\(|expression|javascript:/i.test(text)) return;
        made.style[key] = typeof value === "number" && /^(margin|padding|fontSize|borderWidth|borderRadius)/.test(key) ? value + "em" : text;
      });
    }
    if (node.lang && /^[A-Za-z-]{2,12}$/.test(node.lang)) made.lang = node.lang;
    if (node.title) made.title = String(node.title);
    if ((tag === "td" || tag === "th") && node.colSpan > 1) made.colSpan = Math.min(20, Number(node.colSpan));
    if ((tag === "td" || tag === "th") && node.rowSpan > 1) made.rowSpan = Math.min(50, Number(node.rowSpan));
    if (tag === "details" && node.open) made.open = true;
    if (tag === "a") made.classList.add("link");
    content(node.content, made);
    out.appendChild(made);
  }

  /** One glossary as its dictionary laid it out: its definitions, each a block of its own. */
  function glossary(items) {
    const out = element("div", "sc");
    (Array.isArray(items) ? items : [items]).forEach((item) => {
      const block = element("div", "sc-definition");
      if (typeof item === "string") content(item, block);
      else if (item && item.type === "text") content(item.text, block);
      else if (item && item.type === "structured-content") content(item.content, block);
      else return; // a picture, or the pointer of an inflected form
      out.appendChild(block);
    });
    return out;
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
      const tag = element("span", "tag name", (model.labels && model.labels.name) || "Name");
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

  /** The senses of [kind] (all of them without tabs). */
  function sensesOf(entry, kind) {
    return (entry.senses || []).filter((sense) => !kind || senseKind(sense) === kind);
  }

  /** A dictionary's name as a label: without the date or revision its title ends in. */
  function dictionaryLabel(name) {
    const short = String(name).replace(/\s*[\[(（]?(?:rev\.?\s*)?\d{4}[-./]\d{1,2}[-./]\d{1,2}[\])）]?\s*$/i, "").trim();
    return short || String(name);
  }

  function senses(entry, kind) {
    const out = document.createDocumentFragment();
    let list = null;
    const look = model.look || {};
    // With its card drawn, a name's own senses (the card's words again, as plain lines) are left out.
    const carded = entry.card && look.cards !== false;
    const shown = sensesOf(entry, kind).filter((sense) => !(carded && sense.name));
    // Compact is for words: a grammar or a character dictionary's entry is shown as it is laid out.
    const most = look.compact > 0 && (!kind || kind === "word") ? look.compact : 0;
    // A number says nothing beside a dictionary's only sense.
    const per = {};
    shown.forEach((sense) => { per[sense.dictionary] = (per[sense.dictionary] || 0) + 1; });
    let count = 0;
    // A dictionary's name says nothing where every sense shown is that dictionary's.
    const several = Object.keys(per).length > 1;
    shown.forEach((sense) => {
      if (sense.heading && several) {
        const name = element("div", "dictionary-name");
        name.appendChild(element("span", "dictionary-label", dictionaryLabel(sense.heading)));
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
      item.dataset.kind = senseKind(sense);
      if (sense.name) item.classList.add("name");
      const laidOut = sense.content !== undefined && sense.content !== null;
      if (laidOut) item.classList.add("laid-out");
      if (!laidOut || per[sense.dictionary] > 1) item.appendChild(element("span", "number", String(sense.number)));
      else item.classList.add("unnumbered");
      // The tags stand as one quiet run before the meaning: the first two, the rest behind a
      // count (a tap shows them), so a sense with five tags does not push its meaning away.
      const tags = sense.tags || [];
      if (tags.length) {
        const run = element("span", "sense-tags");
        tags.forEach((tag, at) => {
          const node = element("span", "tag" + (at >= 2 && tags.length > 3 ? " extra" : ""), tag);
          node.dataset.category = "part-of-speech";
          run.appendChild(node);
        });
        if (tags.length > 3) {
          const more = element("span", "tag more-tags", "+" + (tags.length - 2));
          more.dataset.category = "part-of-speech";
          more.title = tags.slice(2).join(" · ");
          more.addEventListener("click", (event) => { event.stopPropagation(); run.classList.add("all"); reportSoon(); });
          run.appendChild(more);
        }
        item.appendChild(run);
      }
      if (laidOut) {
        // A grammar or a character dictionary: its formation boxes, examples and tables as it set them.
        item.appendChild(glossary(sense.content));
      } else {
        const glosses = element("span", "glosses");
        (sense.glosses || []).forEach((gloss) => glosses.appendChild(element("span", "gloss", gloss)));
        item.appendChild(glosses);
        if (sense.notes) item.appendChild(element("span", "notes", " (" + sense.notes + ")"));
      }
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

  /**
   * A character as a character dictionary sets it out: the glyph large, then its rows (meaning,
   * on, kun, strokes with grade and JLPT where the dictionary gives them, parts). [kanji] is a
   * kanji bank's entry (entry.kanji: character, meanings, onyomi, kunyomi, stats, parts); without
   * one, a term dictionary of characters: the glyph and the dictionary's own layout beside it.
   */
  function character(entry, kind) {
    const labels = model.labels || {};
    const out = element("div", "kanji");
    const kanji = entry.kanji;
    const glyph = element("div", "glyph", kanji ? kanji.character : entry.expression);
    glyph.lang = "ja";
    out.appendChild(glyph);
    const rows = element("dl", "kanji-rows");
    const row = (key, label, value) => {
      if (value === undefined || value === null || value === "" || (Array.isArray(value) && !value.length)) return;
      const term = element("dt", null, labels[key] || label);
      term.dataset.row = key;
      const said = element("dd", null, Array.isArray(value) ? value.join(" · ") : String(value));
      said.dataset.row = key;
      rows.appendChild(term);
      rows.appendChild(said);
    };
    if (kanji) {
      const stats = kanji.stats || {};
      row("meaning", "Meaning", kanji.meanings);
      row("on", "On", kanji.onyomi);
      row("kun", "Kun", kanji.kunyomi);
      const counts = [];
      if (stats.strokes) counts.push(String(stats.strokes));
      if (stats.grade) counts.push((labels.grade || "Grade") + " " + stats.grade);
      if (stats.jlpt) counts.push("JLPT N" + String(stats.jlpt).replace(/^N/i, ""));
      if (stats.freq || stats.frequency) counts.push((labels.frequency || "Frequency") + " " + (stats.freq || stats.frequency));
      row("strokes", "Strokes", counts.join(" · "));
      row("parts", "Parts", kanji.parts);
      out.appendChild(rows);
    } else {
      const side = element("div", "kanji-side");
      if (entry.reading) {
        row("reading", "Reading", entry.reading);
        side.appendChild(rows);
      }
      side.appendChild(senses(entry, kind));
      out.appendChild(side);
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

  /** Draws the model; [fresh] for a new one (Reader.show), else another tab of the same. */
  function draw(fresh) {
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
    popup.classList.toggle("pad", pad);
    popup.dataset.portrait = look.portrait || "small";
    if (model.message) {
      popup.appendChild(element("div", "message", model.message.text));
      if (model.message.dictionaries) {
        const link = element("div", "link", (model.labels && model.labels.dictionaries) || "Open dictionaries");
        link.addEventListener("click", () => post({ type: "dictionaries" }));
        popup.appendChild(link);
      }
    }
    // A tab for each kind of result there is (words, grammar, kanji, names), none for one kind;
    // the popup opens on the best match's.
    if (fresh) {
      const found = tabsOf(model.entries);
      tabs = found.tabs;
      tab = found.first;
    }
    const tabbed = tabs.length > 1;
    popup.classList.toggle("tabbed", tabbed);
    popup.dataset.tab = tab;
    if (tabbed && !model.message) popup.appendChild(tabRow());
    (model.entries || []).forEach((entry, index) => {
      const kinds = kindsOf(entry);
      if (tabbed && kinds.indexOf(tab) < 0) return;
      const kind = tabbed ? tab : "";
      const section = element("section", "entry");
      section.dataset.index = String(index);
      section.dataset.state = entry.state || "add";
      section.dataset.kind = kind || kinds[0];
      section.classList.toggle("unknown", !!entry.unknown);
      const asCharacter = (kind || kinds[0]) === "kanji";
      if (!asCharacter) section.appendChild(headline(entry));
      // look.cards false: an app that shows who a name is in a place of its own (Furochibi's pane).
      if (entry.card && (model.look || {}).cards !== false && (!kind || kind === "name")) section.appendChild(nameCard(entry.card));
      if (asCharacter) section.appendChild(character(entry, kind));
      else section.appendChild(senses(entry, kind));
      if (entry.forms && entry.forms.length && (!kind || kind === "word")) section.appendChild(forms(entry));
      // How often it was looked up: one line under the definition, never above it.
      if (entry.history) {
        const lookups = element("div", "lookups", entry.history);
        lookups.title = entry.history;
        section.appendChild(lookups);
      }
      // A kanji bank's character is not a word of the line: no card of it.
      if (!entry.kanji) section.appendChild(buttons(entry, index));
      popup.appendChild(section);
    });
    popup.scrollTop = 0;
    // A controller's d-pad starts on the first entry of the tab.
    popup.classList.toggle("choosing", !!model.choose);
    chosen = -1;
    if (model.choose) {
      const first = popup.querySelector(".entry");
      if (first) chooseEntry(Number(first.dataset.index), false);
    }
    report();
    if (design) mark(picked);
  }

  /** How tall the popup is, where its buttons are and what it says, for the app. */
  function report() {
    pin();
    const boxes = [];
    popup.querySelectorAll(".card").forEach((node) => {
      const r = node.getBoundingClientRect();
      boxes.push({ index: Number(node.dataset.index), kind: node.dataset.kind, x: r.left, y: r.top, w: r.width, h: r.height });
    });
    const content = popup.scrollHeight + (popup.offsetHeight - popup.clientHeight);
    // fit: the height that shows the entry in hand whole (the chosen one, else the first), with
    // the tab row above the first. need: the least that still shows where its definition begins
    // (two lines of it): a popup is never so short that the meaning is out of sight.
    const frame = popup.offsetHeight - popup.clientHeight;
    const style = getComputedStyle(popup);
    const under = parseFloat(style.paddingBottom) || 0;
    const entries = Array.from(popup.querySelectorAll(".entry"));
    const section = entries.find((node) => Number(node.dataset.index) === chosen) || entries[0];
    let fit = content, need = 0;
    if (section) {
      const box = popup.getBoundingClientRect();
      const place = section.getBoundingClientRect();
      const top = section === entries[0] ? 0 : place.top - box.top + popup.scrollTop - (parseFloat(style.paddingTop) || 0);
      fit = Math.ceil(place.bottom - box.top + popup.scrollTop - top + under + frame);
      const first = section.querySelector(".sense, .name-card, .kanji, .forms");
      if (first) {
        const line = parseFloat(getComputedStyle(first).lineHeight) || 20;
        const start = first.getBoundingClientRect().top - box.top + popup.scrollTop - top;
        need = Math.ceil(Math.min(fit, start + Math.min(first.getBoundingClientRect().height, line * 2) + under + frame));
      }
    } else if (popup.firstElementChild) {
      need = content; // a message
    }
    post({ type: "size", width: popup.offsetWidth, height: content, fit: fit, need: need, entries: entries.length, tabs: tabs.length,
           scrolled: popup.scrollTop, boxes: boxes, text: popup.innerText });
  }

  let pending = false;
  function reportSoon() {
    if (pending) return;
    pending = true;
    requestAnimationFrame(() => { pending = false; report(); });
  }

  popup.addEventListener("scroll", reportSoon);

  /**
   * The headline of an entry taller than the popup stays at the top (base.css: sticky); what
   * of the entry has gone under it is cut away here, part by part, so that it is not seen
   * through the headline on a popup that is not solid.
   */
  function pin() {
    const row = popup.querySelector(".tabs");
    const above = row && getComputedStyle(row).position === "sticky" ? row.offsetHeight : 0;
    popup.style.setProperty("--pin", above + "px");
    popup.querySelectorAll(".entry").forEach((section) => {
      const head = section.querySelector(":scope > .head");
      if (!head) return;
      const edge = head.getBoundingClientRect().bottom;
      Array.from(section.children).forEach((part) => {
        if (part === head) return;
        // The card buttons set above the headline (a top corner) never pass under it.
        if (part.classList.contains("card-buttons") && getComputedStyle(part).order === "-1") return;
        const place = part.getBoundingClientRect();
        const under = Math.min(place.height, edge - place.top);
        const cut = under > 0.5 ? "inset(" + under.toFixed(1) + "px -999px -999px -999px)" : "";
        if (part.style.clipPath !== cut) part.style.clipPath = cut;
      });
    });
  }
  popup.addEventListener("scroll", pin);
  /** How much of the popup's height the pinned headline of entry [section] takes. */
  function pinned(section) {
    const head = section && section.querySelector(":scope > .head");
    return head ? head.offsetHeight + (parseFloat(popup.style.getPropertyValue("--pin")) || 0) : 0;
  }
  window.addEventListener("resize", reportSoon);
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(reportSoon);
  // Clicks between the buttons answer nothing, and never reach the game.
  document.addEventListener("mousedown", (event) => { if (!event.target.closest(".card, .link, .tab")) event.preventDefault(); });

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
      if (model.pad !== undefined) pad = !!model.pad;
      draw(true);
    },
    /** The next tab (1) or the one before (-1), round the row: a controller's R1 and L1. */
    tab(step) {
      if (tabs.length < 2) return;
      const at = Math.max(0, tabs.findIndex((one) => one.kind === tab));
      choose(tabs[(at + (step < 0 ? tabs.length - 1 : 1)) % tabs.length].kind);
    },
    /**
     * The next entry (1) or the one before (-1) of the tab, for the controller's d-pad
     * (model.choose): it is marked and brought into view. At the last entry, down only scrolls
     * on to what is left of it.
     */
    entry(step) {
      const all = Array.from(popup.querySelectorAll(".entry")).map((node) => Number(node.dataset.index));
      if (!all.length) return;
      const at = all.indexOf(chosen);
      const next = at < 0 ? 0 : at + (step < 0 ? -1 : 1);
      if (next < 0 || next >= all.length) {
        // A page on, less what the pinned headline covers and a line that was already read.
        const section = popup.querySelector('.entry[data-index="' + chosen + '"]');
        const page = Math.max(40, popup.clientHeight - pinned(section) - 20);
        popup.scrollTop += step < 0 ? -page : page;
        if (next < 0) popup.scrollTop = 0;
      } else {
        chooseEntry(all[next], true, step);
        // The app's own answer to a step (a tick, a touch of the motor).
        post({ type: "feedback", kind: "step" });
      }
      reportSoon();
    },
    /** The chosen entry's card ([audio]: with the line's audio), as its own button makes it. */
    addChosen(audio) {
      if (asking >= 0) return;
      const section = popup.querySelector('.entry[data-index="' + chosen + '"]') || popup.querySelector(".entry");
      const button = section && section.querySelector(".card." + (audio ? "addAudio" : "add"));
      if (button) button.click();
    },
    /** Whether a controller is the input: its buttons are marked on the tabs and the card buttons. */
    setPad(on, keys) {
      pad = !!on;
      popup.classList.toggle("pad", pad);
      // The controller's own letters for the marks (model.keys), known once it was used.
      if (keys) {
        model.keys = keys;
        popup.querySelectorAll(".card .key-mark").forEach((mark) => { mark.textContent = keyOf(mark.parentNode.dataset.kind); });
        const left = popup.querySelector(".tabs .key-mark.left");
        const right = popup.querySelector(".tabs .key-mark.right");
        if (left) left.textContent = keyOf("left") || "L";
        if (right) right.textContent = keyOf("right") || "R";
      }
    },
    /** The tabs these entries make and the one the popup opens on (for the app and the tests). */
    kinds(entries) {
      return tabsOf(entries);
    },
    setBox(index, state) {
      const entry = (model.entries || [])[index];
      if (!entry || entry.state === state) return;
      const was = entry.state;
      entry.state = state;
      const section = popup.querySelector('.entry[data-index="' + index + '"]');
      if (!section) return;
      section.dataset.state = state;
      section.replaceChild(buttons(entry, index), section.querySelector(".card-buttons"));
      // The card was just made: the book lands where the plus was.
      if (state === "inDeck" && was === "working") {
        play(section.querySelector(".card.book"), "landing");
        post({ type: "feedback", kind: "land" });
      }
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
    /** A step down (or up) through the popup: a controller's stick on Android. */
    scrollBy(dy) {
      popup.scrollTop += dy;
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
.headword rt { display: revert; color: var(--dim); letter-spacing: 0.1em; }
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
   small tracked capitals in the indigo's own pale blue, hairlines in cream, a white headword
   with its furigana over it, a white rule along the top, and the word not yet known tinted.
   Every colour is a variable. Since 2026-11-17 Kon is the maintainer's own Kon of their Z13
   (its design "kon-mine": headword #fff in Gothic, furigana at 64 %, unknown as a tint, accent
   and top edge #fff, card buttons 24, frequencies 12.5), for every app. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Hiragino Sans", "Meiryo", sans-serif;
  --ground: #101a3a; --raised: #18244d; --text: #ece5d3; --dim: #8a93b5; --accent: #ffffff;
  --rule: rgba(236, 229, 211, 0.22); --headword: #ffffff; --top: #ffffff;
  --edge: var(--rule); --radius: 2px;
  --box: transparent; --box-pressed: var(--raised); --sign: var(--text); --book: var(--dim);
}
.popup {
  border: 1px solid var(--rule); border-top: 3px solid var(--top);
  padding: calc(11px * var(--scale)) calc(16px * var(--scale)) calc(10px * var(--scale));
  font-size: calc(13.5px * var(--scale)); line-height: 1.5; font-feature-settings: "tnum" 1, "palt" 1;
}
.entry { padding: 0 0 calc(10px * var(--scale)); }
.entry + .entry { border-top: 1px solid var(--rule); padding-top: calc(10px * var(--scale)); }
.head { gap: 0 0.9em; align-items: baseline; }
.headword { font-size: calc(25px * var(--scale)); font-weight: 600; letter-spacing: 0.02em; line-height: 1.3; color: var(--headword);
  font-family: "Noto Sans JP", "Yu Gothic UI", "Hiragino Sans", "Meiryo", sans-serif; }
.headword rt { display: revert; font-size: 0.64em; color: var(--dim); letter-spacing: 0.1em; }
.reading { display: none; }
.reasons, .pitch, .lookups, .tag, .dictionary-name, .romaji, .more {
  font-size: calc(10px * var(--scale)); letter-spacing: 0.12em; text-transform: uppercase; color: var(--dim);
}
.lookups { margin: calc(5px * var(--scale)) 0 0; text-transform: none; letter-spacing: 0.04em; }
.tags { gap: calc(4px * var(--scale)) 0.6em; }
.tag { background: none; border: 1px solid var(--rule); border-radius: 2px; padding: 0 5px; }
.tag.frequency { background: var(--raised); border-color: transparent; font-size: calc(12.5px * var(--scale)); }
.tag.frequency .tag-value { color: var(--text); letter-spacing: 0.04em; }
.tag.name { color: var(--accent); border-color: var(--accent); }
.tag.role { color: var(--text); }
.dictionary-name { margin-top: calc(5px * var(--scale)); }
.dictionary-name::after { background: var(--rule); opacity: 1; }
.popup.tabbed { padding-top: calc(5px * var(--scale)); }
.head { margin-top: calc(2px * var(--scale)); }
.senses { margin-top: calc(3px * var(--scale)); }
.sense { padding-left: 1.6em; margin: 1px 0; }
.sense > .number { display: inline-block; width: 1.6em; margin: 0 0 0 -1.6em; color: var(--dim); font-size: 0.9em; }
.sense .tag { margin-right: 0.35em; }
.sense-tags .tag { font-size: calc(8.5px * var(--scale)); letter-spacing: 0.08em; padding: 0 3px; border-color: transparent;
  background: color-mix(in srgb, var(--text) 9%, transparent); border-radius: 2px; }
.forms .tag { font-size: calc(8.5px * var(--scale)); letter-spacing: 0.08em; padding: 0 3px; }
.glosses .gloss + .gloss::before { content: " · "; color: var(--dim); }
.notes { color: var(--dim); font-size: 0.9em; }
.forms { margin-top: calc(4px * var(--scale)); }
.entry.unknown .headword { box-shadow: none; padding: 0 3px; border-radius: 4px; color: var(--headword);
  background: color-mix(in srgb, var(--accent) 22%, transparent); }
.popup.no-mark .entry.unknown .headword { background: none; padding: 0; }
.name-card { gap: calc(14px * var(--scale)); margin: calc(6px * var(--scale)) 0 calc(4px * var(--scale)); }
.name-card .portrait { width: calc(60px * var(--scale)); border-radius: 2px; border: 1px solid var(--rule); }
.popup[data-portrait="large"] .name-card .portrait { width: calc(92px * var(--scale)); }
.who-name { font-size: calc(19px * var(--scale)); font-weight: 600; letter-spacing: 0.04em; }
.who-line { gap: 0 1em; }
.alias { background: var(--raised); border: none; border-radius: 2px; font-size: calc(12px * var(--scale)); }
.about { color: var(--text); opacity: 0.85; margin-top: calc(6px * var(--scale)); max-width: 46em; }
.card-buttons { margin-top: calc(6px * var(--scale)); }
.card { border: 1px solid var(--rule); border-radius: 2px; width: calc(24px * var(--scale)); height: calc(24px * var(--scale)); }
.card:hover { border-color: var(--text); }
.card svg { width: 64%; height: 64%; }
.card .with-audio { width: 36%; height: 36%; top: 4%; right: 4%; }
.link { color: var(--text); text-decoration: none; border-bottom: 1px solid var(--accent); }
.more { margin-top: 2px; display: inline-block; }
.tab.on { box-shadow: inset 0 -2px 0 var(--accent); }
.kanji .glyph { color: var(--headword); font-size: calc(58px * var(--scale)); }
.sc [data-sc-content="formation"], .sc [data-sc-content="structure"], .sc [data-sc-class="formation"] { border-color: var(--rule); border-radius: 2px; }
.sc [data-sc-content="example-sentence"], .sc [data-sc-content="examples"], .sc [data-sc-content="example"] { border-left-color: var(--rule); }
@supports not (color: color-mix(in srgb, red 50%, blue)) {
  .entry.unknown .headword { background: rgba(255, 255, 255, 0.22); }
}
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
.headword rt { display: revert; font-size: 0.4em; color: var(--dim); letter-spacing: 0.12em; }
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
.headword rt { display: revert; font-size: 0.38em; color: var(--dim); font-weight: 400; letter-spacing: 0.12em; }
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
.headword rt { display: revert; color: var(--dim); letter-spacing: 0.14em; font-size: 0.4em; }
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
.headword rt { display: revert; color: #839496; }
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
.headword rt { display: revert; color: #93a1a1; }
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
.headword rt { display: revert; }
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
