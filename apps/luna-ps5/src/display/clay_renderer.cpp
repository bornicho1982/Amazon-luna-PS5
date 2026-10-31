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
                    std::string prevUrl = custom.tile->backdropUrl.empty() ? custom.tile->coverUrl : custom.tile->backdropUrl;
                    auto prevImg = luna::util::ImageLoader::instance().getImage(prevUrl, luna::util::ImagePreset::HorizontalCard16_9);
                    if (prevImg && prevImg->ready) {
                        display_->drawPreScaledImage(x, y, prevImg->rgba.data(), w, h, prevImg->width, prevImg->height);
                    } else {
                        if (!prevUrl.empty()) {
                            luna::util::ImageLoader::instance().requestImage(prevUrl, luna::util::ImagePreset::HorizontalCard16_9);
                        }
                        display_->fillRect(x, y, w, h, 0xFF181326);
                    }
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
                    auto* img = static_cast<ClayImageElement*>(cmd->renderData.image.imageData);
                    if (img && img->rgba && img->width > 0 && img->height > 0) {
                        display_->drawPreScaledImage(x, y, img->rgba, w, h, img->width, img->height);
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
