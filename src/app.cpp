#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cam {

namespace {

constexpr float kPi = 3.14159265358979f;

// World-unit sizes, so the drawing keeps its proportions at any zoom and in
// the export.
constexpr float kWallWidth = 4.f;
constexpr float kHandleRadius = 7.f;    // screen pixels - handles are UI

inline float deg2rad(float d) { return d * kPi / 180.f; }

inline float angleOf(float dx, float dy) {
    return std::atan2(dy, dx) * 180.f / kPi;
}

inline float normDeg(float d) {
    while (d < 0) d += 360.f;
    while (d >= 360.f) d -= 360.f;
    return d;
}

// The look.  Dark is the control-room one; light is white paper with the
// pale-blue grid.
struct Theme {
    uint32_t paper, gridMinor, gridMajor;
    uint32_t wallGlow, wallCore;
    uint32_t fanFill, fanEdge, fanRing, aimLine;
    uint32_t camFill, camRing, camText;
    uint32_t selFanFill, selFanEdge, selRing;
    uint32_t handleFill, handleRing;
    uint32_t pendingWall;
    uint32_t imageDim;      // painted over a background image; 0 = none
};

const Theme kDark = {
    rgba(9, 16, 28), rgba(0, 170, 220, 26), rgba(0, 190, 235, 60),
    rgba(120, 200, 255, 46), rgba(168, 205, 235),
    rgba(0, 216, 255, 34), rgba(0, 224, 255, 190), rgba(0, 216, 255, 60),
    rgba(0, 216, 255, 90),
    rgba(10, 26, 40), rgba(0, 224, 255, 230), rgba(235, 250, 255),
    rgba(255, 170, 40, 52), rgba(255, 190, 60, 220), rgba(255, 190, 60, 240),
    rgba(255, 190, 60), rgba(20, 30, 40),
    rgba(255, 190, 60, 200),
    rgba(4, 10, 20, 96),
};

const Theme kLight = {
    rgba(255, 255, 255), rgba(180, 222, 240), rgba(140, 200, 228),
    rgba(70, 90, 110, 40), rgba(70, 90, 110),
    rgba(255, 150, 0, 40), rgba(235, 120, 0, 200), rgba(235, 120, 0, 70),
    rgba(235, 120, 0, 90),
    rgba(255, 255, 255), rgba(20, 90, 200, 235), rgba(20, 40, 70),
    rgba(255, 120, 0, 60), rgba(240, 100, 0, 230), rgba(240, 100, 0, 245),
    rgba(240, 100, 0), rgba(255, 255, 255),
    rgba(240, 100, 0, 200),
    0,
};

int g_theme = 0;    // 0 dark, 1 light

const Theme & theme() { return g_theme ? kLight : kDark; }

}   // namespace

void setAppTheme(int index);   // forward declared for the host

void App::init(int screenW, int screenH) {
    screen_.resize(screenW, screenH);
    dirty_ = true;
}

void App::resizeScreen(int screenW, int screenH) {
    screen_.resize(screenW, screenH);
    dirty_ = true;
}

Camera * App::selectedCamera() {
    if (selected_ < 0 || selected_ >= (int)doc.cameras.size()) return nullptr;
    return &doc.cameras[selected_];
}

void App::selectNumber(int number) {
    selected_ = doc.findCamera(number);
    dirty_ = true;
}

bool App::setSelectedNumber(int number) {
    Camera * cam = selectedCamera();
    if (!cam || number < 1 || number > 99) return false;
    const int existing = doc.findCamera(number);
    if (existing >= 0 && existing != selected_) return false;
    cam->number = number;
    dirty_ = true;
    return true;
}

void App::deleteSelected() {
    if (selected_ < 0 || selected_ >= (int)doc.cameras.size()) return;
    pushHistory();
    doc.cameras.erase(doc.cameras.begin() + selected_);
    selected_ = -1;
    dirty_ = true;
}

void App::setMode(Mode m) {
    if (mode_ == Mode::wall && m != Mode::wall) pendingWall_.clear();
    mode_ = m;
    dirty_ = true;
}

void App::pushHistory(const char * tag) {
    if (tag && *tag && historyTag_ == tag) return;   // one step per slider
    historyTag_ = tag ? tag : "";
    undo_.push_back({doc.cameras, doc.walls, doc.markerSize});
    if (undo_.size() > 100) undo_.erase(undo_.begin());
    redo_.clear();
}

void App::undo() {
    if (undo_.empty()) return;
    redo_.push_back({doc.cameras, doc.walls, doc.markerSize});
    const Snapshot & s = undo_.back();
    doc.cameras = s.cameras;
    doc.walls = s.walls;
    doc.markerSize = s.markerSize;
    undo_.pop_back();
    selected_ = -1;
    historyTag_.clear();
    dirty_ = true;
}

void App::redo() {
    if (redo_.empty()) return;
    undo_.push_back({doc.cameras, doc.walls, doc.markerSize});
    const Snapshot & s = redo_.back();
    doc.cameras = s.cameras;
    doc.walls = s.walls;
    doc.markerSize = s.markerSize;
    redo_.pop_back();
    selected_ = -1;
    historyTag_.clear();
    dirty_ = true;
}

void App::zoomToFit() {
    float x0, y0, x1, y1;
    doc.contentBounds(x0, y0, x1, y1);
    const float ww = std::max(x1 - x0, 1.f), wh = std::max(y1 - y0, 1.f);
    scale_ = std::min(screen_.w / ww, screen_.h / wh) * 0.94f;
    scale_ = std::min(std::max(scale_, 0.02f), 8.f);
    ox_ = (screen_.w - ww * scale_) * 0.5f - x0 * scale_;
    oy_ = (screen_.h - wh * scale_) * 0.5f - y0 * scale_;
    dirty_ = true;
}

int App::hitCamera(float wx, float wy) const {
    // Screen-size forgiveness: at least twelve screen pixels of grab.
    const float r = std::max(doc.markerSize, 12.f / scale_);
    for (int i = (int)doc.cameras.size() - 1; i >= 0; i--) {
        const Camera & c = doc.cameras[i];
        const float dx = wx - c.x, dy = wy - c.y;
        if (dx * dx + dy * dy <= r * r) return i;
    }
    return -1;
}

void App::mouseDown(float x, float y, int button) {
    const float wx = worldX(x), wy = worldY(y);
    downX_ = x;
    downY_ = y;
    lastX_ = x;
    lastY_ = y;

    if (button == 2) {
        if (mode_ == Mode::wall && pendingWall_.size() >= 4) finishWall();
        drag_ = Drag::pan;
        return;
    }
    if (button != 0) return;

    switch (mode_) {
    case Mode::select: {
        // Handles of the selected camera first - they sit on top.
        if (Camera * cam = selectedCamera()) {
            const float grab = (kHandleRadius + 5.f) / scale_;
            const float dir = deg2rad(cam->dirDeg);
            const float half = deg2rad(cam->fovDeg * 0.5f);
            const float ax = cam->x + std::cos(dir) * cam->range;
            const float ay = cam->y + std::sin(dir) * cam->range;
            const float eax = cam->x + std::cos(dir - half) * cam->range;
            const float eay = cam->y + std::sin(dir - half) * cam->range;
            const float ebx = cam->x + std::cos(dir + half) * cam->range;
            const float eby = cam->y + std::sin(dir + half) * cam->range;
            auto near = [&](float px, float py) {
                const float dx = wx - px, dy = wy - py;
                return dx * dx + dy * dy <= grab * grab;
            };
            if (near(ax, ay)) { pushHistory(); drag_ = Drag::aimHandle; return; }
            if (near(eax, eay)) { pushHistory(); drag_ = Drag::fovHandleA; return; }
            if (near(ebx, eby)) { pushHistory(); drag_ = Drag::fovHandleB; return; }
        }
        const int hit = hitCamera(wx, wy);
        if (hit >= 0) {
            pushHistory();
            selected_ = hit;
            drag_ = Drag::moveCamera;
            grabDX_ = doc.cameras[hit].x - wx;
            grabDY_ = doc.cameras[hit].y - wy;
            dirty_ = true;
            return;
        }
        drag_ = Drag::maybePan;
        return;
    }
    case Mode::addCamera: {
        const int number = doc.nextNumber();
        if (!number) return;    // all ninety-nine in use
        pushHistory();
        Camera c;
        c.number = number;
        c.x = wx;
        c.y = wy;
        c.dirDeg = 0;
        doc.cameras.push_back(c);
        selected_ = (int)doc.cameras.size() - 1;
        // Placing and aiming are one gesture: keep dragging to point it.
        drag_ = Drag::aimHandle;
        dirty_ = true;
        return;
    }
    case Mode::wall:
        pendingWall_.push_back(wx);
        pendingWall_.push_back(wy);
        dirty_ = true;
        return;
    case Mode::erase:
        eraseAt(wx, wy);
        return;
    }
}

void App::mouseMove(float x, float y) {
    const float wx = worldX(x), wy = worldY(y);
    hoverX_ = wx;
    hoverY_ = wy;
    if (mode_ == Mode::wall && !pendingWall_.empty()) dirty_ = true;

    switch (drag_) {
    case Drag::none:
        return;
    case Drag::maybePan:
        if (std::fabs(x - downX_) + std::fabs(y - downY_) > 3.f)
            drag_ = Drag::pan;
        [[fallthrough]];
    case Drag::pan:
        if (drag_ == Drag::pan) {
            ox_ += x - lastX_;
            oy_ += y - lastY_;
            dirty_ = true;
        }
        break;
    case Drag::moveCamera:
        if (Camera * cam = selectedCamera()) {
            cam->x = wx + grabDX_;
            cam->y = wy + grabDY_;
            dirty_ = true;
        }
        break;
    case Drag::aimHandle:
        if (Camera * cam = selectedCamera()) {
            const float dx = wx - cam->x, dy = wy - cam->y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > 4.f) {
                cam->dirDeg = normDeg(angleOf(dx, dy));
                cam->range = std::min(std::max(d, 24.f), 4000.f);
                dirty_ = true;
            }
        }
        break;
    case Drag::fovHandleA:
    case Drag::fovHandleB:
        if (Camera * cam = selectedCamera()) {
            const float toDeg = normDeg(angleOf(wx - cam->x, wy - cam->y));
            float diff = std::fabs(toDeg - normDeg(cam->dirDeg));
            if (diff > 180.f) diff = 360.f - diff;
            cam->fovDeg = std::min(std::max(diff * 2.f, 10.f), 359.f);
            dirty_ = true;
        }
        break;
    }
    lastX_ = x;
    lastY_ = y;
}

void App::mouseUp(float x, float y, int button) {
    // Placing a camera is one gesture; the next click almost always wants to
    // adjust something, so the tool snaps back to select by itself.
    if (button == 0 && mode_ == Mode::addCamera &&
        drag_ == Drag::aimHandle) {
        mode_ = Mode::select;
        dirty_ = true;
    }
    if (button == 0 && drag_ == Drag::maybePan &&
        std::fabs(x - downX_) + std::fabs(y - downY_) <= 3.f) {
        selected_ = -1;    // a plain click on nothing
        dirty_ = true;
    }
    drag_ = Drag::none;
}

void App::wheel(float x, float y, float deltaY) {
    const float factor = deltaY < 0 ? 1.15f : 1.f / 1.15f;
    const float next = std::min(std::max(scale_ * factor, 0.02f), 8.f);
    // Keep the world point under the cursor under the cursor.
    ox_ = x - (x - ox_) * (next / scale_);
    oy_ = y - (y - oy_) * (next / scale_);
    scale_ = next;
    dirty_ = true;
}

void App::keyDown(int code) {
    switch (code) {
    case 46:    // Delete
    case 8:     // Backspace
        if (mode_ == Mode::wall && pendingWall_.size() >= 2) {
            pendingWall_.pop_back();
            pendingWall_.pop_back();
        } else {
            deleteSelected();
        }
        dirty_ = true;
        break;
    case 13:    // Enter finishes the wall being drawn
        if (mode_ == Mode::wall) finishWall();
        break;
    case 27:    // Escape cancels the wall, else drops back to select
        if (mode_ == Mode::wall && !pendingWall_.empty()) pendingWall_.clear();
        else setMode(Mode::select);
        dirty_ = true;
        break;
    default:
        break;
    }
}

void App::finishWall() {
    if (pendingWall_.size() >= 4) {
        pushHistory();
        Wall wall;
        wall.xy = pendingWall_;
        doc.walls.push_back(std::move(wall));
    }
    pendingWall_.clear();
    dirty_ = true;
}

void App::eraseAt(float wx, float wy) {
    const int hit = hitCamera(wx, wy);
    if (hit >= 0) {
        pushHistory();
        doc.cameras.erase(doc.cameras.begin() + hit);
        if (selected_ == hit) selected_ = -1;
        else if (selected_ > hit) selected_--;
        dirty_ = true;
        return;
    }
    // The nearest wall segment within a forgiving screen distance.
    const float limit = 8.f / scale_;
    for (size_t wi = 0; wi < doc.walls.size(); wi++) {
        std::vector<float> & xy = doc.walls[wi].xy;
        for (size_t i = 0; i + 3 < xy.size(); i += 2) {
            const float x0 = xy[i], y0 = xy[i + 1];
            const float x1 = xy[i + 2], y1 = xy[i + 3];
            const float dx = x1 - x0, dy = y1 - y0;
            const float len2 = dx * dx + dy * dy;
            float t = len2 > 0 ? ((wx - x0) * dx + (wy - y0) * dy) / len2 : 0;
            t = std::min(std::max(t, 0.f), 1.f);
            const float ex = wx - (x0 + t * dx), ey = wy - (y0 + t * dy);
            if (ex * ex + ey * ey > limit * limit) continue;
            pushHistory();
            // Split the polyline around the removed segment.
            Wall tail;
            tail.xy.assign(xy.begin() + i + 2, xy.end());
            xy.resize(i + 2);
            if (xy.size() < 4) doc.walls.erase(doc.walls.begin() + wi);
            if (tail.xy.size() >= 4) doc.walls.push_back(std::move(tail));
            dirty_ = true;
            return;
        }
    }
}

/* -------------------------------------------------------------- drawing */

namespace {

void drawGrid(Canvas & out, float scale, float ox, float oy, const Theme & t) {
    out.clear(t.paper);
    const float minorStep = 20.f * scale;
    const float majorStep = 100.f * scale;
    auto lines = [&](float step, uint32_t color) {
        if (step < 5.f) return;
        const float startX = std::fmod(ox, step);
        for (float x = startX; x < out.w; x += step)
            out.line(x, 0, x, (float)out.h, 1.f, color);
        const float startY = std::fmod(oy, step);
        for (float y = startY; y < out.h; y += step)
            out.line(0, y, (float)out.w, y, 1.f, color);
    };
    lines(minorStep, t.gridMinor);
    lines(majorStep, t.gridMajor);
}

void drawCamera(Canvas & out, const Camera & c, bool isSelected,
                float markerSize, float scale, float ox, float oy,
                const Theme & t) {
    const float sx = c.x * scale + ox;
    const float sy = c.y * scale + oy;
    const float r = c.range * scale;
    const float a0 = deg2rad(c.dirDeg - c.fovDeg * 0.5f);
    const float a1 = deg2rad(c.dirDeg + c.fovDeg * 0.5f);
    const float mid = deg2rad(c.dirDeg);

    out.fillPie(sx, sy, r, a0, a1, isSelected ? t.selFanFill : t.fanFill);
    const uint32_t edgeColor = isSelected ? t.selFanEdge : t.fanEdge;
    out.arc(sx, sy, r, a0, a1, 1.6f, edgeColor);
    out.line(sx, sy, sx + std::cos(a0) * r, sy + std::sin(a0) * r, 1.2f,
             edgeColor);
    out.line(sx, sy, sx + std::cos(a1) * r, sy + std::sin(a1) * r, 1.2f,
             edgeColor);
    // The radar dressing: two faint range rings and the centre ray.
    out.arc(sx, sy, r * 0.4f, a0, a1, 1.f, t.fanRing);
    out.arc(sx, sy, r * 0.72f, a0, a1, 1.f, t.fanRing);
    out.line(sx, sy, sx + std::cos(mid) * r, sy + std::sin(mid) * r, 1.f,
             t.aimLine);

    // The numbered disc, with a soft glow ring when selected.
    const float cr = markerSize * std::max(scale, 0.45f);
    if (isSelected) out.circle(sx, sy, cr + 3.f, 6.f, t.selFanFill);
    out.fillCircle(sx, sy, cr, t.camFill);
    out.circle(sx, sy, cr, 2.2f, isSelected ? t.selRing : t.camRing);
    char label[8];
    std::snprintf(label, sizeof label, "%d", c.number);
    const int textPx = (int)std::max(cr * 1.05f, 8.f);
    const float tw = out.textWidth(label, textPx);
    out.text(sx - tw * 0.5f, sy - textPx * 0.62f, label, textPx, t.camText);
}

}   // namespace

void App::drawScene(Canvas & out, float scale, float ox, float oy,
                    bool withUi) {
    const Theme & t = theme();
    if (doc.background) {
        out.clear(t.paper);
        out.blitScaled(doc.background->pixels.data(), doc.background->w,
                       doc.background->h, ox, oy, scale);
        if (t.imageDim)
            out.fillRect(ox, oy, ox + doc.background->w * scale,
                         oy + doc.background->h * scale, t.imageDim);
    } else {
        drawGrid(out, scale, ox, oy, t);
    }

    for (const Wall & wall : doc.walls) {
        std::vector<float> pts = wall.xy;
        for (size_t i = 0; i < pts.size(); i += 2) {
            pts[i] = pts[i] * scale + ox;
            pts[i + 1] = pts[i + 1] * scale + oy;
        }
        out.polyline(pts, kWallWidth * scale * 2.6f, t.wallGlow);
        out.polyline(pts, kWallWidth * scale, t.wallCore);
    }

    for (size_t i = 0; i < doc.cameras.size(); i++)
        drawCamera(out, doc.cameras[i], withUi && (int)i == selected_,
                   doc.markerSize, scale, ox, oy, t);

    if (!withUi) return;

    // The wall being drawn, rubber-banded to the pointer.
    if (mode_ == Mode::wall && !pendingWall_.empty()) {
        std::vector<float> pts = pendingWall_;
        pts.push_back(hoverX_);
        pts.push_back(hoverY_);
        for (size_t i = 0; i < pts.size(); i += 2) {
            pts[i] = pts[i] * scale + ox;
            pts[i + 1] = pts[i + 1] * scale + oy;
        }
        out.polyline(pts, std::max(kWallWidth * scale, 1.5f), t.pendingWall);
        out.fillCircle(pts[0], pts[1], 3.5f, t.pendingWall);
    }

    // Handles.
    if (const Camera * cam = selectedCamera()) {
        const float sx = cam->x * scale + ox;
        const float sy = cam->y * scale + oy;
        const float r = cam->range * scale;
        const float dir = deg2rad(cam->dirDeg);
        const float half = deg2rad(cam->fovDeg * 0.5f);
        auto handle = [&](float a, float radius) {
            const float hx = sx + std::cos(a) * r;
            const float hy = sy + std::sin(a) * r;
            out.fillCircle(hx, hy, radius, t.handleFill);
            out.circle(hx, hy, radius, 2.f, t.handleRing);
        };
        handle(dir - half, kHandleRadius - 1.5f);
        handle(dir + half, kHandleRadius - 1.5f);
        handle(dir, kHandleRadius);
    }
}

const Canvas & App::render() {
    drawScene(screen_, scale_, ox_, oy_, true);
    dirty_ = false;
    return screen_;
}

const Canvas & App::renderExport() {
    if (doc.background) {
        export_.resize(doc.background->w, doc.background->h);
        drawScene(export_, 1.f, 0.f, 0.f, false);
    } else {
        float x0, y0, x1, y1;
        doc.contentBounds(x0, y0, x1, y1);
        const float margin = 40.f;
        int w = (int)std::ceil(x1 - x0 + margin * 2);
        int h = (int)std::ceil(y1 - y0 + margin * 2);
        w = std::min(std::max(w, 640), 4096);
        h = std::min(std::max(h, 480), 4096);
        export_.resize(w, h);
        drawScene(export_, 1.f, margin - x0, margin - y0, false);
    }
    return export_;
}

void setAppTheme(int index) { g_theme = index; }

}   // namespace cam
