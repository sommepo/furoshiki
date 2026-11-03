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
