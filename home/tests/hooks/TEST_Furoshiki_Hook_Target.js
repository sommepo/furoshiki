// ==UserScript==
// @name         [FUROTEST] Furoshiki Hook Target
// @version      1.0
// @author       Furoshiki contributors
// @description  Linux, Windows
// * A test's stand-in game (home/tests/hooks/hook_target.c), hooked as 0xDC00's scripts hook one.
// ==/UserScript==
const { nameOf } = require('./libFuroshikiTest.js');
const mainHandler = trans.send(handler, '200+');

const target = Process.enumerateModules()[0];
console.log('[FUROTEST] ' + nameOf(target));
Interceptor.attach(Module.findExportByName(null, "target_show_line") ?? DebugSymbol.fromName("target_show_line").address, {
    onEnter(args) {
        mainHandler.call(this, args);
    }
});

function handler(args) {
    return args[0].readUtf8String();
}
