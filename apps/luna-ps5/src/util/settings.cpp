#include "util/settings.h"
#include "util/crash_logger.h"
#include "util/json_parser.h"
#include <cstdio>
#include <sys/stat.h>

namespace luna::util {

AppSettings& AppSettings::instance() {
    static AppSettings s_inst;
    return s_inst;
}

static std::string getDefaultSettingsPath() {
    struct stat st;
    if (::stat("/data/homebrew/PPSA99825", &st) == 0) {
        return "/data/homebrew/PPSA99825/settings.json";
    }
    return "settings.json";
}

bool AppSettings::load(const std::string& customPath) {
    std::string path = customPath.empty() ? getDefaultSettingsPath() : customPath;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        CrashLogger::log("[Settings] No se encontro %s, usando valores por defecto", path.c_str());
        I18n::setRegionByCode(region);
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) {
        std::fclose(f);
        return false;
    }

    std::string content(sz, '\0');
    std::fread(&content[0], 1, sz, f);
    std::fclose(f);

    JsonValue root = JsonValue::parse(content);
    if (!root.isObject()) {
        CrashLogger::log("[Settings] Error parseando JSON en %s", path.c_str());
        return false;
    }

    int vq = root["videoQuality"].asInt(0);
    videoQuality = (vq == 1) ? VideoQuality::P720_60 : VideoQuality::P1080_60;

    int hm = root["hudMode"].asInt(2);
    // Legacy settings files (saved before the HUD default changed) are migrated to HUD hidden
    if (root["hudDefaultV2"].asInt(0) == 0) hm = 2;
    if (hm == 1) hudMode = HudMode::Always;
    else if (hm == 2) hudMode = HudMode::Hidden;
    else hudMode = HudMode::Auto;

    vibrationEnabled = root["vibrationEnabled"].asBool(true);
    deadzonePercent = root["deadzonePercent"].asInt(6);
    if (deadzonePercent != 4 && deadzonePercent != 6 && deadzonePercent != 10) {
        deadzonePercent = 6;
    }

    std::string reg = root["region"].asString("ES");
    if (!reg.empty()) {
        region = reg;
        I18n::setRegionByCode(region);
    }
    language = I18n::getCurrentRegion().languageTag;

    CrashLogger::log("[Settings] Cargado: Q=%d, HUD=%d, VIB=%d, DZ=%d%%, REG=%s, LANG=%s",
                     static_cast<int>(videoQuality), static_cast<int>(hudMode),
                     vibrationEnabled ? 1 : 0, deadzonePercent, region.c_str(), language.c_str());
    return true;
}

bool AppSettings::save(const std::string& customPath) const {
    std::string path = customPath.empty() ? getDefaultSettingsPath() : customPath;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        CrashLogger::log("[Settings] Error abriendo %s para guardar", path.c_str());
        return false;
    }

    std::string json = "{\n";
    json += "  \"videoQuality\": " + std::to_string(static_cast<int>(videoQuality)) + ",\n";
    json += "  \"hudMode\": " + std::to_string(static_cast<int>(hudMode)) + ",\n";
    json += "  \"hudDefaultV2\": 1,\n";
    json += "  \"vibrationEnabled\": " + std::string(vibrationEnabled ? "true" : "false") + ",\n";
    json += "  \"deadzonePercent\": " + std::to_string(deadzonePercent) + ",\n";
    json += "  \"region\": \"" + region + "\",\n";
    json += "  \"language\": \"" + language + "\"\n";
    json += "}\n";

    std::fwrite(json.data(), 1, json.size(), f);
    std::fclose(f);
    CrashLogger::log("[Settings] Guardado en %s", path.c_str());
    return true;
}

void AppSettings::cycleVideoQuality() {
    if (videoQuality == VideoQuality::P1080_60) {
        videoQuality = VideoQuality::P720_60;
    } else {
        videoQuality = VideoQuality::P1080_60;
    }
}

void AppSettings::cycleHudMode() {
    if (hudMode == HudMode::Auto) {
        hudMode = HudMode::Always;
    } else if (hudMode == HudMode::Always) {
        hudMode = HudMode::Hidden;
    } else {
        hudMode = HudMode::Auto;
    }
}

void AppSettings::toggleVibration() {
    vibrationEnabled = !vibrationEnabled;
}

void AppSettings::cycleDeadzone() {
    if (deadzonePercent == 4) deadzonePercent = 6;
    else if (deadzonePercent == 6) deadzonePercent = 10;
    else deadzonePercent = 4;
}

void AppSettings::cycleRegion() {
    I18n::nextRegion();
    const auto& regInfo = I18n::getCurrentRegion();
    region = regInfo.code;
    language = regInfo.languageTag;
}

const char* AppSettings::getVideoQualityLabel() const {
    if (videoQuality == VideoQuality::P1080_60) {
        return "1080p 60 FPS (Recomendado, ~10 GB/h)";
    }
    return "720p 60 FPS (Ahorro de datos, ~4.5 GB/h)";
}

const char* AppSettings::getHudModeLabel() const {
    if (hudMode == HudMode::Auto) return "Automatico (se oculta a los 3 seg)";
    if (hudMode == HudMode::Always) return "Siempre visible";
    return "Oculto";
}

const char* AppSettings::getVibrationLabel() const {
    return vibrationEnabled ? "Activada" : "Desactivada";
}

const char* AppSettings::getDeadzoneLabel() const {
    if (deadzonePercent == 4) return "Sensible (4%)";
    if (deadzonePercent == 10) return "Amplia (10%)";
    return "Estandar (6%)";
}

const char* AppSettings::getRegionLabel() const {
    return I18n::getCurrentRegion().name;
}

} // namespace luna::util
