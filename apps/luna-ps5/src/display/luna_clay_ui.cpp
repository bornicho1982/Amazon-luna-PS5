#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wreorder-init-list"

#define LUNA_BORDER(w) { static_cast<uint16_t>(w), static_cast<uint16_t>(w), static_cast<uint16_t>(w), static_cast<uint16_t>(w), static_cast<uint16_t>(w) }

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
        .backgroundColor = imgReady ? Clay_Color{ 0, 0, 0, 0 } : Clay_Color{ 36, 27, 59, 255 },
        .cornerRadius = CLAY_CORNER_RADIUS(12),
        .image = imgReady ? Clay_ImageElementConfig{ .imageData = clay.allocDecodedImage(img) } : Clay_ImageElementConfig{},
        .border = {
            .color = isSel ? Clay_Color{ 255, 255, 255, 255 } : (imgReady ? Clay_Color{ 0, 0, 0, 0 } : Clay_Color{ 55, 42, 84, 255 }),
            .width = LUNA_BORDER(isSel ? 3 : 1)
        }
    }) {
        if (!imgReady) {
            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(tile.title, 22)), CLAY_TEXT_CONFIG({
                .textColor = { 203, 213, 225, 255 },
                .fontSize = 15
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
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                            .childGap = 16,
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
        scrollY = (clampedRow - 1) * 360;
    }

    clay.beginFrame();

    CLAY(CLAY_ID("RootContainer"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(1080) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { 7, 9, 17, 255 } // Official PS5 Night Blue #070911
    }) {
        // --- 3.1 TOP BAR FIJA ESTILO PS5 OS (1920 x 68 px) ---
        CLAY(CLAY_ID("TopNavBar"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(68) },
                .padding = { 48, 48, 0, 0 },
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .childGap = 32,
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            },
            .backgroundColor = { 10, 14, 24, 245 }, // PS5 Dark Crystal #0A0E18
            .border = { .color = { 34, 44, 66, 140 }, .width = { .bottom = 1 } }
        }) {
            // Icono Luna
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(40) },
                    .childGap = 10,
                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(36), CLAY_SIZING_FIXED(36) } },
                    .custom = { .customData = clay.allocCustom(CustomElementType::LunaLogoIcon) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Luna"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 24 }));
            }

            // Tabs centrales de Navegacion escoltadas por Bumpers graficos [L1] y [R1]
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48) },
                    .childGap = 16,
                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                // Silueta gráfica real L1
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(42), CLAY_SIZING_FIXED(24) } },
                    .custom = { .customData = clay.allocBumper(false, false) }
                }) {}

                for (size_t t = 0; t < props.navTabs.size(); ++t) {
                    bool isTabActive = (props.activeTabIdx == static_cast<int>(t));
                    bool isTabFoc = (props.focusZone == 0 && props.navColIdx == static_cast<int>(t));

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(40) },
                            .padding = { 16, 16, 0, 0 },
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        },
                        .backgroundColor = isTabFoc ? Clay_Color{255, 255, 255, 255} : (isTabActive ? Clay_Color{26, 33, 51, 255} : Clay_Color{0, 0, 0, 0}),
                        .cornerRadius = CLAY_CORNER_RADIUS(20),
                        .border = { .color = isTabFoc ? Clay_Color{0, 112, 209, 255} : (isTabActive ? Clay_Color{44, 54, 80, 255} : Clay_Color{0, 0, 0, 0}), .width = LUNA_BORDER(isTabFoc ? 2 : 1) }
                    }) {
                        CLAY_TEXT(ClayRenderer::toClayString(props.navTabs[t]), CLAY_TEXT_CONFIG({
                            .textColor = isTabFoc ? Clay_Color{7, 9, 17, 255} : (isTabActive ? Clay_Color{240, 244, 255, 255} : Clay_Color{148, 163, 184, 255}),
                            .fontSize = 17
                        }));
                    }
                }

                // Silueta gráfica real R1
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(42), CLAY_SIZING_FIXED(24) } },
                    .custom = { .customData = clay.allocBumper(true, false) }
                }) {}
            }

            // Boton Buscar con icono grafico Lupa
            bool searchBtnFoc = (props.focusZone == 0 && props.navColIdx == static_cast<int>(props.navTabs.size()));
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIXED(116), CLAY_SIZING_FIXED(40) },
                    .padding = { 12, 14, 0, 0 },
                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                    .childGap = 8,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                },
                .backgroundColor = searchBtnFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                .cornerRadius = CLAY_CORNER_RADIUS(20),
                .border = { .color = searchBtnFoc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(searchBtnFoc ? 2 : 1) }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(18), CLAY_SIZING_FIXED(18) } },
                    .custom = { .customData = clay.allocInterfaceIcon(DualSenseBtn::Search, searchBtnFoc ? 0xFF070911 : 0xFFCBD5E1) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Buscar"), CLAY_TEXT_CONFIG({
                    .textColor = searchBtnFoc ? Clay_Color{7, 9, 17, 255} : Clay_Color{203, 213, 225, 255},
                    .fontSize = 15
                }));
            }

            // Avatar de Usuario con Dropdown Menu
            bool avatarFoc = (props.focusZone == 0 && props.navColIdx == static_cast<int>(props.navTabs.size()) + 1);
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(42) },
                    .padding = { 8, 14, 0, 0 },
                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                    .childGap = 10,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                },
                .backgroundColor = avatarFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                .cornerRadius = CLAY_CORNER_RADIUS(21),
                .border = { .color = avatarFoc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(avatarFoc ? 2 : 1) }
            }) {
                // Avatar circle
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(28) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                    .backgroundColor = { 0, 112, 209, 255 }, // PS5 Blue
                    .cornerRadius = CLAY_CORNER_RADIUS(14)
                }) {
                    CLAY_TEXT(CLAY_STRING("B"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 16 }));
                }

                CLAY_TEXT(ClayRenderer::toClayString(props.profile.displayName), CLAY_TEXT_CONFIG({
                    .textColor = avatarFoc ? Clay_Color{7, 9, 17, 255} : Clay_Color{240, 244, 255, 255},
                    .fontSize = 15
                }));

                CLAY_TEXT(CLAY_STRING("\xE2\x96\xBE"), CLAY_TEXT_CONFIG({
                    .textColor = avatarFoc ? Clay_Color{7, 9, 17, 255} : Clay_Color{148, 163, 184, 255},
                    .fontSize = 12
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
                    // Columna Izquierda (880 px): Metadatos, Sinopsis real y Botones de acción PS5
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIXED(880), CLAY_SIZING_FIXED(430) },
                            .padding = { 36, 24, 28, 28 },
                            .childGap = 12,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        }
                    }) {
                        // Fila 1: Píldoras de estado (Suscripción + Distribuidora + Sello Oficial PEGI)
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(32) },
                                .childGap = 12,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            // Chip de suscripción
                            std::string subText = "INCLUIDO CON PRIME";
                            Clay_Color subBg = { 0, 112, 209, 255 }; // PS5 Blue #0070D1
                            Clay_Color subFg = { 255, 255, 255, 255 };
                            if (props.activeTabIdx == 1) {
                                subText = "GAMENIGHT";
                                subBg = { 109, 40, 217, 255 };
                            } else if (activeGame->channelSubtext.find("Luna+") != std::string::npos || activeGame->category == "LUNA+") {
                                subText = "LUNA+";
                                subBg = { 99, 102, 241, 255 };
                            }
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(26) }, .padding = { 10, 10, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                .backgroundColor = subBg,
                                .cornerRadius = CLAY_CORNER_RADIUS(6)
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(subText), CLAY_TEXT_CONFIG({ .textColor = subFg, .fontSize = 12 }));
                            }

                            // Chip Distribuidora real
                            std::string pub = activeGame->publisher.empty() ? activeGame->developer : activeGame->publisher;
                            if (pub.empty() || pub == "Amazon Luna") pub = "SEGA";
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(26) }, .padding = { 10, 10, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                .backgroundColor = { 24, 32, 50, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(6),
                                .border = { .color = { 44, 54, 80, 255 }, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(pub), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 12 }));
                            }

                            // Sello Oficial PEGI reglamentario por colores (3, 7 verde; 12, 16 naranja; 18 rojo)
                            int pegiAge = 12;
                            std::string pegiStr = activeGame->pegiRating;
                            if (pegiStr.find("18") != std::string::npos) pegiAge = 18;
                            else if (pegiStr.find("16") != std::string::npos) pegiAge = 16;
                            else if (pegiStr.find("7") != std::string::npos) pegiAge = 7;
                            else if (pegiStr.find("3") != std::string::npos) pegiAge = 3;

                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIXED(52), CLAY_SIZING_FIXED(26) } },
                                .custom = { .customData = clay.allocPegiBadge(pegiAge) }
                            }) {}
                        }

                        // Fila 2: Título o Logo del Juego (38 px)
                        std::shared_ptr<const luna::util::DecodedImage> logoImg;
                        if (!activeGame->logoUrl.empty()) {
                            luna::util::ImageLoader::instance().requestImage(activeGame->logoUrl, luna::util::ImagePreset::ProductLogo);
                            logoImg = luna::util::ImageLoader::instance().getImage(activeGame->logoUrl, luna::util::ImagePreset::ProductLogo);
                        }
                        if (logoImg && logoImg->ready && !logoImg->rgba.empty() && logoImg->width > 0 && logoImg->height > 0) {
                            float lw = static_cast<float>(std::min(380, logoImg->width));
                            float lh = static_cast<float>(std::min(76, logoImg->height));
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIXED(lw), CLAY_SIZING_FIXED(lh) } },
                                .image = { .imageData = clay.allocDecodedImage(logoImg) }
                            }) {}
                        } else {
                            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(activeGame->title, 28)), CLAY_TEXT_CONFIG({
                                .textColor = {255, 255, 255, 255},
                                .fontSize = 38
                            }));
                        }

                        // Fila 3: Píldoras de Género y Jugadores estilo PS5 (#182032)
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30) },
                                .childGap = 10,
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            std::string gen = activeGame->genres.empty() ? "Acción • Aventura" : activeGame->genres;
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(24) }, .padding = { 10, 10, 0, 0 } },
                                .backgroundColor = { 24, 32, 50, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(6),
                                .border = { .color = { 44, 54, 80, 255 }, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(gen), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 12 }));
                            }

                            std::string pl = activeGame->playerCount.empty() ? "1-4 jugadores" : activeGame->playerCount;
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(24) }, .padding = { 10, 10, 0, 0 } },
                                .backgroundColor = { 24, 32, 50, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(6),
                                .border = { .color = { 44, 54, 80, 255 }, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(pl), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 12 }));
                            }

                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(24) }, .padding = { 10, 10, 0, 0 } },
                                .backgroundColor = { 24, 32, 50, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(6),
                                .border = { .color = { 44, 54, 80, 255 }, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(CLAY_STRING("DualSense Vibracion"), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 12 }));
                            }
                        }

                        // Fila 4: Sinopsis real oficial (máx 3 líneas, eliminando cualquier frase inventada)
                        std::string syn = activeGame->synopsis;
                        if (syn.empty() || syn.find("Disfruta de ") == 0) {
                            syn = "Disponible en Amazon Luna Cloud con graficos en alta definicion y soporte completo para el mando DualSense.";
                        }
                        CLAY_AUTO_ID({
                            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(74) } }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(syn, 180)), CLAY_TEXT_CONFIG({
                                .textColor = {148, 163, 184, 255},
                                .fontSize = 15,
                                .lineHeight = 23
                            }));
                        }

                        // Fila 5: Los 3 Botones de Consola PS5 con Iconos Gráficos DualSense (✕, □, △)
                        bool btn0Foc = isHeroActive && (props.heroBtnIdx == 0);
                        bool btn1Foc = isHeroActive && (props.heroBtnIdx == 1);
                        bool btn2Foc = isHeroActive && (props.heroBtnIdx == 2);

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52) },
                                .childGap = 16,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            // Botón 1: (Icono Gráfico ✕) Jugar ahora
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(210), CLAY_SIZING_FIXED(50) },
                                    .padding = { 18, 18, 0, 0 },
                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                    .childGap = 12,
                                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                                },
                                .backgroundColor = btn0Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                                .cornerRadius = CLAY_CORNER_RADIUS(25),
                                .border = { .color = btn0Foc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btn0Foc ? 2 : 1) }
                            }) {
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) } },
                                    .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Cross, btn0Foc) }
                                }) {}
                                CLAY_TEXT(CLAY_STRING("Jugar ahora"), CLAY_TEXT_CONFIG({
                                    .textColor = btn0Foc ? Clay_Color{7, 9, 17, 255} : Clay_Color{255, 255, 255, 255},
                                    .fontSize = 17
                                }));
                            }

                            // Botón 2: (Icono Gráfico □) Ficha del juego
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(50) },
                                    .padding = { 18, 18, 0, 0 },
                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                    .childGap = 12,
                                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                                },
                                .backgroundColor = btn1Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                                .cornerRadius = CLAY_CORNER_RADIUS(25),
                                .border = { .color = btn1Foc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btn1Foc ? 2 : 1) }
                            }) {
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) } },
                                    .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Square, btn1Foc) }
                                }) {}
                                CLAY_TEXT(CLAY_STRING("Ficha del juego"), CLAY_TEXT_CONFIG({
                                    .textColor = btn1Foc ? Clay_Color{7, 9, 17, 255} : Clay_Color{240, 244, 255, 255},
                                    .fontSize = 16
                                }));
                            }

                            // Botón 3: (Icono Gráfico △) Favorito
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(180), CLAY_SIZING_FIXED(50) },
                                    .padding = { 18, 18, 0, 0 },
                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                    .childGap = 12,
                                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                                },
                                .backgroundColor = btn2Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{26, 33, 51, 255},
                                .cornerRadius = CLAY_CORNER_RADIUS(25),
                                .border = { .color = btn2Foc ? Clay_Color{0, 112, 209, 255} : Clay_Color{44, 54, 80, 255}, .width = LUNA_BORDER(btn2Foc ? 2 : 1) }
                            }) {
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) } },
                                    .custom = { .customData = clay.allocControllerButton(DualSenseBtn::Triangle, btn2Foc) }
                                }) {}
                                CLAY_TEXT(CLAY_STRING("Favorito"), CLAY_TEXT_CONFIG({
                                    .textColor = btn2Foc ? Clay_Color{7, 9, 17, 255} : Clay_Color{240, 244, 255, 255},
                                    .fontSize = 16
                                }));
                            }
                        }
                    }

                    // Columna Derecha (944 px): Tráiler en Vídeo MP4 o Arte Panorámico 16:9 con fundido suave de 300 px
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIXED(944), CLAY_SIZING_FIXED(430) },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        },
                        .custom = { .customData = clay.allocCustom(CustomElementType::SpotlightArt, "", 0xFFFFFFFF, activeGame, props.frameCount) }
                    }) {}
                }
            }

            // =================================================================
            // SWIMLANES Y CARROUSELES DE JUEGOS
            // =================================================================
            size_t startRow = hasHero ? 1 : 0;
            for (size_t r = startRow; r < props.collections.size(); ++r) {
                const auto& row = props.collections[r];
                bool isFocusedRow = (props.focusZone != 0 && clampedRow == static_cast<int>(r));
                int selectedCol = (r < props.rowSelectedCol.size()) ? props.rowSelectedCol[r] : 0;

                // --- CASO 1: CATEGORY CAROUSEL (15 TARJETAS CUADRADAS CON ICONO VECTORIAL) ---
                if (row.isCategoryCarousel) {
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(220) },
                            .childGap = 14,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        }
                    }) {
                        CLAY_TEXT(CLAY_STRING("Explorar categorias"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 24 }));

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(168) },
                                .childGap = 20,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            for (size_t c = 0; c < row.items.size(); ++c) {
                                bool isSel = isFocusedRow && (static_cast<int>(c) == selectedCol);
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(168), CLAY_SIZING_FIXED(148) },
                                        .padding = { 12, 12, 16, 16 },
                                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                        .childGap = 14,
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                                    },
                                    .backgroundColor = isSel ? Clay_Color{44, 38, 64, 255} : Clay_Color{33, 29, 48, 255},
                                    .cornerRadius = CLAY_CORNER_RADIUS(12),
                                    .border = { .color = isSel ? Clay_Color{255, 255, 255, 255} : Clay_Color{53, 47, 72, 255}, .width = LUNA_BORDER(isSel ? 3 : 1) }
                                }) {
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) } },
                                        .custom = { .customData = clay.allocCustom(CustomElementType::CategoryIcon, row.items[c].title, 0xFFFFFFFF) }
                                    }) {}

                                    CLAY_TEXT(ClayRenderer::toClayString(row.items[c].title), CLAY_TEXT_CONFIG({
                                        .textColor = {255, 255, 255, 255},
                                        .fontSize = 14
                                    }));
                                }
                            }
                        }
                    }
                    continue;
                }

                // --- CASO 2: BANNERS COMPUESTOS CON ARTE REAL ("Personajes emblemáticos" y "Jugad juntos") ---
                if (row.isPartnerRow) {
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(480) },
                            .padding = { 32, 32, 24, 24 },
                            .childGap = 20,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        },
                        .backgroundColor = { 24, 18, 38, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(16),
                        .border = { .color = isFocusedRow ? Clay_Color{145, 70, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(isFocusedRow ? 2 : 1) }
                    }) {
                        // Header del Banner con Arte Panorámico y Botón "Ver todos los juegos"
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(70) },
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .childGap = 16,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) },
                                    .childGap = 6,
                                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                                }
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(row.title), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 26 }));
                                CLAY_TEXT(ClayRenderer::toClayString(row.subtitle.empty() ? "Descubre los mejores titulos seleccionados para ti." : row.subtitle), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 15 }));
                            }

                            // Botón "Ver todos los juegos"
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(42) },
                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                                },
                                .backgroundColor = { 37, 28, 59, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(21),
                                .border = { .color = { 76, 29, 149, 255 }, .width = LUNA_BORDER(1) }
                            }) {
                                CLAY_TEXT(CLAY_STRING("Ver todos los juegos"), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 14 }));
                            }
                        }

                        // Juegos superpuestos con carátulas verticales reales (sin duplicados)
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(330) },
                                .childGap = 20,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            std::unordered_set<std::string> seenIds;
                            int displayedCount = 0;
                            for (size_t i = 0; i < row.items.size() && displayedCount < 7; ++i) {
                                const auto& item = row.items[i];
                                std::string uniqueKey = item.id.empty() ? item.title : item.id;
                                if (seenIds.count(uniqueKey)) continue;
                                seenIds.insert(uniqueKey);
                                displayedCount++;

                                bool isSel = isFocusedRow && (displayedCount - 1 == selectedCol);
                                renderGameCard(disp, clay, item, isSel, /*isVertical=*/true, 190, 270, props.frameCount);
                            }
                        }
                    }
                    continue;
                }

                // --- CASO 3: RANKINGS (TOP 10 CON NUMEROS GIGANTES AL LADO) ---
                if (row.isRankings) {
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(360) },
                            .childGap = 14,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        }
                    }) {
                        CLAY_TEXT(ClayRenderer::toClayString(row.title), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 24 }));

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(310) },
                                .childGap = 24,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            for (size_t i = 0; i < row.items.size() && i < 10; ++i) {
                                const auto& item = row.items[i];
                                bool isSel = isFocusedRow && (static_cast<int>(i) == selectedCol);

                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(250), CLAY_SIZING_FIXED(300) },
                                        .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                        .childGap = 8,
                                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                                    }
                                }) {
                                    // Numero gigante
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(50), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } }
                                    }) {
                                        CLAY_TEXT(ClayRenderer::toClayString(std::to_string(i + 1)), CLAY_TEXT_CONFIG({
                                            .textColor = isSel ? Clay_Color{252, 211, 77, 255} : Clay_Color{203, 213, 225, 255},
                                            .fontSize = 54
                                        }));
                                    }

                                    // Tarjeta de juego con carátula real
                                    renderGameCard(disp, clay, item, isSel, /*isVertical=*/true, 190, 260, props.frameCount);
                                }
                            }
                        }
                    }
                    continue;
                }

                // --- CASO 4: JUGADOS RECIENTEMENTE 16:9 (MI ESPACIO) ---
                if (row.isRecentlyPlayed16_9) {
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
                                .childGap = 20,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            for (size_t i = 0; i < row.items.size(); ++i) {
                                const auto& item = row.items[i];
                                bool isSel = isFocusedRow && (static_cast<int>(i) == selectedCol);
                                std::string dateSub = item.lastPlayedDate.empty() ? "30 oct" : item.lastPlayedDate;
                                renderGameCard(disp, clay, item, isSel, /*isVertical=*/false, 290, 164, props.frameCount, dateSub);
                            }
                        }
                    }
                    continue;
                }

                // --- CASO 5: FILA ESTANDAR DE JUEGOS VERTICALES ---
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(330) },
                        .childGap = 14,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    }
                }) {
                    CLAY_TEXT(ClayRenderer::toClayString(row.title), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 24 }));

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(280) },
                            .childGap = 20,
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        }
                    }) {
                        for (size_t i = 0; i < row.items.size(); ++i) {
                            const auto& item = row.items[i];
                            bool isSel = isFocusedRow && (static_cast<int>(i) == selectedCol);
                            renderGameCard(disp, clay, item, isSel, /*isVertical=*/true, 190, 270, props.frameCount);
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
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .childGap = 28,
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
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .childGap = 16,
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
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .childGap = 12,
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
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .childGap = 12,
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
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .childGap = 12,
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
                        .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                        .childGap = 16,
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

} // namespace luna::display
