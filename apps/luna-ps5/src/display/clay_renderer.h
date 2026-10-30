#pragma once

#include "display/clay.h"
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace luna::display {

class DisplayManager;
struct GameTile;

enum class CustomElementType : uint8_t {
    PegiBadge,
    GoodiesBadge,       // [GN] Luna+ / GameNight badge
    CategoryIcon,       // 15 square category vector icons
    Mp4PreviewPlayer,   // Miniature MP4 video preview player
    LunaLogoIcon,
    StarIcon,
    DualSenseHint
};

struct ClayCustomElement {
    CustomElementType type = CustomElementType::PegiBadge;
    std::string text;
    uint32_t color = 0xFFFFFFFF;
    const GameTile* tile = nullptr;
    uint64_t frameCount = 0;
};

struct ClayImageElement {
    const uint8_t* rgba = nullptr;
    int width = 0;
    int height = 0;
};

class ClayRenderer {
public:
    static ClayRenderer& instance();

    bool init(DisplayManager* display, int width = 1920, int height = 1080);
    DisplayManager* getDisplay() const { return display_; }

    void beginFrame();
    void renderCommands(Clay_RenderCommandArray commands);

    // Helpers to create Clay_String from strings
    static inline Clay_String toClayString(const std::string& str) {
        return Clay_String{
            .isStaticallyAllocated = false,
            .length = static_cast<int32_t>(str.length()),
            .chars = str.c_str()
        };
    }

    static inline Clay_String toClayString(const char* str) {
        if (!str) return Clay_String{ .isStaticallyAllocated = true, .length = 0, .chars = "" };
        int32_t len = 0;
        while (str[len]) len++;
        return Clay_String{
            .isStaticallyAllocated = false,
            .length = len,
            .chars = str
        };
    }

    ClayCustomElement* allocCustom(CustomElementType type,
                                  const std::string& text = "",
                                  uint32_t color = 0xFFFFFFFF,
                                  const GameTile* tile = nullptr,
                                  uint64_t frameCount = 0);

    ClayImageElement* allocImage(const uint8_t* rgba, int width, int height);

private:
    ClayRenderer() = default;
    DisplayManager* display_ = nullptr;
    std::vector<char> arenaMemory_;
    Clay_Arena arena_{};
    bool isReady_ = false;

    std::vector<ClayCustomElement> customPool_;
    std::vector<ClayImageElement> imagePool_;

    void renderCustomElement(const ClayCustomElement& custom, const Clay_BoundingBox& bb);
};

inline uint32_t clayColorToArgb(Clay_Color c) {
    uint8_t a = static_cast<uint8_t>(std::clamp(c.a, 0.0f, 255.0f));
    uint8_t r = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 255.0f));
    uint8_t g = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 255.0f));
    uint8_t b = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 255.0f));
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
            static_cast<uint32_t>(b);
}

inline Clay_Color argbToClayColor(uint32_t argb) {
    float a = static_cast<float>((argb >> 24) & 0xFF);
    float r = static_cast<float>((argb >> 16) & 0xFF);
    float g = static_cast<float>((argb >> 8) & 0xFF);
    float b = static_cast<float>(argb & 0xFF);
    return Clay_Color{ r, g, b, a };
}

} // namespace luna::display
