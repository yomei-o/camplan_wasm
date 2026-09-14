// Drives the real page in headless Chrome over the DevTools protocol: serves
// docs/, adds a camera by synthetic mouse, edits its properties in the panel,
// and checks what the page hands back to a host script.  This is the only
// test that covers the HTML/JS shell - node_check.js stops at the wasm.
//
//   node tests/page_check.js [--shot out.png]
//
// Chrome is found through $CHROME or the usual install paths.  No packages:
// the CDP client is node's own WebSocket.
'use strict';
const fs = require('fs');
const http = require('http');
const path = require('path');
const { spawn } = require('child_process');
const os = require('os');

const DOCS = path.join(__dirname, '..', 'docs');
const PORT = 8123;
const CDP_PORT = 9222;
const shotArg = process.argv.indexOf('--shot');
const SHOT = shotArg > 0 ? process.argv[shotArg + 1] : null;

const CHROMES = [
    process.env.CHROME,
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    '/usr/bin/google-chrome',
    '/usr/bin/chromium',
];

const TYPES = {
    '.html': 'text/html', '.js': 'text/javascript', '.jpg': 'image/jpeg',
    '.png': 'image/png', '.json': 'application/json',
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function serve() {
    const server = http.createServer((req, res) => {
        const rel = decodeURIComponent(req.url.split('?')[0]);
        const file = path.join(DOCS, rel === '/' ? 'index.html' : rel);
        fs.readFile(file, (err, data) => {
            if (err) { res.writeHead(404); res.end('no'); return; }
            res.writeHead(200,
                { 'Content-Type': TYPES[path.extname(file)] || 'text/plain' });
            res.end(data);
        });
    });
    return new Promise((res) => server.listen(PORT, '127.0.0.1',
                                              () => res(server)));
}

function connect(url) {
    return new Promise((res, rej) => {
        const ws = new WebSocket(url);
        ws.onopen = () => res(ws);
        ws.onerror = () => rej(new Error('cannot connect to ' + url));
    });
}

function client(ws) {
    const pending = new Map();
    const events = [];
    let nextId = 1;
    ws.onmessage = (ev) => {
        const msg = JSON.parse(ev.data);
        if (msg.id && pending.has(msg.id)) {
            const { res, rej } = pending.get(msg.id);
            pending.delete(msg.id);
            if (msg.error) rej(new Error(JSON.stringify(msg.error)));
            else res(msg.result);
        } else if (msg.method) {
            events.push(msg);
        }
    };
    const send = (method, params) => new Promise((res, rej) => {
        const id = nextId++;
        pending.set(id, { res, rej });
        ws.send(JSON.stringify({ id, method, params: params || {} }));
    });
    return { send, events };
}

(async () => {
    const chrome = CHROMES.find((p) => p && fs.existsSync(p));
    if (!chrome) {
        console.log('page check skipped: no Chrome (set $CHROME)');
        return;
    }
    const server = await serve();
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'camplan-'));
    const browser = spawn(chrome, [
        '--headless=new', '--disable-gpu', '--no-first-run',
        '--no-default-browser-check', '--window-size=1400,900',
        '--remote-debugging-port=' + CDP_PORT,
        '--user-data-dir=' + profile, 'about:blank',
    ], { stdio: 'ignore' });

    let failures = 0;
    const check = (name, got, want) => {
        const ok = JSON.stringify(got) === JSON.stringify(want);
        if (!ok) failures++;
        console.log((ok ? 'ok   ' : 'FAIL ') + name + (ok ? '' :
            '\n       got  ' + JSON.stringify(got) +
            '\n       want ' + JSON.stringify(want)));
    };

    try {
        let list = null;
        for (let i = 0; i < 40 && !list; i++) {
            await sleep(250);
            try {
                const r = await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`);
                const j = await r.json();
                if (j.some((t) => t.type === 'page')) list = j;
            } catch (err) { /* not up yet */ }
        }
        if (!list) throw new Error('chrome did not open a debugging port');

        const ws = await connect(
            list.find((t) => t.type === 'page').webSocketDebuggerUrl);
        const { send, events } = client(ws);
        await send('Page.enable');
        await send('Runtime.enable');
        await send('Log.enable');
        await send('Page.navigate', { url: `http://127.0.0.1:${PORT}/` });
        await sleep(3500);   // module init plus the demo background fetch

        const js = async (expr) => {
            const r = await send('Runtime.evaluate',
                { expression: expr, returnByValue: true, awaitPromise: true });
            if (r.exceptionDetails)
                throw new Error('page threw: ' +
                                JSON.stringify(r.exceptionDetails));
            return r.result.value;
        };
        const mouse = (type, x, y, clicks) => send('Input.dispatchMouseEvent', {
            type, x, y, button: 'left', clickCount: clicks || 1,
            buttons: type === 'mouseReleased' ? 0 : 1,
        });
        const labels = () => js(
            `Array.from(document.querySelectorAll('#pKv label'))` +
            `.map(function (i) { return i.textContent; })`);
        const inputs = () => js(
            `Array.from(document.querySelectorAll('#pKv input'))` +
            `.map(function (i) { return i.value; })`);
        // The four fields are fixed and always in this order, so index them.
        const field = (n) => `#pKv input:nth-of-type(${n + 1})`;
        const typeInto = (sel, value) => js(
            `(function () { var el = document.querySelector(` +
            `${JSON.stringify(sel)}); el.value = ${JSON.stringify(value)};` +
            ` el.dispatchEvent(new Event('change')); })()`);

        check('the canvas is up', await js(
            `document.getElementById('view').width > 100`), true);

        // Watch what the page is handed.
        await js(`window.sel = null; window.open_ = null;
            window.onCameraSelect = function (n, p) { window.sel = [n, p]; };
            window.onCameraOpen = function (n, p) { window.open_ = [n, p]; };`);

        // Add a camera: pick the tool, click, drag to aim.
        await js(`document.getElementById('m1').click()`);
        const box = await js(
            `(function () { var r = document.getElementById('view')` +
            `.getBoundingClientRect();` +
            ` return { x: r.left, y: r.top, w: r.width, h: r.height }; })()`);
        const cx = Math.round(box.x + box.w / 2);
        const cy = Math.round(box.y + box.h / 2);
        await mouse('mousePressed', cx, cy);
        await mouse('mouseMoved', cx + 80, cy + 40);
        await mouse('mouseReleased', cx + 80, cy + 40);
        await sleep(400);
        check('the camera is placed and selected',
              await js(`document.getElementById('pNo').value`), '1');
        check('onCameraSelect fired', await js(`window.sel[0]`), 1);
        check('with no properties yet', await js(`window.sel[1]`), {});

        // The properties are a fixed set of four; type into each one.
        check('the four fixed fields', await labels(),
              ['名前', 'デバイスID', 'APIキー', 'コメント']);
        check('empty to start', await inputs(), ['', '', '', '']);
        await typeInto(field(0), 'エントランス');
        await sleep(150);
        await typeInto(field(1), 'dev-123');
        await sleep(150);
        await typeInto(field(2), 'key-abc');
        await sleep(150);
        await typeInto(field(3), '受付の上');
        await sleep(300);

        // Deselect, select again: the fields are refilled from the C++ side,
        // so what shows up now is what was really stored.
        await js(`document.getElementById('m0').click()`);
        await mouse('mousePressed', Math.round(box.x + 20),
                    Math.round(box.y + 20));
        await mouse('mouseReleased', Math.round(box.x + 20),
                    Math.round(box.y + 20));
        await sleep(300);
        check('deselecting tells the page', await js(`window.sel[0]`), 0);
        await js(`document.querySelector('#camList .chip').click()`);
        await sleep(400);
        check('the values come back from C++', await inputs(),
              ['エントランス', 'dev-123', 'key-abc', '受付の上']);
        check('and so do the properties', await js(`window.sel[1]`),
              { name: 'エントランス', device_id: 'dev-123',
                api_key: 'key-abc', comment: '受付の上' });

        // Double click: the same object, as the cue to open the video.
        await mouse('mousePressed', cx, cy, 2);
        await mouse('mouseReleased', cx, cy, 2);
        await sleep(400);
        check('onCameraOpen number', await js(`window.open_[0]`), 1);
        check('onCameraOpen props', await js(`window.open_[1]`),
              { name: 'エントランス', device_id: 'dev-123',
                api_key: 'key-abc', comment: '受付の上' });

        // Clear a field, then take the value back with Ctrl+Z.  Undo drops
        // the selection (it always has), so the camera has to be picked again.
        await typeInto(field(3), '');
        await sleep(300);
        check('the field is cleared', (await inputs())[3], '');
        await js(`document.getElementById('view').focus()`);
        for (const type of ['keyDown', 'keyUp'])
            await send('Input.dispatchKeyEvent', {
                type, key: 'z', code: 'KeyZ', windowsVirtualKeyCode: 90,
                nativeVirtualKeyCode: 90, modifiers: 2,
            });
        await sleep(400);
        await js(`document.querySelector('#camList .chip').click()`);
        await sleep(400);
        check('undo brings it back', await inputs(),
              ['エントランス', 'dev-123', 'key-abc', '受付の上']);

        // Nothing on the page may have thrown.  A missing favicon is the
        // server's business, not the page's.
        const bad = events.filter((e) =>
            e.method === 'Runtime.exceptionThrown' ||
            (e.method === 'Log.entryAdded' &&
             e.params.entry.level === 'error' &&
             !/favicon\.ico/.test(e.params.entry.url || '')));
        check('no errors on the page',
              bad.map((e) => JSON.stringify(e.params).slice(0, 200)), []);

        if (SHOT) {
            const shot = await send('Page.captureScreenshot', { format: 'png' });
            fs.writeFileSync(SHOT, Buffer.from(shot.data, 'base64'));
            console.log('screenshot ' + SHOT);
        }
        ws.close();
    } finally {
        browser.kill();
        server.close();
        try { fs.rmSync(profile, { recursive: true, force: true }); }
        catch (err) { /* chrome may still hold it; it is a temp dir */ }
    }
    if (failures) {
        console.error(`page check: ${failures} failed`);
        process.exit(1);
    }
    console.log('page check ok');
})().catch((e) => { console.error(e); process.exit(1); });
