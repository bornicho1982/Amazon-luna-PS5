# Plan de pantallas — Luna PS5 (Stitch → Clay)

Referencia visual: proyecto Stitch `5120853827804408918` (Luna PS5 Console Dashboard).
HTML de referencia en `docs/stitch/*.html`.

## Pantallas y estado de port a Clay

| # | Pantalla (Stitch) | HTML de referencia | Clay (`LunaClayUI`) | Estado |
|---|---|---|---|---|
| 1 | Dashboard (home) | `dashboard` (a649f8e8) | `renderHomeScreen` | ✅ portado |
| 2 | Ficha de Juego | `game_detail.html` | `renderGameDetail` | ✅ portado |
| 3 | Ajustes y Perfil | `settings_profile.html` | `renderSettingsScreen` | ✅ esqueleto |
| 4 | Remote Play y Mi espacio | `remote_play.html` | — | ⏳ pendiente |
| 5 | Media Viewer (tráiler) | `media_viewer.html` | — | ⏳ pendiente |
| 6 | Login y Vinculación | `login.html` | — | ⏳ pendiente |
| 7 | Streaming Dashboard | `streaming_dashboard.html` | — | ⏳ pendiente |
| 8 | Error | `error.html` | — | ⏳ pendiente |

## Orden sugerido de port a Clay (por complejidad creciente)
1. **Error** — simple (icono + texto + 2 botones).
2. **Login/CBL** — QR + pasos + botón volver.
3. **MediaViewer** — video fullscreen + controles overlay (auto-hide).
4. **StreamingDashboard** — HUD mínimo sobre video.
5. **Remote Play y Mi espacio** — carruseles + lista.
6. **Settings** — completar paneles de detalle por sección (ya esqueleto).

## Notas
- Cada pantalla nueva requiere: struct `XxxProps` + `renderXxx` en `luna_clay_ui.h/.cpp`, método `drawXxxScreen` en `DisplayManager`, y cableado en `main.cpp` (`AppState`).
- Mantener el estilo: fondo `0xFF0E0B16`, acento morado `0xFF9146FF`, texto blanco/gris, Clay con `ClayRenderer::instance()`.
- El legacy `drawXxxScreen` (directo) se mantiene hasta que el port Clay esté completo, para no perder funcionalidad.
- `renderSettingsScreen` es actualmente un esqueleto; completar los paneles de detalle por sección (mandos, cuentas, etc.) antes de reemplazar el legacy.
