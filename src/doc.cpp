#include "doc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cam {

int Document::findCamera(int number) const {
    for (size_t i = 0; i < cameras.size(); i++)
        if (cameras[i].number == number) return (int)i;
    return -1;
}

int Document::nextNumber() const {
    for (int n = 1; n <= 99; n++)
        if (findCamera(n) < 0) return n;
    return 0;
}

void Document::contentBounds(float & x0, float & y0, float & x1,
                             float & y1) const {
    bool any = false;
    x0 = y0 = 1e9f;
    x1 = y1 = -1e9f;
    auto grow = [&](float x, float y, float pad) {
        x0 = std::min(x0, x - pad);
        y0 = std::min(y0, y - pad);
        x1 = std::max(x1, x + pad);
        y1 = std::max(y1, y + pad);
        any = true;
    };
    if (background) {
        grow(0, 0, 0);
        grow((float)background->w, (float)background->h, 0);
    }
    for (const Wall & wall : walls)
        for (size_t i = 0; i + 1 < wall.xy.size(); i += 2)
            grow(wall.xy[i], wall.xy[i + 1], 8);
    for (const Camera & camera : cameras)
        grow(camera.x, camera.y, camera.range + 40);
    if (!any) {
        x0 = y0 = 0;
        x1 = 800;
        y1 = 600;
    }
}

/* ---------------------------------------------------------------- base64 */

namespace {
const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const uint8_t * data, size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        uint32_t v = data[i] << 16;
        if (i + 1 < size) v |= data[i + 1] << 8;
        if (i + 2 < size) v |= data[i + 2];
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(i + 1 < size ? kB64[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? kB64[v & 63] : '=');
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string & text) {
    int8_t table[256];
    std::fill(table, table + 256, (int8_t)-1);
    for (int i = 0; i < 64; i++) table[(uint8_t)kB64[i]] = (int8_t)i;
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : text) {
        if (table[c] < 0) continue;   // whitespace and padding
        acc = (acc << 6) | (uint32_t)table[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(acc >> bits));
        }
    }
    return out;
}

/* ------------------------------------------------------------------ JSON */

namespace {

void appendNumber(std::string & out, float v) {
    char buf[32];
    // Positions carry no meaning below a hundredth of a pixel.
    std::snprintf(buf, sizeof buf, "%.2f", v);
    // Trim the trailing zeros so the file diffs cleanly.
    std::string s = buf;
    while (s.find('.') != std::string::npos &&
           (s.back() == '0' || s.back() == '.')) {
        const bool dot = s.back() == '.';
        s.pop_back();
        if (dot) break;
    }
    out += s;
}

void appendString(std::string & out, const std::string & s) {
    out.push_back('"');
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        if ((unsigned char)c >= 0x20) out.push_back(c);
    }
    out.push_back('"');
}

// A parser for exactly what toJson writes, tolerant of whitespace and of
// key order.  It is not a general JSON library and does not want to be.
struct Parser {
    const char * p;
    const char * end;

    explicit Parser(const std::string & s)
        : p(s.data()), end(s.data() + s.size()) {}

    void ws() { while (p < end && (uint8_t)*p <= ' ') p++; }
    bool eat(char c) {
        ws();
        if (p < end && *p == c) { p++; return true; }
        return false;
    }
    bool literal(const char * s) {
        ws();
        const char * q = p;
        while (*s && q < end && *q == *s) { q++; s++; }
        if (*s) return false;
        p = q;
        return true;
    }
    bool number(float & out) {
        ws();
        char * next = nullptr;
        out = std::strtof(p, &next);
        if (next == p) return false;
        p = next;
        return true;
    }
    bool string(std::string & out) {
        if (!eat('"')) return false;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) p++;
            out.push_back(*p++);
        }
        return eat('"');
    }
    // Skips any one value, so unknown keys never break an older build.
    bool skipValue() {
        ws();
        if (p >= end) return false;
        if (*p == '"') { std::string s; return string(s); }
        if (*p == '{' || *p == '[') {
            const char open = *p++, close = open == '{' ? '}' : ']';
            int depth = 1;
            bool quoted = false;
            while (p < end && depth) {
                if (quoted) {
                    if (*p == '\\') p++;
                    else if (*p == '"') quoted = false;
                } else if (*p == '"') quoted = true;
                else if (*p == open) depth++;
                else if (*p == close) depth--;
                p++;
            }
            return depth == 0;
        }
        if (literal("true") || literal("false") || literal("null")) return true;
        float f;
        return number(f);
    }
};

}   // namespace

std::string Document::toJson() const {
    std::string out = "{\"app\":\"camplan\",\"version\":1";
    out += ",\"marker\":";
    appendNumber(out, markerSize);
    if (background) {
        out += ",\"background\":{\"name\":";
        appendString(out, background->name);
        out += ",\"data\":\"";
        out += base64Encode(background->fileBytes.data(),
                            background->fileBytes.size());
        out += "\"}";
    }
    out += ",\"walls\":[";
    for (size_t i = 0; i < walls.size(); i++) {
        out += i ? ",[" : "[";
        for (size_t k = 0; k < walls[i].xy.size(); k++) {
            if (k) out.push_back(',');
            appendNumber(out, walls[i].xy[k]);
        }
        out.push_back(']');
    }
    out += "],\"cameras\":[";
    for (size_t i = 0; i < cameras.size(); i++) {
        const Camera & c = cameras[i];
        out += i ? ",{" : "{";
        out += "\"no\":";
        appendNumber(out, (float)c.number);
        out += ",\"x\":";
        appendNumber(out, c.x);
        out += ",\"y\":";
        appendNumber(out, c.y);
        out += ",\"dir\":";
        appendNumber(out, c.dirDeg);
        out += ",\"fov\":";
        appendNumber(out, c.fovDeg);
        out += ",\"range\":";
        appendNumber(out, c.range);
        out.push_back('}');
    }
    out += "]}";
    return out;
}

bool Document::fromJson(const std::string & text) {
    Parser in(text);
    if (!in.eat('{')) return false;
    Document loaded;
    bool sawApp = false;
    for (;;) {
        std::string key;
        if (!in.string(key)) break;
        if (!in.eat(':')) return false;
        if (key == "marker") {
            float v;
            if (!in.number(v)) return false;
            loaded.markerSize = std::min(std::max(v, 6.f), 80.f);
        } else if (key == "app") {
            std::string v;
            if (!in.string(v) || v != "camplan") return false;
            sawApp = true;
        } else if (key == "background") {
            if (!in.eat('{')) return false;
            Background bg;
            std::string data;
            for (;;) {
                std::string k;
                if (!in.string(k)) break;
                if (!in.eat(':')) return false;
                if (k == "name") in.string(bg.name);
                else if (k == "data") in.string(data);
                else in.skipValue();
                if (!in.eat(',')) break;
            }
            if (!in.eat('}')) return false;
            bg.fileBytes = base64Decode(data);
            // The pixels arrive later, decoded by the host.
            loaded.background = std::move(bg);
        } else if (key == "walls") {
            if (!in.eat('[')) return false;
            if (!in.eat(']')) {
                do {
                    if (!in.eat('[')) return false;
                    Wall wall;
                    if (!in.eat(']')) {
                        do {
                            float v;
                            if (!in.number(v)) return false;
                            wall.xy.push_back(v);
                        } while (in.eat(','));
                        if (!in.eat(']')) return false;
                    }
                    loaded.walls.push_back(std::move(wall));
                } while (in.eat(','));
                if (!in.eat(']')) return false;
            }
        } else if (key == "cameras") {
            if (!in.eat('[')) return false;
            if (!in.eat(']')) {
                do {
                    if (!in.eat('{')) return false;
                    Camera c;
                    for (;;) {
                        std::string k;
                        if (!in.string(k)) break;
                        if (!in.eat(':')) return false;
                        float v = 0;
                        if (k == "no" && in.number(v)) c.number = (int)v;
                        else if (k == "x" && in.number(v)) c.x = v;
                        else if (k == "y" && in.number(v)) c.y = v;
                        else if (k == "dir" && in.number(v)) c.dirDeg = v;
                        else if (k == "fov" && in.number(v)) c.fovDeg = v;
                        else if (k == "range" && in.number(v)) c.range = v;
                        else if (k != "no") in.skipValue();
                        if (!in.eat(',')) break;
                    }
                    if (!in.eat('}')) return false;
                    loaded.cameras.push_back(c);
                } while (in.eat(','));
                if (!in.eat(']')) return false;
            }
        } else {
            if (!in.skipValue()) return false;
        }
        if (!in.eat(',')) break;
    }
    if (!in.eat('}') || !sawApp) return false;
    *this = std::move(loaded);
    return true;
}

}   // namespace cam
