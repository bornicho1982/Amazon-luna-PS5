# Amazon Luna Reverse-Engineered Protocol Specification

This specification documents the network protocol, signaling, WebRTC negotiation, and input binary format for **Amazon Luna** (code-named *Tempo*), extracted via static analysis of the official Amazon Luna web client bundles (`m.media-amazon.com/images/S/sash/*.js`) and confirmed by runtime inspection.

---

## 1. Core Endpoints & Architecture

Amazon Luna does not use AWS Kinesis Video Streams (KVS) directly from the client. Instead, it utilizes an internal microservice fleet named **Tempo Proxy**.

### Endpoints (Production)
- **Base Proxy URL**: `https://proxy-prod.tempo.digital.a2z.com`
  *(Gamma/Beta endpoints exist: `proxy-gamma.tempo.digital.a2z.com` / `proxy-beta.tempo.digital.a2z.com`)*
- **Create Session Job**: `POST https://proxy-prod.tempo.digital.a2z.com/createGameSessionJob`
- **Poll Session Status**: `POST https://proxy-prod.tempo.digital.a2z.com/getGameSessionJob`
- **Terminate Session**: `POST https://proxy-prod.tempo.digital.a2z.com/terminateGameSession`
- **Cancel Session Job**: `POST https://proxy-prod.tempo.digital.a2z.com/cancelGameSessionJob`
- **Get Active Session**: `POST https://proxy-prod.tempo.digital.a2z.com/getPlayingGameSession`

---

## 2. HTTP Headers & Authentication

All API requests to the Tempo Proxy require standard Amazon metadata headers:

```http
POST /createGameSessionJob HTTP/1.1
Host: proxy-prod.tempo.digital.a2z.com
Content-Type: application/json
x-amz-access-token: <Amazon OAuth Bearer Access Token>
x-amz-locale: es-ES (or en-US)
x-amz-marketplace-id: A1RKKUPIHCS9HS (or ATVPDKIKX0DER for US)
x-amz-country-of-residence: ES (or US)
x-amz-client-version: 1.0.0 (or current Luna web version)
x-amz-device-type: A1PY8QQU9P0WJV (or Luna Web Client ID)
x-amz-device-serial-number: <UUID or stable client DSN>
x-amz-timezone: Europe/Madrid
x-amzn-RequestId: <UUIDv4>
```

---

## 3. Session Creation Workflow

```
Client (PS5)                                 Tempo Proxy (AWS)
    |                                                |
    |---- 1. createGameSessionJob(offer SDP) ------->|
    |<--- 2. { gameSessionJobId, status: QUEUED } ---|
    |                                                |
    | [Loop every 1-2s]                              |
    |---- 3. getGameSessionJob(gameSessionJobId) --->|
    |<--- 4. { status: COMPLETED, sdp answer, IP } --|
    |                                                |
    |================ WebRTC Handshake =============|
    | (ICE / DTLS-SRTP to publicIp on port 33438)   |
```

### 3.1 Request Payload: `createGameSessionJob`
```json
{
  "gameId": "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16",
  "signalingRequest": "{\"type\":\"offer\",\"sdp\":\"v=0...\"}",
  "timeout": 30000,
  "clientFeatures": {
    "h264": true,
    "opus": true,
    "dataChannels": true
  },
  "clientContext": {
    "clientType": "BROWSER",
    "deviceType": "PLAYSTATION_5"
  }
}
```

### 3.2 Response Payload: `getGameSessionJob`
```json
{
  "gameSession": {
    "connectionId": "1997913608",
    "gameId": "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16",
    "ipVersion": "IPV4",
    "protocol": "WEB_RTC",
    "publicIp": "15.217.4.14",
    "region": "eu-south-2",
    "routing": "TURN",
    "sessionId": "6f2315cc-2792-41ca-a93d-d7db4ad13b6b",
    "status": "AVAILABLE",
    "transport": "UDP",
    "signalingResponse": "{\"type\":\"answer\",\"sdp\":\"v=0\\r\\no=- ...\\r\\nm=audio 33438 UDP/TLS/RTP/SAVPF 111...\\r\\nm=video 9 UDP/TLS/RTP/SAVPF 108...\\r\\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel...\"}"
  },
  "gameSessionJobId": "amzn1.tempo.sessionjob.30e1eea5-0790-4c51-8dfa-9e217664940f",
  "status": "COMPLETED_SUCCEEDED"
}
```

---

## 4. Media & Codec Specifications

- **DRM**: **NONE**. The stream is not wrapped in EME, Widevine, or FairPlay.
- **Video Codec**: **H.264 (AVC)**
  - Profile: `42e01f` (Constrained Baseline Profile, Level 3.1)
  - RTP Payload Type: `108`
  - Feedback Mechanisms: `goog-remb`, `transport-cc`, `nack`, `ccm fir`, `pli`
- **Audio Codec**: **Opus**
  - Sampling Rate: 48,000 Hz, 2 Channels (Stereo)
  - RTP Payload Type: `111`
- **Transport Security**: DTLS-SRTP (Fingerprint: SHA-256)
- **Role**: Remote server is `setup:active` (Client must be `setup:passive` / act as DTLS server or respond to DTLS ClientHello).

---

## 5. WebRTC DataChannels & Input Protocol

Luna establishes two WebRTC DataChannels:
1. `clientMessageChannel`: Configured with `{ ordered: true }` for session lifecycle, heartbeat, and telemetry messages.
2. `inputChannel`: Configured with `{ ordered: false, maxRetransmits: 0 }` for ultra-low latency controller, mouse, and keyboard input.

### 5.1 Gamepad Input Packet Format (Protobuf Binary)
Messages sent over `inputChannel` are serialized using Google Protocol Buffers (`ClientMessage` containing a `GamePadEvent`).

#### Protobuf Structure
```protobuf
message ClientMessage {
  optional GamePadEvent gamePadEvent = 1;
  optional ResiliencyAttributes resiliencyAttributes = 2;
}

message GamePadEvent {
  optional uint32 deviceId = 1;
  optional uint32 dwPacketNumber = 2;
  optional uint32 deviceType = 3;        // 0 = DS4, 1 = X360
  optional uint32 wButtons = 4;          // Bitmask of pressed buttons
  optional uint32 bLeftTrigger = 5;      // 0 - 255
  optional uint32 bRightTrigger = 6;     // 0 - 255
  optional int32 sThumbLX = 7;           // 0 - 65535 (center: 32767)
  optional int32 sThumbLY = 8;           // 0 - 65535 (center: 32767)
  optional int32 sThumbRX = 9;           // 0 - 65535 (center: 32767)
  optional int32 sThumbRY = 10;          // 0 - 65535 (center: 32767)
  optional TempoInputLatency til = 13;
}

message TempoInputLatency {
  optional double timestamp = 1;
  optional double captureTimestampMs = 2;
  optional uint32 hardwareClass = 3;     // 3 = CLASS_CONTROLLER_DUALSENSE
}
```

#### Button Bitmask Mapping
| Button | Bit Value (Hex) | Decimal | Note |
|---|---|---|---|
| D-Pad Up | `0x0001` | 1 | |
| D-Pad Down | `0x0002` | 2 | |
| D-Pad Left | `0x0004` | 4 | |
| D-Pad Right | `0x0008` | 8 | |
| Start / Options | `0x0010` | 16 | |
| Back / Share / Select | `0x0020` | 32 | |
| L3 (Left Stick Click) | `0x0040` | 64 | |
| R3 (Right Stick Click) | `0x0080` | 128 | |
| L1 (Left Bumper) | `0x0100` | 256 | |
| R1 (Right Bumper) | `0x0200` | 512 | |
| Cross / A | `0x1000` | 4096 | |
| Circle / B | `0x2000` | 8192 | |
| Square / X | `0x4000` | 16384 | |
| Triangle / Y | `0x8000` | 32768 | |
| Home / Guide / PS Button | `0x10000` | 65536 | |

#### Controller Hardware Class
Luna explicitly has a native hardware class for the PlayStation 5 DualSense:
- `CLASS_CONTROLLER_DUALSENSE = 3`

---

## 6. Authentication Architecture for PS5

Because Amazon Luna SmartTVs use **Code-Based Linking (CBL)**:
1. **Option A (Pure Native CBL)**:
   - Call Amazon Device Auth endpoint to request a device code pair.
   - Display on PS5 screen: *"Go to amazon.com/code and enter: ABCD-1234"*.
   - Poll Amazon OAuth endpoint until user completes login. Receive `access_token` and `refresh_token`.
2. **Option B (Local LAN Web Server - Already implemented in PPSA99825)**:
   - PS5 hosts a tiny web page on port `8085` (`http://192.168.18.21:8085`).
   - The user opens this page on their phone or PC (where they are already logged into Amazon).
   - An embedded script or 1-click button extracts the `access_token` and posts it to the PS5.
