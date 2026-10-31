#pragma once

#include "display/clay.h"
#include "display/ps5_controller_icons.h"
#include <string>
#include <memory>
#include <cstdint>
#include <algorithm>

namespace luna::util {
struct DecodedImage;
}

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
    DualSenseHint,
    SpotlightArt,       // Cinematic 16:9 stage backdrop with seamless left-fade
    ControllerButton,   // DualSense Cross, Circle, Square, Triangle
    Bumper,             // DualSense L1 or R1 bumper
    OptionsButton,      // DualSense Options pill button
    InterfaceIcon       // Search, Star, Play
};

struct ClayCustomElement {
    CustomElementType type = CustomElementType::PegiBadge;
    char text[64]{};
    uint32_t color = 0xFFFFFFFF;
    const GameTile* tile = nullptr;
    uint64_t frameCount = 0;
    DualSenseBtn buttonType = DualSenseBtn::Cross;
    bool inFocus = false;
    int pegiAge = 12;
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

    // Helpers to create Clay_String from strings with frame-lifetime pool (zero heap allocation)
    static Clay_String toClayString(const std::string& str);
    static Clay_String toClayString(const char* str);

    // Keeps refcounted image pointer alive for the duration of the current frame
    void* allocDecodedImage(const std::shared_ptr<const luna::util::DecodedImage>& img);

    ClayCustomElement* allocCustom(CustomElementType type,
                                  const std::string& text = "",
                                  uint32_t color = 0xFFFFFFFF,
                                  const GameTile* tile = nullptr,
                                  uint64_t frameCount = 0);

    // Specialized DualSense / PS5 UI element allocators
    ClayCustomElement* allocControllerButton(DualSenseBtn btn, bool inFocus = false);
    ClayCustomElement* allocBumper(bool isR1, bool inFocus = false);
    ClayCustomElement* allocOptionsButton(bool inFocus = false);
    ClayCustomElement* allocInterfaceIcon(DualSenseBtn icon, uint32_t color = 0xFFFFFFFF);
    ClayCustomElement* allocPegiBadge(int pegiAge);

    ClayImageElement* allocImage(const uint8_t* rgba, int width, int height);

private:
    ClayRenderer() = default;
    DisplayManager* display_ = nullptr;
    Clay_Arena arena_{};
    bool isReady_ = false;

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
