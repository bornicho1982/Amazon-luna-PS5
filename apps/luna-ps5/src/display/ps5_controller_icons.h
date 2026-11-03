#pragma once

#include <cstdint>
#include <string>

namespace luna::display {

class DisplayManager;

enum class DualSenseBtn : uint8_t {
    Cross,       // ✕ PS5 Cross (Blue / White)
    Circle,      // ○ PS5 Circle (Coral Red / White)
    Square,      // □ PS5 Square (Magenta Pink / White)
    Triangle,    // △ PS5 Triangle (Cyan Green / White)
    L1,          // DualSense L1 bumper silhouette
    R1,          // DualSense R1 bumper silhouette
    L3,          // DualSense L3 thumbstick
    R3,          // DualSense R3 thumbstick
    Options,     // DualSense Options pill button
    Search,      // Vector magnifying glass
    Star,        // Favorite 5-point star
    Play,        // Filled play triangle
    PegiBadge    // Official regulatory PEGI badge
};

class PS5ControllerIcons {
public:
    // Draws standard circular PS5 DualSense action button (Cross, Circle, Square, Triangle, L3, R3)
    static void drawButton(DisplayManager& disp, int cx, int cy, int size, DualSenseBtn btn, bool inFocus = false);

    // Draws DualSense L3 or R3 analog thumbstick with concave cap and text
    static void drawThumbstick(DisplayManager& disp, int cx, int cy, int size, bool isR3, bool inFocus = false);

    // Draws DualSense L1 or R1 bumper with asymmetric rounded corners and crisp text
    static void drawBumper(DisplayManager& disp, int x, int y, int w, int h, bool isR1, bool inFocus = false);

    // Draws Options button (oval pill with lines)
    static void drawOptions(DisplayManager& disp, int x, int y, int w, int h, bool inFocus = false);

    // Draws UI icons: Search, Star, Play
    static void drawInterfaceIcon(DisplayManager& disp, int cx, int cy, int size, DualSenseBtn icon, uint32_t colorArgb);

    // Draws official regulatory PEGI badge (3, 7, 12, 16, 18) with color coding
    static void drawOfficialPegi(DisplayManager& disp, int x, int y, int w, int h, int pegiAge);
};

} // namespace luna::display
