#include "input/pad_manager.h"
#include <cstring>
#include <cstdio>
#include <cstddef>

#if defined(LUNA_TARGET_PS5)
extern "C" {
struct ScePadStick {
    uint8_t x;
    uint8_t y;
};

struct ScePadTouchPoint {
    uint16_t x;
    uint16_t y;
    uint8_t id;
    uint8_t reserve[3];
};

struct ScePadTouchData {
    uint8_t touchNum;
    uint8_t reserve[3];
    uint32_t reserve1;
    ScePadTouchPoint touch[2];
};

// Layout oficial de Orbis/Prospero ScePadData (connected en offset 76)
struct ScePadData {
    uint32_t buttons;
    ScePadStick leftStick;
    ScePadStick rightStick;
    uint8_t l2;
    uint8_t r2;
    uint16_t padding;
    float orientation[4];
    float acceleration[3];
    float angularVelocity[3];
    ScePadTouchData touchData;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t count;
    uint8_t unknown[15];
};
static_assert(offsetof(ScePadData, connected) == 76, "ScePadData layout mismatch");

struct SceUserServiceInitializeParams {
    uint32_t priority;
};

struct SceUserServiceLoginUserIdList {
    int32_t userId[4];
};

struct ScePadVibrationParam {
    uint8_t largeMotor;
    uint8_t smallMotor;
};

struct ScePadColor {
    uint8_t r, g, b, reserved;
};

int sceSysmoduleLoadModule(uint32_t id);
int sceUserServiceInitialize(const SceUserServiceInitializeParams* param);
int sceUserServiceGetInitialUser(int32_t* userId);
int sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList* list);
int scePadInit(void);
int scePadOpen(int32_t userId, int32_t type, int32_t index, const void* param);
int scePadClose(int32_t handle);
int scePadReadState(int32_t handle, ScePadData* pData);
int scePadSetVibrationMode(int32_t handle, int32_t mode);
int scePadSetVibration(int32_t handle, const ScePadVibrationParam* param);
int scePadSetLightBar(int32_t handle, const ScePadColor* color);
int scePadResetLightBar(int32_t handle);
}
#endif

namespace luna::input {

PadManager::PadManager() {
    std::memset(&state_, 0, sizeof(state_));
}

PadManager::~PadManager() {
    shutdown();
}

bool PadManager::init() {
#if defined(LUNA_TARGET_PS5)
    sceSysmoduleLoadModule(0x0002); // libScePad
    sceSysmoduleLoadModule(0x0014); // libSceUserService

    int rc = scePadInit();
    if (rc != 0 && rc != static_cast<int>(0x80920002)) {
        std::printf("[PadManager] Advertencia en scePadInit: 0x%08X\n", rc);
    }

    userId_ = -1;
    rc = sceUserServiceGetInitialUser(&userId_);
    if (rc != 0 || userId_ < 0) {
        SceUserServiceInitializeParams param{0x2FF};
        sceUserServiceInitialize(&param);
        rc = sceUserServiceGetInitialUser(&userId_);
    }

    if (rc != 0 || userId_ < 0) {
        SceUserServiceLoginUserIdList list{};
        std::memset(&list, 0xFF, sizeof(list));
        if (sceUserServiceGetLoginUserIdList(&list) == 0) {
            for (int i = 0; i < 4; ++i) {
                if (list.userId[i] >= 0) {
                    userId_ = list.userId[i];
                    break;
                }
            }
        }
    }

    if (userId_ >= 0) {
        padHandle_ = scePadOpen(userId_, 0, 0, nullptr);
    }

    if (padHandle_ < 0) {
        // Fallback: probar usuarios comunes en PS5 (100, 101, 1, 0, 0xFE)
        int32_t tryUsers[] = {100, 101, 102, 1, 0, 0xFE};
        for (int32_t u : tryUsers) {
            padHandle_ = scePadOpen(u, 0, 0, nullptr);
            if (padHandle_ >= 0) {
                userId_ = u;
                std::printf("[PadManager] scePadOpen tuvo exito con user=%d (handle=%d)\n", u, padHandle_);
                break;
            }
        }
    }

    if (padHandle_ < 0) {
        state_.connected = false;
        return false;
    }

    // Modo 2 = Vibracion clasica de doble motor en DualSense
    scePadSetVibrationMode(padHandle_, 2);
    // Color morado/azul caracteristico de Amazon Luna en el LightBar
    setLightBar(110, 40, 220);

    state_.connected = true;
    std::printf("[PadManager] DualSense vinculado a user=%d (handle=%d)\n", userId_, padHandle_);
    return true;
#else
    state_.connected = true;
    std::printf("[PadManager] Modo Desktop activado\n");
    return true;
#endif
}

void PadManager::shutdown() {
#if defined(LUNA_TARGET_PS5)
    if (padHandle_ >= 0) {
        setVibration(0, 0);
        scePadResetLightBar(padHandle_);
        scePadClose(padHandle_);
        padHandle_ = -1;
    }
#endif
    state_.connected = false;
}

void PadManager::poll() {
#if defined(LUNA_TARGET_PS5)
    if (padHandle_ < 0) {
        init();
        if (padHandle_ < 0) {
            state_.connected = false;
            return;
        }
    }

    ScePadData raw{};
    int rc = scePadReadState(padHandle_, &raw);
    if (rc != 0) {
        scePadClose(padHandle_);
        padHandle_ = -1;
        state_.connected = false;
        return;
    }

    state_.connected = true;
    uint32_t b = raw.buttons;
    uint32_t mapped = 0;

    // Mapeo oficial de bits de botones PS5 a mascara unificada Luna/XInput
    if (b & 0x4000)   mapped |= PAD_CROSS;
    if (b & 0x2000)   mapped |= PAD_CIRCLE;
    if (b & 0x8000)   mapped |= PAD_SQUARE;
    if (b & 0x1000)   mapped |= PAD_TRIANGLE;
    if (b & 0x0400)   mapped |= PAD_L1;
    if (b & 0x0800)   mapped |= PAD_R1;
    if (b & 0x0002)   mapped |= PAD_L3;
    if (b & 0x0004)   mapped |= PAD_R3;
    if (b & 0x0010)   mapped |= PAD_UP;
    if (b & 0x0040)   mapped |= PAD_DOWN;
    if (b & 0x0080)   mapped |= PAD_LEFT;
    if (b & 0x0020)   mapped |= PAD_RIGHT;
    if (b & 0x0008)   mapped |= PAD_OPTIONS;
    if (b & 0x100000) mapped |= PAD_TOUCH;
    if ((b & 0x0100) || raw.l2 > 30) mapped |= PAD_L2;
    if ((b & 0x0200) || raw.r2 > 30) mapped |= PAD_R2;

    state_.buttons = mapped;
    state_.leftX = static_cast<int16_t>((static_cast<int>(raw.leftStick.x) - 128) * 256);
    state_.leftY = static_cast<int16_t>((static_cast<int>(raw.leftStick.y) - 128) * 256);
    state_.rightX = static_cast<int16_t>((static_cast<int>(raw.rightStick.x) - 128) * 256);
    state_.rightY = static_cast<int16_t>((static_cast<int>(raw.rightStick.y) - 128) * 256);
    state_.leftTrigger = raw.l2;
    state_.rightTrigger = raw.r2;
#endif
}

const GamepadState& PadManager::getState(int padIndex) const {
    (void)padIndex;
    return state_;
}

bool PadManager::isExitComboPressed() const {
    // Combo de salida: L1 + R1 + OPTIONS + TOUCHPAD
    constexpr uint32_t kExitMask = PAD_L1 | PAD_R1 | PAD_OPTIONS | PAD_TOUCH;
    return (state_.buttons & kExitMask) == kExitMask;
}

void PadManager::setVibration(uint8_t largeMotor, uint8_t smallMotor) {
#if defined(LUNA_TARGET_PS5)
    if (padHandle_ >= 0) {
        ScePadVibrationParam param{largeMotor, smallMotor};
        scePadSetVibration(padHandle_, &param);
    }
#else
    (void)largeMotor; (void)smallMotor;
#endif
}

void PadManager::setLightBar(uint8_t r, uint8_t g, uint8_t b) {
#if defined(LUNA_TARGET_PS5)
    if (padHandle_ >= 0) {
        ScePadColor color{r, g, b, 0};
        scePadSetLightBar(padHandle_, &color);
    }
#else
    (void)r; (void)g; (void)b;
#endif
}

void PadManager::serializeReport(uint8_t* buffer, size_t maxLen, size_t& outLen, uint32_t seq, int padIndex) {
    if (maxLen < 20) {
        outLen = 0;
        return;
    }

    buffer[0] = 0x01; // Reporte de Gamepad
    buffer[1] = static_cast<uint8_t>(padIndex & 0xFF);

    buffer[2] = static_cast<uint8_t>((seq >> 0) & 0xFF);
    buffer[3] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    buffer[4] = static_cast<uint8_t>((seq >> 16) & 0xFF);
    buffer[5] = static_cast<uint8_t>((seq >> 24) & 0xFF);

    buffer[6] = static_cast<uint8_t>((state_.buttons >> 0) & 0xFF);
    buffer[7] = static_cast<uint8_t>((state_.buttons >> 8) & 0xFF);
    buffer[8] = static_cast<uint8_t>((state_.buttons >> 16) & 0xFF);
    buffer[9] = static_cast<uint8_t>((state_.buttons >> 24) & 0xFF);

    std::memcpy(&buffer[10], &state_.leftX, 2);
    std::memcpy(&buffer[12], &state_.leftY, 2);
    std::memcpy(&buffer[14], &state_.rightX, 2);
    std::memcpy(&buffer[16], &state_.rightY, 2);
    buffer[18] = state_.leftTrigger;
    buffer[19] = state_.rightTrigger;

    outLen = 20;
}

namespace {
inline size_t writeVarint(uint8_t* buf, uint64_t val) {
    size_t i = 0;
    while (val >= 0x80) {
        buf[i++] = static_cast<uint8_t>((val & 0x7F) | 0x80);
        val >>= 7;
    }
    buf[i++] = static_cast<uint8_t>(val & 0x7F);
    return i;
}
} // namespace

void PadManager::serializeLunaProtobufReport(uint8_t* buffer, size_t maxLen, size_t& outLen, uint32_t packetNum, int padIndex) {
    if (maxLen < 64) {
        outLen = 0;
        return;
    }

    // 1. Mapear botones a la máscara oficial de Amazon Luna
    uint32_t lunaButtons = 0;
    if (state_.buttons & PAD_UP)       lunaButtons |= 0x0001;
    if (state_.buttons & PAD_DOWN)     lunaButtons |= 0x0002;
    if (state_.buttons & PAD_LEFT)     lunaButtons |= 0x0004;
    if (state_.buttons & PAD_RIGHT)    lunaButtons |= 0x0008;
    if (state_.buttons & PAD_OPTIONS)  lunaButtons |= 0x0010;
    if (state_.buttons & PAD_TOUCH)    lunaButtons |= 0x0020;
    if (state_.buttons & PAD_L3)       lunaButtons |= 0x0040;
    if (state_.buttons & PAD_R3)       lunaButtons |= 0x0080;
    if (state_.buttons & PAD_L1)       lunaButtons |= 0x0100;
    if (state_.buttons & PAD_R1)       lunaButtons |= 0x0200;
    if (state_.buttons & PAD_CROSS)    lunaButtons |= 0x1000;
    if (state_.buttons & PAD_CIRCLE)   lunaButtons |= 0x2000;
    if (state_.buttons & PAD_SQUARE)   lunaButtons |= 0x4000;
    if (state_.buttons & PAD_TRIANGLE) lunaButtons |= 0x8000;

    // Normalizar sticks a rango 0-65535 (centro en 32767)
    uint32_t thumbLX = static_cast<uint32_t>(state_.leftX + 32768);
    uint32_t thumbLY = static_cast<uint32_t>(32767 - state_.leftY);
    uint32_t thumbRX = static_cast<uint32_t>(state_.rightX + 32768);
    uint32_t thumbRY = static_cast<uint32_t>(32767 - state_.rightY);

    // 2. Serializar sub-mensaje GamePadEvent
    uint8_t evtBuf[64];
    size_t e = 0;

    // tag 1: deviceId (varint)
    evtBuf[e++] = (1 << 3) | 0;
    e += writeVarint(evtBuf + e, padIndex);

    // tag 2: dwPacketNumber (varint)
    evtBuf[e++] = (2 << 3) | 0;
    e += writeVarint(evtBuf + e, packetNum);

    // tag 3: deviceType (0 = DS4/DualSense)
    evtBuf[e++] = (3 << 3) | 0;
    e += writeVarint(evtBuf + e, 0);

    // tag 4: wButtons (varint)
    evtBuf[e++] = (4 << 3) | 0;
    e += writeVarint(evtBuf + e, lunaButtons);

    // tag 5: bLeftTrigger (varint)
    evtBuf[e++] = (5 << 3) | 0;
    e += writeVarint(evtBuf + e, state_.leftTrigger);

    // tag 6: bRightTrigger (varint)
    evtBuf[e++] = (6 << 3) | 0;
    e += writeVarint(evtBuf + e, state_.rightTrigger);

    // tag 7: sThumbLX (varint)
    evtBuf[e++] = (7 << 3) | 0;
    e += writeVarint(evtBuf + e, thumbLX);

    // tag 8: sThumbLY (varint)
    evtBuf[e++] = (8 << 3) | 0;
    e += writeVarint(evtBuf + e, thumbLY);

    // tag 9: sThumbRX (varint)
    evtBuf[e++] = (9 << 3) | 0;
    e += writeVarint(evtBuf + e, thumbRX);

    // tag 10: sThumbRY (varint)
    evtBuf[e++] = (10 << 3) | 0;
    e += writeVarint(evtBuf + e, thumbRY);

    // 3. Envolver en ClientMessage (tag 1: length-delimited, wire type 2 = 0x0A)
    size_t outIdx = 0;
    buffer[outIdx++] = 0x0A;
    outIdx += writeVarint(buffer + outIdx, e);
    std::memcpy(buffer + outIdx, evtBuf, e);
    outIdx += e;

    outLen = outIdx;
}

} // namespace luna::input
