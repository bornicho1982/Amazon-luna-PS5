#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace luna::input {

// Máscaras de botones normalizadas (compatibles con estándar XInput / Luna)
enum PadButton : uint32_t {
    PAD_CROSS    = 1 << 0,  // A
    PAD_CIRCLE   = 1 << 1,  // B
    PAD_SQUARE   = 1 << 2,  // X
    PAD_TRIANGLE = 1 << 3,  // Y
    PAD_L1       = 1 << 4,  // LB
    PAD_R1       = 1 << 5,  // RB
    PAD_L3       = 1 << 6,  // LS
    PAD_R3       = 1 << 7,  // RS
    PAD_UP       = 1 << 8,
    PAD_DOWN     = 1 << 9,
    PAD_LEFT     = 1 << 10,
    PAD_RIGHT    = 1 << 11,
    PAD_OPTIONS  = 1 << 12, // Start / Menu
    PAD_TOUCH    = 1 << 13, // Select / View
    PAD_L2       = 1 << 14, // Left Trigger
    PAD_R2       = 1 << 15, // Right Trigger
};

struct GamepadState {
    uint32_t buttons = 0;
    int16_t leftX = 0;
    int16_t leftY = 0;
    int16_t rightX = 0;
    int16_t rightY = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    bool connected = false;
};

class PadManager {
public:
    PadManager();
    ~PadManager();

    bool init();
    void shutdown();
    void poll();

    const GamepadState& getState(int padIndex = 0) const;
    bool isExitComboPressed() const;
    int32_t getUserId() const { return userId_; }
    int32_t getHandle() const { return padHandle_; }
    bool isConnected() const { return state_.connected; }

    void setVibration(uint8_t largeMotor, uint8_t smallMotor);
    void setLightBar(uint8_t r, uint8_t g, uint8_t b);

    void serializeReport(uint8_t* buffer, size_t maxLen, size_t& outLen, uint32_t seq, int padIndex = 0);
    void serializeLunaProtobufReport(uint8_t* buffer, size_t maxLen, size_t& outLen, uint32_t packetNum, int padIndex = 0);
    void serializeGamepadConnectionEvent(uint8_t* buffer, size_t maxLen, size_t& outLen, uint32_t deviceId = 0, uint32_t status = 0, uint32_t devicetype = 1);

private:
    int32_t userId_ = -1;
    int32_t padHandle_ = -1;
    GamepadState state_{};
};

} // namespace luna::input
