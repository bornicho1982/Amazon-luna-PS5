#!/usr/bin/env python3

path = "apps/luna-ps5/src/display/display_manager.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

content = content.replace("\r\n", "\n")

target = "void DisplayManager::drawStreamingStatusScreen("

implementation = r'''void DisplayManager::drawNativeEngineWaitingScreen(const std::string& localWebUrl,
                                                   const std::string& lanWebUrl,
                                                   const std::string& statusMsg,
                                                   uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;

    // 1. Fondo cósmico oficial
    clear(0xFF0E0B16);

    // 2. Barra superior
    fillRect(0, 0, 1920, 72, 0xFF0D0A1C);
    fillRect(0, 72, 1920, 2, 0xFF6D28D9);

    drawLunaLogoIcon(60, 22, 28);
    drawText(100, 18, 5, "luna", 0xFFFFFFFF);
    drawText(220, 26, 3, "MOTOR DE STREAMING NATIVO PS5", 0xFFA5B4FC);

    uint32_t greenGlow = ((frameCount / 30) % 2 == 0) ? 0xFF10B981 : 0xFF059669;
    drawRoundedRect(1560, 20, 300, 34, 17, 0x2210B981);
    drawRoundedBorder(1560, 20, 300, 34, 17, 1, greenGlow);
    fillRect(1580, 32, 10, 10, greenGlow);
    drawText(1600, 27, 2, "1080p 60 FPS LISTO", 0xFFFFFFFF);

    // 3. Panel central
    int cardX = 310;
    int cardY = 150;
    int cardW = 1300;
    int cardH = 740;
    drawCard(*this, cardX, cardY, cardW, cardH, 0xFF161026, 0xFF2C2046, 2);

    // Encabezado
    drawRoundedRect(cardX + 40, cardY + 36, cardW - 80, 96, 12, 0xFF1C152E);
    drawText(cardX + 70, cardY + 54, 4, "Web Oficial de Amazon Luna Activa", 0xFFFFFFFF);
    drawText(cardX + 70, cardY + 98, 2, "Navega por todo el catalogo, portadas, buscador y fichas en el Navegador de tu PS5.", 0xFF94A3B8);

    // Mensaje de estado dinámico
    if (!statusMsg.empty()) {
        drawRoundedRect(cardX + 40, cardY + 146, cardW - 80, 48, 8, 0xFF231B38);
        drawText(cardX + 60, cardY + 161, 2, statusMsg, 0xFF38BDF8);
    }

    // Caja 1: Consola PS5
    int boxY1 = cardY + 210;
    drawRoundedRect(cardX + 40, boxY1, cardW - 80, 96, 12, 0xFF1F1833);
    drawRoundedBorder(cardX + 40, boxY1, cardW - 80, 96, 12, 1, 0xFF4C1D95);
    drawText(cardX + 70, boxY1 + 18, 2, "EN ESTA PLAYSTATION 5 (NAVEGADOR WEB)", 0xFFA78BFA);
    drawText(cardX + 70, boxY1 + 50, 4, localWebUrl, 0xFFFFFFFF);

    // Caja 2: Red Local
    int boxY2 = cardY + 322;
    drawRoundedRect(cardX + 40, boxY2, cardW - 80, 96, 12, 0xFF1F1833);
    drawRoundedBorder(cardX + 40, boxY2, cardW - 80, 96, 12, 1, 0xFF4C1D95);
    drawText(cardX + 70, boxY2 + 18, 2, "DESDE CUALQUIER PC / TABLET / MOVIL EN TU RED", 0xFFA78BFA);
    drawText(cardX + 70, boxY2 + 50, 4, lanWebUrl, 0xFF38BDF8);

    // Instrucción de juego
    int boxY3 = cardY + 434;
    drawRoundedRect(cardX + 40, boxY3, cardW - 80, 100, 12, 0xFF120E22);
    drawText(cardX + 70, boxY3 + 24, 3, "Al pulsar 'Jugar ahora' en la Web:", 0xFFFCD34D);
    drawText(cardX + 70, boxY3 + 60, 2, "El motor WebRTC + decodificador H.264 (libSceVideodec2) iniciara aqui a pantalla completa y 60 FPS.", 0xFFE2E8F0);

    // Botones de acción
    int btnY = cardY + 560;
    uint32_t btnXBorder = ((frameCount / 20) % 2 == 0) ? 0xFF02FDDB : 0xFF9146FF;
    drawRoundedRect(cardX + 60, btnY, 440, 60, 30, 0xFF281C44);
    drawRoundedBorder(cardX + 60, btnY, 440, 60, 30, 2, btnXBorder);
    drawText(cardX + 85, btnY + 18, 3, "[X] Abrir Web en PS5", 0xFFFFFFFF);

    drawRoundedRect(cardX + 530, btnY, 360, 60, 30, 0xFF1F1833);
    drawRoundedBorder(cardX + 530, btnY, 360, 60, 30, 1, 0xFF4C1D95);
    drawText(cardX + 555, btnY + 18, 3, "[CUADRADO] Desvincular QR", 0xFFCBD5E1);

    drawRoundedRect(cardX + 920, btnY, 300, 60, 30, 0xFF1F1833);
    drawRoundedBorder(cardX + 920, btnY, 300, 60, 30, 1, 0xFF4C1D95);
    drawText(cardX + 945, btnY + 18, 3, "[OPTIONS] Salir", 0xFF94A3B8);

    present();
}

void DisplayManager::drawStreamingStatusScreen('''

if target in content:
    content = content.replace(target, implementation, 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print("drawNativeEngineWaitingScreen added to display_manager.cpp!")
else:
    print("Target not found in display_manager.cpp")
