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

} // namespace

void LunaClayUI::renderHomeScreen(DisplayManager& disp, const HomeScreenProps& props) {
    disp.clear(0xFF0E0B16); // Cosmic Deep official background
    auto& clay = ClayRenderer::instance();

    // 1. VISTA DE BUSQUEDA
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

            // Results Grid (5 columns 16:9 cards)
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

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(200) },
                                .childGap = 8,
                                .layoutDirection = CLAY_TOP_TO_BOTTOM
                            }
                        }) {
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(180) } },
                                .backgroundColor = { 36, 27, 59, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(10),
                                .border = { .color = isSel ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(isSel ? 3 : 1) },
                                .custom = { .customData = clay.allocCustom(CustomElementType::Mp4PreviewPlayer, "", 0xFFFFFFFF, &tile, props.frameCount) }
                            }) {}
                            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(tile.title, 26)), CLAY_TEXT_CONFIG({
                                .textColor = {255, 255, 255, 255},
                                .fontSize = 16
                            }));
                        }
                    }
                }
            }
        }
        Clay_RenderCommandArray commands = Clay_EndLayout(0.0f);
        clay.renderCommands(commands);
        disp.present();
        return;
    }

    // 2. REMOTE PLAY TAB (activeTabIdx == 3)
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
                    .padding = { 32, 32, 16, 16 },
                    .childGap = 8,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = { 24, 18, 38, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(16),
                .border = { .color = { 45, 34, 72, 255 }, .width = LUNA_BORDER(1) }
            }) {
                CLAY_TEXT(CLAY_STRING("Remote Play"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 32 }));
                CLAY_TEXT(CLAY_STRING("Juega con tus amigos esteis donde esteis. Compartid partidas multijugador y cooperativas al instante sin coste adicional."), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 16 }));
            }

            // 2 Columns: Host Card & Guest Card
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(480) },
                    .childGap = 32,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                // Host Card
                bool hostFoc = (props.focusZone != 0 && props.selectedRowIdx == 0 && (!props.rowSelectedCol.empty() && props.rowSelectedCol[0] == 0));
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_PERCENT(0.5f), CLAY_SIZING_GROW(0) },
                        .padding = { 32, 32, 28, 28 },
                        .childGap = 16,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = { 20, 15, 34, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(16),
                    .border = { .color = hostFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(hostFoc ? 3 : 1) }
                }) {
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(130), CLAY_SIZING_FIXED(32) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = { 59, 27, 104, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                    }) {
                        CLAY_TEXT(CLAY_STRING("ANFITRION"), CLAY_TEXT_CONFIG({ .textColor = {233, 213, 255, 255}, .fontSize = 16 }));
                    }
                    CLAY_TEXT(CLAY_STRING("Crear una partida"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 26 }));
                    CLAY_TEXT(CLAY_STRING("1. Genera un codigo unico de acceso de 6 digitos.\n2. Enviaselo a tus amigos (hasta 8 jugadores).\n3. Inicia cualquier juego compatible de tu biblioteca."), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 16, .lineHeight = 26 }));

                    // Code box
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(80) },
                            .padding = { 24, 24, 0, 0 },
                            .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                            .childGap = 20,
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        },
                        .backgroundColor = { 34, 23, 59, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(12),
                        .border = { .color = { 76, 29, 149, 255 }, .width = LUNA_BORDER(1) }
                    }) {
                        CLAY_TEXT(CLAY_STRING("CODIGO:"), CLAY_TEXT_CONFIG({ .textColor = {167, 139, 250, 255}, .fontSize = 16 }));
                        CLAY_TEXT(CLAY_STRING("2 9 9 9 8 3"), CLAY_TEXT_CONFIG({ .textColor = {252, 211, 77, 255}, .fontSize = 32 }));
                        CLAY_TEXT(CLAY_STRING("Sala activa lista para jugar"), CLAY_TEXT_CONFIG({ .textColor = {16, 185, 129, 255}, .fontSize = 16 }));
                    }

                    // Button
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(54) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = hostFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{121, 82, 236, 255},
                        .cornerRadius = CLAY_CORNER_RADIUS(27)
                    }) {
                        CLAY_TEXT(CLAY_STRING("Generar nuevo codigo"), CLAY_TEXT_CONFIG({
                            .textColor = hostFoc ? Clay_Color{14, 11, 22, 255} : Clay_Color{255, 255, 255, 255},
                            .fontSize = 18
                        }));
                    }
                }

                // Guest Card
                bool guestFoc = (props.focusZone != 0 && props.selectedRowIdx == 0 && (!props.rowSelectedCol.empty() && props.rowSelectedCol[0] == 1));
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_PERCENT(0.5f), CLAY_SIZING_GROW(0) },
                        .padding = { 32, 32, 28, 28 },
                        .childGap = 16,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = { 20, 15, 34, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(16),
                    .border = { .color = guestFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(guestFoc ? 3 : 1) }
                }) {
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(32) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = { 6, 78, 59, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                    }) {
                        CLAY_TEXT(CLAY_STRING("INVITADO"), CLAY_TEXT_CONFIG({ .textColor = {167, 243, 208, 255}, .fontSize = 16 }));
                    }
                    CLAY_TEXT(CLAY_STRING("Unirse a una partida"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 26 }));
                    CLAY_TEXT(CLAY_STRING("Introduce el codigo de 6 digitos facilitado por el anfitrion:"), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 16 }));

                    // Pin Boxes
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(76) },
                            .childGap = 14,
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        }
                    }) {
                        static const char* kDemoPin[] = {"2", "9", "9", "9", "8", "3"};
                        for (int p = 0; p < 6; ++p) {
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(74) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                                .backgroundColor = { 34, 23, 59, 255 },
                                .cornerRadius = CLAY_CORNER_RADIUS(10),
                                .border = { .color = guestFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{55, 65, 81, 255}, .width = LUNA_BORDER(2) }
                            }) {
                                CLAY_TEXT(ClayRenderer::toClayString(kDemoPin[p]), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 28 }));
                            }
                        }
                    }

                    // Button
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(54) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = guestFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{121, 82, 236, 255},
                        .cornerRadius = CLAY_CORNER_RADIUS(27)
                    }) {
                        CLAY_TEXT(CLAY_STRING("Unirme a la partida"), CLAY_TEXT_CONFIG({
                            .textColor = guestFoc ? Clay_Color{14, 11, 22, 255} : Clay_Color{255, 255, 255, 255},
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

    // 3. PANTALLA PRINCIPAL (INICIO, GAMENIGHT, MI ESPACIO)
    clay.beginFrame();

    // Calculo del scroll vertical suave
    int clampedRow = std::clamp(props.selectedRowIdx, 0, std::max(0, static_cast<int>(props.collections.size()) - 1));
    bool hasHero = (props.activeTabIdx != 2) && !props.collections.empty() && props.collections[0].isHeroRotator && !props.collections[0].items.empty();

    int scrollY = 0;
    if (clampedRow > 1) {
        scrollY = (clampedRow - 1) * 360;
    }

    CLAY(CLAY_ID("RootContainer"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(1080) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { 14, 11, 22, 255 }
    }) {
        // --- TOP BAR FIJA (1920 x 76 px) ---
        CLAY(CLAY_ID("TopBar"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(76) },
                .padding = { 56, 56, 0, 0 },
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            },
            .backgroundColor = { 15, 17, 24, 242 },
            .border = { .color = { 255, 255, 255, 20 }, .width = { .bottom = 1 } }
        }) {
            // Left: Logo + [L1]
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_GROW(0) },
                    .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                    .childGap = 12,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32) } },
                    .custom = { .customData = clay.allocCustom(CustomElementType::LunaLogoIcon) }
                }) {}
                CLAY_TEXT(CLAY_STRING("Luna"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 24 }));
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(36), CLAY_SIZING_FIXED(26) } },
                    .custom = { .customData = clay.allocCustom(CustomElementType::DualSenseHint, "L1", 0xFFCBD5E1) }
                }) {}
            }

            // Spacer
            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}

            // Center: Nav Tabs
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_GROW(0) },
                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                    .childGap = 12,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                for (size_t t = 0; t < props.navTabs.size(); ++t) {
                    bool isActive = (props.activeTabIdx == static_cast<int>(t));
                    bool isFoc = (props.focusZone == 0 && props.navColIdx == static_cast<int>(t));

                    Clay_Color bg = isActive ? (isFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{121, 82, 236, 255})
                                             : (isFoc ? Clay_Color{45, 34, 72, 255} : Clay_Color{0, 0, 0, 0});
                    Clay_Color borderCol = isFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{0, 0, 0, 0};

                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(40) },
                            .padding = { 20, 20, 0, 0 },
                            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                        },
                        .backgroundColor = bg,
                        .cornerRadius = CLAY_CORNER_RADIUS(20),
                        .border = { .color = borderCol, .width = LUNA_BORDER(isFoc ? 2 : 0) }
                    }) {
                        CLAY_TEXT(ClayRenderer::toClayString(props.navTabs[t]), CLAY_TEXT_CONFIG({
                            .textColor = isActive ? Clay_Color{255, 255, 255, 255} : Clay_Color{148, 163, 184, 255},
                            .fontSize = 17
                        }));
                    }
                }
            }

            // Spacer
            CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}

            // Right: [R1] + Search + Profile
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_GROW(0) },
                    .childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER },
                    .childGap = 16,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                }
            }) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(36), CLAY_SIZING_FIXED(26) } },
                    .custom = { .customData = clay.allocCustom(CustomElementType::DualSenseHint, "R1", 0xFFCBD5E1) }
                }) {}

                // Search Icon Button (navColIdx == 4)
                bool searchFoc = (props.focusZone == 0 && props.navColIdx == 4);
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) },
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                    },
                    .backgroundColor = searchFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{28, 21, 46, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(20),
                    .border = { .color = searchFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(searchFoc ? 2 : 1) }
                }) {
                    CLAY_TEXT(CLAY_STRING("Q"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 18 }));
                }

                // Profile Chip (navColIdx == 5)
                bool profFoc = (props.focusZone == 0 && props.navColIdx == 5);
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(44) },
                        .padding = { 10, 16, 0, 0 },
                        .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                        .childGap = 10,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = profFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{24, 18, 38, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(22),
                    .border = { .color = profFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(profFoc ? 2 : 1) }
                }) {
                    // Avatar circle
                    CLAY_AUTO_ID({
                        .layout = { .sizing = { CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(28) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = { 20, 184, 166, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(14)
                    }) {
                        CLAY_TEXT(CLAY_STRING("B"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 14 }));
                    }
                    CLAY_TEXT(ClayRenderer::toClayString(props.profile.displayName), CLAY_TEXT_CONFIG({
                        .textColor = {255, 255, 255, 255},
                        .fontSize = 16
                    }));
                }
            }
        }

        // --- CONTENIDO PRINCIPAL CON SCISSOR CLIPPING ---
        CLAY(CLAY_ID("MainScrollContainer"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(960) },
                .padding = { 48, 48, 16, 0 },
                .childGap = 36,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .clip = { .horizontal = false, .vertical = true, .childOffset = { 0, static_cast<float>(-scrollY) } }
        }) {
            // ==================== SPOTLIGHT DYNAMIC STAGE (1824 x 400 px) ====================
            if (hasHero) {
                const auto& heroCol = props.collections[0];
                int heroIdx = (!props.rowSelectedCol.empty()) ? props.rowSelectedCol[0] : 0;
                if (heroIdx < 0 || heroIdx >= static_cast<int>(heroCol.items.size())) heroIdx = 0;
                const auto& heroGame = heroCol.items[heroIdx];
                bool heroFocused = (props.focusZone != 0 && clampedRow == 0);

                CLAY(CLAY_ID("SpotlightStage"), {
                    .layout = {
                        .sizing = { CLAY_SIZING_FIXED(1824), CLAY_SIZING_FIXED(400) },
                        .padding = { 36, 36, 28, 28 },
                        .childGap = 36,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = { 20, 15, 36, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(16),
                    .border = { .color = heroFocused ? Clay_Color{121, 82, 236, 255} : Clay_Color{46, 34, 72, 255}, .width = LUNA_BORDER(heroFocused ? 2 : 1) }
                }) {
                    // Columna Izquierda (55% width ~ 960 px)
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_PERCENT(0.55f), CLAY_SIZING_GROW(0) },
                            .childGap = 12,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        }
                    }) {
                        // Badge Tag
                        std::string badgeText = (props.activeTabIdx == 1) ? "[GN] USA TELEFONOS COMO MANDOS" : "INCLUIDO CON PRIME";
                        CLAY_AUTO_ID({
                            .layout = { .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(28) }, .padding = { 12, 12, 0, 0 }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                            .backgroundColor = { 124, 58, 237, 255 },
                            .cornerRadius = CLAY_CORNER_RADIUS(6)
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(badgeText), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 13 }));
                        }

                        // Titulo
                        CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(heroGame.title, 32)), CLAY_TEXT_CONFIG({
                            .textColor = {255, 255, 255, 255},
                            .fontSize = 36
                        }));

                        // Metadata row
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(32) },
                                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                                .childGap = 14,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(26) } },
                                .custom = { .customData = clay.allocCustom(CustomElementType::PegiBadge, heroGame.pegiRating.empty() ? "PEGI 12" : heroGame.pegiRating) }
                            }) {}

                            std::string genres = heroGame.genres.empty() ? "Accion, Aventura" : heroGame.genres;
                            CLAY_TEXT(ClayRenderer::toClayString(genres), CLAY_TEXT_CONFIG({ .textColor = {203, 213, 225, 255}, .fontSize = 16 }));

                            std::string yr = heroGame.releaseYear.empty() ? "2024" : heroGame.releaseYear;
                            CLAY_TEXT(ClayRenderer::toClayString(yr), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 16 }));

                            CLAY_TEXT(CLAY_STRING("4.5 \xE2\x98\x85"), CLAY_TEXT_CONFIG({ .textColor = {245, 158, 11, 255}, .fontSize = 16 }));
                        }

                        // Synopsis (auto-wrap)
                        std::string syn = heroGame.synopsis.empty() ? "Disponible para jugar al instante en streaming de alta calidad en Amazon Luna Cloud." : heroGame.synopsis;
                        CLAY_AUTO_ID({
                            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(70) } }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(syn, 140)), CLAY_TEXT_CONFIG({
                                .textColor = {226, 232, 240, 255},
                                .fontSize = 17,
                                .lineHeight = 24
                            }));
                        }

                        // Buttons row
                        bool btn0Foc = (props.focusZone == 2) ? (props.heroBtnIdx == 0) : heroFocused;
                        bool btn1Foc = (props.focusZone == 2 && props.heroBtnIdx == 1);
                        bool btn2Foc = (props.focusZone == 2 && props.heroBtnIdx == 2);

                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(56) },
                                .childGap = 16,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            // Button 0: Jugar ahora
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(54) },
                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                    .childGap = 10,
                                    .layoutDirection = CLAY_LEFT_TO_RIGHT
                                },
                                .backgroundColor = btn0Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{121, 82, 236, 255},
                                .cornerRadius = CLAY_CORNER_RADIUS(27),
                                .border = { .color = btn0Foc ? Clay_Color{145, 70, 255, 255} : Clay_Color{0, 0, 0, 0}, .width = LUNA_BORDER(btn0Foc ? 3 : 0) }
                            }) {
                                CLAY_TEXT(CLAY_STRING("\xE2\x96\xB6  Jugar ahora"), CLAY_TEXT_CONFIG({
                                    .textColor = btn0Foc ? Clay_Color{14, 11, 22, 255} : Clay_Color{255, 255, 255, 255},
                                    .fontSize = 18
                                }));
                            }

                            // Button 1: Añadir a Biblioteca
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(54) },
                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                                },
                                .backgroundColor = btn1Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{34, 24, 56, 255},
                                .cornerRadius = CLAY_CORNER_RADIUS(27),
                                .border = { .color = btn1Foc ? Clay_Color{145, 70, 255, 255} : Clay_Color{55, 42, 84, 255}, .width = LUNA_BORDER(btn1Foc ? 3 : 1) }
                            }) {
                                CLAY_TEXT(CLAY_STRING("+ Biblioteca"), CLAY_TEXT_CONFIG({
                                    .textColor = btn1Foc ? Clay_Color{14, 11, 22, 255} : Clay_Color{203, 213, 225, 255},
                                    .fontSize = 18
                                }));
                            }

                            // Button 2: Ver detalles
                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(180), CLAY_SIZING_FIXED(54) },
                                    .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                                },
                                .backgroundColor = btn2Foc ? Clay_Color{255, 255, 255, 255} : Clay_Color{34, 24, 56, 255},
                                .cornerRadius = CLAY_CORNER_RADIUS(27),
                                .border = { .color = btn2Foc ? Clay_Color{145, 70, 255, 255} : Clay_Color{55, 42, 84, 255}, .width = LUNA_BORDER(btn2Foc ? 3 : 1) }
                            }) {
                                CLAY_TEXT(CLAY_STRING("Detalles"), CLAY_TEXT_CONFIG({
                                    .textColor = btn2Foc ? Clay_Color{14, 11, 22, 255} : Clay_Color{203, 213, 225, 255},
                                    .fontSize = 18
                                }));
                            }
                        }
                    }

                    // Columna Derecha (45% width ~ 760 px) - Artwork 16:9 o Video Player
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_PERCENT(0.45f), CLAY_SIZING_GROW(0) },
                            .childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                            .childGap = 12
                        }
                    }) {
                        CLAY_AUTO_ID({
                            .layout = { .sizing = { CLAY_SIZING_FIXED(640), CLAY_SIZING_FIXED(310) } },
                            .backgroundColor = { 24, 19, 38, 255 },
                            .cornerRadius = CLAY_CORNER_RADIUS(14),
                            .border = { .color = { 55, 42, 84, 255 }, .width = LUNA_BORDER(1) },
                            .custom = { .customData = clay.allocCustom(CustomElementType::Mp4PreviewPlayer, "", 0xFFFFFFFF, &heroGame, props.frameCount) }
                        }) {}

                        // Rotator dots
                        int totalDots = std::min(static_cast<int>(heroCol.items.size()), 6);
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_FIT(), CLAY_SIZING_FIXED(12) },
                                .childGap = 8,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            for (int d = 0; d < totalDots; ++d) {
                                bool isDot = (d == heroIdx);
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIXED(isDot ? 28.0f : 8.0f), CLAY_SIZING_FIXED(6) } },
                                    .backgroundColor = isDot ? Clay_Color{255, 255, 255, 255} : Clay_Color{148, 163, 184, 136},
                                    .cornerRadius = CLAY_CORNER_RADIUS(3)
                                }) {}
                            }
                        }
                    }
                }
            }

            // ==================== SWIMLANES Y CARROUSELES DE JUEGOS ====================
            size_t startRow = hasHero ? 1 : 0;
            for (size_t r = startRow; r < props.collections.size(); ++r) {
                const auto& row = props.collections[r];
                bool isFocusedRow = (props.focusZone != 0 && clampedRow == static_cast<int>(r));
                int selectedCol = (r < props.rowSelectedCol.size()) ? props.rowSelectedCol[r] : 0;

                // --- CASO 1: CATEGORY CAROUSEL (15 TARJETAS CUADRADAS CON ICONO VECTORIAL) ---
                if (row.isCategoryCarousel) {
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(230) },
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
                            int catScroll = std::max(0, (selectedCol - 4) * 188);
                            for (size_t c = 0; c < row.items.size(); ++c) {
                                bool isSel = isFocusedRow && (static_cast<int>(c) == selectedCol);
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(168), CLAY_SIZING_FIXED(148) },
                                        .padding = { 12, 12, 20, 16 },
                                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                                        .childGap = 16,
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

                // --- CASO 2: COMPOSITE BANNERS ("Personajes emblemáticos" y "Jugad juntos" 560 px, SIN DUPLICADOS) ---
                if (row.isPartnerRow) {
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(560) },
                            .padding = { 32, 32, 28, 28 },
                            .childGap = 28,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        },
                        .backgroundColor = { 24, 18, 38, 255 },
                        .cornerRadius = CLAY_CORNER_RADIUS(16),
                        .border = { .color = isFocusedRow ? Clay_Color{145, 70, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(isFocusedRow ? 2 : 1) }
                    }) {
                        // Header del Banner
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) },
                                .childGap = 8,
                                .layoutDirection = CLAY_TOP_TO_BOTTOM
                            }
                        }) {
                            CLAY_TEXT(ClayRenderer::toClayString(row.title), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 28 }));
                            CLAY_TEXT(ClayRenderer::toClayString(row.subtitle.empty() ? "Descubre los mejores titulos seleccionados para ti." : row.subtitle), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 16 }));
                        }

                        // Juegos superpuestos sin duplicados
                        CLAY_AUTO_ID({
                            .layout = {
                                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(380) },
                                .childGap = 24,
                                .layoutDirection = CLAY_LEFT_TO_RIGHT
                            }
                        }) {
                            std::unordered_set<std::string> seenIds;
                            int displayedCount = 0;
                            for (size_t i = 0; i < row.items.size() && displayedCount < 7; ++i) {
                                const auto& item = row.items[i];
                                std::string uniqueKey = item.id.empty() ? item.title : item.id;
                                if (seenIds.count(uniqueKey)) continue; // Evitar duplicados
                                seenIds.insert(uniqueKey);
                                displayedCount++;

                                bool isSel = isFocusedRow && (displayedCount - 1 == selectedCol);
                                CLAY_AUTO_ID({
                                    .layout = {
                                        .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(360) },
                                        .childGap = 10,
                                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                                    }
                                }) {
                                    CLAY_AUTO_ID({
                                        .layout = { .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(280) } },
                                        .backgroundColor = { 36, 27, 59, 255 },
                                        .cornerRadius = CLAY_CORNER_RADIUS(12),
                                        .border = { .color = isSel ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(isSel ? 3 : 1) },
                                        .custom = { .customData = clay.allocCustom(CustomElementType::Mp4PreviewPlayer, "", 0xFFFFFFFF, &item, props.frameCount) }
                                    }) {
                                        // Badge [GN]
                                        CLAY_AUTO_ID({
                                            .layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(24) } },
                                            .custom = { .customData = clay.allocCustom(CustomElementType::GoodiesBadge) }
                                        }) {}
                                    }

                                    CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(item.title, 20)), CLAY_TEXT_CONFIG({
                                        .textColor = {255, 255, 255, 255},
                                        .fontSize = 16
                                    }));
                                }
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
                            int rScroll = std::max(0, (selectedCol - 4) * 260);
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

                                    // Tarjeta de juego
                                    CLAY_AUTO_ID({
                                        .layout = {
                                            .sizing = { CLAY_SIZING_FIXED(190), CLAY_SIZING_FIXED(290) },
                                            .childGap = 8,
                                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                                        }
                                    }) {
                                        CLAY_AUTO_ID({
                                            .layout = { .sizing = { CLAY_SIZING_FIXED(190), CLAY_SIZING_FIXED(260) } },
                                            .backgroundColor = { 36, 27, 59, 255 },
                                            .cornerRadius = CLAY_CORNER_RADIUS(10),
                                            .border = { .color = isSel ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(isSel ? 3 : 1) },
                                            .custom = { .customData = clay.allocCustom(CustomElementType::Mp4PreviewPlayer, "", 0xFFFFFFFF, &item, props.frameCount) }
                                        }) {}

                                        CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(item.title, 18)), CLAY_TEXT_CONFIG({
                                            .textColor = {255, 255, 255, 255},
                                            .fontSize = 15
                                        }));
                                    }
                                }
                            }
                        }
                    }
                    continue;
                }

                // --- CASO 4: FILA ESTANDAR DE JUEGOS ---
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
                        int sScroll = std::max(0, (selectedCol - 4) * 214);
                        for (size_t i = 0; i < row.items.size(); ++i) {
                            const auto& item = row.items[i];
                            bool isSel = isFocusedRow && (static_cast<int>(i) == selectedCol);

                            CLAY_AUTO_ID({
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(190), CLAY_SIZING_FIXED(300) },
                                    .childGap = 8,
                                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                                }
                            }) {
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIXED(190), CLAY_SIZING_FIXED(260) } },
                                    .backgroundColor = { 36, 27, 59, 255 },
                                    .cornerRadius = CLAY_CORNER_RADIUS(10),
                                    .border = { .color = isSel ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(isSel ? 3 : 1) },
                                    .custom = { .customData = clay.allocCustom(CustomElementType::Mp4PreviewPlayer, "", 0xFFFFFFFF, &item, props.frameCount) }
                                }) {}

                                CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(item.title, 18)), CLAY_TEXT_CONFIG({
                                    .textColor = {255, 255, 255, 255},
                                    .fontSize = 15
                                }));
                            }
                        }
                    }
                }
            }
        }

        // --- FOOTER HUD DE BOTONES (1920 x 44 px) ---
        CLAY(CLAY_ID("FooterHUD"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(1920), CLAY_SIZING_FIXED(44) },
                .padding = { 56, 56, 0, 0 },
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .childGap = 24,
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            },
            .backgroundColor = { 15, 17, 24, 242 },
            .border = { .color = { 255, 255, 255, 20 }, .width = { .top = 1 } }
        }) {
            CLAY_TEXT(CLAY_STRING("[X] Seleccionar / Jugar"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 15 }));
            CLAY_TEXT(CLAY_STRING("[O] Volver"), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 15 }));
            CLAY_TEXT(CLAY_STRING("[L1]/[R1] Pestanas"), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 15 }));
            CLAY_TEXT(CLAY_STRING("[\xE2\x96\xA1] Buscar"), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 15 }));
            CLAY_TEXT(CLAY_STRING("[\xE2\x96\xB3] Opciones"), CLAY_TEXT_CONFIG({ .textColor = {148, 163, 184, 255}, .fontSize = 15 }));

            // Spacer
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
                .padding = { 56, 56, 0, 0 },
                .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER },
                .childGap = 16,
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            },
            .backgroundColor = { 15, 17, 24, 242 },
            .border = { .color = { 255, 255, 255, 20 }, .width = { .bottom = 1 } }
        }) {
            CLAY_TEXT(CLAY_STRING("<  Volver  (O)"), CLAY_TEXT_CONFIG({ .textColor = {226, 232, 240, 255}, .fontSize = 18 }));

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
                // Caratula
                CLAY_AUTO_ID({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(440) } },
                    .backgroundColor = { 36, 27, 59, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(14),
                    .border = { .color = { 55, 42, 84, 255 }, .width = LUNA_BORDER(1) },
                    .custom = { .customData = clay.allocCustom(CustomElementType::Mp4PreviewPlayer, "", 0xFFFFFFFF, &props.game, props.frameCount) }
                }) {}

                // Boton Jugar Ahora
                bool btnPlayFoc = (props.detailZone == 0 && props.detailBtnIdx == 0);
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(56) },
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .childGap = 10,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = btnPlayFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{121, 82, 236, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(28),
                    .border = { .color = btnPlayFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{0, 0, 0, 0}, .width = LUNA_BORDER(btnPlayFoc ? 3 : 0) }
                }) {
                    std::string playText = props.isConnecting ? "CONECTANDO..." : "\xE2\x96\xB6  Jugar ahora";
                    CLAY_TEXT(ClayRenderer::toClayString(playText), CLAY_TEXT_CONFIG({
                        .textColor = btnPlayFoc ? Clay_Color{14, 11, 22, 255} : Clay_Color{255, 255, 255, 255},
                        .fontSize = 20
                    }));
                }

                // Boton Galeria / Capturas
                bool btnGalFoc = (props.detailZone == 0 && props.detailBtnIdx == 1);
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(50) },
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
                    },
                    .backgroundColor = btnGalFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{34, 24, 56, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(25),
                    .border = { .color = btnGalFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{55, 42, 84, 255}, .width = LUNA_BORDER(btnGalFoc ? 3 : 1) }
                }) {
                    CLAY_TEXT(CLAY_STRING("Capturas y Trailer"), CLAY_TEXT_CONFIG({
                        .textColor = btnGalFoc ? Clay_Color{14, 11, 22, 255} : Clay_Color{203, 213, 225, 255},
                        .fontSize = 16
                    }));
                }

                // Boton Favoritos
                bool btnFavFoc = (props.detailZone == 0 && props.detailBtnIdx == 2);
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(50) },
                        .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
                        .childGap = 8,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    },
                    .backgroundColor = btnFavFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{34, 24, 56, 255},
                    .cornerRadius = CLAY_CORNER_RADIUS(25),
                    .border = { .color = btnFavFoc ? Clay_Color{145, 70, 255, 255} : Clay_Color{55, 42, 84, 255}, .width = LUNA_BORDER(btnFavFoc ? 3 : 1) }
                }) {
                    CLAY_TEXT(ClayRenderer::toClayString(props.isFavorite ? "\xE2\x98\x85  En favoritos" : "\xE2\x98\x86  Anadir a favoritos"), CLAY_TEXT_CONFIG({
                        .textColor = props.isFavorite ? Clay_Color{245, 158, 11, 255} : (btnFavFoc ? Clay_Color{14, 11, 22, 255} : Clay_Color{203, 213, 225, 255}),
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
                // Titulo grande
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
                        .layout = { .sizing = { CLAY_SIZING_FIXED(44), CLAY_SIZING_FIXED(28) } },
                        .custom = { .customData = clay.allocCustom(CustomElementType::PegiBadge, props.game.pegiRating.empty() ? "PEGI 12" : props.game.pegiRating) }
                    }) {}

                    std::string metaStr = (props.game.genres.empty() ? "Accion, Aventura" : props.game.genres) + "  \xE2\x80\xA2  " +
                                          (props.game.publisher.empty() ? "Amazon Luna" : props.game.publisher) + "  \xE2\x80\xA2  " +
                                          (props.game.releaseYear.empty() ? "2024" : props.game.releaseYear) + "  \xE2\x80\xA2  " +
                                          (props.game.playerCount.empty() ? "1 jugador" : props.game.playerCount);

                    CLAY_TEXT(ClayRenderer::toClayString(metaStr), CLAY_TEXT_CONFIG({
                        .textColor = {203, 213, 225, 255},
                        .fontSize = 17
                    }));
                }

                // Sinopsis
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(110) },
                        .padding = { 20, 20, 16, 16 }
                    },
                    .backgroundColor = { 20, 15, 34, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(12),
                    .border = { .color = { 45, 34, 72, 255 }, .width = LUNA_BORDER(1) }
                }) {
                    std::string syn = props.game.synopsis.empty() ? "Disponible para jugar al instante en Amazon Luna Cloud en tu PlayStation 5 a 1080p 60 FPS con sonido envolvente y soporte total de mando DualSense." : props.game.synopsis;
                    CLAY_TEXT(ClayRenderer::toClayString(syn), CLAY_TEXT_CONFIG({
                        .textColor = {226, 232, 240, 255},
                        .fontSize = 16,
                        .lineHeight = 24
                    }));
                }

                // Fila de Especificaciones Tecnicas Oficiales
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(70) },
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

                // Galeria de Capturas
                CLAY_TEXT(CLAY_STRING("Capturas de pantalla"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 20 }));
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(160) },
                        .childGap = 16,
                        .layoutDirection = CLAY_LEFT_TO_RIGHT
                    }
                }) {
                    for (int sc = 0; sc < 4; ++sc) {
                        CLAY_AUTO_ID({
                            .layout = { .sizing = { CLAY_SIZING_FIXED(260), CLAY_SIZING_FIXED(150) } },
                            .backgroundColor = { 34, 25, 54, 255 },
                            .cornerRadius = CLAY_CORNER_RADIUS(10),
                            .border = { .color = { 55, 42, 84, 255 }, .width = LUNA_BORDER(1) }
                        }) {}
                    }
                }

                // Juegos similares
                if (!props.game.similarGames.empty()) {
                    CLAY_TEXT(CLAY_STRING("Juegos similares"), CLAY_TEXT_CONFIG({ .textColor = {255, 255, 255, 255}, .fontSize = 20 }));
                    CLAY_AUTO_ID({
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(180) },
                            .childGap = 16,
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        }
                    }) {
                        for (size_t sim = 0; sim < props.game.similarGames.size() && sim < 6; ++sim) {
                            const auto& sg = props.game.similarGames[sim];
                            bool simFoc = (props.detailZone == 2 && props.similarSelectedIdx == static_cast<int>(sim));
                            CLAY_AUTO_ID({
                                .layout = { .sizing = { CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(170) }, .childGap = 6, .layoutDirection = CLAY_TOP_TO_BOTTOM }
                            }) {
                                CLAY_AUTO_ID({
                                    .layout = { .sizing = { CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(140) } },
                                    .backgroundColor = { 36, 27, 59, 255 },
                                    .cornerRadius = CLAY_CORNER_RADIUS(8),
                                    .border = { .color = simFoc ? Clay_Color{255, 255, 255, 255} : Clay_Color{45, 34, 72, 255}, .width = LUNA_BORDER(simFoc ? 2 : 1) }
                                }) {}
                                CLAY_TEXT(ClayRenderer::toClayString(utf8SafeTruncate(sg.title, 16)), CLAY_TEXT_CONFIG({
                                    .textColor = {203, 213, 225, 255},
                                    .fontSize = 14
                                }));
                            }
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
