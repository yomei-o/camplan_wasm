// The document: a background (an image, or graph paper), walls, and cameras.
// Everything lives in world coordinates, which are the background image's own
// pixels when there is an image and plain pixels on the paper when not.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cam {

struct Camera {
    int number = 1;          // 1..99
    float x = 0, y = 0;      // world position
    float dirDeg = 0;        // view direction, degrees, 0 = +x, CCW positive
    float fovDeg = 90;       // full opening angle
    float range = 200;       // world pixels
};

struct Wall {
    std::vector<float> xy;   // x0,y0,x1,y1,... a polyline
};

struct Background {
    std::string name;                 // the dropped file's name
    std::vector<uint8_t> fileBytes;   // the original file, for saving
    std::vector<uint32_t> pixels;     // decoded RGBA
    int w = 0, h = 0;
};

struct Document {
    std::optional<Background> background;
    std::vector<Wall> walls;
    std::vector<Camera> cameras;

    int findCamera(int number) const;
    // The lowest free number in 1..99, or 0 when all are taken.
    int nextNumber() const;

    // The content's world bounding box (for exporting the paper mode).
    void contentBounds(float & x0, float & y0, float & x1, float & y1) const;

    std::string toJson() const;
    // Replaces this document.  Returns false when the text is not a document.
    bool fromJson(const std::string & text);
};

std::string base64Encode(const uint8_t * data, size_t size);
std::vector<uint8_t> base64Decode(const std::string & text);

}   // namespace cam
