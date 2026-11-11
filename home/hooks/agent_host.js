// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The host a 0xDC00 Agent script expects, inside the script itself (docs/hooks.md). Agent's own
// runtime (libLoader.js of github.com/0xDC00/scripts, fetched at the user's request, never part
// of this repository) asks its host for everything by send({cmd, ..., key}) and then waits in
// recv(key).wait(): the script's file, the files it requires, its storage. frida-inject can send
// nothing back to a script, so Furoshiki puts what would be asked for into the bundle (FILES,
// MISSION, STORAGE: written in front of this file by Hooks::Agent::Bundle) and this file answers
// in place. What is not a question goes out as it is: {cmd:"copy", text} is a line of the game.
const __furoshiki = (function () {
    // frida-inject writes a message as a line of its output but lets it wait in a buffer; what is
    // sent as ['frida:stdout', text] it writes at once. So every message goes out as its own
    // line of JSON that way, in the form frida-inject would have given it, and twice a second
    // an empty one pushes out what frida-inject wrote by itself (a script's error).
    const fridaSend = send;
    function realSend(message) {
        fridaSend(['frida:stdout', JSON.stringify({ type: 'send', payload: message }) + '\n']);
    }
    setInterval(() => fridaSend(['frida:stdout', '']), 500);
    const answers = {};
    const dir = '/scripts';

    function clean(path) {
        // "./x.js", "/scripts/./lib/../x.js", "x.js": one name within the scripts folder.
        let p = String(path).replace(/\\/g, '/');
        if (p.startsWith(dir + '/')) p = p.slice(dir.length + 1);
        const out = [];
        for (const part of p.split('/')) {
            if (part === '' || part === '.') continue;
            if (part === '..') out.pop();
            else out.push(part);
        }
        return out.join('/');
    }
    function isDir(name) {
        if (name === '') return true;
        for (const file in FILES) if (file.startsWith(name + '/')) return true;
        return false;
    }
    const functions = {
        __dirname: () => dir,
        __mission: () => './' + MISSION,
        fs_readFileSync(path) {
            const name = clean(path);
            return Object.prototype.hasOwnProperty.call(FILES, name) ? FILES[name] : null;
        },
        fs_writeFileSync: () => false,
        fs_realpathSync: (path) => dir + '/' + clean(path),
        path_dirname(path) {
            const name = clean(path), cut = name.lastIndexOf('/');
            return cut < 0 ? dir : dir + '/' + name.slice(0, cut);
        },
        _statPath(path) {
            const name = clean(path);
            const file = Object.prototype.hasOwnProperty.call(FILES, name);
            return { path: dir + '/' + name, isFile: file, isDir: !file && isDir(name) };
        },
        evalJs: () => null,
    };
    function storage(which, op, args) {
        const store = STORAGE[which];
        switch (op) {
        case 'getItem': return Object.prototype.hasOwnProperty.call(store, args[0]) ? store[args[0]] : (args[1] === undefined ? null : args[1]);
        case 'setItem': store[args[0]] = String(args[1]); return undefined;
        case 'removeItem': delete store[args[0]]; return undefined;
        case 'clear': for (const key in store) delete store[key]; return undefined;
        case 'key': { const keys = Object.keys(store); return args[0] < keys.length ? keys[args[0]] : null; }
        case 'length': return Object.keys(store).length;
        case 'keys': return Object.keys(store);
        }
        return undefined;
    }
    function answer(message) {
        const cmd = message.cmd;
        if (cmd === 'eval') {
            const f = functions[String(message.func)];
            if (f === undefined) {
                console.warn('[furoshiki] the script asked its host for ' + message.func + ', which Furoshiki does not have');
                return null;
            }
            return f.apply(null, message.args || []);
        }
        const split = typeof cmd === 'string' ? cmd.match(/^(sessionStorage|localStorage)_(\w+)$/) : null;
        if (split !== null) {
            const result = storage(split[1], split[2], message.args || []);
            if (split[2] === 'setItem' || split[2] === 'removeItem' || split[2] === 'clear') {
                // Kept by Furoshiki for the next time this game is hooked.
                realSend({ cmd: 'furoshiki_storage', which: split[1], op: split[2], args: (message.args || []).map(String) });
            }
            return result;
        }
        if (cmd === 'prompt') return message.default === undefined ? null : message.default;
        if (cmd === 'getScripts') return [];
        if (cmd === 'rpc_invoke') return null;
        return undefined;
    }
    const mine = /^(eval|prompt|getScripts|rpc_invoke|rpc_send|writeBytes|loadHtml|setTargetID|(sessionStorage|localStorage)_\w+)$/;
    function vsend(message, data) {
        if (message !== null && typeof message === 'object' && typeof message.cmd === 'string' && mine.test(message.cmd)) {
            const result = answer(message);
            if (message.key !== undefined) answers[message.key] = result;
            return;
        }
        realSend(message);
    }
    function vrecv(key, callback) {
        if (typeof key === 'function') return recv.apply(null, arguments);
        const result = answers[key];
        delete answers[key];
        let called = false;
        const call = () => { if (!called) { called = true; callback({ type: key, result: result === undefined ? null : result }, null); } };
        // Agent's runtime waits for the answer; a promise's caller does not.
        setTimeout(call, 0);
        return { wait: call };
    }
    return { send: vsend, recv: vrecv };
})();
