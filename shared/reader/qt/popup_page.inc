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
  --font: "Noto Sans JP", "Noto Sans CJK JP", "Yu Gothic UI", "Meiryo", sans-serif;
}
html, body { margin: 0; padding: 0; background: transparent; overflow: hidden; }
body { font-family: var(--font); color: var(--text); -webkit-user-select: none; user-select: none; }
.popup {
  box-sizing: border-box;
  max-height: 100vh;
  overflow-y: auto;
  background: var(--ground);
  border: 1px solid var(--edge);
  border-radius: 4px;
  padding: calc(5px * var(--scale)) calc(5px * var(--scale)) calc(4px * var(--scale)) calc(7px * var(--scale));
  font-size: calc(13px * var(--scale));
  line-height: 1.35;
  scrollbar-width: thin;
}
.popup::-webkit-scrollbar { width: 4px; }
.popup::-webkit-scrollbar-thumb { background: var(--edge); border-radius: 2px; }
.entry { position: relative; padding: 3px 0 4px; }
.entry + .entry { border-top: 1px solid var(--edge); }
.head { display: flex; flex-wrap: wrap; align-items: baseline; gap: 0 0.6em; }
.headword { font-size: calc(17px * var(--scale)); font-weight: 600; ruby-position: over; }
.headword rt { display: none; font-size: 0.5em; font-weight: 400; color: var(--dim); }
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
    if (window.chrome && window.chrome.webview) {
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

  function senses(entry) {
    const out = document.createDocumentFragment();
    let list = null;
    (entry.senses || []).forEach((sense) => {
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
      list.appendChild(item);
    });
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
    document.documentElement.style.setProperty("--scale", String((model.scale || 100) / 100));
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
      section.appendChild(senses(entry));
      if (entry.forms && entry.forms.length) section.appendChild(forms(entry));
      section.appendChild(buttons(entry, index));
      popup.appendChild(section);
    });
    popup.scrollTop = 0;
    report();
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
    },
    scrollTo(y) {
      popup.scrollTop = y;
      reportSoon();
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
    {"furoshiki", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Furoshiki glass: a translucent indigo panel with a lavender double border and rounded white
   gothic text with a soft glow, as Furoshiki's own menus (and Never7's). */
:root { --font: "M PLUS Rounded 1c", "Hiragino Maru Gothic ProN", "Noto Sans JP", sans-serif; --accent: #7ff0c8; --dim: #cfc9f5; }
.popup {
  background: rgba(52, 40, 130, 0.86);
  border: 3px double #b9b0f2;
  border-radius: 14px;
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
    {"paper-dark", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Paper dark: Paper's quiet serif on dark brown, cream ink. */
:root { --font: "Noto Serif JP", "Yu Mincho", serif; --dim: #b8a88a; --accent: #d9a55a; }
.popup { background: #2b2119; border: 1px solid #5a4632; border-radius: 4px; color: #efe4cf; box-shadow: 0 4px 18px rgba(0, 0, 0, 0.6); }
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
:root { --font: "Noto Serif JP", "Yu Mincho", serif; --dim: #7a6e5a; --accent: #b0782a; }
.popup { background: #fbf8f1; border: 1px solid #d9d0bd; border-radius: 4px; color: #2a2620; box-shadow: 0 4px 18px rgba(0, 0, 0, 0.5); }
.entry + .entry { border-top-color: #e2d9c6; }
.tag { font-family: "Noto Sans JP", sans-serif; color: #6b5a3a; background: #efe6d2; border-radius: 3px; padding: 0 5px; }
.tag.frequency { background: #e3eedc; color: #3e6a35; }
.entry.unknown .headword { background: #fff1b8; box-shadow: none; }
.card, .card.book { background: #efe6d2; border-color: #a8956c; color: #5a4524; }
.card:active, .card.pressed { background: #e2d4b3; }
)popup"},
    {"yomitan-dark", R"popup(/* SPDX-FileCopyrightText: Readers contributors
   SPDX-License-Identifier: GPL-2.0-or-later
   Yomitan dark, after the maintainer's screenshot: black, serif glosses, glowing tag boxes,
   furigana over the headword, a blue triangle beside it. */
:root { --font: "Noto Serif JP", "Yu Mincho", serif; --dim: #c8c8d8; }
.popup { background: #0b0b0b; border: 1px solid #555; border-radius: 8px; padding: 10px 14px 10px 18px; color: #f2f2f2; }
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
