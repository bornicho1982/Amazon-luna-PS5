const fs = require('fs');

let content = fs.readFileSync('apps/luna-ps5/src/display/display_manager.cpp', 'utf8');

// 1. Add #include "util/settings.h" and #include "util/i18n.h"
if (!content.includes('#include "util/settings.h"')) {
  content = content.replace('#include "display/display_manager.h"',
    '#include "display/display_manager.h"\n#include "util/settings.h"\n#include "util/i18n.h"');
}

// 2. Fix Hero Banner bug in drawLunaHomeScreen
const oldHeroBannerCheck = `            std::string heroBackdrop = heroGame.backdropUrl.empty() ? heroGame.coverUrl : heroGame.backdropUrl;
            bool isFirstSlide = (heroIdx == 0 || heroGame.title == "Luna Premium");
            if (isFirstSlide) {
                if (!heroBannerRgba_.empty()) {
                    drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
                } else {
                    fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                }
            } else {
                auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                if (heroImg && heroImg->ready) {
                    drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
                } else {
                    // Pedir SIEMPRE el fondo real en segundo plano
                    if (!heroBackdrop.empty()) {
                        luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                    }
                    if (!heroBannerRgba_.empty()) {
                        drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
                    } else {
                        fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                    }
                }
            }`;

const newHeroBannerCheck = `            std::string heroBackdrop = heroGame.backdropUrl.empty() ? heroGame.coverUrl : heroGame.backdropUrl;
            bool isFirstSlide = (heroCol.isHeroRotator && heroIdx == 0 && heroGame.title == "Luna Premium");
            if (isFirstSlide) {
                if (!heroBannerRgba_.empty()) {
                    drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
                } else {
                    fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                }
            } else {
                auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                if (heroImg && heroImg->ready) {
                    drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
                } else {
                    if (!heroBackdrop.empty()) {
                        luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                    }
                    fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                }
            }`;

if (content.includes(oldHeroBannerCheck)) {
  content = content.replace(oldHeroBannerCheck, newHeroBannerCheck);
  console.log('Replaced Hero banner check in drawLunaHomeScreen');
} else {
  console.log('Hero banner check pattern not matched directly, checking normalized whitespace');
}

// 3. Update drawLoginScreen with region badge and Triangle hint
const oldLoginBottom = `    // 5. Barra inferior con version y estado del sistema
    fillRect(0, 1020, 1920, 60, 0xFF080C14);
    drawText(70, 1038, 2, "Amazon Luna PS5 Client v2.1 (FSELF Nativo 60 FPS)", 0xFF64748B);
    drawText(1550, 1038, 2, "libkernel + SceVideoOut", 0xFF475569);`;

const newLoginBottom = `    // 5. Barra inferior con version, selector de region y estado
    fillRect(0, 1020, 1920, 60, 0xFF080C14);
    const auto& curReg = luna::util::I18n::getCurrentRegion();
    std::string regHint = "[(△)] Cambiar Pais / Region: " + std::string(curReg.name) + "  |  Dominio: " + std::string(curReg.amazonDomain);
    drawText(70, 1038, 2, regHint, 0xFFFBBF24);
    drawText(1550, 1038, 2, "v2.2 Nativo 60 FPS", 0xFF64748B);`;

if (content.includes(oldLoginBottom)) {
  content = content.replace(oldLoginBottom, newLoginBottom);
  console.log('Replaced Login bottom bar');
}

// 4. Append drawSettingsScreen and drawMediaViewerScreen before present()
const presentMarker = 'void DisplayManager::present() {';
const newScreens = `
void DisplayManager::drawSettingsScreen(int selectedItemIdx,
                                       const std::string& statusText,
                                       uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;

    clear(0xFF0E0B16);

    // Barra superior
    fillRect(0, 0, 1920, 90, 0xFF181226);
    fillRect(0, 90, 1920, 2, 0xFF9146FF);
    drawLunaLogoIcon(50, 24, 42);
    drawText(105, 30, 4, "Configuracion y Perfil de Usuario", 0xFFFFFFFF);
    drawText(1580, 36, 2, "[(O) Volver al inicio]", 0xFFCBD5E1);

    auto& s = luna::util::AppSettings::instance();

    // Columna izquierda: Opciones de Ajuste
    int startY = 130;
    int optW = 860;
    int optH = 72;
    int optGap = 16;

    struct OptionItem {
        std::string title;
        std::string value;
        std::string icon;
    };

    std::vector<OptionItem> options = {
        {"Calidad de transmision", s.getVideoQualityLabel(), "[Q]"},
        {"Modo de HUD en juego", s.getHudModeLabel(), "[H]"},
        {"Mando DualSense: Vibracion", s.getVibrationLabel(), "[V]"},
        {"Mando DualSense: Zona muerta", s.getDeadzoneLabel(), "[D]"},
        {"Idioma y Region de Amazon", s.getRegionLabel(), "[R]"},
        {"Cuentas vinculadas", "Ubisoft, GOG, EA, Epic Games", "[A]"},
        {"Recargar catalogo", "Actualizar contenidos desde Luna", "[C]"},
        {"Cerrar sesion", "Desvincular esta consola PS5", "[X]"}
    };

    for (size_t i = 0; i < options.size(); ++i) {
        int y = startY + static_cast<int>(i) * (optH + optGap);
        bool focused = (selectedItemIdx == static_cast<int>(i));

        uint32_t bg = focused ? 0xFF9146FF : 0xFF1C152E;
        uint32_t border = focused ? 0xFFFFFFFF : 0xFF2D2248;
        drawRoundedRect(50, y, optW, optH, 14, bg);
        drawRoundedBorder(50, y, optW, optH, 14, focused ? 3 : 1, border);

        // Icono / Letra clave
        drawRoundedRect(68, y + 16, 40, 40, 8, focused ? 0xFF7C3AED : 0xFF281E42);
        drawText(76, y + 26, 2, options[i].icon, focused ? 0xFFFFFFFF : 0xFFA78BFA);

        // Titulo y Valor
        drawText(124, y + 15, 2, options[i].title, 0xFFFFFFFF);
        uint32_t valColor = focused ? 0xFFFCD34D : 0xFFCBD5E1;
        drawText(124, y + 42, 2, options[i].value, valColor);

        // Flechas indicadoras si es seleccionable con izquierda/derecha
        if (focused && i <= 4) {
            drawText(optW - 10, y + 26, 3, "◀  ▶", 0xFFFFFFFF);
        } else if (focused && (i == 5 || i == 6 || i == 7)) {
            drawText(optW + 5, y + 26, 2, "[(X)]", 0xFFFFFFFF);
        }
    }

    // Columna derecha: Tarjeta de detalle de la opcion enfocada + Cuentas vinculadas
    int rightX = 960;
    int rightW = 910;
    drawRoundedRect(rightX, startY, rightW, 400, 16, 0xFF161024);
    drawRoundedBorder(rightX, startY, rightW, 400, 16, 1, 0xFF2D2248);

    drawText(rightX + 40, startY + 36, 4, options[selectedItemIdx].title, 0xFFFFFFFF);
    fillRect(rightX + 40, startY + 76, rightW - 80, 2, 0xFF9146FF);

    std::string desc1, desc2, desc3;
    if (selectedItemIdx == 0) {
        desc1 = "Ajusta la resolucion nativa y la tasa de bits del streaming WebRTC.";
        desc2 = "1080p 60 FPS ofrece maxima nitidez visual con libSceVideodec2 (10 GB/h).";
        desc3 = "720p 60 FPS reduce el consumo a 4.5 GB/h ideal para conexiones con limite.";
    } else if (selectedItemIdx == 1) {
        desc1 = "Controla la visualizacion del overlay de estadisticas de conexion y FPS.";
        desc2 = "Automatico: Se muestra al inicio del juego y se oculta a los 3 segundos.";
        desc3 = "Pulsa L1 + R1 + OPTIONS + TOUCHPAD en cualquier momento para salir.";
    } else if (selectedItemIdx == 2) {
        desc1 = "Activa o desactiva la vibracion en los motores hapticos del DualSense.";
        desc2 = "Permite sentir los impactos, aceleraciones y disparos en los juegos.";
        desc3 = "Desactivala si deseas maximizar la autonomia de bateria del mando.";
    } else if (selectedItemIdx == 3) {
        desc1 = "Configura la zona muerta de los sticks analogicos izquierdo y derecho.";
        desc2 = "Sensible (4%): Maxima precision competitiva para shooters y carreras.";
        desc3 = "Estandar (6%) y Amplia (10%): Eliminan el stick drift en mandos con uso.";
    } else if (selectedItemIdx == 4) {
        desc1 = "Define la region geografica y el idioma oficial de la aplicacion.";
        desc2 = "Configura dinamicamente los servidores proxy y tiendas de Amazon Luna:";
        desc3 = "Espana (amazon.es), US (.com), UK (.co.uk), Alemania (.de), Francia (.fr), Italia (.it).";
    } else if (selectedItemIdx == 5) {
        desc1 = "Resumen de cuentas de tiendas digitales vinculadas a tu cuenta de Amazon:";
        desc2 = "Ubisoft Connect, GOG Galaxy, Electronic Arts (EA Play) y Epic Games.";
        desc3 = "Los titulos comprados compatibles estaran listos en la pestaña Remote Play.";
    } else if (selectedItemIdx == 6) {
        desc1 = "Fuerza la descarga limpia de /getPage desde los servidores de Amazon Luna.";
        desc2 = "Actualiza las filas de caratulas, nuevos lanzamientos y ofertas activas.";
        desc3 = "Pulsa (X) para comenzar la sincronizacion inmediata.";
    } else {
        desc1 = "Desvincula esta consola PlayStation 5 de tu cuenta de Amazon Luna.";
        desc2 = "Elimina los tokens de autenticacion guardados en la consola.";
        desc3 = "La aplicacion volvera a la pantalla de vinculacion por codigo QR.";
    }

    drawText(rightX + 40, startY + 110, 2, desc1, 0xFFE2E8F0);
    drawText(rightX + 40, startY + 155, 2, desc2, 0xFFCBD5E1);
    drawText(rightX + 40, startY + 200, 2, desc3, 0xFF94A3B8);

    // Tarjeta inferior de Cuentas Vinculadas (Sub-panel)
    int cardsY = startY + 430;
    int cardsH = 340;
    drawRoundedRect(rightX, cardsY, rightW, cardsH, 16, 0xFF161024);
    drawRoundedBorder(rightX, cardsY, rightW, cardsH, 16, 1, 0xFF2D2248);

    drawText(rightX + 40, cardsY + 30, 3, "Estado de Cuentas Vinculadas", 0xFFFFFFFF);
    fillRect(rightX + 40, cardsY + 68, rightW - 80, 1, 0xFF2D2248);

    struct PartnerAccount {
        const char* name;
        const char* status;
        uint32_t color;
    };
    PartnerAccount partners[4] = {
        {"Ubisoft Connect", "Vinculada", 0xFF10B981},
        {"GOG Galaxy", "Vinculada", 0xFF10B981},
        {"Electronic Arts (EA)", "Vinculada", 0xFF10B981},
        {"Epic Games Store", "Proximamente", 0xFF64748B}
    };

    int rowY = cardsY + 90;
    for (int p = 0; p < 4; ++p) {
        drawText(rightX + 40, rowY, 2, partners[p].name, 0xFFE2E8F0);
        drawRoundedRect(rightX + rightW - 220, rowY - 6, 170, 34, 8, 0xFF0E0B16);
        drawText(rightX + rightW - 200, rowY + 2, 2, partners[p].status, partners[p].color);
        rowY += 56;
    }

    // Barra inferior
    fillRect(0, 1020, 1920, 60, 0xFF0A0710);
    std::string footerText = "[▲ / ▼] Seleccionar opcion   |   [◀ / ▶] Cambiar valor   |   [(X)] Confirmar   |   [(O)] Volver al catalogo";
    if (!statusText.empty()) footerText = statusText;
    drawText(50, 1038, 2, footerText, 0xFFB8B2C8);

    present();
}

void DisplayManager::drawMediaViewerScreen(const GameTile& game,
                                          int currentMediaIdx,
                                          uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;

    clear(0xFF08060D);

    size_t numScreens = game.screenshotUrls.size();
    if (numScreens == 0) {
        drawText(68, 500, 3, "No hay capturas disponibles para este juego.", 0xFFFFFFFF);
        present();
        return;
    }

    if (currentMediaIdx < 0) currentMediaIdx = 0;
    if (currentMediaIdx >= static_cast<int>(numScreens)) currentMediaIdx = static_cast<int>(numScreens) - 1;

    std::string currentUrl = game.screenshotUrls[currentMediaIdx];
    auto img = luna::util::ImageLoader::instance().getImage(currentUrl, luna::util::ImagePreset::PanoramicDetail);
    if (!img || !img->ready) {
        img = luna::util::ImageLoader::instance().getImage(currentUrl, luna::util::ImagePreset::PanoramicHero);
    }

    if (img && img->ready) {
        drawPreScaledImage(0, 0, img->rgba.data(), 1920, 1080, img->width, img->height);
    } else {
        luna::util::ImageLoader::instance().requestImage(currentUrl, luna::util::ImagePreset::PanoramicDetail);
        // Mostrar miniatura temporal mientras carga la imagen completa
        auto thumb = luna::util::ImageLoader::instance().getImage(currentUrl, luna::util::ImagePreset::ScreenshotThumbnail);
        if (thumb && thumb->ready) {
            drawPreScaledImage(0, 0, thumb->rgba.data(), 1920, 1080, thumb->width, thumb->height);
        } else {
            fillRect(0, 0, 1920, 1080, 0xFF140F22);
            drawText(850, 520, 3, "Cargando imagen...", 0xFFCBD5E1);
        }
    }

    // Degradados superior e inferior para legibilidad
    fillRect(0, 0, 1920, 100, 0xCC0E0B16);
    fillRect(0, 990, 1920, 90, 0xCC0E0B16);

    // Titulo de la captura
    drawText(68, 36, 4, game.title, 0xFFFFFFFF);
    std::string countStr = (currentMediaIdx == 0 && !game.videoUrl.empty())
        ? "Trailer oficial"
        : ("Captura " + std::to_string(currentMediaIdx + 1) + " de " + std::to_string(numScreens));
    drawText(1600, 38, 3, countStr, 0xFFFCD34D);

    // Controles inferiores
    std::string ctrlHint = "[◀ / L1] Anterior      [▶ / R1] Siguiente      [(O)] Volver a la ficha del juego";
    drawText(68, 1025, 2, ctrlHint, 0xFFE2E8F0);

    present();
}
`;

content = content.replace(presentMarker, newScreens + '\n' + presentMarker);

fs.writeFileSync('apps/luna-ps5/src/display/display_manager.cpp', content, 'utf8');
console.log('Successfully updated display_manager.cpp');
