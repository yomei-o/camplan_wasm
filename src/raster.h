// The drawing library: lines, circles, pies, text, image blits - all software,
// all identical between the native test build and WebAssembly.
//
// Everything anti-aliases by computing a coverage value per pixel from exact
// geometry (distance to a segment, to a circle, to the edge rays of a wedge)
// rather than by supersampling, so a shape is one pass over its bounding box
// and adjacent shapes never seam.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cam {

// One RGBA pixel in the byte order the browser's putImageData wants.
inline uint32_t rgba(int r, int g, int b, int a = 255) {
    return (uint32_t)(r & 255) | ((uint32_t)(g & 255) << 8) |
           ((uint32_t)(b & 255) << 16) | ((uint32_t)(a & 255) << 24);
}

struct Canvas {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> px;

    void resize(int width, int height);
    void clear(uint32_t color);

    // coverage is 0..1 and multiplies the colour's own alpha.
    void blend(int x, int y, uint32_t color, float coverage);

    void fillRect(float x0, float y0, float x1, float y1, uint32_t color);

    // A segment with round caps, of the given width, anti-aliased.
    void line(float x0, float y0, float x1, float y1, float width,
              uint32_t color);

    void polyline(const std::vector<float> & xy, float width, uint32_t color);

    // Anti-aliased disc and ring.
    void fillCircle(float cx, float cy, float r, uint32_t color);
    void circle(float cx, float cy, float r, float width, uint32_t color);

    // A filled pie wedge: from angle a0 to a1 (radians, screen-space,
    // y grows downward), sweeping counter-clockwise from a0 to a1 in that
    // screen sense.  The sweep may exceed pi.  Soft on every edge.
    void fillPie(float cx, float cy, float r, float a0, float a1,
                 uint32_t color);
    // The arc of the same wedge, plus nothing else.
    void arc(float cx, float cy, float r, float a0, float a1, float width,
             uint32_t color);

    // Text out of the baked font.  Returns the advance in pixels.
    float text(float x, float y, const std::string & s, int heightPx,
               uint32_t color);
    float textWidth(const std::string & s, int heightPx) const;

    // Source image (RGBA, same byte order), scaled by `scale` and placed with
    // its top-left corner at (ox, oy) on this canvas.  Bilinear.
    void blitScaled(const uint32_t * src, int sw, int sh, float ox, float oy,
                    float scale);
};

}   // namespace cam
