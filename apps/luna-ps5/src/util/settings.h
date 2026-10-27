#pragma once

#include <string>
#include <cstdint>
#include "util/i18n.h"

namespace luna::util {

enum class VideoQuality {
    P1080_60 = 0,
    P720_60 = 1
};

enum class HudMode {
    Auto = 0,
    Always = 1,
    Hidden = 2
};

struct AppSettings {
    VideoQuality videoQuality = VideoQuality::P1080_60;
    HudMode hudMode = HudMode::Auto;
    bool vibrationEnabled = true;
    int deadzonePercent = 6; // 4, 6, 10
    std::string language = "es-ES";
    std::string region = "ES";

    static AppSettings& instance();

    bool load(const std::string& customPath = "");
    bool save(const std::string& customPath = "") const;

    void cycleVideoQuality();
    void cycleHudMode();
    void toggleVibration();
    void cycleDeadzone();
    void cycleRegion();

    const char* getVideoQualityLabel() const;
    const char* getHudModeLabel() const;
    const char* getVibrationLabel() const;
    const char* getDeadzoneLabel() const;
    const char* getRegionLabel() const;
};

} // namespace luna::util
