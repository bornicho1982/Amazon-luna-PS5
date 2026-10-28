import sys

with open("apps/luna-ps5/src/display/display_manager.h", "r", encoding="utf-8") as f:
    c = f.read()

target1 = """    bool detailsLoaded = false;
    std::vector<GameTile> similarGames;
};"""

replacement1 = """    bool detailsLoaded = false;
    std::string lastPlayedDate;
    std::vector<GameTile> similarGames;
};"""

target2 = """struct LunaCollection {
    std::string id;
    std::string title;
    std::string subtitle;
    bool isHeroRotator = false;
    bool isRankings = false;
    std::vector<GameTile> items;
};"""

replacement2 = """struct LunaCollection {
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
    std::vector<GameTile> items;
};"""

target3 = """struct PlayerProfileInfo {
    std::string displayName = "Bornicho";
    std::string discriminator = "#8663";
    std::string planStatus = "Incluido con Prime";
};"""

replacement3 = """struct PlayerProfileInfo {
    std::string displayName = "Bornicho";
    std::string discriminator = "#8663";
    std::string planStatus = "Incluido con Prime";
    std::string avatarUrl = "https://m.media-amazon.com/images/G/01/T/TC05316420/A07531864/Avatars/Privates_Poo.png";
    std::string profileId = "amzn1.actor.person.oid.A24FP4VJ7KI5CD";
};"""

target4 = """    void drawMediaViewerScreen(const GameTile& game,
                               int currentMediaIdx,
                               uint64_t frameCount);"""

replacement4 = """    void drawMediaViewerScreen(const GameTile& game,
                               int currentMediaIdx,
                               uint64_t frameCount);

    void drawStartingSession(const GameTile& game,
                             const std::string& statusMsg,
                             uint64_t frameCount,
                             bool cancelModalOpen = false,
                             int cancelModalBtnIdx = 0);

    void drawPostSessionModal(const std::vector<GameTile>& suggestions,
                              int selectedSuggestionIdx,
                              uint64_t frameCount);

    void drawFaqModal(const std::string& faqQuestion,
                      const std::string& faqAnswer,
                      uint64_t frameCount);

    void drawBluetoothGamepadModal(uint64_t frameCount);"""

# Normalize CRLF
c_norm = c.replace("\r\n", "\n")
t1_norm = target1.replace("\r\n", "\n")
t2_norm = target2.replace("\r\n", "\n")
t3_norm = target3.replace("\r\n", "\n")
t4_norm = target4.replace("\r\n", "\n")

ok = True
if t1_norm in c_norm and t2_norm in c_norm and t3_norm in c_norm and t4_norm in c_norm:
    c_norm = c_norm.replace(t1_norm, replacement1.replace("\r\n", "\n"), 1)
    c_norm = c_norm.replace(t2_norm, replacement2.replace("\r\n", "\n"), 1)
    c_norm = c_norm.replace(t3_norm, replacement3.replace("\r\n", "\n"), 1)
    c_norm = c_norm.replace(t4_norm, replacement4.replace("\r\n", "\n"), 1)
    with open("apps/luna-ps5/src/display/display_manager.h", "w", encoding="utf-8") as f:
        f.write(c_norm.replace("\n", "\r\n"))
    print("[+] display_manager.h patched successfully!")
else:
    print("[-] Could not find all targets in display_manager.h")
    print("t1:", t1_norm in c_norm)
    print("t2:", t2_norm in c_norm)
    print("t3:", t3_norm in c_norm)
    print("t4:", t4_norm in c_norm)
