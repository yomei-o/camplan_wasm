// The document: a background (an image, or graph paper), walls, and cameras.
// Everything lives in world coordinates, which are the background image's own
// pixels when there is an image and plain pixels on the paper when not.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cam {

// One free-form string pair on a camera.  The editor never reads these; they
// are for the page (a stream URL, a room name, an asset tag).
struct Prop {
    std::string key;     // never empty, unique within its camera
    std::string value;

    bool operator==(const Prop & other) const = default;
};

struct Camera {
    int number = 1;          // 1..99
    float x = 0, y = 0;      // world position
    float dirDeg = 0;        // view direction, degrees, 0 = +x, CCW positive
    float fovDeg = 90;       // full opening angle
    float range = 200;       // world pixels
    std::vector<Prop> props;   // in the order they were first set

    int findProp(const std::string & key) const;   // -1 when there is none
    // Adds or replaces.  false when the key is empty.  Control characters are
    // dropped from both strings - these end up in JSON and in a text input.
    bool setProp(const std::string & key, const std::string & value);
    // Keeps the row where it was.  false when `to` is empty or already used.
    bool renameProp(const std::string & from, const std::string & to);
    bool removeProp(const std::string & key);
    // {"key":"value",...} - what the page receives on a click.
    std::string propsJson() const;
};

struct Wall {
    std::vector<float> xy;   // x0,y0,x1,y1,... a polyline
};

struct Background {
    std::string name;                 // the dropped file's name
    std::vector<uint8_t> fileBytes;   // the original PNG/JPEG, for saving
    std::vector<uint32_t> pixels;     // decoded RGBA, from the bytes above
    int w = 0, h = 0;
};

struct Document {
    std::optional<Background> background;
    std::vector<Wall> walls;
    std::vector<Camera> cameras;
    float markerSize = 16;   // the numbered disc's radius, world pixels

    // Decodes the file and makes it the background.  Returns false when the
    // bytes are not a readable image, and then the old background stays.
    bool setBackgroundFile(const uint8_t * data, size_t size,
                           const std::string & name);

    int findCamera(int number) const;
    // The lowest free number in 1..99, or 0 when all are taken.
    int nextNumber() const;

    // The content's world bounding box (for exporting the paper mode).
    void contentBounds(float & x0, float & y0, float & x1, float & y1) const;

    std::string toJson() const;
    // Replaces this document.  Returns false when the text is not a document.
    bool fromJson(const std::string & text);
};

// Strips what a property may not hold (control characters).  setProp and
// renameProp run both of their strings through this.
std::string plainText(const std::string & s);

std::string base64Encode(const uint8_t * data, size_t size);
std::vector<uint8_t> base64Decode(const std::string & text);

}   // namespace cam
