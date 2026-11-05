#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wreorder-init-list"

#define LUNA_BORDER(w) { static_cast<uint16_t>(w), static_cast<uint16_t>(w), static_cast<uint16_t>(w), static_cast<uint16_t>(w), 0 }

#include "display/luna_clay_ui.h"
#include "display/clay_renderer.h"
#include "util/image_loader.h"
#include "media/mp4_preview_player.h"
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <cstdio>
#include <memory>

namespace luna::display {

namespace {

std::string utf8SafeTruncate(const std::string& str, size_t maxChars) {
    if (str.empty() || maxChars == 0) return "";
    size_t charCount = 0;
    size_t byteIdx = 0;
    while (byteIdx < str.size() && charCount < maxChars) {
        uint8_t c = static_cast<uint8_t>(str[byteIdx]);
        if (c < 0x80) byteIdx += 1;
        else if ((c & 0xE0) == 0xC0) byteIdx += 2;
        else if ((c & 0xF0) == 0xE0) byteIdx += 3;
        else if ((c & 0xF8) == 0xF0) byteIdx += 4;
        else byteIdx += 1;
        charCount++;
    }
    if (byteIdx >= str.size()) return str;
    return str.substr(0, byteIdx) + "...";
}

// Reusable professional game card with 100% full-bleed decoded cover image and focus ring
static void renderGameCard(DisplayManager& disp, ClayRenderer& clay,
                           const GameTile& tile, bool isSel, bool isVertical,
                           float cardW, float cardH, uint64_t frameCount,
                           const std::string& subtext = "") {
    (void)disp;
    (void)frameCount;
    auto preset = isVertical ? luna::util::ImagePreset::VerticalPoster : luna::util::ImagePreset::HorizontalCard16_9;
    std::string imgUrl = isVertical
        ? (tile.coverUrl.empty() ? tile.backdropUrl : tile.coverUrl)
        : (tile.backdropUrl.empty() ? tile.coverUrl : tile.backdropUrl);

    std::shared_ptr<const luna::util::DecodedImage> img;
    if (!imgUrl.empty()) {
        luna::util::ImageLoader::instance().requestImage(imgUrl, preset);
        img = luna::util::ImageLoader::instance().getImage(imgUrl, preset);
    }
    bool imgReady = img && img->ready && !img->rgba.empty() && img->width > 0 && img->height > 0;

    CLAY_AUTO_ID({
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(cardW), CLAY_SIZING_FIXED(cardH) },
            .padding = { 8, 8, 8, 8 },
            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = imgReady ? Clay_Color{ 0, 0, 0, 0 } : Clay_Color{ 22, 27, 42, 255 },
        .cornerRadius = CLAY_CORNER_RADIUS(14),
        .image = imgReady ? Clay_ImageElementConfig{ .imageData = clay.allocDecodedImage(img) } : Clay_ImageElementConfig{},
        .border = {
            .color = isSel ? Clay_Color{ 56, 189, 248, 255 } : (imgReady ? Clay_Color{ 0, 0, 0, 0 } : Clay_Color{ 42, 45, 61, 255 }),
            .width = LUNA_BORDER(isSel ? 3 : 1)
        }
    }) {
        if (!imgReady) {
            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(tile.title, 20)), CLAY_TEXT_CONFIG({
                .textColor = { 203, 213, 225, 255 },
                .fontSize = 14
            }));
        }
    }

    if (!subtext.empty()) {
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(cardW), CLAY_SIZING_FIXED(24) },
                .padding = { 4, 4, 0, 0 },
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER }
            }
        }) {
            CLAY_TEXT(ClayRenderer::toClayString(subtext), CLAY_TEXT_CONFIG({
                .textColor = { 148, 163, 184, 255 },
                .fontSize = 13
            }));
        }
    }
}

// Stitch v2 Clean Resting Vertical Card OR Pop-out Preview Card when focused (isSel == true)
static void renderCleanOrPopoutGameCard(DisplayManager& disp, ClayRenderer& clay,
                                        const GameTile& tile, bool isSel,
                                        float cleanW, float cleanH, uint64_t frameCount) {
    if (!isSel) {
        renderGameCard(disp, clay, tile, false, /*isVertical=*/true, cleanW, cleanH, frameCount);
        return;
    }

    // Pop-out Preview Card (310 x 304 px) showing real game data from Amazon Luna
    int pegiAge = 12;
    if (tile.pegiRating.find("18") != std::string::npos) pegiAge = 18;
    else if (tile.pegiRating.find("16") != std::string::npos) pegiAge = 16;
    else if (tile.pegiRating.find("7") != std::string::npos) pegiAge = 7;
    else if (tile.pegiRating.find("3") != std::string::npos) pegiAge = 3;

    std::string pub = tile.publisher.empty() ? (tile.developer.empty() ? "Amazon Luna" : tile.developer) : tile.publisher;
    std::string inputDesc = tile.inputMethods.empty() ? "Mando, Teclado y rat\xC3\xB3n" : tile.inputMethods;

    // Real genre pills parsed dynamically from tile.genres
    std::vector<std::string> realGenres;
    if (!tile.genres.empty()) {
        size_t start = 0;
        while (start < tile.genres.size()) {
            size_t pos = tile.genres.find(" • ", start);
            if (pos == std::string::npos) pos = tile.genres.find(", ", start);
            if (pos == std::string::npos) {
                realGenres.push_back(tile.genres.substr(start));
                break;
            }
            realGenres.push_back(tile.genres.substr(start, pos - start));
            start = (tile.genres[pos] == ',') ? pos + 2 : pos + 3;
            if (realGenres.size() >= 2) break;
        }
    }
    if (realGenres.empty()) {
        realGenres.push_back("Acci\xC3\xB3n");
        realGenres.push_back("Aventura");
    }

    std::string playBtnText = tile.buttonText.empty() ? "JUGAR AHORA" : tile.buttonText;
    if (playBtnText.find("JUGAR") == std::string::npos && playBtnText.find("Jugar") == std::string::npos) {
        playBtnText = "JUGAR AHORA";
    }
    std::string playLabel = "\xE2\x96\xB6  " + playBtnText;

    CLAY_AUTO_ID({
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(310), CLAY_SIZING_FIXED(304) },
            .padding = { 10, 10, 10, 10 },
            .childGap = 8,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { 22, 27, 42, 255 }, // #161B2A
        .cornerRadius = CLAY_CORNER_RADIUS(16),
        .border = { .color = { 56, 189, 248, 255 }, .width = LUNA_BORDER(2) } // #38BDF8
    }) {
        // 1. Top 16:9 Live Video Trailer / Backdrop Box (290 x 148 px)
        CLAY_AUTO_ID({
            .layout = { .sizing = { CLAY_SIZING_FIXED(290), CLAY_SIZING_FIXED(148) } },
            .custom = { .customData = clay.allocCustom(CustomElementType::Mp4PreviewPlayer, "", 0xFFFFFFFF, &tile, frameCount, true) }
        }) {}

        // 2. Center Wide Pill Button: "▶ JUGAR AHORA"
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(290), CLAY_SIZING_FIXED(34) },
                .padding = { 0, 0, 0, 0 },
                .childGap = 8,
                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            },
            .backgroundColor = { 213, 227, 255, 255 }, // #D5E3FF
            .cornerRadius = CLAY_CORNER_RADIUS(17)
        }) {
            CLAY_TEXT(ClayRenderer::toClayString(playLabel), CLAY_TEXT_CONFIG({
                .textColor = { 0, 48, 96, 255 }, // #003060
                .fontSize = 13
            }));
        }

        // 3. Bottom Metadata + Real Genre Pills + Official PEGI Badge
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(290), CLAY_SIZING_FIXED(84) },
                .padding = { 0, 0, 0, 0 },
                .childGap = 4,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            }
        }) {
            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(tile.title, 26)), CLAY_TEXT_CONFIG({
                .textColor = { 255, 255, 255, 255 },
                .fontSize = 15
            }));
            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(inputDesc, 22) + "  \xE2\x80\xA2  " + utf8SafeTruncate(pub, 14)), CLAY_TEXT_CONFIG({
                .textColor = { 148, 163, 184, 255 },
                .fontSize = 11
            }));

            // Bottom row: Real Genre pills on left + PEGI badge on right
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(290), CLAY_SIZING_FIXED(36) },
                    .padding = { 0, 0, 0, 0 },
                    .childGap = 6,
                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26) },
                        .padding = { 8, 8, 0, 0 },
                        .childGap = 6,
                        .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    }
                }) {
                    for (const auto& g : realGenres) {
                        CLAY_AUTO_ID({
                            .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(22) }, .padding = { 8, 8, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                            .backgroundColor = { 42, 45, 61, 255 },
                            .cornerRadius = CLAY_CORNER_RADIUS(6),
                            .border = { .color = { 65, 71, 82, 255 }, .width = LUNA_BORDER(1) }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(g, 14)), CLAY_TEXT_CONFIG({ .textColor = { 226, 232, 240, 255 }, .fontSize = 11 }));
                        }
                    }
                }

                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(30), CLAY_SIZING_FIXED(36) } },
                    .custom = { .customData = clay.allocPegiBadge(pegiAge) }
                }) {}
            }
        }
    }
}

// Helper for dynamic '<' and '>' lateral navigation arrow pills on carousels
static void renderCarouselArrow(bool isLeft, bool visible) {
    if (!visible) {
        CLAY_AUTO_ID({
            .layout = { .sizing = { CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(44) } }
        }) {}
        return;
    }
    CLAY_AUTO_ID({
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(44) },
            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = { 22, 27, 42, 230 },
        .cornerRadius = CLAY_CORNER_RADIUS(14),
        .border = { .color = { 56, 189, 248, 200 }, .width = LUNA_BORDER(1) }
    }) {
        CLAY_TEXT(isLeft ? CLAY_STRING("<") : CLAY_STRING(">"), CLAY_TEXT_CONFIG({
            .textColor = { 255, 255, 255, 255 },
            .fontSize = 18
        }));
    }
}

} // namespace

void LunaClayUI::renderHomeScreen(DisplayManager& disp, const HomeScreenProps& props) {
    disp.clear(0xFF0E0B16); // Cosmic Deep official background
    auto& clay = ClayRenderer::instance();

    // =========================================================================
    // 1. VISTA DE BUSQUEDA
    // =========================================================================
    if (props.searchOpen) {
        clay.beginFrame();
        CLAY(CLAY_ID("SearchRoot"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(1080) },
                .padding = { 72, 72, 36, 44 },
                .childGap = 16,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = { 14, 11, 22, 255 }
        }) {
            // Search Input Bar
            bool barFoc = (props.searchZone == 0);
            CLAY(CLAY_ID("SearchBar"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(58) },
                    .padding = { 24, 24, 0, 0 },
                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                },
                .backgroundColor = { 28, 21, 46, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(12),
                .border = { .color = barFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(barFoc ? 2 : 1) }
            }) {
                std::string displayQuery = props.searchQuery.empty() ? "Buscar en Amazon Luna..." : (props.searchQuery + (((props.frameCount / 30) % 2 == 0) ? "|" : ""));
                CLAY_TEXT(ClayRenderer::toClayString(displayQuery), CLAY_TEXT_CONFIG({
                    .textColor = props.searchQuery.empty() ? Clay_Color{148, 163, 184, 255} : Clay_Color{255, 255, 255, 255},
                    .fontSize = 20
                }));
            }

            // Virtual Keyboard Keys (2 rows of 14)
            static const char* kKeys[28] = {
                "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
                "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "ESPACIO", "BORRAR"
            };
            for (int r = 0; r < 2; ++r) {
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42) },
                        .childGap = 10,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    }
                }) {
                    for (int c = 0; c < 14; ++c) {
                        int k = r * 14 + c;
                        bool kf = (props.searchZone == 0 && props.searchLetterIdx == k);
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_FIXED(116), CLAY_SIZING_FIXED(42) },
                                .padding = { 12, 12, 0, 0 },
                                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            },
                            .backgroundColor = kf ? Clay_Color{145, 70, 255, 255} : Clay_Color{28, 21, 46, 255},
                            .cornerRadius = CLAY_CORNER_RADIUS(8),
                            .border = { .color = kf ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(kf ? 2 : 1) }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(kKeys[k]), CLAY_TEXT_CONFIG({
                                .textColor = kf ? Clay_Color{255, 255, 255, 255} : Clay_Color{203, 213, 225, 255},
                                .fontSize = 16
                            }));
                        }
                    }
                }
            }

            // Results count
            int numF = static_cast<int>(props.filteredGames.size());
            CLAY_AUTO_ID({
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30) } }
            }) {
                CLAY_TEXT(ClayRenderer::toClayString(std::to_string(numF) + " juegos encontrados"), CLAY_TEXT_CONFIG({
                    .textColor = {148, 163, 184, 255},
                    .fontSize = 18
                }));
            }

            // Cuadrícula 16:9 de 5 columnas con carátulas horizontales reales
            int firstRow = (props.searchZone == 1) ? std::max(0, props.searchGridIdx / 5 - 1) : 0;
            int maxRows = 2;
            for (int row = firstRow; row < firstRow + maxRows && row * 5 < numF; ++row) {
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(210) },
                        .padding = { 8, 8, 0, 0 },
                        .childGap = 32,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    }
                }) {
                    for (int col = 0; col < 5; ++col) {
                        int g = row * 5 + col;
                        if (g >= numF) break;
                        const auto& tile = props.filteredGames[g];
                        bool isSel = (props.searchZone == 1 && props.searchGridIdx == g);

                        renderGameCard(disp, clay, tile, isSel, /*isVertical=*/false, 320, 180, props.frameCount, utf8SafeTruncate(tile.title, 24));
                    }
                }
            }
        }
        Clay_RenderCommandArray commands = Clay_EndLayout(0.0f);
        clay.renderCommands(commands);
        disp.present();
        return;
    }

    // =========================================================================
    // 2. REMOTE PLAY TAB (activeTabIdx == 3)
    // =========================================================================
    if (props.activeTabIdx == 3) {
        clay.beginFrame();
        CLAY(CLAY_ID("RemotePlayRoot"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(1080) },
                .padding = { 56, 56, 90, 44 },
                .childGap = 24,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = { 14, 11, 22, 255 }
        }) {
            // Header card
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(100) },
                    .padding = { 32, 32, 20, 20 },
                    .childGap = 10,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = { 24, 18, 38, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(16),
                .border = { .color = { 45, 34, 72, 255 }, .width = LUNA_BORDER(1) }
            }) {
                CLAY_TEXT(CLAY_STRING("Remote Play"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 32 }));
                CLAY_TEXT(CLAY_STRING("Juega con tus amigos esteis donde esteis. Compartid partidas multijugador y cooperativas al instante sin coste adicional."), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 16 }));
            }

            // Dos cajas principales: Host y Guest (2 columnas)
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(480) },
                    .padding = { 16, 16, 0, 0 },
                    .childGap = 32,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                int rpCardSel = (!props.rowSelectedCol.empty()) ? props.rowSelectedCol[0] : 0;
                bool hostFoc = (props.focusZone != 0 && rpCardSel == 0);
                bool guestFoc = (props.focusZone != 0 && rpCardSel == 1);

                // Caja 1: ANFITRION (Host)
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_PERCENT(0.5f), CLAY_SIZING_GROW(0) },
                        .padding = { 36, 36, 32, 32 },
                        .childGap = 20,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = { 20, 15, 36, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(16),
                    .border = { .color = hostFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(hostFoc ? 2 : 1) }
                }) {
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(28) }, .padding = { 10, 10, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = { 88, 28, 135, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(6)
                    }) {
                        CLAY_TEXT(CLAY_STRING("ANFITRION"), CLAY_TEXT_CONFIG({ .textColor = {233, 213, 255, 255}, .fontSize = 16 }));
                    }
                    CLAY_TEXT(CLAY_STRING("Crear una partida"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 26 }));
                    CLAY_TEXT(CLAY_STRING("1. Genera un codigo unico de acceso de 6 digitos.\n2. Enviaselo a tus amigos (hasta 8 jugadores).\n3. Inicia cualquier juego compatible de tu biblioteca."), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 16, .lineHeight = 26 }));

                    // Codigo PIN box
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(88) },
                            .padding = { 20, 20, 0, 0 },
                            .childGap = 16,
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        },
                        .backgroundColor = { 30, 22, 50, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(12),
                        .border = { .color = { 76, 29, 149, 255 }, .width = LUNA_BORDER(1) }
                    }) {
                        CLAY_TEXT(CLAY_STRING("CODIGO:"), CLAY_TEXT_CONFIG({ .textColor = {167, 139, 250, 255}, .fontSize = 16 }));
                        CLAY_TEXT(CLAY_STRING("2 9 9 9 8 3"), CLAY_TEXT_CONFIG({ .textColor = {252, 211, 77, 255}, .fontSize = 32 }));
                        CLAY_TEXT(CLAY_STRING("Sala activa lista para jugar"), CLAY_TEXT_CONFIG({ .textColor = {16, 185, 129, 255}, .fontSize = 16 }));
                    }

                    // Boton Generar Nuevo
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIXED(260), CLAY_SIZING_FIXED(52) },
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                        },
                        .backgroundColor = hostFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{121, 82, 236, 255},
                        .cornerRadius = CLAY_CORNER_RADIUS(26),
                        .border = { .color = hostFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{0, 0, 0, 0}, .width = LUNA_BORDER(hostFoc ? 2 : 0) }
                    }) {
                        CLAY_TEXT(CLAY_STRING("Generar nuevo codigo"), CLAY_TEXT_CONFIG({
                            .textColor = hostFoc ? Clay_Color{14, 11, 22, 255} : Clay_Color{255, 255, 255, 255},
                            .fontSize = 18
                        }));
                    }
                }

                // Caja 2: INVITADO (Guest)
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_PERCENT(0.5f), CLAY_SIZING_GROW(0) },
                        .padding = { 36, 36, 32, 32 },
                        .childGap = 20,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = { 20, 15, 36, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(16),
                    .border = { .color = guestFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(guestFoc ? 2 : 1) }
                }) {
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(28) }, .padding = { 10, 10, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = { 6, 78, 59, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(6)
                    }) {
                        CLAY_TEXT(CLAY_STRING("INVITADO"), CLAY_TEXT_CONFIG({ .textColor = {167, 243, 208, 255}, .fontSize = 16 }));
                    }
                    CLAY_TEXT(CLAY_STRING("Unirse a una partida"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 26 }));
                    CLAY_TEXT(CLAY_STRING("Introduce el codigo de 6 digitos facilitado por el anfitrion:"), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 16 }));

                    // Casillas PIN
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(70) },
                            .padding = { 12, 12, 0, 0 },
                            .childGap = 14,
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        }
                    }) {
                        static const char* kDemoPin[6] = {"5", "8", "1", "4", "2", "0"};
                        for (int p = 0; p < 6; ++p) {
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(56), CLAY_SIZING_FIXED(64) },
                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                                },
                                .backgroundColor = { 28, 21, 46, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(10),
                                .border = { .color = { 76, 29, 149, 255 }, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(kDemoPin[p]), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 28 }));
                            }
                        }
                    }

                    // Boton Unirse
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(52) },
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                        },
                        .backgroundColor = guestFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{30, 22, 50, 255},
                        .cornerRadius = CLAY_CORNER_RADIUS(26),
                        .border = { .color = guestFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{55, 42, 84, 255}, .width = LUNA_BORDER(guestFoc ? 2 : 1) }
                    }) {
                        CLAY_TEXT(CLAY_STRING("Unirme a la partida"), CLAY_TEXT_CONFIG({
                            .textColor = guestFoc ? Clay_Color{14, 11, 22, 255} : Clay_Color{203, 213, 225, 255},
                            .fontSize = 18
                        }));
                    }
                }
            }
        }
        Clay_RenderCommandArray commands = Clay_EndLayout(0.0f);
        clay.renderCommands(commands);
        disp.present();
        return;
    }

    // =========================================================================
    // 3. PANTALLA PRINCIPAL CON SWIMLANES (Inicio, GameNight, Mi espacio)
    // =========================================================================
    int clampedRow = std::clamp(props.selectedRowIdx, 0, std::max(0, static_cast<int>(props.collections.size()) - 1));
    bool hasHero = (props.activeTabIdx != 2) && !props.collections.empty() && props.collections[0].isHeroRotator && !props.collections[0].items.empty();

    int scrollY = 0;
    if (clampedRow > 1) {
        for (int sr = 1; sr < clampedRow && sr < static_cast<int>(props.collections.size()); ++sr) {
            if (props.collections[sr].isPartnerRow) scrollY += 490;
            else if (props.collections[sr].isRankings) scrollY += 380;
            else scrollY += 360;
        }
    }

    clay.beginFrame();

    CLAY(CLAY_ID("RootContainer"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(1080) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { 10, 14, 23, 255 } // Stitch v2 Obsidian #0A0E17
    }) {
        // --- 3.1 TOP BAR FIJA ESTILO STITCH PS5 OS (1920 x 68 px) ---
        CLAY(CLAY_ID("TopNavBar"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(68) },
                .padding = { 48, 48, 0, 0 },
                .childGap = 24,
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            },
            .backgroundColor = { 10, 14, 23, 245 }, // #0A0E17
            .border = { .color = { 65, 71, 82, 80 }, .width = { .bottom = 1 } }
        }) {
            // Left: LUNA (#00DFC1) + PS5 (#0070D1 badge)
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(40) },
                    .padding = { 8, 8, 0, 0 },
                    .childGap = 10,
                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(34) } },
                    .custom = { .customData = clay.allocCustom(CustomElementType::LunaLogoIcon) }
                }) {}
                CLAY_TEXT(CLAY_STRING("LUNA"), CLAY_TEXT_CONFIG({ .textColor = { 0, 223, 193, 255 }, .fontSize = 22 }));
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(22) }, .padding = { 8, 8, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                    .backgroundColor = { 0, 112, 209, 255 }, // #0070D1
                    .cornerRadius = CLAY_CORNER_RADIUS(6)
                }) {
                    CLAY_TEXT(CLAY_STRING("PS5"), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 11 }));
                }
            }

            // Center: Frosted Navigation Capsule escorted by [L1] and [R1]
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48) },
                    .padding = { 12, 12, 0, 0 },
                    .childGap = 14,
                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(42), CLAY_SIZING_FIXED(24) } },
                    .custom = { .customData = clay.allocBumper(false, false) }
                }) {}

                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(44) },
                        .padding = { 6, 6, 4, 4 },
                        .childGap = 6,
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = { 22, 27, 42, 220 }, // #161B2A
                    .cornerRadius = CLAY_CORNER_RADIUS(22),
                    .border = { .color = { 65, 71, 82, 120 }, .width = LUNA_BORDER(1) }
                }) {
                    for (size_t t = 0; t < props.navTabs.size(); ++t) {
                        bool isTabActive = (props.activeTabIdx == static_cast<int>(t));
                        bool isTabFoc = (props.focusZone == 0 && props.navColIdx == static_cast<int>(t));

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(34) },
                                .padding = { 18, 18, 0, 0 },
                                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            },
                            .backgroundColor = isTabFoc ? Clay_Color{ 255, 255, 255, 255 } : (isTabActive ? Clay_Color{ 0, 112, 209, 255 } : Clay_Color{ 0, 0, 0, 0 }),
                            .cornerRadius = CLAY_CORNER_RADIUS(17),
                            .border = { .color = isTabFoc ? Clay_Color{ 56, 189, 248, 255 } : Clay_Color{ 0, 0, 0, 0 }, .width = LUNA_BORDER(isTabFoc ? 2 : 0) }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(props.navTabs[t]), CLAY_TEXT_CONFIG({
                                .textColor = isTabFoc ? Clay_Color{ 7, 9, 17, 255 } : (isTabActive ? Clay_Color{ 255, 255, 255, 255 } : Clay_Color{ 148, 163, 184, 255 }),
                                .fontSize = 15
                            }));
                        }
                    }
                }

                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(42), CLAY_SIZING_FIXED(24) } },
                    .custom = { .customData = clay.allocBumper(true, false) }
                }) {}
            }

            // Right: Search Button + DualSense 12ms Telemetry Pill + Gamer Profile Capsule
            bool searchBtnFoc = (props.focusZone == 0 && props.navColIdx == static_cast<int>(props.navTabs.size()));
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(108), CLAY_SIZING_FIXED(38) },
                    .padding = { 12, 12, 0, 0 },
                    .childGap = 8,
                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                },
                .backgroundColor = searchBtnFoc ? Clay_Color{ 255, 255, 255, 255 } : Clay_Color{ 22, 27, 42, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(19),
                .border = { .color = searchBtnFoc ? Clay_Color{ 56, 189, 248, 255 } : Clay_Color{ 65, 71, 82, 160 }, .width = LUNA_BORDER(searchBtnFoc ? 2 : 1) }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16) } },
                    .custom = { .customData = clay.allocInterfaceIcon(DualSenseBtn::Search, searchBtnFoc ? 0xFF070911 : 0xFFCBD5E1) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Buscar"), CLAY_TEXT_CONFIG({
                    .textColor = searchBtnFoc ? Clay_Color{ 7, 9, 17, 255 } : Clay_Color{ 226, 232, 240, 255 },
                    .fontSize = 14
                }));
            }

            // DualSense Telemetry Pill (12ms #00DFC1)
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(36) },
                    .padding = { 12, 12, 0, 0 },
                    .childGap = 6,
                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                },
                .backgroundColor = { 22, 27, 42, 220 },
                .cornerRadius = CLAY_CORNER_RADIUS(18),
                .border = { .color = { 65, 71, 82, 120 }, .width = LUNA_BORDER(1) }
            }) {
                CLAY_TEXT(CLAY_STRING("\xE2\x97\x8F"), CLAY_TEXT_CONFIG({ .textColor = { 0, 223, 193, 255 }, .fontSize = 11 }));
                CLAY_TEXT(CLAY_STRING("12ms"), CLAY_TEXT_CONFIG({ .textColor = { 0, 223, 193, 255 }, .fontSize = 13 }));
            }

            // Gamer Profile Capsule
            bool avatarFoc = (props.focusZone == 0 && props.navColIdx == static_cast<int>(props.navTabs.size()) + 1);
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(40) },
                    .padding = { 8, 14, 0, 0 },
                    .childGap = 10,
                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                },
                .backgroundColor = avatarFoc ? Clay_Color{ 255, 255, 255, 255 } : Clay_Color{ 22, 27, 42, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(20),
                .border = { .color = avatarFoc ? Clay_Color{ 56, 189, 248, 255 } : Clay_Color{ 65, 71, 82, 160 }, .width = LUNA_BORDER(avatarFoc ? 2 : 1) }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(28) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                    .backgroundColor = { 0, 112, 209, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(14)
                }) {
                    CLAY_TEXT(CLAY_STRING("B"), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 15 }));
                }

                CLAY_TEXT(ClayRenderer::toClayString(props.profile.displayName + "  \xE2\x80\xA2  Prime Activo"), CLAY_TEXT_CONFIG({
                    .textColor = avatarFoc ? Clay_Color{ 7, 9, 17, 255 } : Clay_Color{ 240, 244, 255, 255 },
                    .fontSize = 13
                }));
            }
        }

        // --- 3.2 SCROLLABLE BODY ---
        CLAY(CLAY_ID("ScrollContainer"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(968) },
                .padding = { 48, 48, 20, 28 },
                .childGap = 32,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .clip = { .horizontal = false, .vertical = true, .childOffset = { 0, static_cast<float>(-scrollY) } }
        }) {
            // =================================================================
            // DYNAMIC PS5 SPOTLIGHT STAGE (1824 x 420 px)
            // Reactivo en tiempo real al juego enfocado con el mando DualSense
            // =================================================================
            if (hasHero) {
                // Determine active game: if browsing rows (focusZone == 2), show focused card!
                const GameTile* activeGame = nullptr;
                const auto& heroCol = props.collections[0];
                int heroIdx = (!props.rowSelectedCol.empty()) ? props.rowSelectedCol[0] : 0;
                if (heroIdx < 0 || heroIdx >= static_cast<int>(heroCol.items.size())) heroIdx = 0;
                activeGame = &heroCol.items[heroIdx];

                if ((props.focusZone == 2 || clampedRow > 0) && clampedRow >= 0 && clampedRow < static_cast<int>(props.collections.size())) {
                    const auto& curRow = props.collections[clampedRow];
                    int selCol = (clampedRow < static_cast<int>(props.rowSelectedCol.size())) ? props.rowSelectedCol[clampedRow] : 0;
                    if (selCol >= 0 && selCol < static_cast<int>(curRow.items.size())) {
                        activeGame = &curRow.items[selCol];
                    }
                }
                if (!activeGame && !props.collections.empty() && !props.collections[0].items.empty()) {
                    activeGame = &props.collections[0].items[0];
                }

                bool isHeroActive = (props.focusZone == 1 && clampedRow == 0);

                CLAY(CLAY_ID("SpotlightStage"), {
                    .layout = {
                        .sizing = { CLAY_SIZING_FIXED(1824), CLAY_SIZING_FIXED(430) },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = { 14, 19, 32, 255 }, // Cristal oscuro PS5 #0E1320 (Escenario continuo unico)
                    .cornerRadius = CLAY_CORNER_RADIUS(22),
                    .border = { .color = isHeroActive ? Clay_Color{0, 112, 209, 255} : Clay_Color{34, 44, 66, 255}, .width = LUNA_BORDER(isHeroActive ? 2 : 1) }
                }) {
                    // Columna Izquierda (880 px): Metadatos, Sinopsis real, Botones PS5 y Deslizador del Carrusel
                    bool isPremiumSlide = (activeGame->title.find("Luna Premium") != std::string::npos ||
                                           activeGame->id.find("luna_premium") != std::string::npos ||
                                           activeGame->backdropUrl == "local:luna_premium");

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIXED(880), CLAY_SIZING_FIXED(430) },
                            .padding = { 36, 24, 22, 18 },
                            .childGap = 10,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        },
                        .custom = { .customData = clay.allocCustom(CustomElementType::HeroLeftBackdrop, "", 0xFFFFFFFF, activeGame, props.frameCount, isHeroActive) }
                    }) {
                        // Fila 1: Píldoras de estado (Suscripción + Distribuidora + Sello Oficial PEGI)
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52) },
                                .childGap = 12,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            // Chip de suscripción estilo PS5
                            bool isPremium = (isPremiumSlide ||
                                              activeGame->channelSubtext.find("Luna+") != std::string::npos ||
                                              activeGame->category == "LUNA+");

                            std::string subText = isPremium ? "LUNA PREMIUM" : "INCLUIDO CON PRIME";
                            Clay_Color subBg = isPremium ? Clay_Color{ 94, 40, 184, 255 } : Clay_Color{ 0, 85, 179, 255 }; // #5E28B8 o #0055B3
                            Clay_Color subBorder = isPremium ? Clay_Color{ 139, 92, 246, 255 } : Clay_Color{ 43, 136, 240, 255 }; // #8B5CF6 o #2B88F0

                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(28) }, .padding = { 14, 14, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                .backgroundColor = subBg,
                                .cornerRadius = CLAY_CORNER_RADIUS(8),
                                .border = { .color = subBorder, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(subText), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 12 }));
                            }

                            // Chip Distribuidora real (Gris grafito #1C2536, borde #344360, texto #E2ECFF)
                            std::string pub = isPremiumSlide ? "AMAZON CLOUD GAMING" : (activeGame->publisher.empty() ? activeGame->developer : activeGame->publisher);
                            if (pub.empty() || pub == "Amazon Luna") pub = "UBISOFT / SEGA";
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(28) }, .padding = { 14, 14, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                .backgroundColor = { 28, 37, 54, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(8),
                                .border = { .color = { 52, 67, 96, 255 }, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(pub), CLAY_TEXT_CONFIG({ .textColor = { 226, 236, 255, 255 }, .fontSize = 12 }));
                            }

                            if (!isPremiumSlide) {
                                // Sello Oficial PEGI reglamentario grande (44 x 52 px)
                                int pegiAge = 12;
                                std::string pegiStr = activeGame->pegiRating;
                                if (pegiStr.find("18") != std::string::npos) pegiAge = 18;
                                else if (pegiStr.find("16") != std::string::npos) pegiAge = 16;
                                else if (pegiStr.find("7") != std::string::npos) pegiAge = 7;
                                else if (pegiStr.find("3") != std::string::npos) pegiAge = 3;

                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIXED(44), CLAY_SIZING_FIXED(52) } },
                                    .custom = { .customData = clay.allocPegiBadge(pegiAge) }
                                }) {}
                            }
                        }

                        // Fila 2: Título o Logo del Juego (38 px)
                        std::shared_ptr<const luna::util::DecodedImage> logoImg;
                        if (!isPremiumSlide && !activeGame->logoUrl.empty()) {
                            luna::util::ImageLoader::instance().requestImage(activeGame->logoUrl, luna::util::ImagePreset::ProductLogo);
                            logoImg = luna::util::ImageLoader::instance().getImage(activeGame->logoUrl, luna::util::ImagePreset::ProductLogo);
                        }
                        if (logoImg && logoImg->ready && !logoImg->rgba.empty() && logoImg->width > 0 && logoImg->height > 0) {
                            float lw = static_cast<float>(std::min(380, logoImg->width));
                            float lh = static_cast<float>(std::min(68, logoImg->height));
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIXED(lw), CLAY_SIZING_FIXED(lh) } },
                                .image = { .imageData = clay.allocDecodedImage(logoImg) }
                            }) {}
                        } else {
                            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(activeGame->title, 28)), CLAY_TEXT_CONFIG({
                                .textColor = {255, 255, 255, 255},
                                .fontSize = 36
                            }));
                        }

                        // Fila 3: Píldoras individuales de Género por colores estilo PS5 (#152238) y Jugadores (#172926)
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30) },
                                .padding = { 8, 8, 0, 0 },
                                .childGap = 10,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            std::vector<std::string> gList;
                            if (isPremiumSlide) {
                                gList.push_back("+100 Juegos en la nube");
                                gList.push_back("1080p 60 FPS");
                            } else {
                                std::string genStr = activeGame->genres.empty() ? "Acción • Aventura" : activeGame->genres;
                                size_t gStart = 0;
                                while (gStart < genStr.size()) {
                                    size_t pos = genStr.find(" • ", gStart);
                                    if (pos == std::string::npos) pos = genStr.find(", ", gStart);
                                    if (pos == std::string::npos) {
                                        gList.push_back(genStr.substr(gStart));
                                        break;
                                    }
                                    gList.push_back(genStr.substr(gStart, pos - gStart));
                                    gStart = (genStr[pos] == ',') ? pos + 2 : pos + 3;
                                    if (gList.size() >= 3) break;
                                }
                                if (gList.empty()) gList.push_back("Acción");
                            }

                            for (const auto& gName : gList) {
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(26) }, .padding = { 12, 12, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                    .backgroundColor = { 21, 34, 56, 255 }, // #152238
                                    .cornerRadius = CLAY_CORNER_RADIUS(8),
                                    .border = { .color = { 30, 58, 95, 255 }, .width = LUNA_BORDER(1) } // #1E3A5F
                                }) {
                                    CLAY_TEXT(ClayRenderer::toClayString(gName), CLAY_TEXT_CONFIG({ .textColor = { 56, 189, 248, 255 }, .fontSize = 12 })); // Cian acento #38BDF8
                                }
                            }

                            if (!isPremiumSlide) {
                                std::string pl = activeGame->playerCount.empty() ? "1-4 jugadores" : activeGame->playerCount;
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(26) }, .padding = { 12, 12, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                    .backgroundColor = { 23, 41, 38, 255 }, // #172926
                                    .cornerRadius = CLAY_CORNER_RADIUS(8),
                                    .border = { .color = { 31, 71, 62, 255 }, .width = LUNA_BORDER(1) } // #1F473E
                                }) {
                                    CLAY_TEXT(ClayRenderer::toClayString(pl), CLAY_TEXT_CONFIG({ .textColor = { 52, 211, 153, 255 }, .fontSize = 12 })); // Verde esmeralda #34D399
                                }
                            }

                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(26) }, .padding = { 12, 12, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                .backgroundColor = { 23, 41, 38, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(8),
                                .border = { .color = { 31, 71, 62, 255 }, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(CLAY_STRING("DualSense Vibracion"), CLAY_TEXT_CONFIG({ .textColor = { 52, 211, 153, 255 }, .fontSize = 12 }));
                            }
                        }

                        // Fila 4: Sinopsis real oficial (máx 3 líneas)
                        std::string syn = activeGame->synopsis;
                        if (isPremiumSlide) {
                            syn = "Accede al instante a mas de 100 juegos en la nube a 1080p y 60 FPS en tu PS5 sin descargas ni tiempos de espera.";
                        } else if (syn.empty()) {
                            syn = "Explora este juego en Amazon Luna Cloud con graficos en alta definicion y control con DualSense.";
                        }
                        CLAY_AUTO_ID({
                            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(66) } }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(syn, 200)), CLAY_TEXT_CONFIG({
                                .textColor = {148, 163, 184, 255},
                                .fontSize = 15,
                                .lineHeight = 22
                            }));
                        }

                        // Fila 5: Botones de Consola PS5 con Iconos Gráficos DualSense
                        bool btn0Foc = isHeroActive && (props.heroBtnIdx == 0);
                        bool btn1Foc = isHeroActive && (props.heroBtnIdx == 1);
                        bool btn2Foc = isHeroActive && (props.heroBtnIdx == 2);
                        bool btn3Foc = isHeroActive && (props.heroBtnIdx == 3);

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(50) },
                                .padding = { 8, 8, 0, 0 },
                                .childGap = 12,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            if (isPremiumSlide) {
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(48) },
                                        .padding = { 16, 20, 0, 0 },
                                        .childGap = 10,
                                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                                    },
                                    .backgroundColor = isHeroActive ? Clay_Color{255, 255, 255, 255} : Clay_Color{94, 40, 184, 255},
                                    .cornerRadius = CLAY_CORNER_RADIUS(24),
                                    .border = { .color = isHeroActive ? Clay_Color{0, 112, 209, 255} : Clay_Color{139, 92, 246, 255}, .width = LUNA_BORDER(2) }
                                }) {
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                        .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Cross, isHeroActive) }
                                    }) {}
                                    CLAY_TEXT(CLAY_STRING("Explorar catalogo"), CLAY_TEXT_CONFIG({
                                        .textColor = isHeroActive ? Clay_Color{7, 9, 17, 255} : Clay_Color{255, 255, 255, 255},
                                        .fontSize = 16
                                    }));
                                }
                            } else {
                                // Botón 1: (Icono Gráfico ✕) Jugar ahora
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(192), CLAY_SIZING_FIXED(48) },
                                        .padding = { 14, 16, 0, 0 },
                                        .childGap = 10,
                                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                                    },
                                    .backgroundColor = btn0Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                                    .cornerRadius = CLAY_CORNER_RADIUS(24),
                                    .border = { .color = btn0Foc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btn0Foc ? 2 : 1) }
                                }) {
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                        .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Cross, btn0Foc) }
                                    }) {}
                                    CLAY_TEXT(CLAY_STRING("Jugar ahora"), CLAY_TEXT_CONFIG({
                                        .textColor = btn0Foc ? Clay_Color{7, 9, 17, 255} : Clay_Color{255, 255, 255, 255},
                                        .fontSize = 16
                                    }));
                                }

                                // Botón 2: (Icono Gráfico □) Ficha del juego
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(192), CLAY_SIZING_FIXED(48) },
                                        .padding = { 14, 16, 0, 0 },
                                        .childGap = 10,
                                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                                    },
                                    .backgroundColor = btn1Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                                    .cornerRadius = CLAY_CORNER_RADIUS(24),
                                    .border = { .color = btn1Foc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btn1Foc ? 2 : 1) }
                                }) {
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                        .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Square, btn1Foc) }
                                    }) {}
                                    CLAY_TEXT(CLAY_STRING("Ficha del juego"), CLAY_TEXT_CONFIG({
                                        .textColor = btn1Foc ? Clay_Color{7, 9, 17, 255} : Clay_Color{240, 244, 255, 255},
                                        .fontSize = 15
                                    }));
                                }

                                // Botón 3: (Icono ▶) Ver tráiler (pantalla completa)
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(172), CLAY_SIZING_FIXED(48) },
                                        .padding = { 14, 16, 0, 0 },
                                        .childGap = 10,
                                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                                    },
                                    .backgroundColor = btn2Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                                    .cornerRadius = CLAY_CORNER_RADIUS(24),
                                    .border = { .color = btn2Foc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btn2Foc ? 2 : 1) }
                                }) {
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                        .custom = { .customData = clay.allocInterfaceIcon(DualSenseBtn::Play, btn2Foc ? 0xFF0070D1 : 0xFF38BDF8) }
                                    }) {}
                                    CLAY_TEXT(CLAY_STRING("Ver trailer"), CLAY_TEXT_CONFIG({
                                        .textColor = btn2Foc ? Clay_Color{7, 9, 17, 255} : Clay_Color{240, 244, 255, 255},
                                        .fontSize = 15
                                    }));
                                }

                                // Botón 4: (Icono Gráfico △) Favorito / En Favoritos
                                bool isHeroFav = false;
                                if (props.favoriteIds) {
                                    for (const auto& fid : *props.favoriteIds) {
                                        if (fid == activeGame->id) {
                                            isHeroFav = true;
                                            break;
                                        }
                                    }
                                }
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(isHeroFav ? 178.0f : 160.0f), CLAY_SIZING_FIXED(48) },
                                        .padding = { 14, 16, 0, 0 },
                                        .childGap = 10,
                                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                                    },
                                    .backgroundColor = btn3Foc
                                        ? Clay_Color{255, 255, 255, 255}
                                        : (isHeroFav ? Clay_Color{16, 62, 52, 255} : Clay_Color{26, 33, 51, 255}),
                                    .cornerRadius = CLAY_CORNER_RADIUS(24),
                                    .border = {
                                        .color = btn3Foc
                                            ? Clay_Color{0, 112, 209, 255}
                                            : (isHeroFav ? Clay_Color{16, 185, 129, 255} : Clay_Color{44, 54, 80, 255}),
                                        .width = LUNA_BORDER((btn3Foc || isHeroFav) ? 2 : 1)
                                    }
                                }) {
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                        .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Triangle, btn3Foc) }
                                    }) {}
                                    CLAY_TEXT(isHeroFav ? CLAY_STRING("En Favoritos") : CLAY_STRING("Favorito"), CLAY_TEXT_CONFIG({
                                        .textColor = btn3Foc
                                            ? Clay_Color{7, 9, 17, 255}
                                            : (isHeroFav ? Clay_Color{110, 231, 183, 255} : Clay_Color{240, 244, 255, 255}),
                                        .fontSize = 15
                                    }));
                                }
                            }
                        }

                        // Fila 6: Deslizador / Indicadores del Carrusel Hero (Píldoras interactivas + Contador)
                        if (heroCol.items.size() > 1) {
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28) },
                                    .padding = { 8, 8, 0, 0 },
                                    .childGap = 10,
                                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                                }
                            }) {
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(22) }, .padding = { 10, 10, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                    .backgroundColor = isHeroActive ? Clay_Color{0, 112, 209, 255} : Clay_Color{24, 34, 54, 255},
                                    .cornerRadius = CLAY_CORNER_RADIUS(11),
                                    .border = { .color = isHeroActive ? Clay_Color{56, 189, 248, 255} : Clay_Color{44, 55, 78, 255}, .width = LUNA_BORDER(1) }
                                }) {
                                    CLAY_TEXT(CLAY_STRING("Joystick: Deslizar   |   Cruceta: Botones"), CLAY_TEXT_CONFIG({
                                        .textColor = { 255, 255, 255, 255 },
                                        .fontSize = 11
                                    }));
                                }

                                size_t numSlides = std::min<size_t>(heroCol.items.size(), 10);
                                for (size_t sIdx = 0; sIdx < numSlides; ++sIdx) {
                                    bool isCurSlide = (static_cast<int>(sIdx) == heroIdx);
                                    CLAY_AUTO_ID({
                                        .layout = {
                                            .sizing = { CLAY_SIZING_FIXED(isCurSlide ? 34.0f : 10.0f), CLAY_SIZING_FIXED(8) }
                                        },
                                        .backgroundColor = isCurSlide
                                            ? (isHeroActive ? Clay_Color{56, 189, 248, 255} : Clay_Color{255, 255, 255, 255})
                                            : Clay_Color{51, 65, 85, 255},
                                        .cornerRadius = CLAY_CORNER_RADIUS(4)
                                    }) {}
                                }

                                std::string slidePos = std::to_string(heroIdx + 1) + " / " + std::to_string(heroCol.items.size());
                                CLAY_TEXT(ClayRenderer::toClayString(slidePos), CLAY_TEXT_CONFIG({
                                    .textColor = { 148, 163, 184, 255 },
                                    .fontSize = 12
                                }));
                            }
                        }
                    }

                    // Columna Derecha (944 px): Tráiler en Vídeo MP4 o Arte Panorámico 16:9 con fundido suave de 340 px
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIXED(944), CLAY_SIZING_FIXED(430) },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        },
                        .custom = { .customData = clay.allocCustom(CustomElementType::SpotlightArt, "", 0xFFFFFFFF, activeGame, props.frameCount, isHeroActive) }
                    }) {}
                }
            }

            // =================================================================
            // SWIMLANES Y CARROUSELES DE JUEGOS (STITCH V2 CONSOLE ARCHITECTURE)
            // =================================================================
            size_t startRow = hasHero ? 1 : 0;
            for (size_t r = startRow; r < props.collections.size(); ++r) {
                const auto& row = props.collections[r];
                bool isFocusedRow = (props.focusZone != 0 && clampedRow == static_cast<int>(r));
                int selectedCol = (r < props.rowSelectedCol.size()) ? props.rowSelectedCol[r] : 0;
                int totalItems = static_cast<int>(row.items.size());

                // --- CASO 1: CATEGORY CAROUSEL ("Explorar categorías") ---
                if (row.isCategoryCarousel) {
                    int visibleCats = 10;
                    int startIdx = std::max(0, std::min(selectedCol - visibleCats / 2, std::max(0, totalItems - visibleCats)));
                    bool showLeft = (startIdx > 0 || selectedCol > 0);
                    bool showRight = (startIdx + visibleCats < totalItems || selectedCol < totalItems - 1);

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(220) },
                            .padding = { 8, 8, 0, 0 },
                            .childGap = 14,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        }
                    }) {
                        CLAY_TEXT(CLAY_STRING("Explorar categor\xC3\xAD" "as"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 24 }));

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(168) },
                                .padding = { 0, 0, 0, 0 },
                                .childGap = 16,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            renderCarouselArrow(true, showLeft);

                            for (int c = startIdx; c < totalItems && c < startIdx + visibleCats; ++c) {
                                bool isSel = isFocusedRow && (c == selectedCol);
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(154), CLAY_SIZING_FIXED(152) },
                                        .padding = { 12, 12, 16, 16 },
                                        .childGap = 14,
                                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                                    },
                                    .backgroundColor = isSel ? Clay_Color{ 52, 57, 78, 255 } : Clay_Color{ 42, 45, 61, 255 }, // #2A2D3D
                                    .cornerRadius = CLAY_CORNER_RADIUS(14),
                                    .border = { .color = isSel ? Clay_Color{ 56, 189, 248, 255 } : Clay_Color{ 65, 71, 82, 180 }, .width = LUNA_BORDER(isSel ? 3 : 1) }
                                }) {
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(42), CLAY_SIZING_FIXED(42) } },
                                        .custom = { .customData = clay.allocCustom(CustomElementType::CategoryIcon, row.items[c].title, isSel ? 0xFF38BDF8 : 0xFFFFFFFF) }
                                    }) {}

                                    CLAY_TEXT(ClayRenderer::toClayString(row.items[c].title), CLAY_TEXT_CONFIG({
                                        .textColor = { 255, 255, 255, 255 },
                                        .fontSize = 14
                                    }));
                                }
                            }

                            renderCarouselArrow(false, showRight);
                        }
                    }
                    continue;
                }

                // --- CASO 2: BANNERS PROMOCIONALES INTERCALADOS ("Personajes emblemáticos" y "Jugad juntos") ---
                if (row.isPartnerRow) {
                    GameTile promoTile;
                    if (!row.items.empty()) {
                        size_t rotIdx = (isFocusedRow && selectedCol >= 0 && selectedCol < static_cast<int>(row.items.size()))
                            ? static_cast<size_t>(selectedCol)
                            : (static_cast<size_t>(props.frameCount / 450) % row.items.size());
                        const auto& targetItem = row.items[rotIdx];
                        promoTile.backdropUrl = !targetItem.backdropUrl.empty()
                            ? targetItem.backdropUrl
                            : (!targetItem.screenshotUrls.empty() ? targetItem.screenshotUrls[0] : targetItem.coverUrl);
                        if (promoTile.backdropUrl.empty() && !row.partnerPromoImage.empty()) {
                            promoTile.backdropUrl = row.partnerPromoImage;
                        }
                    } else {
                        promoTile.backdropUrl = row.partnerPromoImage;
                    }

                    int visibleCards = isFocusedRow ? 8 : 8;
                    int startIdx = std::max(0, std::min(selectedCol - visibleCards / 2, std::max(0, totalItems - visibleCards)));
                    bool showLeft = (startIdx > 0 || selectedCol > 0);
                    bool showRight = (startIdx + visibleCards < totalItems || selectedCol < totalItems - 1);

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIXED(1824), CLAY_SIZING_FIXED(472) },
                            .padding = { 28, 28, 24, 20 },
                            .childGap = 14,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        },
                        .custom = { .customData = clay.allocCustom(CustomElementType::PartnerBannerBg, row.title, 0xFFFFFFFF, &promoTile, props.frameCount, isFocusedRow) }
                    }) {
                        // Top Header (sin botón "Ver todos los juegos")
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_FIXED(980), CLAY_SIZING_FIXED(78) },
                                .childGap = 6,
                                .layoutDirection = CLAY_TOP_TO_BOTTOM
                            }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(row.title), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 28 }));
                            CLAY_TEXT(ClayRenderer::toClayString(row.subtitle.empty()
                                ? "Hay leyendas que no dejan de mejorar. Ad\xC3\xA9ntrate en mundos inolvidables y descubre qu\xC3\xA9 viene despu\xC3\xA9s."
                                : utf8SafeTruncate(row.subtitle, 130)),
                                CLAY_TEXT_CONFIG({ .textColor = { 226, 232, 240, 255 }, .fontSize = 14 }));
                        }

                        // Integrated Bottom Carousel of Clean Cards + Pop-out Preview Card on Focus
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(308) },
                                .padding = { 0, 0, 0, 0 },
                                .childGap = 16,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            renderCarouselArrow(true, showLeft);

                            for (int i = startIdx; i < totalItems && i < startIdx + visibleCards; ++i) {
                                const auto& item = row.items[i];
                                bool isSel = isFocusedRow && (i == selectedCol);
                                renderCleanOrPopoutGameCard(disp, clay, item, isSel, 185, 248, props.frameCount);
                            }

                            renderCarouselArrow(false, showRight);
                        }
                    }
                    continue;
                }

                // --- CASO 3: RANKINGS ("Las mejores novedades en juegos" — Numeradas del 1 al 10) ---
                if (row.isRankings) {
                    int maxRankItems = std::min(10, totalItems);
                    int visibleRankings = isFocusedRow ? 6 : 7;
                    int startIdx = std::max(0, std::min(selectedCol - visibleRankings / 2, std::max(0, maxRankItems - visibleRankings)));
                    bool showLeft = (startIdx > 0 || selectedCol > 0);
                    bool showRight = (startIdx + visibleRankings < maxRankItems || selectedCol < maxRankItems - 1);

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(356) },
                            .childGap = 12,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        }
                    }) {
                        CLAY_TEXT(ClayRenderer::toClayString(row.title), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 24 }));

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(310) },
                                .padding = { 0, 0, 0, 0 },
                                .childGap = 16,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            renderCarouselArrow(true, showLeft);

                            for (int i = startIdx; i < maxRankItems && i < startIdx + visibleRankings; ++i) {
                                const auto& item = row.items[i];
                                bool isSel = isFocusedRow && (i == selectedCol);

                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(308) },
                                        .padding = { 0, 0, 0, 0 },
                                        .childGap = 6,
                                        .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                                    }
                                }) {
                                    // Giant stylized outlined number 1..10
                                    CLAY_AUTO_ID({
                                        .layout = {
                                            .sizing = { CLAY_SIZING_FIXED(i == 9 ? 76.0f : 56.0f), CLAY_SIZING_FIXED(230) }
                                        },
                                        .custom = { .customData = clay.allocCustom(CustomElementType::RankNumber, std::to_string(i + 1), 0xFFFFFFFF, nullptr, 0, isSel) }
                                    }) {}

                                    renderCleanOrPopoutGameCard(disp, clay, item, isSel, 185, 248, props.frameCount);
                                }
                            }

                            renderCarouselArrow(false, showRight);
                        }
                    }
                    continue;
                }

                // --- CASO 4: JUGADOS RECIENTEMENTE 16:9 (MI ESPACIO) ---
                if (row.isRecentlyPlayed16_9) {
                    int visible16_9 = 5;
                    int startIdx = std::max(0, std::min(selectedCol - visible16_9 / 2, std::max(0, totalItems - visible16_9)));
                    bool showLeft = (startIdx > 0 || selectedCol > 0);
                    bool showRight = (startIdx + visible16_9 < totalItems || selectedCol < totalItems - 1);

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(240) },
                            .childGap = 14,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        }
                    }) {
                        CLAY_TEXT(ClayRenderer::toClayString(row.title), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 24 }));

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(190) },
                                .padding = { 0, 0, 0, 0 },
                                .childGap = 18,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            renderCarouselArrow(true, showLeft);
                            for (int i = startIdx; i < totalItems && i < startIdx + visible16_9; ++i) {
                                const auto& item = row.items[i];
                                bool isSel = isFocusedRow && (i == selectedCol);
                                std::string dateSub = item.lastPlayedDate.empty() ? "Reciente" : item.lastPlayedDate;
                                renderGameCard(disp, clay, item, isSel, /*isVertical=*/false, 290, 164, props.frameCount, dateSub);
                            }
                            renderCarouselArrow(false, showRight);
                        }
                    }
                    continue;
                }

                // --- CASO 5: CARRUSELES ESTANDAR DE JUEGOS VERTICALES LIMPIOS + POP-OUT PREVIEW ---
                // ("Seguir jugando", "Añadidos recientemente", "Selecciones del editor", "Aventuras en mundos inmersivos")
                {
                    int visibleStd = isFocusedRow ? 8 : 9;
                    int startIdx = std::max(0, std::min(selectedCol - visibleStd / 2, std::max(0, totalItems - visibleStd)));
                    bool showLeft = (startIdx > 0 || selectedCol > 0);
                    bool showRight = (startIdx + visibleStd < totalItems || selectedCol < totalItems - 1);

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(346) },
                            .childGap = 12,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        }
                    }) {
                        CLAY_TEXT(ClayRenderer::toClayString(row.title), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 24 }));

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(308) },
                                .padding = { 0, 0, 0, 0 },
                                .childGap = 18,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            renderCarouselArrow(true, showLeft);

                            for (int i = startIdx; i < totalItems && i < startIdx + visibleStd; ++i) {
                                const auto& item = row.items[i];
                                bool isSel = isFocusedRow && (i == selectedCol);
                                renderCleanOrPopoutGameCard(disp, clay, item, isSel, 185, 248, props.frameCount);
                            }

                            renderCarouselArrow(false, showRight);
                        }
                    }
                }
            }
        }

        // --- 3.3 FOOTER HUD DE BOTONES ESTILO PS5 DUALSENSE (1920 x 44 px) ---
        CLAY(CLAY_ID("FooterHUD"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(44) },
                .padding = { 48, 48, 0, 0 },
                .childGap = 28,
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            },
            .backgroundColor = { 10, 14, 24, 245 }, // PS5 Dark
            .border = { .color = { 34, 44, 66, 140 }, .width = { .top = 1 } }
        }) {
            // (✕) Seleccionar / Jugar
            CLAY_AUTO_ID({
                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(32) }, .childGap = 8, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .layoutDirection = CLAY_LEFT_TO_RIGHT }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) } },
                    .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Cross, false) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Seleccionar / Jugar"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 15 }));
            }

            // (□) Ficha completa
            CLAY_AUTO_ID({
                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(32) }, .childGap = 8, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .layoutDirection = CLAY_LEFT_TO_RIGHT }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) } },
                    .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Square, false) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Ficha completa"), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 15 }));
            }

            // (△) Favorito
            CLAY_AUTO_ID({
                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(32) }, .childGap = 8, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .layoutDirection = CLAY_LEFT_TO_RIGHT }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) } },
                    .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Triangle, false) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Favorito"), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 15 }));
            }

            // (○) Volver
            CLAY_AUTO_ID({
                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(32) }, .childGap = 8, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .layoutDirection = CLAY_LEFT_TO_RIGHT }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) } },
                    .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Circle, false) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Volver"), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 15 }));
            }

            // (L1) (R1) Cambiar pestaña
            CLAY_AUTO_ID({
                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(32) }, .childGap = 6, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .layoutDirection = CLAY_LEFT_TO_RIGHT }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(36), CLAY_SIZING_FIXED(20) } },
                    .custom = { .customData = clay.allocBumper(false, false) }
                }) {}
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(36), CLAY_SIZING_FIXED(20) } },
                    .custom = { .customData = clay.allocBumper(true, false) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Cambiar pestana"), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 15 }));
            }

            // (OPTIONS) Ajustes
            CLAY_AUTO_ID({
                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(32) }, .childGap = 8, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .layoutDirection = CLAY_LEFT_TO_RIGHT }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(22) } },
                    .custom = { .customData = clay.allocOptionsButton(false) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Ajustes"), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 15 }));
            }

            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}

            if (!props.statusText.empty()) {
                CLAY_TEXT(ClayRenderer::toClayString(props.statusText), CLAY_TEXT_CONFIG({ .textColor = {16, 185, 129, 255}, .fontSize = 14 }));
            }
        }
    }

    Clay_RenderCommandArray commands = Clay_EndLayout(0.0f);
    clay.renderCommands(commands);

    // Profile Dropdown Menu Modal
    if (props.profileMenuOpen) {
        disp.fillRect(1500, 76, 360, 560, 0xF8161026);
        disp.drawRoundedBorder(1500, 76, 360, 560, 12, 1, 0xFF453466);
        static const char* kProfileOpts[10] = {
            "Gestionar perfiles", "Planes Luna", "Distintivos", "Stream", "Cuentas vinculadas",
            "Idioma", "Configuracion", "Novedades de Luna", "Ayuda", "Acerca de Luna"
        };
        for (int m = 0; m < 10; ++m) {
            int my = 92 + m * 52;
            bool mf = (props.profileMenuIdx == m);
            if (mf) {
                disp.drawRoundedRect(1516, my, 328, 44, 8, 0xFF7952EC);
            }
            disp.drawTextPx(1536, my + 12, 16, kProfileOpts[m], mf ? 0xFFFFFFFF : 0xFFCBD5E1);
        }
    }

    disp.present();
}

void LunaClayUI::renderGameDetail(DisplayManager& disp, const GameDetailProps& props) {
    disp.clear(0xFF0E0B16);
    auto& clay = ClayRenderer::instance();
    clay.beginFrame();

    CLAY(CLAY_ID("GameDetailRoot"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(1080) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { 14, 11, 22, 255 }
    }) {
        // --- TOP BAR FIJA ---
        CLAY(CLAY_ID("DetailTopBar"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(68) },
                .padding = { 48, 48, 0, 0 },
                .childGap = 16,
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            },
            .backgroundColor = { 10, 14, 24, 245 },
            .border = { .color = { 34, 44, 66, 140 }, .width = { .bottom = 1 } }
        }) {
            CLAY_AUTO_ID({
                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(36) }, .childGap = 8, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .layoutDirection = CLAY_LEFT_TO_RIGHT }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) } },
                    .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Circle, false) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Volver"), CLAY_TEXT_CONFIG({ .textColor = {226, 232, 240, 255}, .fontSize = 17 }));
            }

            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}

            CLAY_TEXT(ClayRenderer::toClayString(props.game.title), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 16 }));
        }

        // --- CONTENIDO DE LA FICHA EN 2 COLUMNAS ---
        CLAY(CLAY_ID("DetailMainContent"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(960) },
                .padding = { 56, 56, 24, 24 },
                .childGap = 48,
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            }
        }) {
            // COLUMNA IZQUIERDA: Carátula vertical + Botones de acción (320 px)
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(320), CLAY_SIZING_GROW(0) },
                    .childGap = 20,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                }
            }) {
                // Carátula vertical real
                renderGameCard(disp, clay, props.game, false, /*isVertical=*/true, 320, 440, props.frameCount);

                // Botón Jugar Ahora
                bool btnPlayFoc = (props.detailZone == 0 && props.detailBtnIdx == 0);
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(56) },
                        .padding = { 16, 16, 0, 0 },
                        .childGap = 12,
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = btnPlayFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(28),
                    .border = { .color = btnPlayFoc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btnPlayFoc ? 2 : 1) }
                }) {
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) } },
                        .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Cross, btnPlayFoc) }
                    }) {}
                    std::string playText = props.isConnecting ? "CONECTANDO..." : "Jugar ahora";
                    CLAY_TEXT(ClayRenderer::toClayString(playText), CLAY_TEXT_CONFIG({
                        .textColor = btnPlayFoc ? Clay_Color{7, 9, 17, 255} : Clay_Color{255, 255, 255, 255},
                        .fontSize = 19
                    }));
                }

                // Botón Galería / Capturas
                bool btnGalFoc = (props.detailZone == 0 && props.detailBtnIdx == 1);
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(50) },
                        .padding = { 16, 16, 0, 0 },
                        .childGap = 12,
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = btnGalFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(25),
                    .border = { .color = btnGalFoc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btnGalFoc ? 2 : 1) }
                }) {
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) } },
                        .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Square, btnGalFoc) }
                    }) {}
                    CLAY_TEXT(CLAY_STRING("Capturas y Trailer"), CLAY_TEXT_CONFIG({
                        .textColor = btnGalFoc ? Clay_Color{7, 9, 17, 255} : Clay_Color{203, 213, 225, 255},
                        .fontSize = 16
                    }));
                }

                // Botón Favoritos
                bool btnFavFoc = (props.detailZone == 0 && props.detailBtnIdx == 2);
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(50) },
                        .padding = { 16, 16, 0, 0 },
                        .childGap = 12,
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = btnFavFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(25),
                    .border = { .color = btnFavFoc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btnFavFoc ? 2 : 1) }
                }) {
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) } },
                        .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Triangle, btnFavFoc) }
                    }) {}
                    CLAY_TEXT(ClayRenderer::toClayString(props.isFavorite ? "En favoritos" : "Anadir a favoritos"), CLAY_TEXT_CONFIG({
                        .textColor = props.isFavorite ? Clay_Color{245, 158, 11, 255} : (btnFavFoc ? Clay_Color{7, 9, 17, 255} : Clay_Color{203, 213, 225, 255}),
                        .fontSize = 16
                    }));
                }
            }

            // COLUMNA DERECHA: Metadatos, Sinopsis, Especificaciones y Capturas
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .childGap = 20,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                }
            }) {
                // Título grande
                CLAY_TEXT(ClayRenderer::toClayString(props.game.title), CLAY_TEXT_CONFIG({
                    .textColor = {255, 255, 255, 255},
                    .fontSize = 40
                }));

                // Chips de metadatos
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(36) },
                        .padding = { 0, 0, 0, 0 },
                        .childGap = 16,
                        .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    }
                }) {
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(30) } },
                        .custom = { .customData = clay.allocCustom(CustomElementType::PegiBadge, props.game.pegiRating.empty() ? "PEGI 12" : props.game.pegiRating) }
                    }) {}

                    std::string catText = props.game.category.empty() ? "LUNA+" : props.game.category;
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(30) }, .padding = { 12, 12, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = { 124, 58, 237, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                    }) {
                        CLAY_TEXT(ClayRenderer::toClayString(catText), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 14 }));
                    }

                    std::string pubText = props.game.publisher.empty() ? "Amazon Games" : props.game.publisher;
                    CLAY_TEXT(ClayRenderer::toClayString(pubText), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 16 }));

                    std::string devText = props.game.developer.empty() ? "" : ("• " + props.game.developer);
                    if (!devText.empty()) {
                        CLAY_TEXT(ClayRenderer::toClayString(devText), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 16 }));
                    }
                }

                // Sinopsis completa
                std::string syn = props.game.synopsis.empty() ? "Disponible para jugar al instante en streaming 1080p 60 FPS en Amazon Luna." : props.game.synopsis;
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(90) } }
                }) {
                    CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(syn, 280)), CLAY_TEXT_CONFIG({
                        .textColor = {226, 232, 240, 255},
                        .fontSize = 18,
                        .lineHeight = 28
                    }));
                }

                // Especificaciones técnicas en tarjetas
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(80) },
                        .childGap = 16,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    }
                }) {
                    static const char* kSpecs[4] = {
                        "Resolucion: 1080p 60 FPS",
                        "Latencia nube: <25 ms",
                        "Audio: Estereo 48 kHz",
                        "Mando: DualSense Haptics"
                    };
                    for (int s = 0; s < 4; ++s) {
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_PERCENT(0.25f), CLAY_SIZING_GROW(0) },
                                .padding = { 12, 12, 10, 10 },
                                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                            },
                            .backgroundColor = { 24, 18, 38, 255 },
                            .cornerRadius = CLAY_CORNER_RADIUS(8),
                            .border = { .color = { 45, 34, 72, 255 }, .width = LUNA_BORDER(1) }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(kSpecs[s]), CLAY_TEXT_CONFIG({
                                .textColor = {167, 139, 250, 255},
                                .fontSize = 14
                            }));
                        }
                    }
                }

                // Galería de Capturas 16:9
                CLAY_TEXT(CLAY_STRING("Capturas de pantalla"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 20 }));
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(160) },
                        .childGap = 16,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    }
                }) {
                    for (int sc = 0; sc < 4; ++sc) {
                        std::shared_ptr<const luna::util::DecodedImage> scImg;
                        if (sc < static_cast<int>(props.game.screenshotUrls.size())) {
                            const auto& scUrl = props.game.screenshotUrls[sc];
                            luna::util::ImageLoader::instance().requestImage(scUrl, luna::util::ImagePreset::ScreenshotThumbnail);
                            scImg = luna::util::ImageLoader::instance().getImage(scUrl, luna::util::ImagePreset::ScreenshotThumbnail);
                        }
                        bool scReady = scImg && scImg->ready && !scImg->rgba.empty() && scImg->width > 0 && scImg->height > 0;
                        CLAY_AUTO_ID({
                            .layout = { .sizing = { CLAY_SIZING_FIXED(260), CLAY_SIZING_FIXED(150) } },
                            .backgroundColor = scReady ? Clay_Color{0, 0, 0, 0} : Clay_Color{ 34, 25, 54, 255 },
                            .cornerRadius = CLAY_CORNER_RADIUS(10),
                            .image = scReady ? Clay_ImageElementConfig{ .imageData = clay.allocDecodedImage(scImg) } : Clay_ImageElementConfig{},
                            .border = { .color = { 55, 42, 84, 255 }, .width = LUNA_BORDER(1) }
                        }) {}
                    }
                }

                // Juegos similares
                if (!props.game.similarGames.empty()) {
                    CLAY_TEXT(CLAY_STRING("Juegos similares"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 20 }));
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(210) },
                            .childGap = 16,
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        }
                    }) {
                        for (size_t sim = 0; sim < props.game.similarGames.size() && sim < 6; ++sim) {
                            const auto& sg = props.game.similarGames[sim];
                            bool simFoc = (props.detailZone == 2 && props.similarSelectedIdx == static_cast<int>(sim));
                            renderGameCard(disp, clay, sg, simFoc, /*isVertical=*/true, 140, 200, props.frameCount);
                        }
                    }
                }
            }
        }
    }

    Clay_RenderCommandArray commands = Clay_EndLayout(0.0f);
    clay.renderCommands(commands);
    disp.present();
}

void LunaClayUI::renderSettingsScreen(DisplayManager& disp, const SettingsScreenProps& props) {
    disp.clear(0xFF0E0B16);
    auto& clay = ClayRenderer::instance();
    clay.beginFrame();

    static const char* kTitles[10] = {
        "Mandos", "Cuentas vinculadas", "Mi cuenta", "Control parental",
        "Calidad de video", "Idioma", "Datos respaldados", "Descargar Luna",
        "Ayuda y comentarios", "Acerca de y juridicos"
    };

    CLAY(CLAY_ID("SettingsRoot"), {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .backgroundColor = { 14, 11, 22, 255 }
    }) {
        CLAY(CLAY_ID("TopBar"), {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(90) },
                        .padding = { 50, 50, 0, 0 },
                        .childGap = 16,
                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT },
            .backgroundColor = { 24, 18, 38, 255 }
        }) {
            CLAY_TEXT(ClayRenderer::toClayString("Ajustes y Configuracion de Amazon Luna"),
                      CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontId = 1, .fontSize = 28 }));
        }
        CLAY(CLAY_ID("Sep"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(2) } },
                              .backgroundColor = { 145, 70, 255, 255 } }) {}

        CLAY(CLAY_ID("Body"), {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                        .padding = { 50, 50, 25, 25 },
                        .childGap = 30,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT }
        }) {
            CLAY(CLAY_ID("List"), {
                .layout = { .sizing = { CLAY_SIZING_FIXED(750), CLAY_SIZING_GROW(0) },
                            .childGap = 12,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM }
            }) {
                for (int i = 0; i < 10; ++i) {
                    bool focused = (props.selectedItemIdx == i);
                    Clay_Color bg = focused ? Clay_Color{ 145, 70, 255, 255 } : Clay_Color{ 28, 21, 46, 255 };
                    Clay_Color border = focused ? Clay_Color{ 255, 255, 255, 255 } : Clay_Color{ 45, 34, 72, 255 };
                    CLAY(CLAY_IDI("SettingRow", i), {
                        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(78) },
                                    .padding = { 18, 18, 12, 12 },
                                    .childGap = 12,
                                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                                    .layoutDirection = CLAY_LEFT_TO_RIGHT },
                        .backgroundColor = bg,
                        .cornerRadius = CLAY_CORNER_RADIUS(12),
                        .border = { .color = border, .width = { (uint16_t)(focused ? 3 : 1), (uint16_t)(focused ? 3 : 1), (uint16_t)(focused ? 3 : 1), (uint16_t)(focused ? 3 : 1), 0 } }
                    }) {
                        CLAY(CLAY_IDI("RowText", i), {
                            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                                        .childGap = 2,
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(kTitles[i]),
                                      CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontId = 1, .fontSize = 16 }));
                        }
                    }
                }
            }

            CLAY(CLAY_ID("DetailPanel"), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                            .padding = { 40, 40, 32, 32 },
                            .childGap = 16,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM },
                .backgroundColor = { 22, 16, 36, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(16),
                .border = { .color = { 45, 34, 72, 255 }, .width = { 1, 1, 1, 1, 0 } }
            }) {
                CLAY_TEXT(ClayRenderer::toClayString(kTitles[props.selectedItemIdx < 0 || props.selectedItemIdx > 9 ? 0 : props.selectedItemIdx]),
                          CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontId = 1, .fontSize = 28 }));
                CLAY(CLAY_ID("DetailSep"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } },
                                            .backgroundColor = { 145, 70, 255, 255 } }) {}
                CLAY_TEXT(ClayRenderer::toClayString(props.statusText.empty() ? std::string("Listo") : props.statusText),
                          CLAY_TEXT_CONFIG({ .textColor = { 203, 213, 225, 255 }, .fontSize = 16 }));
            }
        }
    }

    Clay_RenderCommandArray commands = Clay_EndLayout(0.0f);
    clay.renderCommands(commands);
    disp.present();
}
void LunaClayUI::renderErrorScreen(DisplayManager& disp, const ErrorScreenProps& props) {
    disp.clear(0xFF0E0B16);
    auto& clay = ClayRenderer::instance();
    clay.beginFrame();

    CLAY(CLAY_ID("ErrorRoot"), {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .childGap = 24,
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .backgroundColor = { 14, 11, 22, 255 }
    }) {
        CLAY(CLAY_ID("IconBox"), {
            .layout = { .sizing = { CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(120) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
            .backgroundColor = { 147, 0, 10, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(60)
        }) {
            CLAY_TEXT(ClayRenderer::toClayString("!"),
                      CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontId = 1, .fontSize = 64 }));
        }
        CLAY(CLAY_ID("Title"), {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(0) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }
        }) {
            CLAY_TEXT(ClayRenderer::toClayString("Error de conexion"),
                      CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontId = 1, .fontSize = 40 }));
        }
        CLAY(CLAY_ID("Msg"), {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(0) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }
        }) {
            CLAY_TEXT(ClayRenderer::toClayString(props.errorMessage.empty() ? std::string("No se pudo conectar a Amazon Luna. Verifica tu conexion a internet.") : props.errorMessage),
                      CLAY_TEXT_CONFIG({ .textColor = { 203, 213, 225, 255 }, .fontSize = 18 }));
        }
        if (!props.errorCode.empty()) {
            CLAY(CLAY_ID("Code"), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(0) },
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }
            }) {
                CLAY_TEXT(ClayRenderer::toClayString("Codigo: " + props.errorCode),
                          CLAY_TEXT_CONFIG({ .textColor = { 148, 163, 184, 255 }, .fontSize = 14 }));
            }
        }
        CLAY(CLAY_ID("Buttons"), {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(0) },
                        .childGap = 20,
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                        .layoutDirection = CLAY_LEFT_TO_RIGHT }
        }) {
            CLAY(CLAY_ID("RetryBtn"), {
                .layout = { .sizing = { CLAY_SIZING_FIXED(260), CLAY_SIZING_FIXED(60) },
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                .backgroundColor = { 145, 70, 255, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(30)
            }) {
                CLAY_TEXT(ClayRenderer::toClayString("Reintentar"),
                          CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontId = 1, .fontSize = 18 }));
            }
            CLAY(CLAY_ID("BackBtn"), {
                .layout = { .sizing = { CLAY_SIZING_FIXED(260), CLAY_SIZING_FIXED(60) },
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                .backgroundColor = { 28, 21, 46, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(30),
                .border = { .color = { 45, 34, 72, 255 }, .width = { 1, 1, 1, 1, 0 } }
            }) {
                CLAY_TEXT(ClayRenderer::toClayString("Volver al inicio"),
                          CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontId = 1, .fontSize = 18 }));
            }
        }
        CLAY(CLAY_ID("Status"), {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(0) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }
        }) {
            CLAY_TEXT(ClayRenderer::toClayString(props.statusText.empty() ? std::string("Listo") : props.statusText),
                      CLAY_TEXT_CONFIG({ .textColor = { 148, 163, 184, 255 }, .fontSize = 12 }));
        }
    }

    Clay_RenderCommandArray commands = Clay_EndLayout(0.0f);
    clay.renderCommands(commands);
    disp.present();
}
} // namespace luna::display
