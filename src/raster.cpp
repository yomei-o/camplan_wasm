#include "raster.h"

#include <algorithm>
#include <cmath>

#include "font_data.h"

namespace cam {

namespace {

inline float clamp01(float v) {
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

// Coverage of a pixel centred at distance d from an edge whose inside is
// negative d: full at -0.5, empty at +0.5.
inline float edge(float d) {
    return clamp01(0.5f - d);
}

}   // namespace

void Canvas::resize(int width, int height) {
    w = width;
    h = height;
    px.assign((size_t)w * h, 0);
}

void Canvas::clear(uint32_t color) {
    std::fill(px.begin(), px.end(), color);
}

void Canvas::blend(int x, int y, uint32_t color, float coverage) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    const float a = ((color >> 24) & 255) / 255.f * clamp01(coverage);
    if (a <= 0.f) return;
    uint32_t & dst = px[(size_t)y * w + x];
    const float ia = 1.f - a;
    const int r = (int)(((color) & 255) * a + ((dst) & 255) * ia + 0.5f);
    const int g = (int)(((color >> 8) & 255) * a + ((dst >> 8) & 255) * ia + 0.5f);
    const int b = (int)(((color >> 16) & 255) * a + ((dst >> 16) & 255) * ia + 0.5f);
    const int da = (dst >> 24) & 255;
    const int oa = (int)(255 * a + da * ia + 0.5f);
    dst = rgba(r, g, b, oa);
}

void Canvas::fillRect(float x0, float y0, float x1, float y1, uint32_t color) {
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    const int ix0 = (int)std::floor(x0), ix1 = (int)std::ceil(x1);
    const int iy0 = (int)std::floor(y0), iy1 = (int)std::ceil(y1);
    for (int y = iy0; y < iy1; y++) {
        const float cy = y + 0.5f;
        const float vy = std::min(cy - y0, y1 - cy);
        for (int x = ix0; x < ix1; x++) {
            const float cx = x + 0.5f;
            const float vx = std::min(cx - x0, x1 - cx);
            blend(x, y, color, std::min(clamp01(vx + 0.5f), clamp01(vy + 0.5f)));
        }
    }
}

void Canvas::line(float x0, float y0, float x1, float y1, float width,
                  uint32_t color) {
    const float r = width * 0.5f;
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    const int ix0 = (int)std::floor(std::min(x0, x1) - r - 1);
    const int ix1 = (int)std::ceil(std::max(x0, x1) + r + 1);
    const int iy0 = (int)std::floor(std::min(y0, y1) - r - 1);
    const int iy1 = (int)std::ceil(std::max(y0, y1) + r + 1);
    for (int y = std::max(iy0, 0); y < std::min(iy1, h); y++) {
        for (int x = std::max(ix0, 0); x < std::min(ix1, w); x++) {
            const float pxc = x + 0.5f - x0, pyc = y + 0.5f - y0;
            float t = len2 > 0.f ? (pxc * dx + pyc * dy) / len2 : 0.f;
            t = clamp01(t);
            const float ex = pxc - t * dx, ey = pyc - t * dy;
            const float d = std::sqrt(ex * ex + ey * ey) - r;
            blend(x, y, color, edge(d));
        }
    }
}

void Canvas::polyline(const std::vector<float> & xy, float width,
                      uint32_t color) {
    for (size_t i = 0; i + 3 < xy.size(); i += 2)
        line(xy[i], xy[i + 1], xy[i + 2], xy[i + 3], width, color);
}

void Canvas::fillCircle(float cx, float cy, float r, uint32_t color) {
    const int ix0 = std::max((int)std::floor(cx - r - 1), 0);
    const int ix1 = std::min((int)std::ceil(cx + r + 1), w);
    const int iy0 = std::max((int)std::floor(cy - r - 1), 0);
    const int iy1 = std::min((int)std::ceil(cy + r + 1), h);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            blend(x, y, color, edge(std::sqrt(dx * dx + dy * dy) - r));
        }
}

void Canvas::circle(float cx, float cy, float r, float width, uint32_t color) {
    const float half = width * 0.5f;
    const float ro = r + half;
    const int ix0 = std::max((int)std::floor(cx - ro - 1), 0);
    const int ix1 = std::min((int)std::ceil(cx + ro + 1), w);
    const int iy0 = std::max((int)std::floor(cy - ro - 1), 0);
    const int iy1 = std::min((int)std::ceil(cy + ro + 1), h);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = std::fabs(std::sqrt(dx * dx + dy * dy) - r) - half;
            blend(x, y, color, edge(d));
        }
}

namespace {

// Angular coverage of point p (relative to the apex) inside the wedge from a0
// sweeping `sweep` radians counter-clockwise in screen space (y down means
// visually clockwise, which is irrelevant here: callers pass screen angles).
// Softness comes from the signed distances to the two edge rays.
inline float wedgeCoverage(float pxr, float pyr, float a0, float sweep) {
    if (sweep >= 6.2831853f) return 1.f;
    const float c0 = std::cos(a0), s0 = std::sin(a0);
    const float a1 = a0 + sweep;
    const float c1 = std::cos(a1), s1 = std::sin(a1);
    // Signed distance from the edge rays; inside is the counter-clockwise
    // side of edge 0 and the clockwise side of edge 1.
    const float d0 = c0 * pyr - s0 * pxr;    // >0 counter-clockwise of ray 0
    const float d1 = s1 * pxr - c1 * pyr;    // >0 clockwise of ray 1
    if (sweep <= 3.14159265f) {
        return std::min(edge(-d0), edge(-d1));
    }
    // A reflex wedge is everything except the complementary convex wedge.
    return std::max(edge(-d0), edge(-d1));
}

}   // namespace

void Canvas::fillPie(float cx, float cy, float r, float a0, float a1,
                     uint32_t color) {
    float sweep = a1 - a0;
    if (sweep <= 0.f) return;
    const int ix0 = std::max((int)std::floor(cx - r - 1), 0);
    const int ix1 = std::min((int)std::ceil(cx + r + 1), w);
    const int iy0 = std::max((int)std::floor(cy - r - 1), 0);
    const int iy1 = std::min((int)std::ceil(cy + r + 1), h);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float disc = edge(std::sqrt(dx * dx + dy * dy) - r);
            if (disc <= 0.f) continue;
            const float cov = std::min(disc, wedgeCoverage(dx, dy, a0, sweep));
            blend(x, y, color, cov);
        }
}

void Canvas::arc(float cx, float cy, float r, float a0, float a1, float width,
                 uint32_t color) {
    float sweep = a1 - a0;
    if (sweep <= 0.f) return;
    const float half = width * 0.5f;
    const float ro = r + half;
    const int ix0 = std::max((int)std::floor(cx - ro - 1), 0);
    const int ix1 = std::min((int)std::ceil(cx + ro + 1), w);
    const int iy0 = std::max((int)std::floor(cy - ro - 1), 0);
    const int iy1 = std::min((int)std::ceil(cy + ro + 1), h);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float ring =
                edge(std::fabs(std::sqrt(dx * dx + dy * dy) - r) - half);
            if (ring <= 0.f) continue;
            blend(x, y, color,
                  std::min(ring, wedgeCoverage(dx, dy, a0, sweep)));
        }
}

float Canvas::text(float x, float y, const std::string & s, int heightPx,
                   uint32_t color) {
    const FontSize * font = fontForHeight(heightPx);
    const float scale = (float)heightPx / font->height;
    float penX = x;
    for (unsigned char ch : s) {
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const FontGlyph & g = font->glyphs[ch - 0x20];
        if (g.width) {
            const uint8_t * src = font->pixels + g.offset;
            // Scaled blit of the alpha mask; bilinear on the mask.
            const float gx = penX + g.left * scale;
            const int ox0 = (int)std::floor(gx);
            const int ox1 = (int)std::ceil(gx + g.width * scale);
            const int oy0 = (int)std::floor(y);
            const int oy1 = (int)std::ceil(y + font->height * scale);
            for (int oy = oy0; oy < oy1; oy++) {
                const float sy = (oy + 0.5f - y) / scale - 0.5f;
                const int y0i = (int)std::floor(sy);
                const float fy = sy - y0i;
                for (int ox = ox0; ox < ox1; ox++) {
                    const float sx = (ox + 0.5f - gx) / scale - 0.5f;
                    const int x0i = (int)std::floor(sx);
                    const float fx = sx - x0i;
                    float acc = 0.f;
                    for (int k = 0; k < 4; k++) {
                        const int qx = x0i + (k & 1), qy = y0i + (k >> 1);
                        if (qx < 0 || qy < 0 || qx >= g.width ||
                            qy >= font->height) continue;
                        const float wgt = ((k & 1) ? fx : 1.f - fx) *
                                          ((k >> 1) ? fy : 1.f - fy);
                        acc += src[qy * g.width + qx] * wgt;
                    }
                    blend(ox, oy, color, acc / 255.f);
                }
            }
        }
        penX += g.advance * scale;
    }
    return penX - x;
}

float Canvas::textWidth(const std::string & s, int heightPx) const {
    const FontSize * font = fontForHeight(heightPx);
    const float scale = (float)heightPx / font->height;
    float sum = 0.f;
    for (unsigned char ch : s) {
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        sum += font->glyphs[ch - 0x20].advance * scale;
    }
    return sum;
}

void Canvas::blitScaled(const uint32_t * src, int sw, int sh, float ox,
                        float oy, float scale) {
    if (!src || sw <= 0 || sh <= 0 || scale <= 0.f) return;
    const int x0 = std::max((int)std::floor(ox), 0);
    const int y0 = std::max((int)std::floor(oy), 0);
    const int x1 = std::min((int)std::ceil(ox + sw * scale), w);
    const int y1 = std::min((int)std::ceil(oy + sh * scale), h);
    for (int y = y0; y < y1; y++) {
        const float sy = (y + 0.5f - oy) / scale - 0.5f;
        const int yi = (int)std::floor(sy);
        const float fy = sy - yi;
        for (int x = x0; x < x1; x++) {
            const float sx = (x + 0.5f - ox) / scale - 0.5f;
            const int xi = (int)std::floor(sx);
            const float fx = sx - xi;
            float r = 0, g = 0, b = 0, a = 0, wsum = 0;
            for (int k = 0; k < 4; k++) {
                const int qx = xi + (k & 1), qy = yi + (k >> 1);
                if (qx < 0 || qy < 0 || qx >= sw || qy >= sh) continue;
                const float wgt = ((k & 1) ? fx : 1.f - fx) *
                                  ((k >> 1) ? fy : 1.f - fy);
                const uint32_t p = src[(size_t)qy * sw + qx];
                r += (p & 255) * wgt;
                g += ((p >> 8) & 255) * wgt;
                b += ((p >> 16) & 255) * wgt;
                a += ((p >> 24) & 255) * wgt;
                wsum += wgt;
            }
            if (wsum <= 0.f) continue;
            blend(x, y, rgba((int)(r / wsum), (int)(g / wsum),
                             (int)(b / wsum), 255),
                  a / wsum / 255.f);
        }
    }
}

}   // namespace cam
