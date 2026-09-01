// Drives the WebAssembly build the way the page does, without a browser:
// places cameras by synthetic mouse, draws a wall, saves, reloads, exports,
// and writes the frames as BMP for eyes.
'use strict';
const fs = require('fs');
const path = require('path');

const createCamplan = require('../docs/camplan.js');

function writeBmp(name, buf, w, h) {
    const stride = w * 3, pad = (4 - stride % 4) % 4;
    const dataSize = (stride + pad) * h;
    const out = Buffer.alloc(54 + dataSize);
    out.write('BM');
    out.writeUInt32LE(54 + dataSize, 2);
    out.writeUInt32LE(54, 10);
    out.writeUInt32LE(40, 14);
    out.writeInt32LE(w, 18);
    out.writeInt32LE(h, 22);
    out.writeUInt16LE(1, 26);
    out.writeUInt16LE(24, 28);
    out.writeUInt32LE(dataSize, 34);
    let o = 54;
    for (let y = h - 1; y >= 0; y--) {
        for (let x = 0; x < w; x++) {
            const i = (y * w + x) * 4;
            out[o++] = buf[i + 2];
            out[o++] = buf[i + 1];
            out[o++] = buf[i];
        }
        o += pad;
    }
    fs.writeFileSync(path.join(__dirname, name), out);
}

createCamplan().then((M) => {
    const W = 1280, H = 800;
    M._cp_init(W, H);

    // Three cameras, each placed with the click-and-aim gesture.
    const plans = [[300, 300, 420, 380], [800, 250, 700, 400],
                   [500, 600, 620, 540]];
    for (const [x, y, ax, ay] of plans) {
        M._cp_set_mode(1);
        M._cp_mouse_down(x, y, 0);
        M._cp_mouse_move(ax, ay);
        M._cp_mouse_up(ax, ay, 0);
    }
    if (M._cp_camera_count() !== 3) throw new Error('camera count');
    if (M._cp_sel_number() !== 3) throw new Error('selection after add');

    // A wall of three points, finished with Enter.
    M._cp_set_mode(2);
    M._cp_mouse_down(250, 200, 0); M._cp_mouse_up(250, 200, 0);
    M._cp_mouse_down(900, 200, 0); M._cp_mouse_up(900, 200, 0);
    M._cp_mouse_down(900, 700, 0); M._cp_mouse_up(900, 700, 0);
    M._cp_key(13);

    // Adjust the selected camera through the panel setters.
    M._cp_set_mode(0);
    M._cp_select_number(2);
    M._cp_sel_set_fov(120);
    M._cp_sel_set_range(300);
    if (!M._cp_sel_set_number(20)) throw new Error('renumber');
    if (M._cp_sel_set_number(1)) throw new Error('renumber to taken');

    const ptr = M._cp_render();
    writeBmp('node_view.bmp',
             Buffer.from(M.HEAPU8.buffer, ptr, W * H * 4), W, H);

    // Save, wipe, reload.
    const savePtr = M._cp_save();
    const saveLen = M._cp_save_size();
    const json = Buffer.from(M.HEAPU8.buffer, savePtr, saveLen).toString();
    const bytes = Buffer.from(json);
    const p = M._cp_alloc(bytes.length);
    M.HEAPU8.set(bytes, p);
    if (!M._cp_load(p, bytes.length)) throw new Error('load');
    M._cp_free(p);
    if (M._cp_camera_count() !== 3) throw new Error('count after reload');
    const numbers = [];
    for (let i = 0; i < 3; i++) numbers.push(M._cp_camera_number_at(i));
    numbers.sort((a, b) => a - b);
    if (numbers.join() !== '1,3,20') throw new Error('numbers ' + numbers);

    const ex = M._cp_export_render();
    writeBmp('node_export.bmp',
             Buffer.from(M.HEAPU8.buffer, ex, M._cp_export_w() *
                         M._cp_export_h() * 4),
             M._cp_export_w(), M._cp_export_h());

    console.log('node check ok: cameras', numbers.join(','),
                'export', M._cp_export_w() + 'x' + M._cp_export_h());
});
