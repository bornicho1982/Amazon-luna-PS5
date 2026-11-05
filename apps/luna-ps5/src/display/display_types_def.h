#pragma once

#include "display/types.h"
#include <functional>

namespace luna::display {

struct GameTile {
    std::string id;
    std::string gameId;  // ID único del juego (para identificación en backend)
    std::string title;
    std::string subtitle;
    std::string buttonText = "▶ Jugar ahora";
    std::string secondaryButtonText = "";
    bool isEntitled = true;
    std::string playToken;
    std::string buildId;

    std::string publisher;
    std::string developer;
    std::string releaseYear;
    std::string genres;
    std::string inputMethods = "Mando DualSense";
    std::string playerCount = "1 jugador";
    std::string metacritic;
    std::string channelSubtext = "Incluido con Prime";
    uint32_t badgeColor = 0xFF9146FF;
    std::string category = "LUNA+";
    std::string synopsis = "Disponible para jugar al instante en Amazon Luna Cloud.";
    uint32_t heroColor = 0xFF9146FF;

    std::string coverUrl;
    std::string thumbnailUrl;  // URL de miniatura para display
    std::string backdropUrl;
    std::string logoUrl;
    std::vector<std::string> screenshotUrls;
    std::string videoUrl;

    std::vector<uint8_t> coverRgba;
    int coverWidth = 0;
    int coverHeight = 0;
    std::string pegiRating = "PEGI 12";
    std::vector<std::string> ageRatingDescriptors;
    bool detailsLoaded = false;
    std::string lastPlayedDate;
    std::vector<GameTile> similarGames;
};

struct LunaCollection {
    std::string id;
    std::string title;
    std::string subtitle;
    bool isHeroRotator = false;
    bool isRankings = false;
    bool isPartnerRow = false;
    bool isCategoryCarousel = false;
    bool isRecentlyPlayed16_9 = false;
    std::string partnerPromoImage;
    std::string partnerTitle;
    std::string partnerDescription;
    std::string partnerButtonText = "Ver todos los juegos";
    std::string gameId;  // ID del juego principal en la colección (si aplica)
    std::string thumbnailUrl;  // Miniatura de la colección
    std::vector<GameTile> items;
};

struct PlayerProfileInfo {
    std::string displayName = "Bornicho";
    std::string discriminator = "#8663";
    std::string planStatus = "Incluido con Prime";
    std::string avatarUrl = "https://m.media-amazon.com/images/G/01/T/TC05316420/A07531864/Avatars/Privates_Poo.png";
    std::string profileId = "amzn1.actor.person.oid.A24FP4VJ7KI5CD";
};

} // namespace luna::display
