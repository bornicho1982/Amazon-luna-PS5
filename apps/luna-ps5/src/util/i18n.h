#pragma once

#include <string>
#include <vector>

namespace luna::util {

enum class Language {
    ES, // es-ES
    EN_US, // en-US
    EN_GB, // en-GB
    DE, // de-DE
    FR, // fr-FR
    IT  // it-IT
};

struct RegionInfo {
    const char* code;          // "ES", "US", "GB", "DE", "FR", "IT"
    const char* name;          // "España", "United States", etc.
    const char* languageTag;   // "es-ES", "en-US", etc.
    const char* marketplaceId; // "A1RKKUPIHCS9HS", "ATVPDKIKX0DER", etc.
    const char* amazonDomain;  // "amazon.es", "amazon.com", etc.
    const char* clientProxy;   // "https://proxy-prod.eu-west-1.tempo.digital.a2z.com", etc.
    const char* locale;        // "es_ES", "en_US", etc.
    const char* country;       // "ES", "US", "GB", "DE", "FR", "IT"
    const char* acceptLang;    // "es-ES,es;q=0.9,en;q=0.8"
    Language langEnum;
};

class I18n {
public:
    static const std::vector<RegionInfo>& getAvailableRegions() {
        static const std::vector<RegionInfo> s_regions = {
            {"ES", "España (ES)", "es-ES", "A1RKKUPIHCS9HS", "amazon.es", "https://proxy-prod.eu-west-1.tempo.digital.a2z.com", "es_ES", "ES", "es-ES,es;q=0.9,en;q=0.8", Language::ES},
            {"US", "United States (US)", "en-US", "ATVPDKIKX0DER", "amazon.com", "https://proxy-prod.tempo.digital.a2z.com", "en_US", "US", "en-US,en;q=0.9", Language::EN_US},
            {"GB", "United Kingdom (GB)", "en-GB", "A1F83G8C2ARO7P", "amazon.co.uk", "https://proxy-prod.eu-west-1.tempo.digital.a2z.com", "en_GB", "GB", "en-GB,en;q=0.9", Language::EN_GB},
            {"DE", "Deutschland (DE)", "de-DE", "A1PA6795UKMFR9", "amazon.de", "https://proxy-prod.eu-west-1.tempo.digital.a2z.com", "de_DE", "DE", "de-DE,de;q=0.9,en;q=0.8", Language::DE},
            {"FR", "France (FR)", "fr-FR", "A13V1IB3VIYZZH", "amazon.fr", "https://proxy-prod.eu-west-1.tempo.digital.a2z.com", "fr_FR", "FR", "fr-FR,fr;q=0.9,en;q=0.8", Language::FR},
            {"IT", "Italia (IT)", "it-IT", "APJ6JRA9NG5V4", "amazon.it", "https://proxy-prod.eu-west-1.tempo.digital.a2z.com", "it_IT", "IT", "it-IT,it;q=0.9,en;q=0.8", Language::IT}
        };
        return s_regions;
    }

    static const RegionInfo& getCurrentRegion() {
        const auto& regions = getAvailableRegions();
        int idx = s_currentRegionIdx;
        if (idx < 0 || idx >= static_cast<int>(regions.size())) idx = 0;
        return regions[idx];
    }

    static int getCurrentRegionIndex() {
        return s_currentRegionIdx;
    }

    static void setRegionIndex(int idx) {
        const auto& regions = getAvailableRegions();
        if (idx >= 0 && idx < static_cast<int>(regions.size())) {
            s_currentRegionIdx = idx;
            s_currentLang = regions[idx].langEnum;
        }
    }

    static void setRegionByCode(const std::string& code) {
        const auto& regions = getAvailableRegions();
        for (size_t i = 0; i < regions.size(); ++i) {
            if (regions[i].code == code) {
                setRegionIndex(static_cast<int>(i));
                return;
            }
        }
    }

    static void nextRegion() {
        const auto& regions = getAvailableRegions();
        s_currentRegionIdx = (s_currentRegionIdx + 1) % static_cast<int>(regions.size());
        s_currentLang = regions[s_currentRegionIdx].langEnum;
    }

    static Language getCurrentLanguage() {
        return s_currentLang;
    }

    static void setLanguage(Language lang) {
        s_currentLang = lang;
    }

    // Traductores de interfaz
    static const char* trHome() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Home";
            case Language::DE: return "Startseite";
            case Language::FR: return "Accueil";
            case Language::IT: return "Inizio";
            default: return "Inicio";
        }
    }

    static const char* trGameNight() {
        return "GameNight";
    }

    static const char* trMyStuff() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "My stuff";
            case Language::DE: return "Meine Inhalte";
            case Language::FR: return "Mon espace";
            case Language::IT: return "Il mio spazio";
            default: return "Mi espacio";
        }
    }

    static const char* trRemotePlay() {
        return "Remote Play";
    }

    static const char* trPlayNow() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Play now";
            case Language::DE: return "Jetzt spielen";
            case Language::FR: return "Jouer";
            case Language::IT: return "Gioca ora";
            default: return "Jugar ahora";
        }
    }

    static const char* trConnecting() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "CONNECTING...";
            case Language::DE: return "VERBINDE...";
            case Language::FR: return "CONNEXION...";
            case Language::IT: return "CONNESSIONE...";
            default: return "CONECTANDO...";
        }
    }

    static const char* trLearnMore() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Learn more";
            case Language::DE: return "Mehr erfahren";
            case Language::FR: return "En savoir plus";
            case Language::IT: return "Ulteriori info";
            default: return "Más información";
        }
    }

    static const char* trSettings() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Settings";
            case Language::DE: return "Einstellungen";
            case Language::FR: return "Paramètres";
            case Language::IT: return "Impostazioni";
            default: return "Configuración";
        }
    }

    static const char* trSearch() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Search";
            case Language::DE: return "Suchen";
            case Language::FR: return "Rechercher";
            case Language::IT: return "Cerca";
            default: return "Buscar";
        }
    }

    static const char* trLogout() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Log out";
            case Language::DE: return "Abmelden";
            case Language::FR: return "Se déconnecter";
            case Language::IT: return "Disconnetti";
            default: return "Cerrar sesión";
        }
    }

    static const char* trReloadCatalog() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Reload catalog";
            case Language::DE: return "Katalog neu laden";
            case Language::FR: return "Recharger le catalogue";
            case Language::IT: return "Ricarica catalogo";
            default: return "Recargar catálogo";
        }
    }

    static const char* trTrailerAndScreenshots() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Trailer and screenshots";
            case Language::DE: return "Trailer und Screenshots";
            case Language::FR: return "Bande-annonce et captures d'écran";
            case Language::IT: return "Trailer e screenshot";
            default: return "Tráiler y capturas de pantalla";
        }
    }

    static const char* trSimilarGames() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "You might also like";
            case Language::DE: return "Das könnte dir auch gefallen";
            case Language::FR: return "Vous pourriez aussi aimer";
            case Language::IT: return "Potrebbe interessarti anche";
            default: return "También te puede interesar";
        }
    }

    static const char* trFavoritesAdded() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Game added to Favorites.";
            case Language::DE: return "Zu Favoriten hinzugefügt.";
            case Language::FR: return "Jeu ajouté aux favoris.";
            case Language::IT: return "Aggiunto ai preferiti.";
            default: return "Juego añadido a Favoritos.";
        }
    }

    static const char* trFavoritesRemoved() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Game removed from Favorites.";
            case Language::DE: return "Aus Favoriten entfernt.";
            case Language::FR: return "Jeu retiré des favoris.";
            case Language::IT: return "Rimosso dai preferiti.";
            default: return "Juego quitado de Favoritos.";
        }
    }

    static const char* trExitCombo() {
        switch (s_currentLang) {
            case Language::EN_US:
            case Language::EN_GB: return "Press L1 + R1 + OPTIONS + TOUCHPAD to return to catalog";
            case Language::DE: return "Drücke L1 + R1 + OPTIONS + TOUCHPAD zum Beenden";
            case Language::FR: return "Appuyez sur L1 + R1 + OPTIONS + TOUCHPAD pour quitter";
            case Language::IT: return "Premi L1 + R1 + OPTIONS + TOUCHPAD per tornare al catalogo";
            default: return "Pulsa L1 + R1 + OPTIONS + TOUCHPAD para volver al catalogo";
        }
    }

private:
    static inline int s_currentRegionIdx = 0; // Default ES
    static inline Language s_currentLang = Language::ES;
};

} // namespace luna::util
