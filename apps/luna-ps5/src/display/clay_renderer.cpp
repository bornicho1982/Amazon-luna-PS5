#define CLAY_IMPLEMENTATION
#include "display/clay.h"
#include "display/clay_renderer.h"
#include "display/display_manager.h"
#include "display/ps5_controller_icons.h"
#include "util/image_loader.h"
#include "media/mp4_preview_player.h"
#include <cstring>
#include <algorithm>

namespace luna::display {

namespace {

constexpr size_t kMaxCustomElements = 512;
constexpr size_t kMaxImageElements = 256;
constexpr size_t kMaxDecodedImageRefs = 1024;
constexpr size_t kStringPoolCapacity = 64 * 1024;

alignas(64) static uint8_t g_clayArenaBuffer[8 * 1024 * 1024];

alignas(16) static ClayCustomElement g_customPool[kMaxCustomElements];
static size_t g_customCount = 0;

alignas(16) static ClayImageElement g_imagePool[kMaxImageElements];
static size_t g_imageCount = 0;

static std::shared_ptr<const luna::util::DecodedImage> g_frameDecodedImages[kMaxDecodedImageRefs];
static size_t g_frameDecodedImageCount = 0;

static char g_clayStringPool[kStringPoolCapacity];
static size_t g_clayStringPoolIdx = 0;

static Clay_Dimensions measureTextCallback(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) {
    auto* renderer = static_cast<ClayRenderer*>(userData);
    if (!renderer || !renderer->getDisplay()) {
        return Clay_Dimensions{ 0.0f, 0.0f };
    }
    std::string s(text.chars, text.length);
    int fontSize = config ? config->fontSize : 16;
    int width = renderer->getDisplay()->getFontRenderer().measureText(s, fontSize);
    int height = fontSize;
    return Clay_Dimensions{ static_cast<float>(width), static_cast<float>(height) };
}

static void errorHandler(Clay_ErrorData errorData) {
    (void)errorData;
}

} // namespace

ClayRenderer& ClayRenderer::instance() {
    static ClayRenderer s_instance;
    return s_instance;
}

Clay_String ClayRenderer::toClayString(const std::string& str) {
    return toClayString(str.c_str());
}

Clay_String ClayRenderer::toClayString(const char* str) {
    if (!str) return Clay_String{ .isStaticallyAllocated = false, .length = 0, .chars = nullptr };
    size_t len = std::strlen(str);
    if (len == 0) return Clay_String{ .isStaticallyAllocated = false, .length = 0, .chars = nullptr };

    if (g_clayStringPoolIdx + len + 1 >= kStringPoolCapacity) {
        g_clayStringPoolIdx = 0; // Wrap around if exhausted in extreme cases
    }

    char* dst = &g_clayStringPool[g_clayStringPoolIdx];
    std::memcpy(dst, str, len);
    dst[len] = '\0';
    g_clayStringPoolIdx += len + 1;

    return Clay_String{ .isStaticallyAllocated = false, .length = static_cast<int32_t>(len), .chars = dst };
}

void* ClayRenderer::allocDecodedImage(const std::shared_ptr<const luna::util::DecodedImage>& img) {
    if (!img || !img->ready || img->rgba.empty() || img->width <= 0 || img->height <= 0) {
        return nullptr;
    }
    if (g_frameDecodedImageCount < kMaxDecodedImageRefs) {
        g_frameDecodedImages[g_frameDecodedImageCount] = img;
        const void* ptr = g_frameDecodedImages[g_frameDecodedImageCount].get();
        g_frameDecodedImageCount++;
        return const_cast<void*>(ptr);
    }
    return const_cast<void*>(static_cast<const void*>(img.get()));
}

bool ClayRenderer::init(DisplayManager* display, int width, int height) {
    display_ = display;
    if (isReady_) return true;

    arena_ = Clay_CreateArenaWithCapacityAndMemory(sizeof(g_clayArenaBuffer), g_clayArenaBuffer);
    Clay_Initialize(arena_, Clay_Dimensions{ static_cast<float>(width), static_cast<float>(height) }, Clay_ErrorHandler{ errorHandler, nullptr });
    Clay_SetMeasureTextFunction(measureTextCallback, this);

    isReady_ = true;
    return true;
}

void ClayRenderer::beginFrame() {
    g_customCount = 0;
    g_imageCount = 0;
    g_clayStringPoolIdx = 0;
    for (size_t i = 0; i < g_frameDecodedImageCount; ++i) {
        g_frameDecodedImages[i].reset();
    }
    g_frameDecodedImageCount = 0;
    Clay_BeginLayout();
}

ClayCustomElement* ClayRenderer::allocCustom(CustomElementType type,
                                            const std::string& text,
                                            uint32_t color,
                                            const GameTile* tile,
                                            uint64_t frameCount,
                                            bool inFocus) {
    if (g_customCount < kMaxCustomElements) {
        auto& elem = g_customPool[g_customCount++];
        elem.type = type;
        size_t len = std::min(text.length(), sizeof(elem.text) - 1);
        std::memcpy(elem.text, text.data(), len);
        elem.text[len] = '\0';
        elem.color = color;
        elem.tile = tile;
        elem.frameCount = frameCount;
        elem.buttonType = DualSenseBtn::Cross;
        elem.inFocus = inFocus;
        elem.pegiAge = 12;
        return &elem;
    }
    return nullptr;
}

ClayCustomElement* ClayRenderer::allocControllerButton(DualSenseBtn btn, bool inFocus) {
    if (g_customCount < kMaxCustomElements) {
        auto& elem = g_customPool[g_customCount++];
        elem.type = CustomElementType::ControllerButton;
        elem.buttonType = btn;
        elem.inFocus = inFocus;
        return &elem;
    }
    return nullptr;
}

ClayCustomElement* ClayRenderer::allocBumper(bool isR1, bool inFocus) {
    if (g_customCount < kMaxCustomElements) {
        auto& elem = g_customPool[g_customCount++];
        elem.type = CustomElementType::Bumper;
        elem.buttonType = isR1 ? DualSenseBtn::R1 : DualSenseBtn::L1;
        elem.inFocus = inFocus;
        return &elem;
    }
    return nullptr;
}

ClayCustomElement* ClayRenderer::allocOptionsButton(bool inFocus) {
    if (g_customCount < kMaxCustomElements) {
        auto& elem = g_customPool[g_customCount++];
        elem.type = CustomElementType::OptionsButton;
        elem.inFocus = inFocus;
        return &elem;
    }
    return nullptr;
}

ClayCustomElement* ClayRenderer::allocInterfaceIcon(DualSenseBtn icon, uint32_t color) {
    if (g_customCount < kMaxCustomElements) {
        auto& elem = g_customPool[g_customCount++];
        elem.type = CustomElementType::InterfaceIcon;
        elem.buttonType = icon;
        elem.color = color;
        return &elem;
    }
    return nullptr;
}

ClayCustomElement* ClayRenderer::allocPegiBadge(int pegiAge) {
    if (g_customCount < kMaxCustomElements) {
        auto& elem = g_customPool[g_customCount++];
        elem.type = CustomElementType::PegiBadge;
        elem.pegiAge = pegiAge;
        return &elem;
    }
    return nullptr;
}

ClayImageElement* ClayRenderer::allocImage(const uint8_t* rgba, int width, int height) {
    if (g_imageCount < kMaxImageElements) {
        auto& elem = g_imagePool[g_imageCount++];
        elem.rgba = rgba;
        elem.width = width;
        elem.height = height;
        return &elem;
    }
    return nullptr;
}

void ClayRenderer::renderCustomElement(const ClayCustomElement& custom, const Clay_BoundingBox& bb) {
    if (!display_) return;
    int x = static_cast<int>(bb.x);
    int y = static_cast<int>(bb.y);
    int w = static_cast<int>(bb.width);
    int h = static_cast<int>(bb.height);

    switch (custom.type) {
        case CustomElementType::ControllerButton: {
            PS5ControllerIcons::drawButton(*display_, x + w / 2, y + h / 2, std::min(w, h), custom.buttonType, custom.inFocus);
            break;
        }
        case CustomElementType::Bumper: {
            PS5ControllerIcons::drawBumper(*display_, x, y, w, h, custom.buttonType == DualSenseBtn::R1, custom.inFocus);
            break;
        }
        case CustomElementType::OptionsButton: {
            PS5ControllerIcons::drawOptions(*display_, x, y, w, h, custom.inFocus);
            break;
        }
        case CustomElementType::InterfaceIcon: {
            PS5ControllerIcons::drawInterfaceIcon(*display_, x + w / 2, y + h / 2, std::min(w, h), custom.buttonType, custom.color);
            break;
        }
        case CustomElementType::PegiBadge: {
            PS5ControllerIcons::drawOfficialPegi(*display_, x, y, w, h, custom.pegiAge > 0 ? custom.pegiAge : 12);
            break;
        }
        case CustomElementType::GoodiesBadge: {
            display_->drawRoundedRect(x, y, w, h, 6, 0xFF0070D1); // PS5 Blue
            int tw = display_->getFontRenderer().measureText("GN", 14);
            display_->drawTextPx(x + (w - tw) / 2, y + (h - 14) / 2, 14, "GN", 0xFFFFFFFF);
            break;
        }
        case CustomElementType::CategoryIcon: {
            display_->drawCategoryIcon(x + w / 2, y + h / 2, std::min(w, h) / 2, custom.text, custom.color);
            break;
        }
        case CustomElementType::Mp4PreviewPlayer: {
            if (custom.tile) {
                auto& player = luna::media::Mp4PreviewPlayer::instance();
                bool drewVideo = false;
                if (!custom.tile->videoUrl.empty() && player.hasFrame() && player.currentUrl() == custom.tile->videoUrl) {
                    const uint32_t* vPix = player.getFrameRgba();
                    if (vPix) {
                        display_->drawPreScaledImage(x, y, reinterpret_cast<const uint8_t*>(vPix), w, h, player.getWidth(), player.getHeight());
                        drewVideo = true;
                    }
                }
                if (!drewVideo) {
                    std::string artUrl = custom.tile->backdropUrl.empty() ? custom.tile->coverUrl : custom.tile->backdropUrl;
                    if (!artUrl.empty()) {
                        luna::util::ImageLoader::instance().requestImage(artUrl, luna::util::ImagePreset::HorizontalCard16_9);
                        auto img = luna::util::ImageLoader::instance().getImage(artUrl, luna::util::ImagePreset::HorizontalCard16_9);
                        if (!img || !img->ready) {
                            img = luna::util::ImageLoader::instance().getImage(custom.tile->coverUrl, luna::util::ImagePreset::VerticalPoster);
                        }
                        if (img && img->ready && !img->rgba.empty() && img->width > 0 && img->height > 0) {
                            display_->drawPreScaledImage(x, y, img->rgba.data(), w, h, img->width, img->height);
                        } else {
                            display_->fillRect(x, y, w, h, 0xFF0E1320);
                        }
                    } else {
                        display_->fillRect(x, y, w, h, 0xFF0E1320);
                    }
                    // Center translucent play badge when static backdrop is shown
                    int cx = x + w / 2, cy = y + h / 2;
                    display_->drawRoundedRect(cx - 20, cy - 20, 40, 40, 20, 0xB0070911);
                    display_->drawRoundedBorder(cx - 20, cy - 20, 40, 40, 20, 1, 0xFF38BDF8);
                    display_->drawPlayTriangleIcon(cx + 2, cy, 8, 0xFFFFFFFF);
                }
                // Bottom-right speaker/mute badge in Pop-out Preview Card
                int spkX = x + w - 34, spkY = y + h - 34;
                display_->drawRoundedRect(spkX, spkY, 26, 26, 13, 0xCC0A0E17);
                display_->drawRoundedBorder(spkX, spkY, 26, 26, 13, 1, 0xFF414752);
                display_->drawPlayTriangleIcon(spkX + 11, spkY + 13, 5, 0xFFFFFFFF);
                if (player.isMuted()) {
                    display_->drawTextPx(spkX + 15, spkY + 7, 10, "x", 0xFFEF4444);
                }
            }
            break;
        }
        case CustomElementType::PartnerBannerBg: {
            std::string bannerTitle(custom.text);
            bool isMultiplayerPromo = (bannerTitle.find("Jugad") != std::string::npos || bannerTitle == "home_2");
            if (isMultiplayerPromo) {
                // Rich purple/violet gradient (#4C1D95 -> #6D28D9 -> #9333EA) matching Stitch Banner 2
                for (int dy = 0; dy < h; ++dy) {
                    for (int dx = 0; dx < w; ++dx) {
                        float tx = static_cast<float>(dx) / static_cast<float>(std::max(1, w));
                        int r = static_cast<int>(76.0f + tx * 71.0f);
                        int g = static_cast<int>(29.0f + tx * 22.0f);
                        int b = static_cast<int>(149.0f + tx * 85.0f);
                        uint32_t col = 0xFF000000U | (static_cast<uint32_t>(std::clamp(r, 0, 255)) << 16) |
                                                     (static_cast<uint32_t>(std::clamp(g, 0, 255)) << 8)  |
                                                      static_cast<uint32_t>(std::clamp(b, 0, 255));
                        display_->putPixel(x + dx, y + dy, col);
                    }
                }
                // 2x2 decorative photo mosaic on top-right of the banner
                int mosX = x + w - 500;
                int mosY = y + 18;
                int cellW = 220, cellH = 82, gap = 10;
                const uint32_t kCellCols[4] = { 0xFF2E1065, 0xFF3B0764, 0xFF4C1D95, 0xFF581C87 };
                const char* kCellLabels[4] = { "CO-OP ONLINE", "PARTY 4P", "LUNA COUCH", "GAME NIGHT" };
                for (int idx = 0; idx < 4; ++idx) {
                    int colIdx = idx % 2;
                    int rowIdx = idx / 2;
                    int cx = mosX + colIdx * (cellW + gap);
                    int cy = mosY + rowIdx * (cellH + gap);
                    display_->drawRoundedRect(cx, cy, cellW, cellH, 10, kCellCols[idx]);
                    display_->drawRoundedBorder(cx, cy, cellW, cellH, 10, 1, 0xFFA855F7);
                    display_->drawRoundedRect(cx + 16, cy + (cellH - 24) / 2, 32, 24, 6, 0xFF0070D1);
                    display_->drawTextPx(cx + 22, cy + (cellH - 12) / 2, 12, "GN", 0xFFFFFFFF);
                    display_->drawTextPx(cx + 58, cy + (cellH - 14) / 2, 14, kCellLabels[idx], 0xFFF3E8FF);
                }
            } else {
                // Dark midnight blue gradient (#0C1220 -> #121C33 -> #1C2C4F) + panoramic artwork on right for Banner 1
                for (int dy = 0; dy < h; ++dy) {
                    for (int dx = 0; dx < w; ++dx) {
                        float tx = static_cast<float>(dx) / static_cast<float>(std::max(1, w));
                        int r = static_cast<int>(12.0f + tx * 16.0f);
                        int g = static_cast<int>(18.0f + tx * 26.0f);
                        int b = static_cast<int>(32.0f + tx * 47.0f);
                        uint32_t col = 0xFF000000U | (static_cast<uint32_t>(std::clamp(r, 0, 255)) << 16) |
                                                     (static_cast<uint32_t>(std::clamp(g, 0, 255)) << 8)  |
                                                      static_cast<uint32_t>(std::clamp(b, 0, 255));
                        display_->putPixel(x + dx, y + dy, col);
                    }
                }
                if (custom.tile && !custom.tile->backdropUrl.empty()) {
                    luna::util::ImageLoader::instance().requestImage(custom.tile->backdropUrl, luna::util::ImagePreset::PanoramicDetail);
                    auto artImg = luna::util::ImageLoader::instance().getImage(custom.tile->backdropUrl, luna::util::ImagePreset::PanoramicDetail);
                    if (artImg && artImg->ready && !artImg->rgba.empty() && artImg->width > 0 && artImg->height > 0) {
                        int artW = (w * 60) / 100;
                        int artX = x + w - artW;
                        display_->drawPreScaledImage(artX, y, artImg->rgba.data(), artW, h, artImg->width, artImg->height);
                        // Smooth left fade into #0E1628
                        int fadeW = 240;
                        for (int fx = 0; fx < fadeW; ++fx) {
                            float t = static_cast<float>(fx) / static_cast<float>(fadeW);
                            uint8_t alpha = static_cast<uint8_t>(std::clamp((1.0f - t) * 255.0f, 0.0f, 255.0f));
                            display_->blendRectUniform(artX + fx, y, 1, h, 0xFF10192E, alpha);
                        }
                    }
                }
            }
            display_->drawRoundedBorder(x, y, w, h, 20, 1, isMultiplayerPromo ? 0xFF9333EA : 0xFF2B3B5E);
            break;
        }
        case CustomElementType::SpotlightArt: {
            if (custom.tile) {
                // Helper to draw an image or video frame with Aspect-Fill (center crop) instead of stretching
                auto drawAspectFill = [&](const uint8_t* rgba, int srcW, int srcH) {
                    if (!rgba || srcW <= 0 || srcH <= 0 || w <= 0 || h <= 0) return;
                    if (srcW < srcH) {
                        display_->fillRect(x, y, w, h, 0xFF0E1320);
                        int fitH = h;
                        int fitW = (fitH * srcW) / srcH;
                        int px = x + (w - fitW) / 2 + 60;
                        display_->drawPreScaledImage(px, y, rgba, fitW, fitH, srcW, srcH);
                        return;
                    }
                    float scaleX = static_cast<float>(w) / static_cast<float>(srcW);
                    float scaleY = static_cast<float>(h) / static_cast<float>(srcH);
                    float scale = std::max(scaleX, scaleY);
                    int cropW = std::clamp(static_cast<int>(static_cast<float>(w) / scale), 1, srcW);
                    int cropH = std::clamp(static_cast<int>(static_cast<float>(h) / scale), 1, srcH);
                    int cropX = (srcW - cropW) / 2;
                    int cropY = (srcH - cropH) / 2;

                    for (int dy = 0; dy < h; ++dy) {
                        int sy = cropY + (dy * cropH) / h;
                        if (sy >= srcH) sy = srcH - 1;
                        const uint8_t* rowSrc = rgba + (static_cast<size_t>(sy) * static_cast<size_t>(srcW) * 4);
                        for (int dx = 0; dx < w; ++dx) {
                            int sx = cropX + (dx * cropW) / w;
                            if (sx >= srcW) sx = srcW - 1;
                            const uint8_t* p = rowSrc + static_cast<size_t>(sx) * 4;
                            uint32_t argb = 0xFF000000U | (static_cast<uint32_t>(p[0]) << 16) |
                                                          (static_cast<uint32_t>(p[1]) << 8)  |
                                                           static_cast<uint32_t>(p[2]);
                            display_->putPixel(x + dx, y + dy, argb);
                        }
                    }
                };

                auto& player = luna::media::Mp4PreviewPlayer::instance();
                bool drewVideo = false;
                if (!custom.tile->videoUrl.empty() && player.hasFrame() && player.currentUrl() == custom.tile->videoUrl) {
                    const uint32_t* vPix = player.getFrameRgba();
                    if (vPix) {
                        drawAspectFill(reinterpret_cast<const uint8_t*>(vPix), player.getWidth(), player.getHeight());
                        drewVideo = true;
                    }
                }
                if (!drewVideo) {
                    std::string artUrl = custom.tile->backdropUrl.empty() ? custom.tile->coverUrl : custom.tile->backdropUrl;
                    bool isPremiumHero = (artUrl == "local:luna_premium" ||
                                          custom.tile->backdropUrl == "local:luna_premium" ||
                                          custom.tile->title.find("Luna Premium") != std::string::npos ||
                                          custom.tile->title.find("100 juegos") != std::string::npos);

                    if (isPremiumHero) {
                        for (int dy = 0; dy < h; ++dy) {
                            float ty = static_cast<float>(dy) / static_cast<float>(std::max(1, h));
                            for (int dx = 0; dx < w; ++dx) {
                                float tx = static_cast<float>(dx) / static_cast<float>(std::max(1, w));
                                float glow1 = std::max(0.0f, 1.0f - std::hypot((tx - 0.62f) * 1.4f, (ty - 0.38f) * 1.1f));
                                float glow2 = std::max(0.0f, 1.0f - std::hypot((tx - 0.35f) * 1.3f, (ty - 0.72f) * 1.2f));
                                int r = std::clamp(static_cast<int>(18.0f + glow1 * 96.0f + glow2 * 22.0f), 0, 255);
                                int g = std::clamp(static_cast<int>(16.0f + glow1 * 32.0f + glow2 * 98.0f), 0, 255);
                                int b = std::clamp(static_cast<int>(42.0f + glow1 * 175.0f + glow2 * 165.0f), 0, 255);
                                uint32_t col = 0xFF000000U | (static_cast<uint32_t>(r) << 16) |
                                                             (static_cast<uint32_t>(g) << 8)  |
                                                              static_cast<uint32_t>(b);
                                display_->putPixel(x + dx, y + dy, col);
                            }
                        }

                        struct ShowcaseTile { int ox, oy, cw, ch; uint32_t bg, accent; const char* tag; const char* name; };
                        const ShowcaseTile kTiles[3] = {
                            { 160, 74,  210, 286, 0xFF1A1438, 0xFF8B5CF6, "ACCION",    "1080p 60 FPS" },
                            { 400, 48,  230, 314, 0xFF0F2442, 0xFF38BDF8, "LUNA+",     "+100 JUEGOS"  },
                            { 660, 82,  210, 286, 0xFF172836, 0xFF10B981, "CO-OP",     "SIN DESCARGAS"}
                        };
                        for (int ti = 0; ti < 3; ++ti) {
                            const auto& st = kTiles[ti];
                            int tx = x + st.ox, ty = y + st.oy;
                            display_->blendRectUniform(tx + 8, ty + 10, st.cw, st.ch, 0xFF05070E, 140);
                            display_->drawRoundedRect(tx, ty, st.cw, st.ch, 16, st.bg);
                            display_->drawRoundedBorder(tx, ty, st.cw, st.ch, 16, 2, st.accent);
                            display_->drawRoundedRect(tx + 18, ty + 18, 86, 24, 6, st.accent);
                            display_->drawTextPx(tx + 28, ty + 23, 11, st.tag, 0xFF070911);
                            display_->drawLunaLogoIcon(tx + (st.cw - 56) / 2, ty + st.ch / 2 - 36, 56);
                            int nw = display_->getFontRenderer().measureText(st.name, 16);
                            display_->drawTextPx(tx + (st.cw - nw) / 2, ty + st.ch - 48, 16, st.name, 0xFFFFFFFF);
                        }
                    } else if (!artUrl.empty()) {
                        luna::util::ImageLoader::instance().requestImage(artUrl, luna::util::ImagePreset::PanoramicDetail);
                        auto artImg = luna::util::ImageLoader::instance().getImage(artUrl, luna::util::ImagePreset::PanoramicDetail);
                        if (!artImg || !artImg->ready) {
                            artImg = luna::util::ImageLoader::instance().getImage(artUrl, luna::util::ImagePreset::HorizontalCard16_9);
                        }
                        if (!artImg || !artImg->ready) {
                            if (!custom.tile->coverUrl.empty()) {
                                artImg = luna::util::ImageLoader::instance().getImage(custom.tile->coverUrl, luna::util::ImagePreset::VerticalPoster);
                            }
                        }
                        if (artImg && artImg->ready && !artImg->rgba.empty() && artImg->width > 0 && artImg->height > 0) {
                            drawAspectFill(artImg->rgba.data(), artImg->width, artImg->height);
                        } else {
                            display_->fillRect(x, y, w, h, 0xFF0E1320);
                        }
                    } else {
                        display_->fillRect(x, y, w, h, 0xFF0E1320);
                    }
                }

                // Clean 160px Hermite cubic alpha gradient blend on the left edge into PS5 Dark Crystal (#0E1320)
                int fadeW = std::min(160, w);
                for (int fx = 0; fx < fadeW; ++fx) {
                    float t = static_cast<float>(fx) / static_cast<float>(fadeW);
                    float alphaF = 1.0f - (t * t * (3.0f - 2.0f * t));
                    uint8_t alpha = static_cast<uint8_t>(std::clamp(alphaF * 255.0f, 0.0f, 255.0f));
                    if (alpha > 0) {
                        display_->blendRectUniform(x + fx, y, 1, h, 0xFF0E1320, alpha);
                    }
                }

                // Stitch v2 Bottom Video Timeline & Controls HUD (NO top frame/header!)
                int hudH = 68;
                int hudY = y + h - hudH;
                for (int dy = 0; dy < hudH; ++dy) {
                    float t = static_cast<float>(dy) / static_cast<float>(hudH);
                    uint8_t alpha = static_cast<uint8_t>(std::clamp(t * 210.0f, 0.0f, 210.0f));
                    display_->blendRectUniform(x + 40, hudY + dy, w - 40, 1, 0xFF070911, alpha);
                }

                // Timeline progress bar + MM:SS / MM:SS
                int barX = x + 64;
                int barY = y + h - 46;
                int barW = w - 180;
                int barH = 4;
                display_->drawRoundedRect(barX, barY, barW, barH, 2, 0xFF303444);
                float prog = drewVideo ? std::clamp(player.getProgress(), 0.05f, 1.0f) : 0.35f;
                int fillW = std::max(8, static_cast<int>(static_cast<float>(barW) * prog));
                display_->drawRoundedRect(barX, barY, fillW, barH, 2, 0xFF38BDF8);

                int curSec = drewVideo ? player.getCurrentSeconds() : 42;
                int totSec = drewVideo ? std::max(1, player.getTotalSeconds()) : 135;
                char timeBuf[32];
                std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d / %02d:%02d",
                              curSec / 60, curSec % 60, totSec / 60, totSec % 60);
                display_->drawTextPx(barX + barW + 12, barY - 5, 11, timeBuf, 0xFF94A3B8);

                // Bottom-left pill: (R3) Pantalla Completa
                int pillX = barX, pillY = y + h - 32;
                display_->drawRoundedRect(pillX, pillY, 174, 24, 6, 0xCC161B2A);
                display_->drawRoundedBorder(pillX, pillY, 174, 24, 6, 1, 0xFF414752);
                display_->drawRoundedRect(pillX + 6, pillY + 4, 26, 16, 4, 0xFF2A2D3D);
                display_->drawTextPx(pillX + 11, pillY + 6, 10, "R3", 0xFF38BDF8);
                display_->drawTextPx(pillX + 38, pillY + 6, 11, "Pantalla Completa", 0xFFE2E8F0);

                // Bottom-right speaker + Audio en Pantalla Completa
                int audX = x + w - 216, audY = pillY + 5;
                display_->drawPlayTriangleIcon(audX + 4, audY + 7, 5, 0xFF38BDF8);
                display_->drawTextPx(audX + 14, audY, 11,
                                     (!custom.tile->videoUrl.empty() && player.isDownloading() && !drewVideo)
                                         ? "Cargando tr\xC3\xA1iler HD..."
                                         : "Audio en Pantalla Completa",
                                     0xFFCBD5E1);

                // Clip top-right and bottom-right corners (radius 22px) to screen background #0A0E17
                const int cornerR = 22;
                const uint32_t screenBg = 0xFF0A0E17;
                int trCx = x + w - cornerR;
                int brCx = x + w - cornerR;
                int brCy = y + h - cornerR;

                for (int cy = 0; cy < cornerR; ++cy) {
                    for (int cx = 0; cx < cornerR; ++cx) {
                        float dx = static_cast<float>(cx) + 0.5f;
                        float dy = static_cast<float>(cornerR - 1 - cy) + 0.5f;
                        float dist = std::sqrt(dx * dx + dy * dy);
                        if (dist > static_cast<float>(cornerR) - 1.0f) {
                            float cov = std::clamp(dist - (static_cast<float>(cornerR) - 1.0f), 0.0f, 1.0f);
                            int px = trCx + cx;
                            int py = y + cy;
                            if (cov >= 1.0f) {
                                display_->fillRect(px, py, 1, 1, screenBg);
                            } else {
                                display_->blendPixel(px, py, screenBg, static_cast<uint8_t>(cov * 255.0f));
                            }
                        }

                        float bdy = static_cast<float>(cy) + 0.5f;
                        float bdist = std::sqrt(dx * dx + bdy * bdy);
                        if (bdist > static_cast<float>(cornerR) - 1.0f) {
                            float cov = std::clamp(bdist - (static_cast<float>(cornerR) - 1.0f), 0.0f, 1.0f);
                            int px = brCx + cx;
                            int py = brCy + cy;
                            if (cov >= 1.0f) {
                                display_->fillRect(px, py, 1, 1, screenBg);
                            } else {
                                display_->blendPixel(px, py, screenBg, static_cast<uint8_t>(cov * 255.0f));
                            }
                        }
                    }
                }

                // Re-draw outer card border for SpotlightStage
                uint32_t borderColor = custom.inFocus ? 0xFF38BDF8 : 0xFF222C42;
                int borderThick = custom.inFocus ? 2 : 1;
                display_->drawRoundedBorder(x - 880, y, 1824, h, 22, borderThick, borderColor);
            }
            break;
        }
        case CustomElementType::LunaLogoIcon: {
            display_->drawLunaLogoIcon(x, y, w);
            break;
        }
        case CustomElementType::StarIcon: {
            display_->drawStarIcon(x + w / 2, y + h / 2, w / 2, custom.color);
            break;
        }
        case CustomElementType::DualSenseHint: {
            display_->drawRoundedRect(x, y, w, h, 6, 0xFF182236);
            display_->drawRoundedBorder(x, y, w, h, 6, 1, 0xFF2C374E);
            int tw = display_->getFontRenderer().measureText(custom.text, 14);
            display_->drawTextPx(x + (w - tw) / 2, y + (h - 14) / 2, 14, custom.text, custom.color);
            break;
        }
    }
}

void ClayRenderer::renderCommands(Clay_RenderCommandArray commands) {
    if (!display_) return;

    for (int i = 0; i < commands.length; ++i) {
        Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(&commands, i);
        if (!cmd) continue;
        const auto& bb = cmd->boundingBox;
        int x = static_cast<int>(bb.x);
        int y = static_cast<int>(bb.y);
        int w = static_cast<int>(bb.width);
        int h = static_cast<int>(bb.height);

        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                uint32_t argb = clayColorToArgb(cmd->renderData.rectangle.backgroundColor);
                uint8_t alpha = static_cast<uint8_t>((argb >> 24) & 0xFF);
                if (alpha == 0) break; // Transparent
                int r = static_cast<int>(cmd->renderData.rectangle.cornerRadius.topLeft);
                if (alpha < 255 && r == 0) {
                    display_->blendRectUniform(x, y, w, h, argb | 0xFF000000U, alpha);
                } else if (r > 0) {
                    display_->drawRoundedRect(x, y, w, h, r, argb);
                } else {
                    display_->fillRect(x, y, w, h, argb);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                uint32_t argb = clayColorToArgb(cmd->renderData.border.color);
                if ((argb >> 24) == 0) break; // Transparent
                int r = static_cast<int>(cmd->renderData.border.cornerRadius.topLeft);
                int thickness = static_cast<int>(cmd->renderData.border.width.top);
                if (thickness <= 0) thickness = static_cast<int>(cmd->renderData.border.width.left);
                if (thickness <= 0) thickness = 1;
                display_->drawRoundedBorder(x, y, w, h, r, thickness, argb);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                uint32_t argb = clayColorToArgb(cmd->renderData.text.textColor);
                std::string txt(cmd->renderData.text.stringContents.chars, cmd->renderData.text.stringContents.length);
                display_->drawTextPx(x, y, cmd->renderData.text.fontSize, txt, argb);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                if (cmd->renderData.image.imageData) {
                    const auto* decImg = static_cast<const luna::util::DecodedImage*>(cmd->renderData.image.imageData);
                    if (decImg && decImg->ready && !decImg->rgba.empty() && decImg->width > 0 && decImg->height > 0) {
                        display_->drawPreScaledImage(x, y, decImg->rgba.data(), w, h, decImg->width, decImg->height);
                    } else {
                        const auto* imgElem = static_cast<const ClayImageElement*>(cmd->renderData.image.imageData);
                        if (imgElem && imgElem->rgba && imgElem->width > 0 && imgElem->height > 0) {
                            display_->drawPreScaledImage(x, y, imgElem->rgba, w, h, imgElem->width, imgElem->height);
                        }
                    }
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                display_->pushScissor(x, y, w, h);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                display_->popScissor();
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                if (cmd->renderData.custom.customData) {
                    const auto* custom = static_cast<const ClayCustomElement*>(cmd->renderData.custom.customData);
                    renderCustomElement(*custom, bb);
                }
                break;
            }
            default:
                break;
        }
    }
}

} // namespace luna::display
