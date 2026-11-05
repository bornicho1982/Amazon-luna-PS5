---
name: Luna PS5 Development
description: Build, test and navigate the native Amazon Luna PS5 client (C++20, CMake/MSVC desktop, PS5 cross-build)
---

# Luna PS5 Development

Native C++20 Amazon Luna client. Two build paths share the same sources:

- `apps/luna-ps5/src/` — application sources.
- `apps/luna-ps5/ps5/` — PS5 packaging, stubs and prebuilt static libraries.
- `tools/reference/xbox-cloud-gaming-ps5/deps/` — vendored libdatachannel 0.23.1 and mbedTLS 3.6.2 (git-ignored).

## Desktop (Windows / MSVC)

```powershell
cmake -S apps/luna-ps5 -B build
cmake --build build --config Release --parallel
```

Output: `build/Release/luna_ps5.exe`.

Run unit tests:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## PS5 cross-build

```powershell
powershell -File apps/luna-ps5/build_ps5.ps1
```

Requires `C:\llvm-ps5\bin` and `C:\ps5-payload-sdk` (the script adds them to `PATH`). The PS5 link uses prebuilt archives in `apps/luna-ps5/ps5/lib/`; it does not use the desktop CMake graph.

## Architecture map

- `src/main.cpp` — app loop, wiring, navigation and state.
- `src/session/luna_session.*` — session state machine and observer notifications (`ISessionObserver`).
- `src/stream/webrtc_client.*` — libdatachannel peer connection, SDP/ICE, RTP video/audio, protobuf input.
- `src/net/http_client.*`, `src/auth/*` — HTTP and Code-Based Linking authentication.
- `src/display/*` — Clay UI, fonts, controller icons, hero assets.
- `src/media/*` — H.264/VP9 decode pipeline, MP4 preview, Helix AAC.
- `src/audio/audio_manager.*` — Opus decode and PS5 audio output.
- `tests/unit/` — observer pattern tests linked against `luna_core`.

## Rules

- Keep `LUNA_SOURCES` in `apps/luna-ps5/CMakeLists.txt` in sync with the real source list, and keep tests linking `luna_core` instead of re-listing sources.
- Do not invent WebRTC stubs: use the vendored libdatachannel/mbedTLS targets.
- DTLS-SRTP is enabled on desktop through `apps/luna-ps5/cmake/luna_mbedtls_user_config.h` (`MBEDTLS_USER_CONFIG_FILE`). Do not edit vendored mbedTLS config.
- Tests must not delete observers they do not own; `LunaSessionClient` only clears its observer list.
- `src/game/game_loop.*` and `game_state.*` are an **incomplete refactor** (won't compile: missing `PadState`, `util/timer.h`, enums). Do not add them to the build until finished.
- `luna_session.cpp` methods `fetchCatalog`, `fetchPageCollections`, `fetchRawPage` are restored with real HTTP logic; others (`resolveGameDetails`, `fetchAppConfig`, etc.) may still be stubs.
- Run both desktop build and tests before reporting success. For PS5-only changes, also run `build_ps5.ps1` when the toolchain is available.
