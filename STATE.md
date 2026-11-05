# ESTADO ACTUAL DEL PROYECTO — AMAZON LUNA PS5

> **Última Actualización:** 5 de noviembre de 2026  
> **Rama:** `main` | **Sincronización:** Al día con `origin/main`  
> **Binarios Compilados:** `PPSA99825/eboot.bin` (6,657,581 bytes) + `PPSA99825/sce_module/libc.prx` (1,284,418 bytes)  
> **Integridad FSELF:** Válida, 0 syscalls prohibidas, enlace limpio contra `libkernel.prx`.

---

## 1. Estado de los Componentes Principales

| Componente | Estado | Implementación / Archivos | Notas Técnicas |
|---|---|---|---|
| **Compilador PS5** | 🟢 100% Operativo | `apps/luna-ps5/build_ps5.ps1` | Clang LLVM PS5 + payload-sdk. Genera FSELF firmado. |
| **Compilador Desktop** | 🟢 100% Operativo | `apps/luna-ps5/CMakeLists.txt` | MSVC C++20 + libdatachannel + mbedTLS 3.6.2. |
| **Motor de UI (Clay)** | 🟢 100% Operativo | `src/display/clay.h`, `luna_clay_ui.cpp` | Motor declarativo en C puro a 1080p con escalado a 4K. |
| **Renderer Nativo PS5** | 🟢 100% Operativo | `src/display/clay_renderer.cpp`, `display_manager.cpp` | Double/Triple buffering directo a framebuffer `libSceVideoOut`. |
| **Iconos DualSense** | 🟢 100% Operativo | `src/display/ps5_controller_icons.cpp` | Vectoriales procedurales con anti-aliasing (incluye L3/R3 con textura de goma). |
| **Autenticación CBL** | 🟢 100% Operativo | `src/auth/cbl_client.cpp`, `util/qrcodegen.c` | Amazon OAuth Device Code + QR Code dinámico + código PIN. |
| **Catálogo de Juegos** | 🟢 100% Operativo | `src/session/luna_session.cpp` | Conexión a APIs REST/GraphQL de Luna, parsing de colecciones y metadatos. |
| **Reproductor Tráilers** | 🟢 100% Operativo | `src/media/mp4_preview_player.cpp` | HTTP Range progresivo (48 MB buffer), decodificador H.264 + Helix AAC. |
| **Audio de Tráilers** | 🟢 100% Operativo | `src/audio/audio_manager.cpp` | Arena de 128 KB, buffer FIFO de ~250 ms para eliminar chasquidos y cortes. |
| **Patrón Observer** | 🟢 100% Operativo | `src/session/isession_observer.h` | `DisplayManager` desacoplado de `LunaSessionClient` mediante eventos. |
| **WebRTC Streaming** | 🟡 85% Integrado | `src/stream/webrtc_client.cpp` | libdatachannel + mbedTLS DTLS-SRTP funcional. Pendiente test final con cuenta activa en consola. |
| **Entrada de Control** | 🟢 100% Operativo | `src/display/display_manager.cpp`, `main.cpp` | Polling `libScePad`, navegación analógica + cruceta, Protobuf a 250 Hz. |
| **Refactor GameLoop** | 🟡 En Progreso | `src/game/game_loop.cpp`, `game_state.cpp` | Borrador de singleton para reducir `main.cpp`. Excluido temporalmente del build. |

---

## 2. Matriz de Estado de Pantallas Clay

Todas las pantallas han sido migradas desde el prototipo de diseño Stitch (`docs/stitch/*.html`) al motor declarativo `Clay` en [luna_clay_ui.cpp](file:///c:/proyectos%20antigravity/webkit%20PS5%20v2/apps/luna-ps5/src/display/luna_clay_ui.cpp):

| # | Pantalla | Función Clay | Método DisplayManager | Cableado en main.cpp | Estado Visual |
|---|---|---|---|---|---|
| 1 | **Dashboard (Home)** | `renderHomeScreen` | `drawHomeScreenClay` | ✅ `AppState::Catalog` | Carruseles a 1824 px, tráiler integrado, fondos rotativos y números gigantes 1..10. |
| 2 | **Ficha de Juego** | `renderGameDetail` | `drawGameDetailScreenClay` | ✅ `AppState::GameDetail` | Sinopsis, géneros, PEGI, desarrollador y opciones Jugar/Favorito. |
| 3 | **Ajustes y Perfil** | `renderSettingsScreen` | `drawSettingsScreenClay` | ✅ `AppState::Settings` | Pestañas de cuenta, calidad de streaming, audio y mandos. |
| 4 | **Pantalla de Error** | `renderErrorScreen` | `drawErrorScreenClay` | ✅ `AppState::Error` | Mensaje formal, código de error y retorno con botón Círculo. |
| 5 | **Login CBL (Amazon)** | `renderLoginScreen` | `drawLoginScreenClay` | ✅ `AppState::Login` | Código QR dinámico en pantalla, URL y PIN de 6 caracteres. |
| 6 | **Media Viewer** | `renderMediaViewerScreen` | `drawMediaViewerScreenClay` | ✅ `AppState::MediaViewer` | Tráiler a pantalla completa, barra de progreso y controles auto-ocultables. |
| 7 | **Streaming Dashboard** | `renderStreamingDashboardScreen` | `drawStreamingDashboardScreenClay` | ✅ `AppState::Streaming` | HUD superpuesto sobre frame YUV420 con ping, bitrate y calidad. |
| 8 | **Remote Play / Mi Espacio** | `renderRemotePlayScreen` | — | ⏳ Por cablear a `AppState` | Implementado en Clay, listo para conectar al selector de modos. |

---

## 3. Estado de Seguridad del Repositorio

- **Auditoría de Historial Git:** 100% LIMPIO (los 67 commits escaneados con scripts forenses automatizados).
- **GitHub Push Protection:** Aprobado sin alertas.
- **Claves API y Credenciales:** 0 credenciales en código fuente. La clave temporal de pruebas en `opencode.json` fue saneada y reemplazada por `"${STITCH_API_KEY}"`.
- **Ficheros Protegidos en `.gitignore`:** `account.json`, `device_id.txt`, `tools/luna_probe/session.json`, `docs/notes/test_*`.
