// The WebAssembly boundary.  The page owns the DOM, file pickers and PNG
// encoding (canvas.toBlob); everything else - drawing, hit-testing, the
// document, save and load - lives on this side.
#include <emscripten.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "app.h"

using cam::App;
using cam::Mode;

namespace cam { void setAppTheme(int index); }

namespace {

App g_app;
std::string g_saveText;

}   // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void cp_init(int w, int h) { g_app.init(w, h); }
EMSCRIPTEN_KEEPALIVE void cp_resize(int w, int h) { g_app.resizeScreen(w, h); }

EMSCRIPTEN_KEEPALIVE int cp_dirty(void) { return g_app.dirty() ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE const uint32_t * cp_render(void) {
    return g_app.render().px.data();
}

EMSCRIPTEN_KEEPALIVE void cp_mouse_down(float x, float y, int button) {
    g_app.mouseDown(x, y, button);
}
EMSCRIPTEN_KEEPALIVE void cp_mouse_move(float x, float y) {
    g_app.mouseMove(x, y);
}
EMSCRIPTEN_KEEPALIVE void cp_mouse_up(float x, float y, int button) {
    g_app.mouseUp(x, y, button);
}
EMSCRIPTEN_KEEPALIVE void cp_wheel(float x, float y, float dy) {
    g_app.wheel(x, y, dy);
}
EMSCRIPTEN_KEEPALIVE void cp_key(int code) { g_app.keyDown(code); }

EMSCRIPTEN_KEEPALIVE void cp_set_mode(int m) { g_app.setMode((Mode)m); }
EMSCRIPTEN_KEEPALIVE int cp_get_mode(void) { return (int)g_app.mode(); }
EMSCRIPTEN_KEEPALIVE void cp_set_theme(int t) {
    cam::setAppTheme(t);
    g_app.markDirty();
}
EMSCRIPTEN_KEEPALIVE void cp_zoom_fit(void) { g_app.zoomToFit(); }
EMSCRIPTEN_KEEPALIVE void cp_set_marker(float r) {
    g_app.pushHistory("marker");
    g_app.doc.markerSize = r < 6 ? 6 : (r > 80 ? 80 : r);
    g_app.markDirty();
}
EMSCRIPTEN_KEEPALIVE float cp_get_marker(void) { return g_app.doc.markerSize; }
EMSCRIPTEN_KEEPALIVE int cp_camera_at(float x, float y) {
    return g_app.cameraNumberAtScreen(x, y);
}
EMSCRIPTEN_KEEPALIVE void cp_undo(void) { g_app.undo(); }
EMSCRIPTEN_KEEPALIVE void cp_redo(void) { g_app.redo(); }

/* ------------------------------------------------------------- selection */

EMSCRIPTEN_KEEPALIVE int cp_sel_number(void) {
    const cam::Camera * c = g_app.selectedCamera();
    return c ? c->number : 0;
}
EMSCRIPTEN_KEEPALIVE float cp_sel_dir(void) {
    const cam::Camera * c = g_app.selectedCamera();
    return c ? c->dirDeg : 0;
}
EMSCRIPTEN_KEEPALIVE float cp_sel_fov(void) {
    const cam::Camera * c = g_app.selectedCamera();
    return c ? c->fovDeg : 0;
}
EMSCRIPTEN_KEEPALIVE float cp_sel_range(void) {
    const cam::Camera * c = g_app.selectedCamera();
    return c ? c->range : 0;
}
EMSCRIPTEN_KEEPALIVE int cp_sel_set_number(int n) {
    g_app.pushHistory("number");
    return g_app.setSelectedNumber(n) ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE void cp_sel_set_dir(float v) {
    if (cam::Camera * c = g_app.selectedCamera()) {
        g_app.pushHistory("dir");
        c->dirDeg = v;
        g_app.markDirty();
    }
}
EMSCRIPTEN_KEEPALIVE void cp_sel_set_fov(float v) {
    if (cam::Camera * c = g_app.selectedCamera()) {
        g_app.pushHistory("fov");
        c->fovDeg = v < 10 ? 10 : (v > 359 ? 359 : v);
        g_app.markDirty();
    }
}
EMSCRIPTEN_KEEPALIVE void cp_sel_set_range(float v) {
    if (cam::Camera * c = g_app.selectedCamera()) {
        g_app.pushHistory("range");
        c->range = v < 24 ? 24 : (v > 4000 ? 4000 : v);
        g_app.markDirty();
    }
}
EMSCRIPTEN_KEEPALIVE void cp_delete_selected(void) { g_app.deleteSelected(); }
EMSCRIPTEN_KEEPALIVE void cp_select_number(int n) { g_app.selectNumber(n); }

EMSCRIPTEN_KEEPALIVE int cp_camera_count(void) {
    return (int)g_app.doc.cameras.size();
}
EMSCRIPTEN_KEEPALIVE int cp_camera_number_at(int i) {
    if (i < 0 || i >= (int)g_app.doc.cameras.size()) return 0;
    return g_app.doc.cameras[i].number;
}

/* ------------------------------------------------------------ background */

EMSCRIPTEN_KEEPALIVE void cp_set_background(const uint32_t * pixels, int w,
                                            int h, const uint8_t * file,
                                            int fileLen, const char * name) {
    cam::Background bg;
    bg.name = name ? name : "";
    bg.w = w;
    bg.h = h;
    bg.pixels.assign(pixels, pixels + (size_t)w * h);
    if (file && fileLen > 0) bg.fileBytes.assign(file, file + fileLen);
    g_app.doc.background = std::move(bg);
    g_app.zoomToFit();
}

// After cp_load, the stored file bytes still need decoding by the page; it
// reads them here and hands the pixels back through this.
EMSCRIPTEN_KEEPALIVE void cp_set_background_pixels(const uint32_t * pixels,
                                                   int w, int h) {
    if (!g_app.doc.background) return;
    g_app.doc.background->w = w;
    g_app.doc.background->h = h;
    g_app.doc.background->pixels.assign(pixels, pixels + (size_t)w * h);
    g_app.zoomToFit();
}

EMSCRIPTEN_KEEPALIVE void cp_clear_background(void) {
    g_app.doc.background.reset();
    g_app.markDirty();
}

EMSCRIPTEN_KEEPALIVE const uint8_t * cp_bg_bytes(void) {
    return g_app.doc.background ? g_app.doc.background->fileBytes.data()
                                : nullptr;
}
EMSCRIPTEN_KEEPALIVE int cp_bg_size(void) {
    return g_app.doc.background ? (int)g_app.doc.background->fileBytes.size()
                                : 0;
}

/* ----------------------------------------------------------- save / load */

EMSCRIPTEN_KEEPALIVE const char * cp_save(void) {
    g_saveText = g_app.doc.toJson();
    return g_saveText.c_str();
}
EMSCRIPTEN_KEEPALIVE int cp_save_size(void) { return (int)g_saveText.size(); }

EMSCRIPTEN_KEEPALIVE int cp_load(const char * text, int len) {
    if (!g_app.doc.fromJson(std::string(text, (size_t)len))) return 0;
    g_app.selectNumber(0);
    g_app.zoomToFit();
    return 1;
}

/* ---------------------------------------------------------------- export */

static int g_exportW = 0;
static int g_exportH = 0;

EMSCRIPTEN_KEEPALIVE const uint32_t * cp_export_render(void) {
    const cam::Canvas & out = g_app.renderExport();
    g_exportW = out.w;
    g_exportH = out.h;
    return out.px.data();
}
EMSCRIPTEN_KEEPALIVE int cp_export_w(void) { return g_exportW; }
EMSCRIPTEN_KEEPALIVE int cp_export_h(void) { return g_exportH; }

EMSCRIPTEN_KEEPALIVE void * cp_alloc(int size) { return std::malloc(size); }
EMSCRIPTEN_KEEPALIVE void cp_free(void * p) { std::free(p); }

}   // extern "C"
