// The WebAssembly boundary.  The page owns the DOM, file pickers and PNG
// encoding (canvas.toBlob); everything else - drawing, hit-testing, the
// document, save and load - lives on this side.
#include <limits>
#include <emscripten.h>

#include <cstddef>
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
std::string g_propsText;

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

// This floor's position.  NaN means "not set", both ways - it keeps the pair
// of calls symmetrical and needs no second "is it there" export.  Out of range
// clears, so a typo cannot put a camera in the sea.
// No pushHistory: undo covers what is drawn, and a typed-in coordinate is not
// something anyone expects Ctrl+Z to take back.
EMSCRIPTEN_KEEPALIVE double cp_get_lat(void) {
    return g_app.doc.lat ? *g_app.doc.lat
                         : std::numeric_limits<double>::quiet_NaN();
}
EMSCRIPTEN_KEEPALIVE double cp_get_lon(void) {
    return g_app.doc.lon ? *g_app.doc.lon
                         : std::numeric_limits<double>::quiet_NaN();
}
EMSCRIPTEN_KEEPALIVE void cp_set_lat(double v) {
    if (v >= -90 && v <= 90) g_app.doc.lat = v;
    else g_app.doc.lat.reset();
}
EMSCRIPTEN_KEEPALIVE void cp_set_lon(double v) {
    if (v >= -180 && v <= 180) g_app.doc.lon = v;
    else g_app.doc.lon.reset();
}
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

/* ----------------------------------------------------------- properties */

// Free-form key/value strings on a camera, beside its number.  The editor
// itself never looks at them; they exist for the page, which gets them as an
// object when a camera is clicked.  Every setter refuses the edit (and leaves
// no undo step behind) rather than silently making the table inconsistent.

EMSCRIPTEN_KEEPALIVE int cp_sel_prop_count(void) {
    const cam::Camera * c = g_app.selectedCamera();
    return c ? (int)c->props.size() : 0;
}
EMSCRIPTEN_KEEPALIVE const char * cp_sel_prop_key(int i) {
    const cam::Camera * c = g_app.selectedCamera();
    if (!c || i < 0 || i >= (int)c->props.size()) return "";
    return c->props[(size_t)i].key.c_str();
}
EMSCRIPTEN_KEEPALIVE const char * cp_sel_prop_value(int i) {
    const cam::Camera * c = g_app.selectedCamera();
    if (!c || i < 0 || i >= (int)c->props.size()) return "";
    return c->props[(size_t)i].value.c_str();
}

// Adds the key or replaces its value.  0 = no selection, or an empty key.
EMSCRIPTEN_KEEPALIVE int cp_sel_set_prop(const char * key,
                                         const char * value) {
    cam::Camera * c = g_app.selectedCamera();
    if (!c || !key || cam::plainText(key).empty()) return 0;
    g_app.pushHistory();
    return c->setProp(key, value ? value : "") ? 1 : 0;
}

// Renames in place, keeping the row's position.  0 when the new key is empty
// or already used by another row.
EMSCRIPTEN_KEEPALIVE int cp_sel_rename_prop(const char * from,
                                            const char * to) {
    cam::Camera * c = g_app.selectedCamera();
    if (!c || !from || !to) return 0;
    const std::string want = cam::plainText(to);
    if (want.empty() || c->findProp(from) < 0) return 0;
    if (want != from && c->findProp(want) >= 0) return 0;
    g_app.pushHistory();
    return c->renameProp(from, want) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int cp_sel_remove_prop(const char * key) {
    cam::Camera * c = g_app.selectedCamera();
    if (!c || !key || c->findProp(key) < 0) return 0;
    g_app.pushHistory();
    return c->removeProp(key) ? 1 : 0;
}

// Every property of one camera as a JSON object, for the page's click hook.
// An unknown number gives {}.
EMSCRIPTEN_KEEPALIVE const char * cp_camera_props(int number) {
    const int at = g_app.doc.findCamera(number);
    g_propsText = at < 0 ? "{}" : g_app.doc.cameras[(size_t)at].propsJson();
    return g_propsText.c_str();
}

/* ------------------------------------------------------------ background */

// The page hands over the dropped file untouched; the decoding happens here
// (stb_image), so the big RGBA buffer never crosses the boundary and the
// browser is not asked to decode anything.  0 = not a readable image.
EMSCRIPTEN_KEEPALIVE int cp_set_background(const uint8_t * file, int fileLen,
                                           const char * name) {
    if (fileLen <= 0 || !file) return 0;
    if (!g_app.doc.setBackgroundFile(file, (size_t)fileLen,
                                     name ? name : "")) return 0;
    g_app.zoomToFit();
    return 1;
}

EMSCRIPTEN_KEEPALIVE void cp_clear_background(void) {
    g_app.doc.background.reset();
    g_app.markDirty();
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
