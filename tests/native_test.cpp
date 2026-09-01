// The native build: the same document, the same renderer, a BMP on disk.
// This is what makes the drawing testable without a browser, and what a
// future pixel-identity check between native and WebAssembly compares.
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/app.h"

using namespace cam;

static bool writeBmp(const char * path, const Canvas & c) {
    FILE * f = std::fopen(path, "wb");
    if (!f) return false;
    const int stride = c.w * 3;
    const int pad = (4 - stride % 4) % 4;
    const uint32_t dataSize = (uint32_t)((stride + pad) * c.h);
    uint8_t header[54] = {'B', 'M'};
    const uint32_t fileSize = 54 + dataSize;
    std::memcpy(header + 2, &fileSize, 4);
    const uint32_t offset = 54, infoSize = 40;
    std::memcpy(header + 10, &offset, 4);
    std::memcpy(header + 14, &infoSize, 4);
    std::memcpy(header + 18, &c.w, 4);
    std::memcpy(header + 22, &c.h, 4);
    const uint16_t planes = 1, bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &dataSize, 4);
    std::fwrite(header, 1, 54, f);
    std::string row(stride + pad, '\0');
    for (int y = c.h - 1; y >= 0; y--) {
        for (int x = 0; x < c.w; x++) {
            const uint32_t p = c.px[(size_t)y * c.w + x];
            row[x * 3 + 0] = (char)((p >> 16) & 255);   // B
            row[x * 3 + 1] = (char)((p >> 8) & 255);    // G
            row[x * 3 + 2] = (char)(p & 255);           // R
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

int main(int argc, char ** argv) {
    App app;
    app.init(1280, 800);

    // A little scene: three rooms of walls, four cameras of varied aim.
    Wall outline;
    outline.xy = {100, 100, 1100, 100, 1100, 640, 100, 640, 100, 100};
    app.doc.walls.push_back(outline);
    Wall split;
    split.xy = {560, 100, 560, 400, 900, 400, 900, 640};
    app.doc.walls.push_back(split);

    auto add = [&](int no, float x, float y, float dir, float fov,
                   float range) {
        Camera c;
        c.number = no;
        c.x = x;
        c.y = y;
        c.dirDeg = dir;
        c.fovDeg = fov;
        c.range = range;
        app.doc.cameras.push_back(c);
    };
    add(1, 130, 130, 45, 78, 300);
    add(2, 1070, 130, 135, 92, 340);
    add(12, 590, 430, 20, 60, 260);
    add(99, 130, 610, -35, 110, 280);

    app.selectNumber(12);
    app.zoomToFit();

    const bool light = argc > 1 && std::strcmp(argv[1], "light") == 0;
    setAppTheme(light ? 1 : 0);

    if (!writeBmp("view.bmp", app.render())) return 1;
    if (!writeBmp("export.bmp", app.renderExport())) return 1;

    // The document must survive its own round trip.
    const std::string saved = app.doc.toJson();
    Document reloaded;
    if (!reloaded.fromJson(saved)) {
        std::fprintf(stderr, "round trip: parse failed\n");
        return 1;
    }
    if (reloaded.cameras.size() != app.doc.cameras.size() ||
        reloaded.walls.size() != app.doc.walls.size() ||
        reloaded.toJson() != saved) {
        std::fprintf(stderr, "round trip: documents differ\n");
        return 1;
    }
    std::printf("view.bmp and export.bmp written, round trip ok\n");
    return 0;
}
