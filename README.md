# Amazon Luna for PlayStation 5 (Native Homebrew Client — `PPSA99825`)

An unofficial, high-performance native C++20 homebrew application bringing **Amazon Luna Cloud Gaming** directly to jailbroken **PlayStation 5** consoles.

Inspired by open-source projects like **PSBox** (*RafaelNGP/xbox-cloud-gaming-ps5*), this client provides a direct, low-latency cloud gaming experience without needing a web browser or Linux installation.

---

## 🎮 Key Features

* **Native Prospero C++20 Application**: Runs directly from the PS5 home screen as a directory-based homebrew app (`PPSA99825`) compatible with **ShadowMountPlus**, **Itemzflow**, and **PS5Upload**.
* **Direct Hardware Video Pipeline**: 1080p @ 60 FPS video decoding using PS5 hardware acceleration (`libSceVideodec2`), rendered onto 64 KB macro-tiled framebuffers via `libSceVideoOut`.
* **Low-Latency Audio**: Decodes Opus audio streams to 48 kHz stereo PCM with direct kernel submission (`libSceAudioOut`).
* **Full DualSense Integration**: Native gamepad reading via `libScePad` and `libSceUserService`, mapping analog sticks, triggers, and digital buttons directly into Amazon Tempo Protobuf packets over WebRTC SCTP DataChannels.
* **Code-Based Linking (CBL Authentication)**: Seamless sign-in via `amazon.com/code` or QR code. No need to enter passwords on the console; tokens are stored securely in `/data/homebrew/PPSA99825/account.json`.
* **Companion LAN Web Bridge**: Built-in HTTP server on port `8085` allowing instant 1-click game session handover from any phone or PC on the same Wi-Fi network.
* **Official Amazon Luna Game Catalog**: Interactive native UI carousel showcasing Luna titles (Fortnite, Star Wars Outlaws, Avatar: Frontiers of Pandora, Trackmania, and more).

---

## 🏗️ Architecture Overview

Unlike desktop or Smart TV clients that wrap heavy Chromium or WebKit browsers, this client talks directly to Amazon's cloud gaming infrastructure (**Project Tempo**):

```
+--------------------------------------------------------------------+
|                         PlayStation 5                              |
|                                                                    |
|  +--------------------+   +-------------------+   +-------------+  |
|  | Native UI & Input  |   | DualSense Handler |   | Video/Audio |  |
|  |  (1080p Carousel)  |   |   (libScePad)     |   |  Decoders   |  |
|  +---------+----------+   +---------+---------+   +------+------+  |
|            |                        |                    ^         |
|            v                        v                    |         |
|  +-------------------------------------------------------+------+  |
|  |               WebRTC Engine (DTLS-SRTP / SCTP)               |  |
|  +-----------------------------------+--------------------------+  |
+--------------------------------------|-----------------------------+
                                       |
                     Signaling & Media | (H.264 / Opus / Protobuf)
                                       v
+--------------------------------------------------------------------+
|                       Amazon Cloud (Tempo)                         |
|                                                                    |
|  +---------------------------+       +--------------------------+  |
|  | Tempo Proxy API Gateway   |       | AWS EC2 / Gamelift Fleet |  |
|  | (Session & Entitlements)  |       | (Live Game Containers)   |  |
|  +---------------------------+       +--------------------------+  |
+--------------------------------------------------------------------+
```

### Protocol Specifications
* **Transport**: DTLS-SRTP over UDP (Port 33438)
* **Signaling**: JSON SDP Offer/Answer exchanged via `createGameSessionJob` and `getGameSessionJob` on `proxy-prod.tempo.digital.a2z.com`
* **Video Codec**: H.264 (Constrained Baseline Profile, Level 3.1, RTP Payload 108)
* **Audio Codec**: Opus (48 kHz, 2 channels, RTP Payload 111)
* **Input**: Protobuf `ClientMessage` containing `GamePadEvent` serialized over an unordered SCTP DataChannel (`inputChannel`)

---

## 📦 Directory Structure

```text
Amazon-luna-PS5/
├── apps/
│   └── luna-ps5/
│       ├── build_ps5.ps1           # PS5 LLVM cross-compiler & FSELF signer
│       └── src/
│           ├── audio/              # AudioManager (libSceAudioOut)
│           ├── auth/               # CblClient (CBL), AuthServer (LAN bridge)
│           ├── display/            # DisplayManager (libSceVideoOut, font renderer)
│           ├── input/              # PadManager (DualSense libScePad)
│           ├── media/              # VideoDecoder, AudioDecoder
│           ├── net/                # HttpClient (libSceHttp2 / libSceSsl)
│           ├── session/            # LunaSessionClient (Amazon Tempo API)
│           └── stream/             # WebRtcClient (DTLS-SRTP & SDP offer/answer)
├── docs/                           # Documentation and notes
├── PPSA99825/                      # PS5 target directory package
│   ├── sce_module/
│   │   └── libc.prx                # Custom CRT / libc runtime module
│   └── sce_sys/
│       ├── icon0.png               # Official 512x512 app icon
│       └── param.json              # Title metadata (PPSA99825)
├── reports/                        # Reverse-engineered protocol specifications
└── tools/                          # Diagnostic probes, bundle analyzers & helper scripts
```

---

## 🚀 Installation on PS5

### Requirements
* Jailbroken PlayStation 5 (compatible with firmware 3.xx - 4.xx, or loaders supporting directory homebrew apps such as **ShadowMountPlus**).
* Active **Amazon Luna** subscription (Prime Gaming, Luna+, or linked Ubisoft Connect / Epic Games account).

### Steps
1. Build the binary using the compilation script or download the latest release package.
2. Transfer the `PPSA99825` folder to your PS5 using FTP or a USB drive:
   ```text
   /data/homebrew/PPSA99825/
   ```
3. Ensure the folder contents look like this:
   ```text
   /data/homebrew/PPSA99825/eboot.bin
   /data/homebrew/PPSA99825/sce_module/libc.prx
   /data/homebrew/PPSA99825/sce_sys/icon0.png
   /data/homebrew/PPSA99825/sce_sys/param.json
   ```
4. Launch the app from the PS5 home menu.
5. On the first launch, scan the QR code or visit `amazon.com/code` on your phone or PC to pair your account.

---

## 🛠️ Building from Source

The build pipeline uses LLVM/Clang targeting the FreeBSD PS5 ABI with a custom ELF-to-FSELF packager:

```powershell
powershell -ExecutionPolicy Bypass -File apps/luna-ps5/build_ps5.ps1
```

The script performs:
1. Compiles C++20 source files and system stub libraries.
2. Links an intermediate PIE ELF image.
3. Injects SCE program headers (`PT_SCE_PROCPARAM`) and resolves NIDs.
4. Generates and cryptographically signs `eboot.bin` and `sce_module/libc.prx` in **FSELF (`0x1D3D154F`)** format.
5. Verifies SHA-256 integrity of the final package.

---

## 🔒 Privacy & Security Notice

This application is intended strictly for personal interoperability using your own legitimate Amazon account. It does not bypass DRM, does not store or transmit credentials to third-party servers, and only connects directly to Amazon's official endpoints.

---

## 🤝 Acknowledgments & Credits

* **RafaelNGP** and the **PSBox** project for pioneering WebRTC cloud gaming on PS5.
* **SpecterDev**, **ChendoChap**, **flat_z**, and the PS5 scene researchers for homebrew toolchain foundations.
