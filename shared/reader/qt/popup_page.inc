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
.popup.translucent {
  -webkit-backdrop-filter: blur(8px);
  backdrop-filter: blur(8px);
}
/* The scroll bar is part of the popup, not a thing beside it: no track, no buttons, hidden at
   rest and shown only while actively scrolling. (No scrollbar-width or scrollbar-color here: with
   either, Chromium draws its own bar with a track and passes these rules over.) */
.popup::-webkit-scrollbar { width: 7px; height: 7px; background: transparent; }
.popup::-webkit-scrollbar-track, .popup::-webkit-scrollbar-corner { background: transparent; }
.popup::-webkit-scrollbar-button { display: none; width: 0; height: 0; }
.popup::-webkit-scrollbar-thumb {
  background: transparent;
  background-clip: padding-box;
  border: 2px solid transparent;
  border-radius: 4px;
}
.popup.scrolling::-webkit-scrollbar-thumb { background-color: color-mix(in srgb, var(--text) 34%, transparent); }
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
/* A top corner (the default): the buttons in the headline, pinned with it (popup.js, placeButtons);
   left just after the word, right at the line's end. */
.head > .card-buttons.in-head { margin: 0; align-self: center; }
.popup[data-corner$="right"] .entry > .head:has(> .card-buttons.in-head) { padding-right: calc(64px * var(--scale)); }
.popup[data-corner$="right"] .head > .card-buttons.in-head { position: absolute; top: 0; right: 0; }
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
.card[data-state="done"] { opacity: 0.35; pointer-events: none; cursor: default; }
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
/* ---- A dictionary's own layout (every dictionary's with the full layout; grammar and characters
   always): as it set it, in the popup's colours ---- */
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
/* ---- A word's definitions as its dictionary set them (look.glossary "full", Yomitan's way) ----
   The variables some dictionaries' stylesheets read from Yomitan's page are the popup's colours. */
/* Yomitan's own colour names, which dictionaries' stylesheets use. A label "in the background's
   colour on a filled box" (角川新字源's red and black labels) needs a real colour for its text, so
   --background-color-light is the popup's ground; --background-color stays clear for the boxes
   that only repeat the page's background. Plain rgba: WebView 109 has no color-mix. */
.sc { --text-color: var(--text); --fg: var(--text); --background-color: transparent; --background-color-light: var(--ground);
  --accent-color: var(--accent); --link-color: var(--accent); --input-background-color: rgba(128, 128, 128, 0.18);
  --danger-color: #cc3a34; --danger-color-lighter: rgba(204, 58, 52, 0.42); --danger-color-lightest: rgba(204, 58, 52, 0.2);
  --font-size-no-units: 13; }
/* Each line finds its own direction (an Arabic gloss right to left, a Japanese example left to
   right), and is set from its own start. */
.sc-definition, .sc [dir="auto"], .glosses .gloss { unicode-bidi: plaintext; text-align: start; }
.sc [dir="rtl"] { unicode-bidi: isolate; }
/* A sense laid out by its dictionary: the number in a column of its own, the tags and the
   definitions beside it (a sense without tags starts on the number's line). */
.sense.laid-out { display: grid; grid-template-columns: auto minmax(0, 1fr); column-gap: 0.1em; align-items: baseline; }
.sense.laid-out > :not(.number) { grid-column: 2; }
.sense.laid-out > .number { grid-column: 1; }
.sc-line:empty { display: none; }
/* Compact (look.glossary "compact"), as Yomitan's compact glossaries: a word's gloss lists run on
   one line, and its examples and notes of where it comes from are left out. A grammar or a
   character dictionary keeps its layout. */
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc .sc-ul[data-sc-content="glossary"],
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc .sc-ul[data-sc-content="glossary"] > .sc-li { display: inline; padding: 0; margin: 0; }
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc .sc-ul[data-sc-content="glossary"] > .sc-li + .sc-li::before { content: "; "; color: var(--dim); }
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="extra-info"],
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="example-sentence"],
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="attribution"],
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="forms"] { display: none; }
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc.several > .sc-definition.plain { display: inline; margin: 0; }
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc.several > .sc-definition.plain + .sc-definition.plain::before { content: "; "; color: var(--dim); }
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc-definition + .sc-definition { margin-top: 0; }
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc-definition:not(.bidi) .sc-line { display: inline; }
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc-definition:not(.bidi) .sc-line + .sc-line::before { content: " · "; color: var(--dim); }
/* Compact: every laid-out sense at most four lines; a longer one cut there with a fade, a "more"
   under it (popup.js, clampSenses). */
.popup[data-glossary="compact"] .sense.laid-out:not(.open):not(.uncut) { max-height: 6em; overflow: hidden; position: relative; }
.popup[data-glossary="compact"] .sense.laid-out.clipped::after {
  content: ""; position: absolute; left: 0; right: 0; bottom: 0; height: 1.5em; pointer-events: none;
  background: linear-gradient(to bottom, transparent, var(--ground)); }
.popup[data-glossary="compact"] .sense-more { margin: 0 0 calc(4px * var(--scale)) calc(16px * var(--scale)); cursor: pointer; }
/* The number, the tags and the definition on one line, as the compact layout had them. */
.popup[data-glossary="compact"] .sense.laid-out[data-kind="word"] { display: block; }
.popup[data-glossary="compact"] .sense.laid-out[data-kind="word"] > .sc,
.popup[data-glossary="compact"] .sense.laid-out[data-kind="word"] > .sc > .sc-definition:not(.bidi) { display: inline; margin: 0; }
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="xref"],
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="antonym"],
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="references"],
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="antonyms"],
.popup[data-glossary="compact"] .sense[data-kind="word"] .sc [data-sc-content="attribution-footnote"] { display: none; }
/* Several plain definitions are a list. */
.sc.several > .sc-definition.plain { display: list-item; list-style: disc; margin-left: 1.1em; }
.sc.several > .sc-definition.plain + .sc-definition.plain { margin-top: calc(1px * var(--scale)); }
.sc .sc-li { padding-inline-start: 0.1em; }
.sc .sc-ul .sc-li::marker, .sc .sc-ol .sc-li::marker { color: var(--dim); }
/* JMdict's glossary list (yomitan-import), and how Yomitan sets its parts. */
.sc .sc-ul[data-sc-content="glossary"] { margin: 0; }
.sc [data-sc-content="sense-note"], .sc [data-sc-content="infoGlossary"], .sc [data-sc-content="attribution"] { color: var(--dim); font-size: 0.92em; }
.sc [data-sc-content="xref"], .sc [data-sc-content="antonym"] { color: var(--dim); font-size: 0.92em; }
.sc .sc-a.link { color: var(--accent); text-decoration: underline; text-decoration-thickness: 1px; text-underline-offset: 2px; cursor: default; }
/* A picture from the dictionary's zip: within the popup's width, at the size it asks for. */
.sc .gloss-image-link { display: inline-block; max-width: 100%; vertical-align: middle; line-height: 0; }
.sc .sc-img.gloss-image-container { display: inline-block; position: relative; max-width: 100%; vertical-align: middle; line-height: 0; }
/* As Yomitan has it: in a picture's box an em is a fourteenth of the text (a pixel at Yomitan's
   14 px), which dictionaries' stylesheets size their pictures by: 明鏡's symbols are "15em" wide,
   which came out fifteen characters wide here (2026-11-23). A picture sized in em by its own
   structured content takes the text's em. */
.sc .sc-img.gloss-image-container { font-size: calc(1em / 14); }
.sc .sc-img.gloss-image-container.em-units { font-size: 1em; }
.sc .sc-img.gloss-image-container.natural { width: auto; height: auto; }
.sc .gloss-image { display: block; width: 100%; height: 100%; max-width: 100%; object-fit: contain; }
.sc .sc-img.natural .gloss-image { width: auto; height: auto; }
.sc .gloss-image-container[data-sc-appearance="auto"]:not(.no-background) { background: color-mix(in srgb, #ffffff 92%, transparent); border-radius: 2px; }
/* A monochrome picture (a rare character's glyph) in the text's colour. */
.sc .gloss-image-mask { display: block; width: 100%; height: 100%; background-color: currentColor;
  -webkit-mask-size: contain; mask-size: contain; -webkit-mask-repeat: no-repeat; mask-repeat: no-repeat;
  -webkit-mask-position: center; mask-position: center; }
.sc .sc-img.missing { font-style: italic; }
/* ---- Pitch accent: a graph, the reading with the drop marked, or a number ---- */
.pitch-accent { display: inline-flex; align-items: center; gap: 0.35em; color: var(--accent); font-size: calc(12px * var(--scale)); }
.pitch-graph { height: 1.5em; overflow: visible; vertical-align: middle; }
.pitch-graph .pitch-line { fill: none; stroke: currentColor; stroke-width: 6; stroke-linejoin: round; }
.pitch-graph .pitch-line.tail { stroke-dasharray: 10 8; opacity: 0.7; }
.pitch-graph .pitch-dot { fill: currentColor; stroke: currentColor; stroke-width: 6; }
.pitch-graph .pitch-dot.downstep { fill: var(--ground); }
.pitch-graph .pitch-particle { fill: none; stroke: currentColor; stroke-width: 6; stroke-linejoin: round; opacity: 0.8; }
.pitch-text { white-space: nowrap; letter-spacing: 0.02em; }
.pitch-text .mora { display: inline-block; padding-top: 1px; border-top: 1.5px solid transparent; }
.pitch-text .mora.high { border-top-color: currentColor; }
.pitch-text .mora.high.end { border-right: 1.5px solid currentColor; padding-right: 1px; }
/* look.pitch "over": the line over the reading itself, in the reading's own colour and size. */
.pitch-text.over { letter-spacing: inherit; }
.pitch-text.over .mora { padding-top: 0.08em; border-top-width: max(1px, 0.09em); }
.pitch-text.over .mora.high.end { border-right-width: max(1px, 0.09em); padding-right: 0.04em; }
.headword rt .pitch-text.over .mora { line-height: 1.25; }
.more-frequencies { cursor: pointer; }
.sense.empty { display: none; }
.pitch-text .downstep { font-size: 0.8em; opacity: 0.85; margin: 0 0.05em; }
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
.kanji-rail {
  display: flex; flex-wrap: wrap; align-items: center; gap: calc(6px * var(--scale));
  margin: calc(2px * var(--scale)) 0 calc(8px * var(--scale));
  padding-bottom: calc(6px * var(--scale));
  border-bottom: 1px solid var(--edge);
}
.kanji-chip {
  min-width: calc(32px * var(--scale)); height: calc(28px * var(--scale));
  padding: 0 calc(7px * var(--scale)); border-radius: 5px;
  border: 1px solid color-mix(in srgb, var(--text) 26%, transparent);
  background: color-mix(in srgb, var(--text) 6%, transparent);
  color: var(--dim); font-family: inherit; font-size: calc(15px * var(--scale)); line-height: 1;
  cursor: pointer; display: inline-flex; align-items: center; justify-content: center;
}
.kanji-chip.active {
  background: color-mix(in srgb, var(--accent) 22%, transparent);
  border-color: var(--accent); color: var(--text); font-weight: 600;
}
.kanji-rail-meta {
  margin-left: auto; display: inline-flex; align-items: center; gap: calc(4px * var(--scale));
  font-size: calc(10px * var(--scale)); color: var(--dim);
}
.popup:not(.pad) .kanji-rail-keys { display: none; }
.popup.pad .kanji-rail-count { display: none; }
.kanji { display: grid; grid-template-columns: auto 1fr; gap: 0 calc(12px * var(--scale)); align-items: start; margin: calc(2px * var(--scale)) 0; }
.kanji .glyph { font-size: calc(54px * var(--scale)); line-height: 1.05; font-weight: 400; }
.kanji-rows { display: grid; grid-template-columns: auto 1fr; gap: calc(1px * var(--scale)) calc(10px * var(--scale)); margin: calc(2px * var(--scale)) 0 0; }
.kanji-rows dt { color: var(--dim); font-size: calc(9.5px * var(--scale)); letter-spacing: 0.12em; text-transform: uppercase; padding-top: 0.25em; }
.kanji-rows dd { margin: 0; }
.kanji-side { min-width: 0; }
/* Subtle controller hint for expanding/collapsing "more" and <details> with A */
.popup.pad .entry.chosen .more::after,
.popup.pad .entry.chosen details > summary::after {
  content: " A";
  color: var(--accent);
  font-size: 0.85em;
  opacity: 0.85;
}

/* Where color-mix is not known (the Thor's WebView 109): a color-mix with var() in it is only found
   wrong as it is used, and then counts as unset (no ground at all), so the plain colours are given
   here instead of before it. */
@supports not (color: color-mix(in srgb, red 50%, blue)) {
  .popup { background: var(--ground-see, var(--ground)); }
  .popup.scrolling::-webkit-scrollbar-thumb { background-color: rgba(236, 229, 211, 0.34); }
  .popup::-webkit-scrollbar-thumb:active { background-color: rgba(236, 229, 211, 0.52); }
  .kanji-chip { border-color: var(--edge); background: var(--box); }
  .kanji-chip.active { border-color: var(--accent); background: rgba(217, 71, 43, 0.24); }
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
    if (state === "done") node.disabled = true;
    node.appendChild(sign(kind === "book" ? "book" : state === "checking" || making ? "ring" : "plus"));
    if (kind === "addAudio") node.appendChild(speaker());
    if (keyOf(kind)) node.appendChild(keyMark(keyOf(kind)));
    node.addEventListener("click", (event) => {
      event.stopPropagation();
      if (node.dataset.state === "working" || node.dataset.state === "done") return;
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
  /**
   * Where an entry's card buttons go: with a top corner (the default since 2026-11-24, the
   * maintainer: "visible without scrolling"), in its headline, which stays pinned at the top
   * while a long entry of many dictionaries scrolls (left: just after the word; right: at the
   * edge); with a bottom corner, after the entry as before.
   */
  function placeButtons(section, box) {
    const head = section.querySelector(":scope > .head");
    if (head && /^top/.test(popup.dataset.corner || "top_right")) {
      box.classList.add("in-head");
      const word = head.querySelector(":scope > .headword");
      head.insertBefore(box, word ? word.nextSibling : head.firstChild);
    } else {
      section.appendChild(box);
    }
  }

  /** An entry's card buttons swapped for [box] (a card added, the audio question), in the same place. */
  function swapButtons(section, box) {
    const old = buttonsOf(section);
    if (!old) return;
    if (old.classList.contains("in-head")) box.classList.add("in-head");
    old.replaceWith(box);
  }

  /** An entry's card buttons, wherever placeButtons put them. */
  function buttonsOf(section) {
    return section.querySelector(":scope > .card-buttons, :scope > .head > .card-buttons");
  }

  function buttons(entry, index) {
    const box = element("div", "card-buttons");
    const labels = model.labels || {};
    const inDeck = entry.state === "inDeck";
    // The pluses are the ones nearer the corner, the book beside them.
    const left = /left$/.test(model.corner || "top_right");
    const parts = [];
    if (inDeck && !model.duplicates && entry.addedKind) {
      parts.push(entry.addedKind === "add"
        ? button("book", index, "inDeck", labels.book)
        : button("add", index, "done", labels.add));
      parts.push(entry.addedKind === "addAudio"
        ? button("book", index, "inDeck", labels.book)
        : button("addAudio", index, "done", labels.addAudio || labels.add));
    } else {
      if (!inDeck || model.duplicates) {
        const state = inDeck ? "add" : entry.state;
        parts.push(button("add", index, state, labels.add));
        parts.push(button("addAudio", index, state, labels.addAudio || labels.add));
      }
      if (inDeck) parts.push(button("book", index, "inDeck", labels.book));
    }
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
  let kanjiIndex = 0;

  /** The single character a kanji entry represents (empty if not a kanji entry). */
  function kanjiCharOf(entry) {
    if (entry.kanji && entry.kanji.character) return entry.kanji.character;
    if (kindsOf(entry).indexOf("kanji") >= 0 && entry.expression) return entry.expression;
    return "";
  }

  /** Ordered unique characters available on the Kanji tab. */
  function kanjiCharsOf(entries) {
    const out = [];
    (entries || []).forEach((entry) => {
      const ch = kanjiCharOf(entry);
      if (ch && out.indexOf(ch) < 0) out.push(ch);
    });
    return out;
  }

  /** Compact character rail at the top of the Kanji tab when a word has 2+ kanji. */
  function kanjiRail(chars) {
    const rail = element("div", "kanji-rail");
    chars.forEach((ch, i) => {
      const chip = element("button", "kanji-chip" + (i === kanjiIndex ? " active" : ""), ch);
      chip.type = "button";
      chip.lang = "ja";
      chip.setAttribute("aria-pressed", i === kanjiIndex ? "true" : "false");
      chip.addEventListener("click", (event) => {
        event.stopPropagation();
        if (kanjiIndex === i) return;
        kanjiIndex = i;
        draw(false);
        reportSoon();
      });
      rail.appendChild(chip);
    });
    const keys = element("span", "kanji-rail-meta kanji-rail-keys");
    keys.appendChild(keyMark("◀"));
    keys.appendChild(keyMark("▶"));
    rail.appendChild(keys);
    const count = element("span", "kanji-rail-meta kanji-rail-count", (kanjiIndex + 1) + " / " + chars.length);
    rail.appendChild(count);
    return rail;
  }

  /** Steps to the previous (-1) or next (1) kanji character when the Kanji tab has 2+ characters. */
  function stepKanji(step) {
    if (tab !== "kanji") return false;
    const chars = kanjiCharsOf(model.entries || []);
    if (chars.length < 2) return false;
    kanjiIndex = (kanjiIndex + (step < 0 ? -1 : 1) + chars.length) % chars.length;
    draw(false);
    post({ type: "feedback", kind: "step" });
    reportSoon();
    return true;
  }
  /**
   * The entry the controller is on (model.choose: the d-pad goes from one to the next, its card
   * buttons are the ones marked and pressed: Reader.entry, Reader.addChosen), or -1.
   */
  let chosen = -1;
  // The height following the scroll (look.fit; settled() below says how).
  let follow = -1;     // the entry the height follows (-1: the first)
  let restTop = 0;     // where the scroll was when it last came to rest
  let easeUntil = 0;   // till when a change of height is to be eased by the host
  let touching = false, settling = 0, stepping = false;

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
    easeUntil = performance.now() + 600; // the host brings the height to the entry smoothly
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
  function glide(to, done) {
    cancelAnimationFrame(gliding);
    const from = popup.scrollTop;
    if (still || Math.abs(to - from) < 2) { popup.scrollTop = to; if (done) done(); return; }
    const began = performance.now(), time = 110;
    const frame = (now) => {
      const part = Math.min(1, (now - began) / time);
      popup.scrollTop = from + (to - from) * (1 - Math.pow(1 - part, 3));
      if (part < 1) gliding = requestAnimationFrame(frame);
      else { if (done) done(); reportSoon(); }
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
      node.appendChild(element("span", "tab-name", labels[one.kind + "s"] || (one.kind === "name" ? null : labels[one.kind]) || KIND_LABELS[one.kind]));
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
    "padding", "paddingTop", "paddingLeft", "paddingRight", "paddingBottom", "wordBreak", "whiteSpace", "listStyleType",
    "background", "clipPath", "textShadow", "cursor"];

  const RTL_LANGS = /^(ar|he|fa|ur|yi|ps|ckb|dv|sd|ug)(-|$)/i;
  const BLOCK_TAGS = new Set(["div", "li", "p", "td", "th", "summary", "details"]);

  function unsafeCss(text) {
    const plain = String(text || "").replace(/\\([0-9a-f]{1,6}\s?|.)/gi, (_, esc) => {
      if (/^[0-9a-f]/i.test(esc)) {
        const code = parseInt(esc, 16);
        return code > 0 && code <= 0x10ffff ? String.fromCodePoint(code) : "";
      }
      return esc;
    });
    return /url\s*\(|image-set\s*\(|expression\s*\(|javascript\s*:|-moz-binding|@import/i.test(plain);
  }

  function content(node, out, media) {
    if (node === null || node === undefined) return;
    if (typeof node === "string" || typeof node === "number") {
      out.appendChild(document.createTextNode(String(node)));
      return;
    }
    if (Array.isArray(node)) {
      node.forEach((one) => content(one, out, media));
      return;
    }
    if (typeof node !== "object") return;
    const tag = String(node.tag || "span");
    if (!CONTENT_TAGS.has(tag)) {
      content(node.content, out, media);
      return;
    }
    if (tag === "img") {
      out.appendChild(picture(node, media));
      return;
    }
    // Yomitan's class names beside this page's, as some dictionaries' stylesheets use them.
    const made = element(tag === "a" ? "span" : tag, "sc-" + tag + " gloss-sc-" + tag);
    if (node.data && typeof node.data === "object") {
      Object.keys(node.data).forEach((key) => {
        if (/^[\p{L}\p{N}_-]+$/u.test(key)) made.setAttribute(dataName(key), String(node.data[key]));
      });
    }
    if (node.style && typeof node.style === "object") {
      CONTENT_STYLES.forEach((key) => {
        const value = node.style[key];
        if (value === undefined || value === null) return;
        const text = String(value);
        if (unsafeCss(text)) return;
        made.style[key] = typeof value === "number" && /^(margin|padding|fontSize|borderWidth|borderRadius)/.test(key) ? value + "em" : text;
      });
    }
    if (node.lang && /^[A-Za-z-]{2,12}$/.test(node.lang)) made.lang = node.lang;
    // Arabic, Hebrew, Persian or Urdu runs right to left; any block finds its own direction from
    // its first letters, so a line of Arabic with Japanese in it reads in the right order.
    if (RTL_LANGS.test(made.lang || "")) made.dir = "rtl";
    else if (BLOCK_TAGS.has(tag)) made.dir = "auto";
    if (node.title) made.title = String(node.title);
    if ((tag === "td" || tag === "th") && node.colSpan > 1) made.colSpan = Math.min(20, Number(node.colSpan));
    if ((tag === "td" || tag === "th") && node.rowSpan > 1) made.rowSpan = Math.min(50, Number(node.rowSpan));
    if (tag === "details" && node.open) made.open = true;
    if (tag === "a") made.classList.add("link");
    content(node.content, made, media);
    out.appendChild(made);
  }

  /**
   * A structured-content data key as Yomitan names its attribute (dataset["sc" + Key]): the first
   * letter as a capital, so "Contents" and "contents" are both data-sc-contents. 大辞泉's keys
   * begin with a capital; written data-sc--contents, its stylesheet's "1.5em" missed them and its
   * text came out at 0.7 of the size (2026-11-24). Keys in Japanese ("歴史") are kept as they
   * are, which its stylesheet names too (span[data-sc歴史]).
   */
  function dataName(key) {
    const camel = key.charAt(0).toUpperCase() + key.slice(1);
    return "data-sc" + camel.replace(/[A-Z]/g, (c) => "-" + c.toLowerCase());
  }

  /** A number of a picture's size in its unit (Yomitan's sizeUnits: px, or em to sit in a line). */
  function measure(value, unit) {
    const number = Number(value);
    return number > 0 && number < 4000 ? number + unit : "";
  }

  /**
   * A picture from the dictionary's zip (Yomitan's "img", or an image item), as the app handed it
   * over: [media] maps its path to a data address, the only source a picture may have. A
   * monochrome one (a rare character's glyph) is drawn in the text's colour through it as a mask.
   * Without the picture, what it stands for, when it says.
   */
  function picture(node, media) {
    let source = media && typeof node.path === "string" ? media[node.path] : null;
    // A picture sent once and named after ("pic:" and its key), kept until the page loads again.
    if (typeof source === "string" && source.startsWith("pic:")) source = pictureStore.get(source.slice(4));
    if (typeof source !== "string" || !/^data:image\/[a-z0-9.+-]+;base64,[a-z0-9+/=\s]+$/i.test(source)) {
      return element("span", "sc-img missing", node.alt || node.title || node.description || "");
    }
    const unit = node.sizeUnits === "em" ? "em" : "px";
    const ratio = Number(node.width) > 0 && Number(node.height) > 0 ? Number(node.height) / Number(node.width) : 0;
    let width = Number(node.preferredWidth) || 0, height = Number(node.preferredHeight) || 0;
    if (width && !height && ratio) height = width * ratio;
    if (height && !width && ratio) width = height / ratio;
    if (!width && !height) { width = Number(node.width) || 0; height = Number(node.height) || 0; }
    const box = element("span", "sc-img gloss-image-container" + (unit === "em" ? " em-units" : ""));
    box.dataset.scAppearance = node.appearance === "monochrome" ? "monochrome" : "auto";
    if (node.data && typeof node.data === "object") {
      Object.keys(node.data).forEach((key) => {
        if (/^[\p{L}\p{N}_-]+$/u.test(key)) box.setAttribute(dataName(key), String(node.data[key]));
      });
    }
    if (measure(width, unit)) box.style.width = measure(width, unit);
    if (measure(height, unit)) box.style.height = measure(height, unit);
    if (typeof node.verticalAlign === "string" && /^[a-z-]+$/.test(node.verticalAlign)) box.style.verticalAlign = node.verticalAlign;
    if (typeof node.border === "string" && !unsafeCss(node.border)) box.style.border = node.border;
    if (typeof node.borderRadius === "string" && !unsafeCss(node.borderRadius)) box.style.borderRadius = node.borderRadius;
    if (node.title) box.title = String(node.title);
    if (node.appearance === "monochrome") {
      const mask = element("span", "gloss-image-mask");
      const address = 'url("' + source.replace(/[\s"'()\\]/g, "") + '")';
      mask.style.webkitMaskImage = address;
      mask.style.maskImage = address;
      box.appendChild(mask);
    } else {
      const image = element("img", "gloss-image");
      image.alt = node.alt || "";
      image.decoding = "async";
      if (node.pixelated || node.imageRendering === "pixelated") image.style.imageRendering = "pixelated";
      else if (node.imageRendering === "crisp-edges") image.style.imageRendering = "crisp-edges";
      if (node.background === false) box.classList.add("no-background");
      image.addEventListener("load", () => {
        // A picture that says no size takes its own, as Yomitan gives it (it keeps each picture's
        // size when it imports), so that the dictionary's stylesheet can cap it (max-width on
        // .gloss-image-link: a rhyme mark or a rare character one letter wide).
        if (!width && !height && image.naturalWidth > 0 && image.naturalHeight > 0) {
          box.style.width = image.naturalWidth + "px";
          box.style.aspectRatio = image.naturalWidth + " / " + image.naturalHeight;
          box.classList.remove("natural");
        }
        reportSoon();
      });
      image.src = source;
      box.appendChild(image);
    }
    if (!width && !height) box.classList.add("natural");
    // Yomitan's own wrapper, which dictionaries' stylesheets size and pick pictures by.
    const link = element("span", "gloss-image-link");
    if (typeof node.path === "string") link.dataset.path = node.path;
    link.appendChild(box);
    return link;
  }

  /**
   * A plain definition's lines, each a block that finds its own direction: one line of Arabic and
   * the next of Japanese each read in their own order (a bidi paragraph per line).
   */
  function lines(text, out) {
    const all = String(text === undefined || text === null ? "" : text);
    // Right-to-left lines among the others keep a line each even in the compact layout: run on,
    // a line of Arabic and one of Japanese cannot be read in either order.
    if (/[\u0590-\u08FF\uFB1D-\uFDFF\uFE70-\uFEFF]/.test(all)) out.classList.add("bidi");
    all.split(/\r?\n/).forEach((line) => {
      const node = element("div", "sc-line", line);
      node.dir = "auto";
      out.appendChild(node);
    });
  }

  /**
   * One glossary as its dictionary laid it out: its definitions, each a block of its own, with
   * the pictures of [media] (path to data address). Several plain definitions are a list.
   */
  function glossary(items, media) {
    const out = element("div", "sc");
    const list = Array.isArray(items) ? items : [items];
    if (list.length > 1) out.classList.add("several");
    list.forEach((item) => {
      const block = element("div", "sc-definition");
      block.dir = "auto";
      if (typeof item === "string" || (item && item.type === "text")) lines(typeof item === "string" ? item : item.text, block);
      else if (item && item.type === "structured-content") content(item.content, block, media);
      else if (item && item.type === "image") block.appendChild(picture(item, media));
      else return; // the pointer of an inflected form
      if (typeof item === "string" || (item && item.type === "text")) block.classList.add("plain");
      out.appendChild(block);
    });
    return out;
  }

  // ---- The dictionaries' own stylesheets (styles.css), as Yomitan applies them ----
  // Each rule's selectors are put under its own dictionary's senses, so it styles nothing else,
  // and nested rules are written out flat (the Thor's WebView, Chromium 109, has no CSS nesting,
  // and Jitendex's stylesheet uses it). Nothing may be fetched: @import, @font-face and any
  // declaration with url() are left out.

  /** The rules of a stylesheet: {prelude, decls, rules} blocks, at-rules among them. */
  function parseCss(text) {
    let at = 0;
    function block() {
      const out = { decls: [], rules: [] };
      let buffer = "";
      while (at < text.length) {
        const c = text[at];
        if (c === '"' || c === "'") {
          let stop = at + 1;
          while (stop < text.length) {
            if (text[stop] === "\\") { stop += 2; continue; }
            if (text[stop] === c) { stop++; break; }
            stop++;
          }
          const end = Math.min(stop, text.length);
          buffer += text.slice(at, end);
          at = end;
          continue;
        }
        at++;
        if (c === "{") {
          const prelude = buffer.trim();
          buffer = "";
          const inner = block();
          inner.prelude = prelude;
          out.rules.push(inner);
        } else if (c === "}") {
          break;
        } else if (c === ";") {
          if (buffer.trim()) out.decls.push(buffer.trim());
          buffer = "";
        } else {
          buffer += c;
        }
      }
      if (buffer.trim()) out.decls.push(buffer.trim());
      return out;
    }
    text = text.replace(/\/\*[\s\S]*?\*\//g, "");
    return block();
  }

  /** A selector list split at its top-level commas. */
  function selectorsOf(prelude) {
    const out = [];
    let depth = 0, from = 0, quote = "";
    for (let i = 0; i < prelude.length; ++i) {
      const c = prelude[i];
      if (quote) {
        if (c === "\\") { i++; continue; }
        if (c === quote) quote = "";
        continue;
      }
      if (c === '"' || c === "'") quote = c;
      else if (c === "(" || c === "[") depth++;
      else if (c === ")" || c === "]") depth = Math.max(0, depth - 1);
      else if (c === "," && depth === 0) { out.push(prelude.slice(from, i).trim()); from = i + 1; }
    }
    out.push(prelude.slice(from).trim());
    return out.filter(Boolean);
  }

  /** The rules of [node] written out under [parents] (selectors), flat. */
  function writeCss(node, parents, out) {
    const decls = node.decls.filter((decl) => !unsafeCss(decl) && !/^@/.test(decl));
    if (node.prelude !== undefined && decls.length) out.push(parents.join(",\n") + " { " + decls.join("; ") + "; }");
    node.rules.forEach((rule) => {
      const prelude = rule.prelude || "";
      if (prelude[0] === "@") {
        // @media and @supports keep their condition; the rest (@font-face, @keyframes, @import...) go.
        if (!/^@(media|supports)\b/i.test(prelude) || unsafeCss(prelude)) return;
        const inner = [];
        writeCss({ decls: [], rules: rule.rules.concat(rule.decls.length ? [{ prelude: "&", decls: rule.decls, rules: [] }] : []) }, parents, inner);
        if (inner.length) out.push(prelude + " {\n" + inner.join("\n") + "\n}");
        return;
      }
      const selectors = [];
      selectorsOf(prelude).forEach((selector) => parents.forEach((parent) => {
        selectors.push(selector.indexOf("&") >= 0 ? selector.replace(/&/g, parent) : parent + " " + selector);
      }));
      if (selectors.length) writeCss(rule, selectors, out);
    });
  }

  let stylesShown = "";

  let scopedStyles = new Map(); // title → { css, scoped }
  const pictureStore = new Map(); // a picture's key → its data address (SharePictures)
  function dictionaryStyles(styles) {
    let node = document.getElementById("dictionaries");
    if (!node) {
      node = document.createElement("style");
      node.id = "dictionaries";
      const before = document.getElementById("console");
      document.head.insertBefore(node, before || null);
    }
    const parts = [];
    const kept = new Map();
    Object.keys(styles || {}).sort().forEach((title) => {
      const css = String(styles[title] || "");
      if (!css.trim()) return;
      // A stylesheet is scoped once and kept: the same ones come with every lookup.
      const known = scopedStyles.get(title);
      let scoped;
      if (known && known.css === css) {
        scoped = known.scoped;
      } else {
        const name = String(title).replace(/["\\]/g, "\\$&").replace(/\n/g, " ");
        const out = [];
        writeCss(parseCss(css), ['.sense[data-dictionary="' + name + '"] .sc'], out);
        scoped = out.join("\n");
      }
      kept.set(title, { css, scoped });
      parts.push(scoped);
    });
    scopedStyles = kept;
    const text = parts.join("\n").replace(/<\/style/gi, "");
    if (text !== stylesShown) {
      stylesShown = text;
      node.textContent = text;
    }
  }
  /** For the tests: a stylesheet as this page puts it under [scope]. */
  function scopedCss(css, scope) {
    const out = [];
    writeCss(parseCss(String(css || "")), [scope], out);
    return out.join("\n");
  }

  function headline(entry) {
    const head = element("div", "head");
    const word = element("ruby", "headword");
    // look.pitch "over" (the default): the pitch line is drawn over the reading's kana, where the
    // reading is (the furigana, the reading beside the word, or a kana word itself).
    const over = overReading(entry);
    word.appendChild(over && !entry.reading ? over.draw() : document.createTextNode(entry.expression));
    if (entry.reading) {
      const rt = element("rt", null, over ? null : entry.reading);
      if (over) rt.appendChild(over.draw());
      word.appendChild(rt);
    }
    head.appendChild(word);
    if (entry.reading) {
      const reading = element("span", "reading");
      reading.appendChild(over ? over.draw() : document.createTextNode(entry.reading));
      head.appendChild(reading);
    }
    // A name in this game is shown on the Names tab, with no badge of its own by the word (the
    // maintainer, 2026-11-23: "dont put name in japanese on the popup").
    if (entry.reasons && entry.reasons.length) head.appendChild(element("span", "reasons", "« " + entry.reasons.join(" « ")));
    pitchesOf(entry, over).forEach((node) => head.appendChild(node));
    if (entry.frequencies && entry.frequencies.length) {
      const tags = element("span", "tags frequencies");
      // The first few; the others behind "+N" (look.frequencies, 0 for all), open for this
      // word once tapped.
      const few = (model.look || {}).frequencies;
      const shown = allFrequencies ? 0 : (few === undefined ? FREQUENCIES_SHOWN : Number(few) || 0);
      // One badge a dictionary: a list with several figures for a word (BCCWJ's short and long
      // units) says its name once, the figures after it.
      const lists = [];
      entry.frequencies.forEach((frequency) => {
        const said = String(frequency.value);
        const list = lists.find((one) => one.dictionary === frequency.dictionary);
        if (!list) lists.push({ dictionary: frequency.dictionary, values: [said] });
        else if (list.values.indexOf(said) < 0) list.values.push(said);
      });
      lists.forEach((list, index) => {
        if (shown && index >= shown) return;
        const tag = element("span", "tag frequency");
        tag.dataset.category = "frequency";
        tag.dataset.dictionary = list.dictionary;
        tag.appendChild(element("span", "tag-name", list.dictionary));
        tag.appendChild(element("span", "tag-value", list.values.join(", ")));
        tags.appendChild(tag);
      });
      if (shown && lists.length > shown) {
        const more = element("span", "tag frequency more-frequencies", "+" + (lists.length - shown));
        more.dataset.category = "frequency";
        more.title = lists.slice(shown).map((f) => f.dictionary + " " + f.values.join(", ")).join("\n");
        more.addEventListener("click", (event) => {
          event.stopPropagation();
          allFrequencies = true;
          document.querySelectorAll(".tags.frequencies").forEach((row) => {
            const index = Number(row.closest(".entry") ? row.closest(".entry").dataset.index : -1);
            const one = (model.entries || [])[index];
            if (one) row.replaceWith(headline(one).querySelector(".tags.frequencies"));
          });
          reportSoon();
        });
        tags.appendChild(more);
      }
      head.appendChild(tags);
    }
    return head;
  }

  // ---- Pitch accent (entry.pitchAccents: dictionary, reading, positions), as Yomitan shows it ----
  const SMALL_KANA = "ぁぃぅぇぉゃゅょゎァィゥェォャュョヮ";

  /** A reading's morae: a small kana goes with the one before it. */
  function morae(reading) {
    const out = [];
    Array.from(reading || "").forEach((c) => {
      if (out.length && SMALL_KANA.indexOf(c) >= 0) out[out.length - 1] += c;
      else out.push(c);
    });
    return out;
  }

  /** Whether mora [at] (0-based; the count of morae is the particle after them) is high. */
  function high(position, at) {
    if (position === 0) return at > 0;
    if (position === 1) return at === 0;
    return at > 0 && at < position;
  }

  /** The downstep graph: a dot a mora, the particle after them a triangle, as Yomitan draws it. */
  function pitchGraph(count, position) {
    const svg = document.createElementNS(SVG, "svg");
    const width = 50 * (count + 1);
    svg.setAttribute("viewBox", "0 0 " + width + " 100");
    svg.setAttribute("class", "pitch-graph");
    svg.style.width = (count + 1) * 0.75 + "em"; // half as wide a mora as tall (base.css: 1.5em)
    const y = (at) => (high(position, at) ? 25 : 75);
    const points = [];
    for (let at = 0; at <= count; ++at) points.push((25 + at * 50) + "," + y(at));
    const line = document.createElementNS(SVG, "polyline");
    line.setAttribute("points", points.slice(0, count).join(" "));
    line.setAttribute("class", "pitch-line");
    svg.appendChild(line);
    const tail = document.createElementNS(SVG, "polyline");
    tail.setAttribute("points", points.slice(count - 1, count + 1).join(" "));
    tail.setAttribute("class", "pitch-line tail");
    svg.appendChild(tail);
    for (let at = 0; at < count; ++at) {
      const dot = document.createElementNS(SVG, "circle");
      dot.setAttribute("cx", String(25 + at * 50));
      dot.setAttribute("cy", String(y(at)));
      dot.setAttribute("r", "13");
      // The last high mora before the fall is drawn open.
      const falls = position > 0 && at === position - 1;
      dot.setAttribute("class", "pitch-dot" + (falls ? " downstep" : ""));
      svg.appendChild(dot);
    }
    const particle = document.createElementNS(SVG, "path");
    const px = 25 + count * 50, py = y(count);
    particle.setAttribute("d", "M" + px + " " + (py - 13) + " L" + (px + 13) + " " + (py + 10) + " L" + (px - 13) + " " + (py + 10) + " Z");
    particle.setAttribute("class", "pitch-particle");
    svg.appendChild(particle);
    return svg;
  }

  /** The reading with a line over the high morae and ꜜ where it falls. */
  function pitchText(parts, position, mark) {
    const out = element("span", "pitch-text");
    parts.forEach((mora, at) => {
      const node = element("span", "mora" + (high(position, at) ? " high" : ""), mora);
      // The line comes down where it falls: after this mora (a flat word's goes on into the particle).
      if (high(position, at) && (at === parts.length - 1 ? position === parts.length : !high(position, at + 1))) node.classList.add("end");
      out.appendChild(node);
      if (mark && position > 0 && at === position - 1) out.appendChild(element("span", "downstep", "ꜜ"));
    });
    return out;
  }

  /** The frequencies shown before "+N" when look.frequencies does not say. */
  const FREQUENCIES_SHOWN = 4;
  let allFrequencies = false;

  function toHiragana(text) {
    return String(text || "").replace(/[\u30a1-\u30f6]/g, (c) => String.fromCharCode(c.charCodeAt(0) - 0x60));
  }

  /**
   * The pitch accent to draw over the entry's reading (look.pitch "over"): the first position a
   * dictionary gives for this very reading, as {position, draw()}; null when there is none or
   * another form is chosen.
   */
  function overReading(entry) {
    if (((model.look || {}).pitch || "over") !== "over") return null;
    const reading = entry.reading || entry.expression;
    const readingHira = toHiragana(reading);
    const accents = Array.isArray(entry.pitchAccents) ? entry.pitchAccents : [];
    const one = accents.find((a) => toHiragana(a.reading || reading) === readingHira && (a.positions || []).length);
    if (!one || !/^[\u3040-\u30ff]+$/.test(reading)) return null;
    const position = one.positions[0];
    const parts = morae(reading);
    return {
      position: position,
      reading: reading,
      draw: () => {
        const node = pitchText(parts, position, false);
        node.classList.add("over");
        node.title = "[" + position + "] " + accents.filter((a) => (a.positions || []).indexOf(position) >= 0)
          .map((a) => a.dictionary).filter((d, i, all) => all.indexOf(d) === i).join(" · ");
        return node;
      },
    };
  }

  /**
   * The headline's pitch accents in the form chosen (look.pitch: over, graph, text, number, all,
   * off). With "over", the one drawn over the reading (`over`) is left out and any other is
   * written beside the word as text.
   */
  function pitchesOf(entry, over) {
    let form = ((model.look || {}).pitch || "over");
    if (form === "off") return [];
    if (form === "over") form = "text";
    const accents = Array.isArray(entry.pitchAccents) ? entry.pitchAccents : [];
    if (!accents.length || form === "number") {
      if (accents.length) {
        const seen = [];
        accents.forEach((one) => (one.positions || []).forEach((p) => { if (seen.indexOf(p) < 0) seen.push(p); }));
        const tag = element("span", "pitch", "[" + seen.join(",") + "]");
        tag.title = accents.map((one) => one.dictionary).filter((d, i, all) => all.indexOf(d) === i).join(" · ");
        return [tag];
      }
      return (entry.pitches || []).map((pitch) => element("span", "pitch", pitch));
    }
    // One of each position, whichever dictionaries say it; the dictionaries in its title.
    const shown = [];
    accents.forEach((one) => (one.positions || []).forEach((position) => {
      const reading = one.reading || entry.reading || entry.expression;
      const same = shown.find((s) => s.position === position && s.reading === reading);
      if (same) { if (same.dictionaries.indexOf(one.dictionary) < 0) same.dictionaries.push(one.dictionary); }
      else shown.push({ position: position, reading: reading, dictionaries: [one.dictionary] });
    }));
    return shown.filter((one) => !over || one.position !== over.position || toHiragana(one.reading) !== toHiragana(over.reading)).map((one) => {
      const parts = morae(one.reading);
      const node = element("span", "pitch-accent");
      node.dataset.position = String(one.position);
      node.title = "[" + one.position + "] " + one.dictionaries.join(" · ");
      if (form === "text" || form === "all") node.appendChild(pitchText(parts, one.position, form === "all"));
      if ((form === "graph" || form === "all") && parts.length) node.appendChild(pitchGraph(parts.length, one.position));
      if (form === "all") node.appendChild(element("span", "pitch", "[" + one.position + "]"));
      return node;
    });
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

  /**
   * `defer`, when given, takes the drawing of the layouts after the first few (a function to
   * run a moment later): an entry with many dictionaries shows its first ones at once.
   */
  /**
   * A sense whose definition came out with nothing to read or see (a dictionary's empty
   * numbered heading: the maintainer's recording showed "1" and "3" alone) is not shown.
   */
  function emptied(item, made) {
    // Compact leaves Yomitan-style gloss lists whole (clampSenses): known as soon as it is drawn.
    if (made.querySelector('[data-sc-content="glossary"]')) item.classList.add("uncut");
    if (made.textContent.trim() || made.querySelector(".sc-img, img, svg, table")) return;
    item.classList.add("empty");
  }

  function senses(entry, kind, defer) {
    const out = document.createDocumentFragment();
    let drawnNow = 0;
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
      // Every dictionary's layout is its own; look.glossary "compact" runs a word's gloss lists on
      // one line and leaves its examples out (base.css, .popup[data-glossary="compact"]), as
      // Yomitan's compact glossaries do. The flattened glosses are for a sense without one.
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
        if (defer && drawnNow >= EAGER_SENSES) {
          const place = element("span", "sc-later");
          item.appendChild(place);
          defer(() => {
            if (!place.parentNode) return;
            const made = glossary(sense.content, sense.media);
            place.parentNode.replaceChild(made, place);
            emptied(item, made);
          });
        } else {
          const made = glossary(sense.content, sense.media);
          item.appendChild(made);
          emptied(item, made);
          ++drawnNow;
        }
      } else {
        const glosses = element("span", "glosses");
        (sense.glosses || []).forEach((gloss) => {
          const one = element("span", "gloss", gloss);
          one.dir = "auto"; // an Arabic gloss keeps its own order among the others
          glosses.appendChild(one);
        });
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

  /**
   * The ground at the look's opacity as a plain rgba colour (--ground-see), for a WebView without
   * color-mix (the AYN Thor's is 109), where base.css falls back on it: the opacity was not kept
   * there before, and the popup was solid whatever was set (the maintainer, 2026-11-27).
   */
  function seeThroughGround(root) {
    const opacity = parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--opacity")) || 1;
    const ground = getComputedStyle(popup).getPropertyValue("--ground").trim() || getComputedStyle(document.documentElement).getPropertyValue("--ground").trim();
    let rgb = null;
    let hex = /^#([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i.exec(ground);
    if (hex) {
      let digits = hex[1];
      if (digits.length === 3) digits = digits.split("").map((one) => one + one).join("");
      rgb = [0, 2, 4].map((at) => parseInt(digits.substr(at, 2), 16));
    } else {
      const found = /^rgba?\(\s*([\d.]+)[ ,]+([\d.]+)[ ,]+([\d.]+)/i.exec(ground);
      if (found) rgb = [found[1], found[2], found[3]].map(Number);
    }
    if (rgb) root.setProperty("--ground-see", `rgba(${rgb[0]}, ${rgb[1]}, ${rgb[2]}, ${Math.min(1, Math.max(0, opacity))})`);
    else root.removeProperty("--ground-see");
  }

  /** Draws the model; [fresh] for a new one (Reader.show), else another tab of the same. */
  /** Entries drawn at once, and of them the laid-out senses; the rest are filled in after (draw). */
  const EAGER = 1;
  const EAGER_SENSES = 4;
  let drawn = 0;

  function draw(fresh) {
    if (fresh) allFrequencies = false;
    popup.textContent = "";
    popup.dataset.corner = model.corner || "top_right";
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
    popup.classList.toggle("translucent", look.opacity > 0 && look.opacity < 100);
    seeThroughGround(root);
    if (look.radius >= 0 && look.radius !== null && look.radius !== undefined && look.radius !== "") root.setProperty("--radius", share(look.radius, 0, 28) + "px");
    else root.removeProperty("--radius");
    if (look.accent && /^#[0-9a-fA-F]{6}$/.test(look.accent)) root.setProperty("--accent", look.accent);
    else root.removeProperty("--accent");
    popup.classList.toggle("no-tags", !!look.hideTags);
    popup.dataset.glossary = look.glossary === "compact" ? "compact" : "full";
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
      kanjiIndex = 0;
    }
    const tabbed = tabs.length > 1;
    popup.classList.toggle("tabbed", tabbed);
    popup.dataset.tab = tab;
    if (tabbed && !model.message) popup.appendChild(tabRow());
    const kanjiChars = (tab === "kanji" || (!tabbed && tabs.length === 1 && tabs[0].kind === "kanji"))
      ? kanjiCharsOf(model.entries || [])
      : [];
    if (kanjiChars.length > 0 && kanjiIndex >= kanjiChars.length) kanjiIndex = 0;
    const activeKanji = kanjiChars.length > 1 ? kanjiChars[kanjiIndex] : "";
    if (kanjiChars.length > 1 && !model.message) popup.appendChild(kanjiRail(kanjiChars));
    // The first entries are drawn at once; the others' definitions a moment later, one entry at
    // a time, so that a hover with many dictionaries shows its best match without waiting for
    // the rest (a long headword list of big dictionaries took tens of milliseconds).
    const drawing = ++drawn;
    let eager = 0;
    const later = [];
    (model.entries || []).forEach((entry, index) => {
      const kinds = kindsOf(entry);
      if (tabbed && kinds.indexOf(tab) < 0) return;
      if (activeKanji && kanjiCharOf(entry) !== activeKanji) return;
      const kind = tabbed ? tab : "";
      const section = element("section", "entry");
      section.dataset.index = String(index);
      section.dataset.state = entry.state || "add";
      section.dataset.kind = kind || kinds[0];
      section.classList.toggle("unknown", !!entry.unknown);
      const asCharacter = (kind || kinds[0]) === "kanji";
      if (!asCharacter) section.appendChild(headline(entry));
      const fill = () => {
        const before = section.querySelector(":scope > .card-buttons");
        const put = (node) => section.insertBefore(node, before);
        // look.cards false: an app that shows who a name is in a place of its own (Furochibi's pane).
        if (entry.card && (model.look || {}).cards !== false && (!kind || kind === "name")) {
          // Two people who share the name (a family name): a card each, the dictionary's first first.
          [entry.card].concat(entry.card.others || []).forEach((card) => put(nameCard(card)));
        }
        if (asCharacter) put(character(entry, kind));
        else put(senses(entry, kind, (job) => later.push(job)));
        if (entry.forms && entry.forms.length && (!kind || kind === "word")) put(forms(entry));
        // How often it was looked up: one line under the definition, never above it.
        if (entry.history) {
          const lookups = element("div", "lookups", entry.history);
          lookups.title = entry.history;
          put(lookups);
        }
      };
      // A kanji bank's character is not a word of the line: no card of it.
      if (!entry.kanji) placeButtons(section, buttons(entry, index));
      if (eager < EAGER) {
        fill();
        ++eager;
      } else {
        section.classList.add("filling");
        later.push(() => { fill(); section.classList.remove("filling"); });
      }
      popup.appendChild(section);
    });
    // A few milliseconds of it at a time, so that a pointer moving on is never kept waiting.
    const next = () => {
      if (drawing !== drawn || !later.length) return;
      const until = performance.now() + 8;
      do later.shift()(); while (later.length && performance.now() < until);
      reportSoon();
      if (later.length) setTimeout(next, 0);
    };
    if (later.length) setTimeout(next, 0);
    popup.scrollTop = 0;
    // A controller's d-pad starts on the first entry of the tab.
    popup.classList.toggle("choosing", !!model.choose);
    chosen = -1;
    follow = -1;
    restTop = 0;
    stepping = false;
    if (model.choose) {
      const first = popup.querySelector(".entry");
      if (first) chooseEntry(Number(first.dataset.index), false);
    }
    report();
    if (design) mark(picked);
  }

  /** How tall the popup is, where its buttons are and what it says, for the app. */
  function report() {
    clampSenses();
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
    const hand = chosen >= 0 ? chosen : follow;
    const section = entries.find((node) => Number(node.dataset.index) === hand) || entries[0];
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
    // The corners and the height as drawn, for what an app draws behind the page (Android's blurred game).
    post({ type: "size", width: popup.offsetWidth, height: content, fit: fit, need: need, entries: entries.length, tabs: tabs.length,
           scrolled: popup.scrollTop, boxes: boxes, ease: performance.now() < easeUntil,
           radius: parseFloat(getComputedStyle(popup).borderTopLeftRadius) || 0,
           box: popup.getBoundingClientRect().height });
  }

  let pending = false;
  /**
   * Compact (look.glossary "compact"): a sense longer than a few lines (a monolingual
   * dictionary's, an encyclopedia's, a grammar's with its pictures) is cut there (base.css) and
   * a "more" under it opens it whole. Checked as the page reports its size, so a sense drawn a
   * moment later or a picture that loaded is seen too.
   */
  function clampSenses() {
    if (popup.dataset.glossary !== "compact") return;
    popup.querySelectorAll(".sense.laid-out:not(.open):not(.clipped):not(.uncut)").forEach((item) => {
      // Yomitan-style gloss lists (JMdict, Jitendex) are already a line a sense in compact: such a
      // dictionary's several senses in one block are not cut.
      if (item.querySelector('[data-sc-content="glossary"]')) {
        item.classList.add("uncut");
        return;
      }
      if (item.scrollHeight <= item.clientHeight + 4) return;
      item.classList.add("clipped");
      const more = element("div", "link more sense-more", (model.labels && model.labels.more) || "more");
      more.addEventListener("click", (event) => {
        event.stopPropagation();
        item.classList.remove("clipped");
        item.classList.add("open");
        more.remove();
        reportSoon();
      });
      item.after(more);
    });
  }

  function reportSoon() {
    if (pending) return;
    pending = true;
    requestAnimationFrame(() => { pending = false; report(); });
  }

  popup.addEventListener("scroll", reportSoon);

  /**
   * The height follows the entry as the popup is scrolled (look.fit, the "fit height to entry"
   * setting; the maintainer, 2026-11-24: "as you scroll, the height begins to match the next entry
   * down"). Once a scroll comes to rest (a finger lifted, a fling or the wheel stopped), the popup
   * steps to the entry it was scrolled towards, as the controller's d-pad does: that entry's top
   * glides to the top and the host brings the height to it. Down goes on to an entry after the
   * one in hand, up back to one before it, so a short entry is never left half shown with the next
   * one's start under it and a short scroll never springs back. Inside an entry taller than the
   * popup the scroll is free until its end (or its start, going up) comes into view.
   */
  function settleSoon() {
    clearTimeout(settling);
    settling = setTimeout(settled, 180);
  }
  function settled() {
    if (touching || stepping || chosen >= 0 || asking >= 0 || !(model.look || {}).fit) return;
    const entries = Array.from(popup.querySelectorAll(".entry"));
    const moved = popup.scrollTop - restTop;
    if (entries.length < 2 || Math.abs(moved) < 12) return;
    const box = popup.getBoundingClientRect();
    const padTop = parseFloat(getComputedStyle(popup).paddingTop) || 0;
    const viewTop = box.top + padTop, viewBottom = box.bottom;
    const at = Math.max(0, entries.findIndex((node) => Number(node.dataset.index) === follow));
    const current = entries[at].getBoundingClientRect();
    let target = at;
    // In a tall entry this scroll began in, the gesture that brings its end (or start) into view
    // stops there: the next one steps on.
    const reached = (moved > 0 ? current.bottom + moved > viewBottom + 2 : current.top + moved < viewTop - 2);
    if (moved > 0) {
      if (current.bottom > viewBottom + 2 || reached) { restTop = popup.scrollTop; return; } // in a tall entry
      // On to the entry after it that is most in view (a fling may have passed several).
      let most = -1;
      for (let i = at + 1; i < entries.length; i++) {
        const r = entries[i].getBoundingClientRect();
        const seen = Math.min(r.bottom, viewBottom) - Math.max(r.top, viewTop);
        if (seen > most) { most = seen; target = i; }
        if (r.top > viewBottom) break;
      }
      if (target === at && at + 1 < entries.length) target = at + 1;
    } else {
      if (current.top < viewTop - 2) { restTop = popup.scrollTop; return; } // in a tall entry
      if (reached) {
        // Its start came into view: there it stands, at the top.
        glideTo(at);
        return;
      }
      // Back to the entry that holds the top of the view.
      target = at > 0 ? at - 1 : 0;
      for (let i = at - 1; i >= 0; i--) {
        const r = entries[i].getBoundingClientRect();
        if (r.top <= viewTop + 2) { target = i; break; }
      }
    }
    glideTo(target);
  }
  /** The scroll glides to entry [target]'s top (of the tab's entries), and the height follows it. */
  function glideTo(target) {
    const entries = Array.from(popup.querySelectorAll(".entry"));
    const section = entries[target];
    if (!section) return;
    const box = popup.getBoundingClientRect();
    const padTop = parseFloat(getComputedStyle(popup).paddingTop) || 0;
    follow = Number(section.dataset.index);
    const top = target === 0 ? 0 : section.getBoundingClientRect().top - box.top + popup.scrollTop - padTop;
    easeUntil = performance.now() + 600;
    stepping = true;
    const landed = () => {
      if (!stepping) return;
      stepping = false;
      restTop = popup.scrollTop;
      reportSoon();
    };
    glide(Math.max(0, top), landed);
    setTimeout(landed, 400); // a page that is given no frames (hidden) is not left stepping
    reportSoon();
  }
  popup.addEventListener("scroll", () => { if (!stepping) settleSoon(); });
  popup.addEventListener("touchstart", () => { touching = true; clearTimeout(settling); }, { passive: true });
  const lifted = () => { touching = false; settleSoon(); };
  popup.addEventListener("touchend", lifted, { passive: true });
  popup.addEventListener("touchcancel", lifted, { passive: true });

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
    swapButtons(section, box);
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
      if (entry && section) swapButtons(section, buttons(entry, index));
      reportSoon();
    },
    show(next) {
      asking = -1;
      model = next || { entries: [] };
      if (model.pad !== undefined) pad = !!model.pad;
      if (model.styles !== undefined) dictionaryStyles(model.styles);
      if (model.forgetPictures) pictureStore.clear();
      if (model.pictures && typeof model.pictures === "object") {
        Object.keys(model.pictures).forEach((key) => pictureStore.set(key, model.pictures[key]));
      }
      draw(true);
    },
    /**
     * The same words with more of their definitions (Android reads the first headwords' before
     * the popup shows and the rest straight after): drawn again on the same tab, at the same
     * scroll, the entry in hand kept.
     */
    fill(next) {
      if (!next || !Array.isArray(next.entries)) return;
      if (next.pictures && typeof next.pictures === "object") {
        Object.keys(next.pictures).forEach((key) => pictureStore.set(key, next.pictures[key]));
      }
      const previous = model.entries || [];
      const merged = next.entries.map((entry, index) => {
        const old = previous[index];
        if (!old || old.expression !== entry.expression || old.reading !== entry.reading) return entry;
        return Object.assign({}, entry, {
          state: old.state && old.state !== "add" ? old.state : entry.state,
          addedKind: old.addedKind || "",
          unknown: old.unknown !== undefined ? old.unknown : entry.unknown,
        });
      });
      const scrolled = popup.scrollTop;
      const wasChosen = chosen;
      const wasFollowed = follow, wasRest = restTop;
      model.entries = merged;
      const found = tabsOf(model.entries);
      tabs = found.tabs;
      if (!tabs.some((one) => one.kind === tab)) tab = found.first;
      draw(false);
      if (model.choose && wasChosen >= 0 && popup.querySelector('.entry[data-index="' + wasChosen + '"]')) {
        chooseEntry(wasChosen, false);
      }
      if (asking >= 0) drawAsk(false);
      popup.scrollTop = scrolled;
      follow = wasFollowed;
      restTop = wasRest;
      reportSoon();
    },
    /** The next tab (1) or the one before (-1), round the row: a controller's R1 and L1. */
    tab(step) {
      if (tabs.length < 2) return;
      const at = Math.max(0, tabs.findIndex((one) => one.kind === tab));
      choose(tabs[(at + (step < 0 ? tabs.length - 1 : 1)) % tabs.length].kind);
    },
    /** Cycles characters on the Kanji tab (d-pad Left / Right on a controller). */
    kanjiStep(step) {
      stepKanji(step);
    },
    /** Expands or collapses "more" and <details> sections inside the chosen entry (A button on a controller). */
    toggleMore() {
      const section = popup.querySelector('.entry[data-index="' + chosen + '"]') || popup.querySelector(".entry");
      if (!section) return;
      const more = Array.from(section.querySelectorAll(".more, .sense-more, .more-tags, .more-frequencies"))
        .find((el) => el.offsetParent !== null);
      if (more) {
        more.click();
        post({ type: "feedback", kind: "step" });
        reportSoon();
        return;
      }
      const closed = section.querySelector("details:not([open])");
      if (closed) {
        closed.open = true;
        post({ type: "feedback", kind: "step" });
        reportSoon();
        return;
      }
      const opened = Array.from(section.querySelectorAll("details[open]"));
      if (opened.length) {
        opened[opened.length - 1].open = false;
        post({ type: "feedback", kind: "step" });
        reportSoon();
        return;
      }
      if (section.classList.contains("all")) {
        section.classList.remove("all");
        post({ type: "feedback", kind: "step" });
        reportSoon();
      }
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
    /** A dictionary's stylesheet as the page applies it under [scope] (for the tests). */
    scopedCss(css, scope) {
      return scopedCss(css, scope);
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
      if (state === "inDeck" && pressed.index === index && (pressed.kind === "add" || pressed.kind === "addAudio")) {
        entry.addedKind = pressed.kind;
      } else if (state !== "inDeck" && state !== "working") {
        entry.addedKind = "";
      }
      const section = popup.querySelector('.entry[data-index="' + index + '"]');
      if (!section) return;
      section.dataset.state = state;
      swapButtons(section, buttons(entry, index));
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
  // Prevent mouse/trackpad clicks inside the popup from stealing Win32 focus into WebView2
  // (which would drop Steam Input out of Gamepad mode into desktop mouse mode).
  popup.addEventListener("mousedown", (event) => {
    if (!design) event.preventDefault();
  });
  // Scroll-only scrollbar thumb: visible while actively scrolling, hidden at rest.
  let scrollHideTimer = 0;
  popup.addEventListener("scroll", () => {
    popup.classList.add("scrolling");
    clearTimeout(scrollHideTimer);
    scrollHideTimer = setTimeout(() => popup.classList.remove("scrolling"), 700);
  }, { passive: true });
  // Horizontal swipe on the Kanji tab cycles characters for single-screen touch users.
  let swipeX = 0, swipeY = 0, swiping = false;
  popup.addEventListener("touchstart", (event) => {
    if (event.touches.length !== 1 || tab !== "kanji") { swiping = false; return; }
    swipeX = event.touches[0].clientX;
    swipeY = event.touches[0].clientY;
    swiping = true;
  }, { passive: true });
  popup.addEventListener("touchend", (event) => {
    if (!swiping || !event.changedTouches.length) return;
    swiping = false;
    const dx = event.changedTouches[0].clientX - swipeX;
    const dy = event.changedTouches[0].clientY - swipeY;
    if (Math.abs(dx) > 36 && Math.abs(dx) > Math.abs(dy) * 1.5) stepKanji(dx < 0 ? 1 : -1);
  }, { passive: true });
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
    {"bunko", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Bunko: low-glare e-reader sepia with warm parchment, walnut ink, burnt sienna. */
:root {
  --font: "Noto Serif JP", "Yu Mincho", serif;
  --ground: #f0e6d2; --text: #2e241b; --dim: #756554; --accent: #9c5a2d; --edge: #cfc0a3; --radius: 6px;
  --box: rgba(156, 90, 45, 0.14); --box-pressed: rgba(156, 90, 45, 0.26); --sign: #8c4e24; --book: #2f6b47;
}
.popup { border: 1px solid #cfc0a3; padding: calc(9px * var(--scale)) calc(12px * var(--scale)); box-shadow: 0 8px 20px rgba(46, 36, 27, 0.18); }
.entry + .entry { border-top-color: #d8cab0; }
.headword { color: #2e241b; font-weight: 500; font-size: calc(19px * var(--scale)); }
.headword rt { display: revert; color: #756554; }
.reading { display: none; }
.tag { font-family: "Noto Sans JP", sans-serif; background: rgba(46, 36, 27, 0.07); color: #4f3f31; border: 1px solid #c4b394; border-radius: 3px; padding: 0 5px; }
.tag.frequency { background: rgba(156, 90, 45, 0.12); color: #7d4520; border-color: rgba(156, 90, 45, 0.28); }
.entry.unknown .headword { background: none; box-shadow: inset 0 -2px 0 #9c5a2d; border-radius: 0; padding: 0; }
.card { border-color: #b59a74; border-radius: 4px; }
.link { color: #9c5a2d; }
)popup"},
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
    {"inaba", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Inaba: warm charcoal with crisp goldenrod accent. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: #181714; --text: #f6f4ec; --dim: #a39f91; --accent: #eab308; --edge: rgba(234, 179, 8, 0.38); --radius: 3px;
  --box: rgba(234, 179, 8, 0.22); --box-pressed: rgba(234, 179, 8, 0.34); --sign: #fde047; --book: #4ade80;
}
.popup { border: 1px solid var(--edge); padding: calc(9px * var(--scale)) calc(12px * var(--scale)); box-shadow: 0 12px 30px rgba(0, 0, 0, 0.56); }
.entry + .entry { border-top-color: rgba(234, 179, 8, 0.22); }
.headword { color: #f6f4ec; font-weight: 700; font-size: calc(19px * var(--scale)); }
.tag { background: rgba(234, 179, 8, 0.13); color: #fde047; border: 1px solid rgba(234, 179, 8, 0.30); border-radius: 3px; padding: 0 5px; }
.tag.frequency { background: rgba(255, 255, 255, 0.08); color: #e7e4d9; border-color: rgba(255, 255, 255, 0.16); }
.entry.unknown .headword { background: none; box-shadow: inset 0 -2px 0 #eab308; border-radius: 0; padding: 0; }
.card { border-color: rgba(234, 179, 8, 0.45); border-radius: 3px; }
.link { color: #fde047; }
)popup"},
    {"ki", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Ki: pale celadon porcelain with deep moss ink and soft matcha green. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: #f2f5f0; --text: #1f2b22; --dim: #5e7062; --accent: #4a7c59; --edge: #cdd9cc; --radius: 8px;
  --box: rgba(74, 124, 89, 0.14); --box-pressed: rgba(74, 124, 89, 0.25); --sign: #366344; --book: #2e5c3c;
}
.popup { border: 1px solid #cdd9cc; padding: calc(9px * var(--scale)) calc(12px * var(--scale)); box-shadow: 0 8px 22px rgba(31, 43, 34, 0.14); }
.entry + .entry { border-top-color: #dce6db; }
.headword { color: #1f2b22; font-weight: 500; font-size: calc(19px * var(--scale)); }
.headword rt { display: revert; color: #5e7062; }
.reading { display: none; }
.tag { background: #e6eee4; color: #334d39; border: 1px solid #bfd0bc; border-radius: 3px; padding: 0 5px; }
.tag.frequency { background: rgba(74, 124, 89, 0.14); color: #2e5c3c; border-color: rgba(74, 124, 89, 0.30); }
.entry.unknown .headword { background: none; box-shadow: inset 0 -2px 0 #4a7c59; border-radius: 0; padding: 0; }
.card { border-color: #8eb097; border-radius: 5px; }
.link { color: #366344; }
)popup"},
    {"kon-neo", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Kon Neo: deep navy, warm ivory, vermilion accent, sharp corners. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: #101a3a; --text: #ece5d3; --dim: #8a93b5; --accent: #d9472b; --edge: rgba(236, 229, 211, 0.28); --radius: 0px;
  --box: #d9472b; --box-pressed: #b83820; --sign: #101a3a; --book: #4ade80;
}
.popup { border: 1px solid rgba(236, 229, 211, 0.28); padding: calc(9px * var(--scale)) calc(12px * var(--scale)); box-shadow: 0 12px 30px rgba(5, 9, 24, 0.6); }
.entry + .entry { border-top-color: rgba(236, 229, 211, 0.16); }
.headword { color: #ece5d3; font-weight: 600; font-size: calc(19px * var(--scale)); }
.headword rt { display: revert; color: #8a93b5; }
.reading { display: none; }
.tag { background: rgba(236, 229, 211, 0.08); color: #d8d1bf; border: 1px solid rgba(236, 229, 211, 0.20); border-radius: 0; padding: 0 5px; }
.tag.frequency { background: rgba(217, 71, 43, 0.16); color: #f2927e; border-color: rgba(217, 71, 43, 0.34); }
.entry.unknown .headword { background: none; box-shadow: inset 0 -2px 0 #d9472b; border-radius: 0; padding: 0; }
.card { border-color: #d9472b; border-radius: 0; }
.card.book { background: rgba(74, 222, 128, 0.14); border-color: #4ade80; color: #4ade80; }
.link { color: #f2927e; }
)popup"},
    {"kon", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Kon (紺, deep indigo): Furoshiki 3's Kon theme as a popup. Cream type on indigo, labels in
   small tracked capitals in the indigo's own pale blue, hairlines in cream, a white headword
   with its furigana over it, a white rule along the top, and the word not yet known tinted.
   Every colour is a variable. Since 2026-11-17 Kon is the maintainer's own Kon of their Z13
   (its design "kon-mine": headword #fff in Gothic, furigana at 64 %, unknown as a tint, accent
   and top edge #fff, card buttons 24, frequencies 12.5), for every app. Frequencies at 10 since 2026-11-23 (the
   maintainer: "decrease the size of the frequency tags by default"), the first four shown. */
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
.tag.frequency { background: var(--raised); border-color: transparent; font-size: calc(10px * var(--scale)); letter-spacing: 0.08em; }
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
    {"notebook", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Notebook: pure white, jet black, sharp corners, signal red accent. */
:root {
  --font: "Noto Sans JP", "Helvetica Neue", Helvetica, Arial, sans-serif;
  --ground: #ffffff; --text: #111111; --dim: #666666; --accent: #d62828; --edge: #111111; --radius: 0px;
  --box: #d62828; --box-pressed: #b21f1f; --sign: #ffffff; --book: #1b6b3a;
}
.popup { border: 1px solid #111111; padding: calc(9px * var(--scale)) calc(12px * var(--scale)); box-shadow: 0 8px 24px rgba(0, 0, 0, 0.16); }
.entry + .entry { border-top-color: #d4d4d4; }
.headword { color: #111111; font-weight: 700; font-size: calc(19px * var(--scale)); }
.tag { background: #f2f2f2; color: #222222; border: 1px solid #d4d4d4; border-radius: 0; padding: 0 5px; }
.tag.frequency { background: #111111; color: #ffffff; border-color: #111111; }
.entry.unknown .headword { background: none; box-shadow: inset 0 -2px 0 #d62828; border-radius: 0; padding: 0; }
.card { border-color: #d62828; border-radius: 0; }
.card.book { background: #e8f5ed; border-color: #1b6b3a; color: #1b6b3a; }
.link { color: #d62828; }
)popup"},
    {"obsidian", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Obsidian: monochrome dark studio card with crisp hairline border. */
:root {
  --font: "Noto Sans JP", "Yu Gothic UI", "Meiryo", sans-serif;
  --ground: #111113; --text: #f4f4f5; --dim: #8e8e98; --accent: #e4e4e7; --edge: #2c2c32; --radius: 6px;
  --box: #27272c; --box-pressed: #34343c; --sign: #ffffff; --book: #4ade80;
}
.popup { border: 1px solid #2c2c32; padding: calc(9px * var(--scale)) calc(12px * var(--scale)); box-shadow: 0 12px 30px rgba(0, 0, 0, 0.6); }
.entry + .entry { border-top-color: #24242a; }
.headword { color: #ffffff; font-weight: 600; font-size: calc(19px * var(--scale)); }
.tag { background: #202024; color: #d4d4d8; border: 1px solid #34343c; border-radius: 3px; padding: 0 5px; }
.tag.frequency { background: #27272c; color: #e4e4e7; border-color: #3a3a44; }
.entry.unknown .headword { background: none; box-shadow: inset 0 -2px 0 #e4e4e7; border-radius: 0; padding: 0; }
.card { border-color: #3f3f48; border-radius: 5px; }
.link { color: #ffffff; }
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
