#include "display/ps5_controller_icons.h"
#include "display/display_manager.h"
#include <cmath>
#include <algorithm>

namespace luna::display {

namespace {

inline float distSegment(float px, float py, float ax, float ay, float bx, float by) {
    float pax = px - ax, pay = py - ay;
    float bax = bx - ax, bay = by - ay;
    float lenSq = bax * bax + bay * bay;
    if (lenSq < 1e-6f) return std::sqrt(pax * pax + pay * pay);
    float h = std::clamp((pax * bax + pay * bay) / lenSq, 0.0f, 1.0f);
    float dx = pax - bax * h, dy = pay - bay * h;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

void PS5ControllerIcons::drawButton(DisplayManager& disp, int cx, int cy, int size, DualSenseBtn btn, bool inFocus) {
    if (size <= 4) return;
    float r = static_cast<float>(size) * 0.5f;
    int minX = static_cast<int>(std::floor(static_cast<float>(cx) - r - 1.0f));
    int maxX = static_cast<int>(std::ceil(static_cast<float>(cx) + r + 1.0f));
    int minY = static_cast<int>(std::floor(static_cast<float>(cy) - r - 1.0f));
    int maxY = static_cast<int>(std::ceil(static_cast<float>(cy) + r + 1.0f));

    // Base background and halo border colors
    uint32_t bgCol = inFocus ? 0xFFFFFFFF : 0xFF141926; // PS5 dark or pure white in focus
    uint32_t borderCol = inFocus ? 0xFF0070D1 : 0xFF2C374E; // PS5 blue or subtle dark border
    uint32_t symCol = 0xFFFFFFFF;

    if (!inFocus) {
        switch (btn) {
            case DualSenseBtn::Cross:    symCol = 0xFF5897E8; break; // PS5 Blue
            case DualSenseBtn::Circle:   symCol = 0xFFFF4E50; break; // Coral Red
            case DualSenseBtn::Square:   symCol = 0xFFE56B9F; break; // Magenta Pink
            case DualSenseBtn::Triangle: symCol = 0xFF28C78E; break; // Cyan Green
            default: break;
        }
    } else {
        symCol = 0xFF070911; // Deep black on white focused button
    }

    const float thick = std::max(2.0f, size * 0.11f);
    const float crossLen = r * 0.44f;
    const float sqHalf = r * 0.40f;
    const float circR = r * 0.44f;

    // Triangle vertices
    const float triTopY = -r * 0.48f;
    const float triBotY = r * 0.36f;
    const float triHalfW = r * 0.45f;

    for (int y = minY; y <= maxY; ++y) {
        float py = static_cast<float>(y) - static_cast<float>(cy);
        for (int x = minX; x <= maxX; ++x) {
            float px = static_cast<float>(x) - static_cast<float>(cx);
            float distCenter = std::sqrt(px * px + py * py);

            if (distCenter > r + 1.0f) continue;

            // Outer button circle antialiasing
            float btnAlpha = std::clamp(r + 0.5f - distCenter, 0.0f, 1.0f);
            if (btnAlpha <= 0.0f) continue;

            uint8_t aByte = static_cast<uint8_t>(btnAlpha * 255.0f);

            // Border ring: outer 1.5 px
            if (distCenter >= r - 1.5f) {
                disp.blendPixel(x, y, borderCol, aByte);
                continue;
            }

            // Fill background
            disp.blendPixel(x, y, bgCol, aByte);

            // Calculate distance to symbol
            float symDist = 999.0f;
            switch (btn) {
                case DualSenseBtn::Cross: {
                    float d1 = distSegment(px, py, -crossLen, -crossLen, crossLen, crossLen);
                    float d2 = distSegment(px, py, -crossLen, crossLen, crossLen, -crossLen);
                    symDist = std::min(d1, d2);
                    break;
                }
                case DualSenseBtn::Circle: {
                    symDist = std::abs(distCenter - circR);
                    break;
                }
                case DualSenseBtn::Square: {
                    float d1 = distSegment(px, py, -sqHalf, -sqHalf, sqHalf, -sqHalf);
                    float d2 = distSegment(px, py, sqHalf, -sqHalf, sqHalf, sqHalf);
                    float d3 = distSegment(px, py, sqHalf, sqHalf, -sqHalf, sqHalf);
                    float d4 = distSegment(px, py, -sqHalf, sqHalf, -sqHalf, -sqHalf);
                    symDist = std::min(std::min(d1, d2), std::min(d3, d4));
                    break;
                }
                case DualSenseBtn::Triangle: {
                    float d1 = distSegment(px, py, 0.0f, triTopY, triHalfW, triBotY);
                    float d2 = distSegment(px, py, triHalfW, triBotY, -triHalfW, triBotY);
                    float d3 = distSegment(px, py, -triHalfW, triBotY, 0.0f, triTopY);
                    symDist = std::min(std::min(d1, d2), d3);
                    break;
                }
                default:
                    break;
            }

            float symCoverage = std::clamp((thick * 0.5f + 0.5f) - symDist, 0.0f, 1.0f);
            if (symCoverage > 0.0f) {
                uint8_t symA = static_cast<uint8_t>(symCoverage * 255.0f);
                disp.blendPixel(x, y, symCol, symA);
            }
        }
    }
}

void PS5ControllerIcons::drawBumper(DisplayManager& disp, int x, int y, int w, int h, bool isR1, bool inFocus) {
    if (w <= 8 || h <= 6) return;

    // Asymmetric rounded corners: top rounded (10px), bottom subtler (3px)
    uint32_t bgCol = inFocus ? 0xFFFFFFFF : 0xFF182236;
    uint32_t borderCol = inFocus ? 0xFF0070D1 : 0xFF3B4864;
    uint32_t textCol = inFocus ? 0xFF070911 : 0xFFF0F4FF;

    // Draw bumper silhouette body
    disp.drawRoundedRect(x, y, w, h, 6, bgCol);
    disp.drawRoundedBorder(x, y, w, h, 6, inFocus ? 2 : 1, borderCol);

    // Subtle curved top highlight for bumper depth
    disp.fillRect(x + 5, y + 1, w - 10, 1, inFocus ? 0xFFE2E8F0 : 0x40FFFFFF);

    // Label L1 / R1
    const char* label = isR1 ? "R1" : "L1";
    int fontSize = std::clamp(h - 8, 11, 14);
    int tw = disp.getFontRenderer().measureText(label, fontSize);
    int tx = x + (w - tw) / 2;
    int ty = y + (h - fontSize) / 2;
    disp.drawTextPx(tx, ty, fontSize, label, textCol);
}

void PS5ControllerIcons::drawOptions(DisplayManager& disp, int x, int y, int w, int h, bool inFocus) {
    if (w <= 4 || h <= 4) return;
    uint32_t bgCol = inFocus ? 0xFFFFFFFF : 0xFF1A2133;
    uint32_t borderCol = inFocus ? 0xFF0070D1 : 0xFF2C374E;
    uint32_t lineCol = inFocus ? 0xFF070911 : 0xFFE2E8F0;

    int r = std::min(w, h) / 2;
    disp.drawRoundedRect(x, y, w, h, r, bgCol);
    disp.drawRoundedBorder(x, y, w, h, r, inFocus ? 2 : 1, borderCol);

    // 3 horizontal micro-lines engraved
    int cx = x + w / 2;
    int cy = y + h / 2;
    int lw = std::max(4, w / 2);
    disp.fillRect(cx - lw / 2, cy - 4, lw, 1, lineCol);
    disp.fillRect(cx - lw / 2, cy,     lw, 1, lineCol);
    disp.fillRect(cx - lw / 2, cy + 4, lw, 1, lineCol);
}

void PS5ControllerIcons::drawInterfaceIcon(DisplayManager& disp, int cx, int cy, int size, DualSenseBtn icon, uint32_t colorArgb) {
    if (size <= 4) return;

    switch (icon) {
        case DualSenseBtn::Play: {
            disp.drawPlayTriangleIcon(cx, cy, size / 2, colorArgb);
            break;
        }
        case DualSenseBtn::Star: {
            disp.drawStarIcon(cx, cy, size / 2, colorArgb);
            break;
        }
        case DualSenseBtn::Search: {
            // Anti-aliased magnifying glass
            float r = static_cast<float>(size) * 0.35f;
            float glassCx = static_cast<float>(cx) - r * 0.25f;
            float glassCy = static_cast<float>(cy) - r * 0.25f;
            float glassR = r * 0.75f;
            float handleLen = r * 0.9f;

            int minX = cx - size / 2 - 1;
            int maxX = cx + size / 2 + 1;
            int minY = cy - size / 2 - 1;
            int maxY = cy + size / 2 + 1;

            float thick = 2.0f;
            float hx0 = glassCx + glassR * 0.707f;
            float hy0 = glassCy + glassR * 0.707f;
            float hx1 = hx0 + handleLen * 0.707f;
            float hy1 = hy0 + handleLen * 0.707f;

            for (int y = minY; y <= maxY; ++y) {
                float py = static_cast<float>(y);
                for (int x = minX; x <= maxX; ++x) {
                    float px = static_cast<float>(x);

                    // Distance to glass circle outline
                    float dCenter = std::sqrt((px - glassCx) * (px - glassCx) + (py - glassCy) * (py - glassCy));
                    float dCircle = std::abs(dCenter - glassR);

                    // Distance to handle segment
                    float dHandle = distSegment(px, py, hx0, hy0, hx1, hy1);

                    float dist = std::min(dCircle, dHandle);
                    float cov = std::clamp((thick * 0.5f + 0.5f) - dist, 0.0f, 1.0f);
                    if (cov > 0.0f) {
                        disp.blendPixel(x, y, colorArgb, static_cast<uint8_t>(cov * 255.0f));
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

void PS5ControllerIcons::drawOfficialPegi(DisplayManager& disp, int x, int y, int w, int h, int pegiAge) {
    if (w <= 8 || h <= 8) return;

    // Regulatory PEGI color code
    uint32_t bgCol = 0xFFE86314; // Default orange (12, 16)
    if (pegiAge <= 7) {
        bgCol = 0xFF55A51C; // Official Bright Green (3, 7)
    } else if (pegiAge >= 18) {
        bgCol = 0xFFD91E18; // Official Crimson Red (18)
    }

    // Outer rounded card
    disp.drawRoundedRect(x, y, w, h, 6, bgCol);
    disp.drawRoundedBorder(x, y, w, h, 6, 1, 0x40FFFFFF);

    std::string ageStr = std::to_string(pegiAge);
    int numH = h * 55 / 100;
    int tw = disp.getFontRenderer().measureText(ageStr, numH);
    disp.drawTextPx(x + (w - tw) / 2, y + 2, numH, ageStr, 0xFFFFFFFF);

    // Bottom official white strip with black "PEGI"
    int stripeH = std::max(6, h * 30 / 100);
    int stripeY = y + h - stripeH;
    disp.fillRect(x + 1, stripeY, w - 2, stripeH - 1, 0xFFFFFFFF);

    int pw = disp.getFontRenderer().measureText("PEGI", stripeH - 2);
    disp.drawTextPx(x + (w - pw) / 2, stripeY, stripeH - 2, "PEGI", 0xFF000000);
}

} // namespace luna::display
