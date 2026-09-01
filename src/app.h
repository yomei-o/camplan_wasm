// The editor: one view transform, four tools, and a renderer that draws the
// same picture on screen and into the exported PNG.
#pragma once

#include "doc.h"
#include "raster.h"

namespace cam {

enum class Mode { select = 0, addCamera = 1, wall = 2, erase = 3 };

class App {
public:
    Document doc;

    void init(int screenW, int screenH);
    void resizeScreen(int screenW, int screenH);

    // Input, in screen pixels.  Button: 0 left, 2 right.
    void mouseDown(float x, float y, int button);
    void mouseMove(float x, float y);
    void mouseUp(float x, float y, int button);
    void wheel(float x, float y, float deltaY);
    void keyDown(int code);

    void setMode(Mode m);
    Mode mode() const { return mode_; }

    // Selection, exposed for the host's panel.  Index into doc.cameras or -1.
    int selected() const { return selected_; }
    void selectNumber(int number);
    Camera * selectedCamera();
    bool setSelectedNumber(int number);   // false when taken or out of 1..99
    void deleteSelected();

    // Places the whole document in view.
    void zoomToFit();

    // True when something changed since the last render() - the host only
    // repaints then.
    bool dirty() const { return dirty_; }
    const Canvas & render();

    // The finished drawing at document resolution, no handles, no grid cursor.
    const Canvas & renderExport();

    void markDirty() { dirty_ = true; }

private:
    enum class Drag {
        none, pan, maybePan, moveCamera, aimHandle, fovHandleA, fovHandleB
    };

    Canvas screen_;
    Canvas export_;
    Mode mode_ = Mode::select;
    int selected_ = -1;
    Drag drag_ = Drag::none;
    float scale_ = 1.f;
    float ox_ = 0.f, oy_ = 0.f;           // world -> screen offset
    float grabDX_ = 0.f, grabDY_ = 0.f;   // world grab offset for moves
    float lastX_ = 0.f, lastY_ = 0.f;     // screen, for panning
    float downX_ = 0.f, downY_ = 0.f;     // screen, to tell a click from a pan
    std::vector<float> pendingWall_;      // world, the polyline being drawn
    float hoverX_ = 0.f, hoverY_ = 0.f;   // world, for the wall preview
    bool dirty_ = true;

    float worldX(float sx) const { return (sx - ox_) / scale_; }
    float worldY(float sy) const { return (sy - oy_) / scale_; }

    int hitCamera(float wx, float wy) const;
    void finishWall();
    void eraseAt(float wx, float wy);
    void drawScene(Canvas & out, float scale, float ox, float oy,
                   bool withUi);
};

// 0 = dark (the control-room look), 1 = light paper.
void setAppTheme(int index);

}   // namespace cam
