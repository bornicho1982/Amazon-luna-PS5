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

// Aspect-fill (center crop) blit of an RGBA source into a destination rectangle
static void drawCoverRgba(DisplayManager* d, int x, int y, int w, int h,
                          const uint8_t* rgba, int srcW, int srcH) {
    if (!d || !rgba || srcW <= 0 || srcH <= 0 || w <= 0 || h <= 0) return;
    float scale = std::max(static_cast<float>(w) / srcW, static_cast<float>(h) / srcH);
    int cropW = std::clamp(static_cast<int>(w / scale), 1, srcW);
    int cropH = std::clamp(static_cast<int>(h / scale), 1, srcH);
    int cropX = (srcW - cropW) / 2;
    int cropY = (srcH - cropH) / 2;
    for (int dy = 0; dy < h; ++dy) {
        int sy = std::min(srcH - 1, cropY + (dy * cropH) / h);
        const uint8_t* row = rgba + static_cast<size_t>(sy) * srcW * 4;
        for (int dx = 0; dx < w; ++dx) {
            int sx = std::min(srcW - 1, cropX + (dx * cropW) / w);
            const uint8_t* p = row + static_cast<size_t>(sx) * 4;
            d->putPixel(x + dx, y + dy, 0xFF000000U | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2]);
        }
    }
}

// Fetch the best available decoded image for a URL (panoramic first, then 16:9)
static std::shared_ptr<const luna::util::DecodedImage> fetchWideImage(const std::string& url) {
    if (url.empty()) return nullptr;
    auto& loader = luna::util::ImageLoader::instance();
    loader.requestImage(url, luna::util::ImagePreset::PanoramicDetail);
    auto img = loader.getImage(url, luna::util::ImagePreset::PanoramicDetail);
    if (!img || !img->ready) img = loader.getImage(url, luna::util::ImagePreset::HorizontalCard16_9);
    if (img && img->ready && !img->rgba.empty() && img->width > 0 && img->height > 0) return img;
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
            // Full-cover rotating artwork (tile chosen per frame by the layout) + legibility gradients
            std::string bannerTitle(custom.text);
            bool isMultiplayerPromo = (bannerTitle.find("Jugad") != std::string::npos || bannerTitle == "home_2");
            uint32_t tint = isMultiplayerPromo ? 0xFF2E1065 : 0xFF0A0E17;
            std::shared_ptr<const luna::util::DecodedImage> img;
            if (custom.tile) {
                img = fetchWideImage(custom.tile->backdropUrl);
                if (!img && !custom.tile->screenshotUrls.empty()) img = fetchWideImage(custom.tile->screenshotUrls[0]);
            }
            if (img) {
                drawCoverRgba(display_, x, y, w, h, img->rgba.data(), img->width, img->height);
            } else {
                display_->fillRect(x, y, w, h, isMultiplayerPromo ? 0xFF4C1D95 : 0xFF121C33);
            }
            // Left-to-right fade for title legibility (strong on the left 45%)
            int fadeW = (w * 55) / 100;
            for (int fx = 0; fx < fadeW; ++fx) {
                float t = static_cast<float>(fx) / static_cast<float>(fadeW);
                float a = 1.0f - t * t * (3.0f - 2.0f * t);
                display_->blendRectUniform(x + fx, y, 1, h, tint, static_cast<uint8_t>(40 + a * 190.0f));
            }
            // Bottom fade behind the integrated card carousel
            int bfH = h / 2;
            for (int dy = 0; dy < bfH; ++dy) {
                float t = static_cast<float>(dy) / static_cast<float>(bfH);
                display_->blendRectUniform(x, y + h - bfH + dy, w, 1, 0xFF0A0E17, static_cast<uint8_t>(t * 200.0f));
            }
            display_->drawRoundedBorder(x, y, w, h, 20, custom.inFocus ? 2 : 1, custom.inFocus ? 0xFF38BDF8 : 0xFF2B3B5E);
            break;
        }
        case CustomElementType::HeroLeftBackdrop: {
            // Game artwork behind the hero text column; heavy dark veil on the left, fading out to the right
            std::shared_ptr<const luna::util::DecodedImage> img;
            if (custom.tile) {
                bool isPremium = (custom.tile->backdropUrl == "local:luna_premium" ||
                                  custom.tile->title.find("Luna Premium") != std::string::npos);
                if (isPremium) {
                    img = fetchWideImage("https://m.media-amazon.com/images/G/01/T/TC05316421/MonthlyPrime/LunaClientFGWPBannerOverride/Desktop_3532x380_esES.jpg");
                } else {
                    if (!custom.tile->screenshotUrls.empty()) img = fetchWideImage(custom.tile->screenshotUrls[0]);
                    if (!img) img = fetchWideImage(custom.tile->backdropUrl);
                }
            }
            if (img) {
                drawCoverRgba(display_, x, y, w, h, img->rgba.data(), img->width, img->height);
                for (int fx = 0; fx < w; ++fx) {
                    float t = static_cast<float>(fx) / static_cast<float>(std::max(1, w));
                    // Dark veil on the left for maximum text contrast, softly lightening toward the seam
                    display_->blendRectUniform(x + fx, y, 1, h, 0xFF0E1320, static_cast<uint8_t>(235.0f - t * 85.0f));
                }
            } else {
                display_->fillRect(x, y, w, h, 0xFF0E1320);
            }

            // Antialiased corner clipping for top-left and bottom-left (radius 22px) to screen background #0A0E17
            const int cr = 22;
            const uint32_t screenBg = 0xFF0A0E17;
            for (int cy = 0; cy < cr; ++cy) {
                for (int cx = 0; cx < cr; ++cx) {
                    float dx = static_cast<float>(cr - 1 - cx) + 0.5f;
                    float dy = static_cast<float>(cr - 1 - cy) + 0.5f;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > static_cast<float>(cr) - 1.0f) {
                        float cov = std::clamp(dist - (static_cast<float>(cr) - 1.0f), 0.0f, 1.0f);
                        int px = x + cx;
                        int py = y + cy;
                        if (cov >= 1.0f) display_->fillRect(px, py, 1, 1, screenBg);
                        else display_->blendPixel(px, py, screenBg, static_cast<uint8_t>(cov * 255.0f));
                    }

                    float bdy = static_cast<float>(cy) + 0.5f;
                    float bdist = std::sqrt(dx * dx + bdy * bdy);
                    if (bdist > static_cast<float>(cr) - 1.0f) {
                        float cov = std::clamp(bdist - (static_cast<float>(cr) - 1.0f), 0.0f, 1.0f);
                        int px = x + cx;
                        int py = y + h - cr + cy;
                        if (cov >= 1.0f) display_->fillRect(px, py, 1, 1, screenBg);
                        else display_->blendPixel(px, py, screenBg, static_cast<uint8_t>(cov * 255.0f));
                    }
                }
            }
            break;
        }
        case CustomElementType::RankNumber: {
            // Big outlined number aligned to the bottom-right of its slot (Luna/Netflix top-10 style)
            int fs = h;
            std::string num(custom.text);
            int tw = display_->getFontRenderer().measureText(num, fs);
            int tx = x + w - tw;
            int ty = y;
            uint32_t stroke = custom.inFocus ? 0xFF38BDF8 : 0xFF94A3B8;
            for (int ox = -3; ox <= 3; ++ox) {
                for (int oy = -3; oy <= 3; ++oy) {
                    if (ox * ox + oy * oy > 9 || (ox == 0 && oy == 0)) continue;
                    display_->drawTextPx(tx + ox, ty + oy, fs, num, stroke);
                }
            }
            display_->drawTextPx(tx, ty, fs, num, custom.inFocus ? 0xFF0A2A4A : 0xFF0A0E17);
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
                        static const char* kPremiumHeroes[3] = {
                            "https://m.media-amazon.com/images/G/01/T/TC05316421/MonthlyPrime/LunaClientFGWPBannerOverride/Desktop_3532x380_esES.jpg",
                            "https://m.media-amazon.com/images/G/01/T/TC05316421/Evergreen/LunaClient/ContentHighlightRows/RemotePlay_Background_Desktop_2560x695.jpg",
                            "https://m.media-amazon.com/images/G/01/T/TC05316421/Evergreen/LunaClient/ContentHighlightRows/GarfieldKart2_Background_Desktop_2560x695.jpg"
                        };
                        size_t pIdx = (custom.frameCount / 600) % 3;
                        const char* premUrl = kPremiumHeroes[pIdx];
                        luna::util::ImageLoader::instance().requestImage(premUrl, luna::util::ImagePreset::PanoramicDetail);
                        auto artImg = luna::util::ImageLoader::instance().getImage(premUrl, luna::util::ImagePreset::PanoramicDetail);
                        if (artImg && artImg->ready && !artImg->rgba.empty() && artImg->width > 0 && artImg->height > 0) {
                            drawAspectFill(artImg->rgba.data(), artImg->width, artImg->height);
                        } else {
                            // Atmospheric deep cosmic gradient base (no tilted cards!)
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
                    uint8_t alpha = static_cast<uint8_t>(std::clamp(alphaF * 150.0f, 0.0f, 150.0f));
                    if (alpha > 0) {
                        display_->blendRectUniform(x + fx, y, 1, h, 0xFF0E1320, alpha);
                    }
                }

                bool isPremiumCard = (custom.tile->backdropUrl == "local:luna_premium" ||
                                      custom.tile->title.find("Luna Premium") != std::string::npos ||
                                      custom.tile->title.find("100 juegos") != std::string::npos);

                // Stitch v2 Bottom Video Timeline HUD (only for real game trailers, never on Luna Premium)
                if (!isPremiumCard) {
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
                float prog = drewVideo ? std::clamp(player.getProgress(), 0.0f, 1.0f) : 0.0f;
                int fillW = std::max(4, static_cast<int>(static_cast<float>(barW) * prog));
                display_->drawRoundedRect(barX, barY, fillW, barH, 2, 0xFF38BDF8);

                int curSec = drewVideo ? player.getCurrentSeconds() : 0;
                int totSec = drewVideo ? std::max(1, player.getTotalSeconds()) : 0;
                char timeBuf[32];
                std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d / %02d:%02d",
                              curSec / 60, curSec % 60, totSec / 60, totSec % 60);
                display_->drawTextPx(barX + barW + 12, barY - 5, 11, timeBuf, 0xFF94A3B8);

                // Bottom-left: DualSense R3 thumbstick glyph + "Pantalla completa"
                int pillX = barX, pillY = y + h - 34;
                display_->drawRoundedRect(pillX, pillY, 178, 28, 14, 0xCC161B2A);
                display_->drawRoundedBorder(pillX, pillY, 178, 28, 14, 1, 0xFF414752);
                PS5ControllerIcons::drawThumbstick(*display_, pillX + 16, pillY + 14, 22, true, false);
                display_->drawTextPx(pillX + 36, pillY + 7, 12, "Pantalla completa", 0xFFE2E8F0);

                if (!custom.tile->videoUrl.empty() && player.isDownloading() && !drewVideo) {
                    display_->drawTextPx(x + w - 200, pillY + 7, 11, "Cargando tr\xC3\xA1iler HD...", 0xFF94A3B8);
                }
                }

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
