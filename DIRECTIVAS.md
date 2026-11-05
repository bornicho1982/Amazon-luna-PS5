# DIRECTIVAS DE ARQUITECTURA Y DESARROLLO — AMAZON LUNA PS5

> **Documento de Gobernanza Técnica Primaria**  
> Cualquier IA o desarrollador que trabaje en este repositorio debe leer este archivo **PRIMERO** y obedecer sus reglas de forma estricta.

---

## 1. Identidad y Misión del Proyecto

- **Nombre:** Amazon Luna Native PS5 Client
- **Title ID:** `PPSA99825`
- **Lenguaje:** C++20 Nativo
- **Plataforma Objetivo:** PlayStation 5 (FW con jailbreak / payloads / FSELF homebrew, e.g. 3.00–4.51 / SDK 13.60) y modo de desarrollo en Windows Desktop (MSVC/CMake).
- **Misión:** Proporcionar un cliente nativo completo de Amazon Luna Cloud Gaming para PS5 con interfaz de consola de salón (10-foot UI a 1080p escalable a 4K), reproducción de tráilers HD, autenticación CBL con QR y streaming en la nube vía WebRTC de ultra-baja latencia con control DualSense nativo.

---

## 2. Reglas Mandatorias de Desarrollo

### ❌ PROHIBICIONES ESTRICTAS
1. **Prohibido `std::thread().detach()`**: Todos los hilos deben ser gestionados formalmente con `std::jthread` o hilos coordinados con `std::atomic<bool> workerRunning_` y `join()` explícito en destructores.
2. **Prohibido el uso de navegadores web o WebKit para la ejecución del juego**: El proyecto es una aplicación nativa compilada en C++20 contra `libkernel.prx` y librerías del sistema PS5.
3. **Prohibido incluir archivos `.mp4`, `.webm` o vídeos locales dentro de `PPSA99825/`**: Los tráilers se reproducen exclusivamente por streaming HTTP progresivo en memoria.
4. **Prohibido `Set-Content` masivo en PowerShell**: Las modificaciones de código deben hacerse mediante herramientas atómicas preservando el formato y finales de línea.
5. **Prohibido borrar archivos sin moverlos antes a `docs/notes/`**.
6. **Prohibido cambiar la arquitectura o stack sin orden explícita del usuario**.
7. **Prohibido almacenar o subir credenciales**: Claves API, tokens OAuth (`account.json`), identificadores de dispositivo (`device_id.txt`) o claves privadas jamás deben registrarse en Git (mantener en `.gitignore`).

### ⚙️ LÍMITES DE MEMORIA Y HARDWARE (PS5)
- **Buffer de streaming de tráilers:** Máximo **48 MB** estáticos (`g_mp4DownloadBuffer`).
- **Arena de decodificación de audio AAC:** Máximo **128 KB** estáticos.
- **Buffer de audio PCM:** FIFO anular de **~250 ms** para evitar chasquidos (*underruns*).
- **Framebuffer:** 1920×1080 ARGB (Double / Triple Buffering con `libSceVideoOut`).
- **Entradas DualSense:** Polling y despacho de paquetes Protobuf a **250 Hz** por WebRTC DataChannel.

---

## 3. Instrucciones de Compilación

### A. Compilación Nativa para PS5 (Cross-Compilation Clang LLVM)
```powershell
powershell -ExecutionPolicy Bypass -File apps\luna-ps5\build_ps5.ps1
```
- **Requisitos:** `C:\llvm-ps5\bin` y `C:\ps5-payload-sdk` configurados.
- **Salida:** `PPSA99825/eboot.bin` (FSELF firmado) y `PPSA99825/sce_module/libc.prx`.
- **Verificación:** El script valida automáticamente 0 syscalls prohibidas y la integridad FSELF del binario.

### B. Compilación de Escritorio (Windows MSVC / CMake)
```powershell
cmake -S apps/luna-ps5 -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
- **Salida:** `build/Release/luna_ps5.exe`.
- **DTLS-SRTP:** Habilitado mediante `apps/luna-ps5/cmake/luna_mbedtls_user_config.h` sin modificar el código fuente de mbedTLS.

---

## 4. Estructura y Mapa de Directorios

```
/
├── DIRECTIVAS.md                  # Este documento (Gobernanza y reglas primarias)
├── STATE.md                       # Estado actual, módulos completados y versiones
├── ROADMAP.md                     # Historial de fases y tareas pendientes
├── apps/luna-ps5/                 # Código fuente principal de la aplicación
│   ├── src/
│   │   ├── main.cpp               # Bucle principal, máquina de estados y navegación
│   │   ├── auth/                  # Cliente Amazon OAuth CBL y generador de QR
│   │   ├── session/               # Cliente LunaSessionClient e ISessionObserver
│   │   ├── display/               # Motor declarativo Clay, renderer PS5 y fuentes
│   │   ├── media/                 # Decodificador MP4 H.264/AAC y streaming progresivo
│   │   ├── audio/                 # AudioManager, decodificador Opus y salida PCM
│   │   ├── stream/                # WebRTC libdatachannel, SDP/ICE y Protobuf inputs
│   │   ├── net/                   # Cliente HTTP seguro con libcurl/mbedtls
│   │   └── util/                  # Logger, cargador de imágenes y ajustes
│   ├── ps5/                       # Stubs de enlace PS5, libkernel y headers oficiales
│   └── tests/                     # Tests unitarios automatizados (Observer, etc.)
├── docs/                          # Documentación técnica profunda y diagramas
├── PPSA99825/                     # Contenedor del paquete distribuible de PS5
└── tools/                         # Herramientas de reverse engineering y referencias
```

---

## 5. Convenciones de Idioma y Código

- **Conversación y Documentación con el Usuario:** Español.
- **Código Fuente, Comentarios y Símbolos:** Inglés exclusivamente (`struct GameTile`, `void renderHomeScreen`, etc.).
- **Estilo de Respuesta:** Ejecutivo, claro y estructurado: implementar, compilar y reportar.
