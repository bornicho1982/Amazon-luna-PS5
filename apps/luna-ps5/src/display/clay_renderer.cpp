#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-function"
#define CLAY_IMPLEMENTATION
#include "display/clay.h"
#pragma clang diagnostic pop

#include "display/clay_renderer.h"
#include "display/display_manager.h"
#include "media/mp4_preview_player.h"
#include "util/image_loader.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace luna::display {

namespace {

// Static string pool in .bss for frame-lifetime text rendering (avoids dangling pointers)
static constexpr size_t kStringPoolCapacity = 1024;
static constexpr size_t kMaxStringLength = 512;
alignas(64) static char g_clayStringPool[kStringPoolCapacity][kMaxStringLength];
static size_t g_clayStringPoolIdx = 0;

// Static arena buffer in .bss (8 MB, 0 bytes in malloc heap)
alignas(64) static uint8_t g_clayArenaBuffer[8 * 1024 * 1024];

// Static element pools in .bss (0 bytes in malloc heap, zero allocations per frame)
static constexpr size_t kMaxCustomElements = 512;
static constexpr size_t kMaxImageElements = 512;
alignas(64) static ClayCustomElement g_customPool[kMaxCustomElements];
static size_t g_customCount = 0;
alignas(64) static ClayImageElement g_imagePool[kMaxImageElements];
static size_t g_imageCount = 0;

// Frame-scoped shared_ptr retention for DecodedImages
static constexpr size_t kMaxFrameImages = 1024;
static std::shared_ptr<const luna::util::DecodedImage> g_frameDecodedImages[kMaxFrameImages];
static size_t g_frameDecodedImageCount = 0;

static Clay_Dimensions measureTextForClay(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) {
    auto* renderer = static_cast<ClayRenderer*>(userData);
    if (!renderer || !renderer->getDisplay() || text.length <= 0 || !text.chars) {
        return Clay_Dimensions{ 0.0f, static_cast<float>(config ? config->fontSize : 16) };
    }
    std::string str(text.chars, text.length);
    int fontSize = config ? config->fontSize : 16;
    int width = renderer->getDisplay()->getFontRenderer().measureText(str, fontSize);
    return Clay_Dimensions{ static_cast<float>(width), static_cast<float>(fontSize) };
}

} // namespace

ClayRenderer& ClayRenderer::instance() {
    static ClayRenderer s_inst;
    return s_inst;
}

Clay_String ClayRenderer::toClayString(const std::string& str) {
    if (str.empty()) {
        return Clay_String{ .isStaticallyAllocated = true, .length = 0, .chars = "" };
    }
    size_t idx = (g_clayStringPoolIdx++) % kStringPoolCapacity;
    size_t len = std::min(str.length(), kMaxStringLength - 1);
    std::memcpy(g_clayStringPool[idx], str.data(), len);
    g_clayStringPool[idx][len] = '\0';
    return Clay_String{
        .isStaticallyAllocated = false,
        .length = static_cast<int32_t>(len),
        .chars = g_clayStringPool[idx]
    };
}

Clay_String ClayRenderer::toClayString(const char* str) {
    if (!str || !*str) {
        return Clay_String{ .isStaticallyAllocated = true, .length = 0, .chars = "" };
    }
    size_t idx = (g_clayStringPoolIdx++) % kStringPoolCapacity;
    size_t len = 0;
    while (str[len] && len < kMaxStringLength - 1) {
        g_clayStringPool[idx][len] = str[len];
        len++;
    }
    g_clayStringPool[idx][len] = '\0';
    return Clay_String{
        .isStaticallyAllocated = false,
        .length = static_cast<int32_t>(len),
        .chars = g_clayStringPool[idx]
    };
}

void* ClayRenderer::allocDecodedImage(const std::shared_ptr<const luna::util::DecodedImage>& img) {
    if (!img) return nullptr;
    if (g_frameDecodedImageCount < kMaxFrameImages) {
        g_frameDecodedImages[g_frameDecodedImageCount++] = img;
    }
    return const_cast<void*>(static_cast<const void*>(img.get()));
}

bool ClayRenderer::init(DisplayManager* display, int width, int height) {
    display_ = display;
    if (isReady_) return true;

    arena_ = Clay_CreateArenaWithCapacityAndMemory(sizeof(g_clayArenaBuffer), g_clayArenaBuffer);
    Clay_Dimensions dims = { static_cast<float>(width), static_cast<float>(height) };
    Clay_ErrorHandler errorHandler = {
        [](Clay_ErrorData error) {
            std::printf("[Clay] Error %d: %.*s\n", (int)error.errorType,
                        (int)error.errorText.length, error.errorText.chars);
        },
        nullptr
    };
    Clay_Initialize(arena_, dims, errorHandler);
    Clay_SetMeasureTextFunction(measureTextForClay, this);
    isReady_ = true;
    std::printf("[Clay] UI Flexbox Engine initialized (8 MB .bss arena, %dx%d)\n", width, height);
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
                                            uint64_t frameCount) {
    if (g_customCount < kMaxCustomElements) {
        auto& elem = g_customPool[g_customCount++];
        elem.type = type;
        size_t len = std::min(text.length(), sizeof(elem.text) - 1);
        std::memcpy(elem.text, text.data(), len);
        elem.text[len] = '\0';
        elem.color = color;
        elem.tile = tile;
        elem.frameCount = frameCount;
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
        case CustomElementType::PegiBadge: {
            display_->drawPegiBadge(x, y, w, h, custom.text[0] ? custom.text : "PEGI 12");
            break;
        }
        case CustomElementType::GoodiesBadge: {
            display_->drawRoundedRect(x, y, w, h, 6, 0xFF7C3AED);
            int tw = display_->getFontRenderer().measureText("GN", 14);
            display_->drawTextPx(x + (w - tw) / 2, y + (h - 14) / 2, 14, "GN", 0xFFFFFFFF);
            break;
        }
        case CustomElementType::CategoryIcon: {
            int cx = x + w / 2;
            int cy = y + h / 2;
            display_->drawCategoryIcon(cx, cy, std::min(w, h), custom.text, custom.color);
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
                    // Determine whether vertical poster or horizontal 16:9
                    bool isVert = (h > w * 1.2f);
                    auto preset = isVert ? luna::util::ImagePreset::VerticalPoster : luna::util::ImagePreset::HorizontalCard16_9;
                    std::string imgUrl = isVert ? (custom.tile->coverUrl.empty() ? custom.tile->backdropUrl : custom.tile->coverUrl)
                                                : (custom.tile->backdropUrl.empty() ? custom.tile->coverUrl : custom.tile->backdropUrl);
                    if (!imgUrl.empty()) {
                        luna::util::ImageLoader::instance().requestImage(imgUrl, preset);
                        auto prevImg = luna::util::ImageLoader::instance().getImage(imgUrl, preset);
                        if (prevImg && prevImg->ready && !prevImg->rgba.empty() && prevImg->width > 0 && prevImg->height > 0) {
                            display_->drawPreScaledImage(x, y, prevImg->rgba.data(), w, h, prevImg->width, prevImg->height);
                        } else {
                            display_->fillRect(x, y, w, h, 0xFF241B3B);
                        }
                    } else {
                        display_->fillRect(x, y, w, h, 0xFF241B3B);
                    }
                }
            }
            break;
        }
        case CustomElementType::SpotlightArt: {
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
                    if (artUrl.empty() && custom.tile->title == "Luna Premium") {
                        const uint8_t* heroRgba = display_->getHeroBannerRgba();
                        if (heroRgba) {
                            display_->drawPreScaledImage(x, y, heroRgba, w, h, display_->getHeroBannerWidth(), display_->getHeroBannerHeight());
                        }
                    } else if (!artUrl.empty()) {
                        luna::util::ImageLoader::instance().requestImage(artUrl, luna::util::ImagePreset::PanoramicDetail);
                        auto artImg = luna::util::ImageLoader::instance().getImage(artUrl, luna::util::ImagePreset::PanoramicDetail);
                        if (!artImg || !artImg->ready) {
                            artImg = luna::util::ImageLoader::instance().getImage(artUrl, luna::util::ImagePreset::HorizontalCard16_9);
                        }
                        if (artImg && artImg->ready && !artImg->rgba.empty() && artImg->width > 0 && artImg->height > 0) {
                            display_->drawPreScaledImage(x, y, artImg->rgba.data(), w, h, artImg->width, artImg->height);
                        } else {
                            display_->fillRect(x, y, w, h, 0xFF120F1D);
                        }
                    } else {
                        display_->fillRect(x, y, w, h, 0xFF120F1D);
                    }
                }
                // Seamless left-edge gradient blend into stage background (#120F1D)
                int fadeW = std::min(260, w);
                for (int fx = 0; fx < fadeW; ++fx) {
                    uint8_t alpha = static_cast<uint8_t>(255 - (fx * 255 / fadeW));
                    display_->fillRect(x + fx, y, 1, h, (static_cast<uint32_t>(alpha) << 24) | 0x00120F1D);
                }
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
            display_->drawRoundedRect(x, y, w, h, 6, 0xFF2A233D);
            display_->drawRoundedBorder(x, y, w, h, 6, 1, 0xFF453966);
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
                if ((argb >> 24) == 0) break; // Transparent
                int r = static_cast<int>(cmd->renderData.rectangle.cornerRadius.topLeft);
                if (r > 0) {
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
                    // Try DecodedImage first
                    const auto* decImg = static_cast<const luna::util::DecodedImage*>(cmd->renderData.image.imageData);
                    if (decImg && decImg->ready && !decImg->rgba.empty() && decImg->width > 0 && decImg->height > 0) {
                        display_->drawPreScaledImage(x, y, decImg->rgba.data(), w, h, decImg->width, decImg->height);
                    } else {
                        // Fallback to ClayImageElement if allocated that way
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
                    auto* custom = static_cast<ClayCustomElement*>(cmd->renderData.custom.customData);
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
