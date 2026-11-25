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
      image.addEventListener("load", reportSoon);
      image.src = source;
      box.appendChild(image);
    }
    if (!width && !height) box.classList.add("natural");
    return box;
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
      entry.frequencies.forEach((frequency, index) => {
        if (shown && index >= shown) return;
        const tag = element("span", "tag frequency");
        tag.dataset.category = "frequency";
        tag.dataset.dictionary = frequency.dictionary;
        tag.appendChild(element("span", "tag-name", frequency.dictionary));
        tag.appendChild(element("span", "tag-value", frequency.value));
        tags.appendChild(tag);
      });
      if (shown && entry.frequencies.length > shown) {
        const more = element("span", "tag frequency more-frequencies", "+" + (entry.frequencies.length - shown));
        more.dataset.category = "frequency";
        more.title = entry.frequencies.slice(shown).map((f) => f.dictionary + " " + f.value).join("\n");
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
        if (entry.card && (model.look || {}).cards !== false && (!kind || kind === "name")) put(nameCard(entry.card));
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
    post({ type: "size", width: popup.offsetWidth, height: content, fit: fit, need: need, entries: entries.length, tabs: tabs.length,
           scrolled: popup.scrollTop, boxes: boxes, ease: performance.now() < easeUntil });
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
