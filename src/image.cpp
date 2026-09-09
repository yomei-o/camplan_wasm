#include "image.h"

#include <cstring>

// The project's only third-party code.  stb_image (public domain) reads the
// formats a user will actually drop on the editor; the rest are compiled out
// to keep the wasm small.  Nothing but this file includes it.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO     // every caller already has the bytes in memory
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#define STBI_ASSERT(x) ((void)0)
#include "stb_image.h"

namespace cam {

// Enough for any floor plan, and it keeps a wild header out of a doomed
// allocation: emscripten aborts the module on a failed malloc rather than
// handing back null, so an absurd size has to be refused before decoding.
constexpr int64_t kMaxPixels = 32 << 20;   // 33.5 Mpx = 128 MB of RGBA

bool decodeImage(const uint8_t * data, size_t size, std::vector<uint32_t> & px,
                 int & w, int & h) {
    px.clear();
    w = h = 0;
    if (!data || size == 0 || size > 0x7fffffff) return false;
    int comp = 0;
    if (!stbi_info_from_memory(data, (int)size, &w, &h, &comp) || w <= 0 ||
        h <= 0 || (int64_t)w * (int64_t)h > kMaxPixels) {
        w = h = 0;
        return false;
    }
    stbi_uc * rgba = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!rgba) {
        w = h = 0;
        return false;
    }
    // stb hands back R,G,B,A bytes, which is exactly what rgba() packs.
    px.resize((size_t)w * (size_t)h);
    std::memcpy(px.data(), rgba, px.size() * sizeof(uint32_t));
    stbi_image_free(rgba);
    return true;
}

}   // namespace cam
