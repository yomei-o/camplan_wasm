// Background image decoding: a PNG/JPEG file in, the rasterizer's RGBA out.
// The document keeps the compressed file (that is what the .json carries) and
// this is the one place that expands it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cam {

// Decodes an image file into RGBA in rgba()'s byte order.  Returns false and
// leaves px empty and w = h = 0 when the bytes are not a readable image.
bool decodeImage(const uint8_t * data, size_t size, std::vector<uint32_t> & px,
                 int & w, int & h);

}   // namespace cam
