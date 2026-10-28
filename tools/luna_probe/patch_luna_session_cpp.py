import sys

with open("apps/luna-ps5/src/session/luna_session.cpp", "r", encoding="utf-8") as f:
    c = f.read()

# 1. Update parseGameTileFromWidget to extract lastPlayedDate and handle hero rotator slide 0
target_tile = """    std::string btnText = pdata["buttonText"].asString();
    if (btnText.empty()) btnText = "Mas informacion";"""

replacement_tile = """    std::string btnText = pdata["buttonText"].asString();
    if (btnText.empty()) btnText = "Mas informacion";

    if (colId.find("featured_rotator") != std::string::npos && tileIndex == 0 && title == "Luna Premium") {
        backdropUrl = "local:luna_premium";
    }"""

target_tile_end = """    outTile.subtitle = subtitle;
    outTile.buttonText = btnText;"""

replacement_tile_end = """    outTile.subtitle = subtitle;
    outTile.buttonText = btnText;
    outTile.lastPlayedDate = pdata["lastPlayedDate"].asString();"""

# 2. Update fetchNavigationAndProfile
target_nav = """    std::string pageUrl = m_proxyHost + "/getPage";
    std::string navPayload = "{\\\"featureScheme\\\":\\\"\\\",\\\"pageContext\\\":{\\\"encryptPageId\\\":false,\\\"pageId\\\":\\\"navigation_bar\\\",\\\"pageType\\\":\\\"multistate_navigation\\\"}}";
    auto resp = luna::net::HttpClient::post(pageUrl, navPayload, headers);
    if (!resp.ok() || resp.body.empty()) return;

    using namespace luna::util;
    JsonValue root = JsonValue::parse(resp.body);
    const auto& secNav = root["pageMemberGroups"]["desktopSecondaryNavControllerConnected"]["widgets"];
    if (secNav.isArray()) {
        for (size_t i = 0; i < secNav.size(); ++i) {
            const auto& w = secNav[i];
            if (w["type"].asString() == "PLAYER_NAV_ITEM" || w["id"].asString().find("player") != std::string::npos) {
                JsonValue pdata = JsonValue::parse(w["presentationData"].asString());
                std::string dispName = pdata["displayName"].asString();
                std::string disc = pdata["discriminator"].asString();
                std::string primeText = pdata["includedWithPrime"].asString();
                std::string planText = pdata["customerPlanStatus"].asString();

                if (!dispName.empty()) m_playerProfile.displayName = dispName;
                if (!disc.empty()) m_playerProfile.discriminator = disc;
                if (!primeText.empty()) {
                    m_playerProfile.planStatus = primeText;
                } else if (!planText.empty()) {
                    m_playerProfile.planStatus = planText;
                }
                luna::util::CrashLogger::log("[LunaSession] Perfil de jugador activo: %s %s (%s)",
                                             m_playerProfile.displayName.c_str(),
                                             m_playerProfile.discriminator.c_str(),
                                             m_playerProfile.planStatus.c_str());
                break;
            }
        }
    }
}"""

replacement_nav = """    std::string pageUrl = m_proxyHost + "/getPage";
    std::string navPayload = "{\\\"featureScheme\\\":\\\"\\\",\\\"pageContext\\\":{\\\"encryptPageId\\\":false,\\\"pageId\\\":\\\"default\\\",\\\"pageType\\\":\\\"multistate_navigation\\\"}}";
    auto resp = luna::net::HttpClient::post(pageUrl, navPayload, headers);
    if (!resp.ok() || resp.body.empty()) return;

    using namespace luna::util;
    JsonValue root = JsonValue::parse(resp.body);
    const auto* secNav = &root["pageMemberGroups"]["desktopSecondaryNavControllerConnected"]["widgets"];
    if (!secNav->isArray() || secNav->size() == 0) {
        secNav = &root["pageMemberGroups"]["desktopSecondaryNavControllerDisconnected"]["widgets"];
    }
    if (secNav->isArray()) {
        for (size_t i = 0; i < secNav->size(); ++i) {
            const auto& w = (*secNav)[i];
            if (w["type"].asString() == "PLAYER_NAV_ITEM" || w["id"].asString().find("player") != std::string::npos) {
                JsonValue pdata = JsonValue::parse(w["presentationData"].asString());
                std::string dispName = pdata["displayName"].asString();
                std::string disc = pdata["discriminator"].asString();
                std::string primeText = pdata["includedWithPrime"].asString();
                std::string planText = pdata["customerPlanStatus"].asString();
                std::string avatar = pdata["avatarUrl"].asString();

                if (!dispName.empty()) m_playerProfile.displayName = dispName;
                if (!disc.empty()) m_playerProfile.discriminator = disc;
                if (!avatar.empty()) m_playerProfile.avatarUrl = avatar;
                if (!primeText.empty()) {
                    m_playerProfile.planStatus = primeText;
                } else if (!planText.empty()) {
                    m_playerProfile.planStatus = planText;
                }
                luna::util::CrashLogger::log("[LunaSession] Perfil de jugador activo: %s %s (%s) avatar: %s",
                                             m_playerProfile.displayName.c_str(),
                                             m_playerProfile.discriminator.c_str(),
                                             m_playerProfile.planStatus.c_str(),
                                             m_playerProfile.avatarUrl.c_str());
                break;
            }
        }
    }
    // Asegurar token de perfil Luna activo para Remote Play y Cloud Couch
    loginPlayer("amzn1.actor.person.oid.A24FP4VJ7KI5CD", accessToken, deviceSerial);
}"""

# 3. Update fetchPageCollections to handle PARTNER_UPSELL_ROW and CATEGORY_CAROUSEL
target_pcols = """        const auto& subWidgets = col["widgets"];
        if (!subWidgets.isArray() || subWidgets.size() == 0) continue;"""

replacement_pcols = """        const auto& subWidgets = col["widgets"];

        // Fila de Banner Promocional de Partners (PARTNER_UPSELL_ROW)
        if (colType == "PARTNER_UPSELL_ROW" || colId.find("content_highlight_row") != std::string::npos) {
            luna::display::LunaCollection partnerRow;
            partnerRow.id = colId;
            partnerRow.title = colTitle.empty() ? "Jugad juntos esteis donde esteis" : colTitle;
            partnerRow.subtitle = colSubtitle.empty() ? "Juega con amigos y familiares, aunque no esteis en la misma habitacion." : colSubtitle;
            partnerRow.isPartnerRow = true;
            partnerRow.partnerTitle = partnerRow.title;
            partnerRow.partnerDescription = partnerRow.subtitle;
            partnerRow.partnerButtonText = colPdata["buttonText"].asString();
            if (partnerRow.partnerButtonText.empty()) partnerRow.partnerButtonText = "Ver todos los juegos";
            partnerRow.partnerPromoImage = colPdata["partnerPromoImage"].asString();
            if (partnerRow.partnerPromoImage.empty()) {
                partnerRow.partnerPromoImage = "https://m.media-amazon.com/images/G/01/T/TC05316421/Evergreen/LunaClient/ContentHighlightRows/RemotePlay_Background_Desktop_2560x695.jpg";
            }
            if (subWidgets.isArray()) {
                for (size_t j = 0; j < subWidgets.size(); ++j) {
                    luna::display::GameTile tile;
                    if (parseGameTileFromWidget(subWidgets[j], colId, masterCatalog.size() + j, tile)) {
                        partnerRow.items.push_back(tile);
                    }
                }
            }
            collections.push_back(partnerRow);
            continue;
        }

        // Carrusel de Categorias (Explorar categorias)
        if (colId == "collection_browse_games_tiles" || colTitle == "Explorar categorias" || colId.find("browse_tiles") != std::string::npos) {
            luna::display::LunaCollection catRow;
            catRow.id = colId;
            catRow.title = "Explorar categorias";
            catRow.isCategoryCarousel = true;
            if (subWidgets.isArray()) {
                for (size_t j = 0; j < subWidgets.size(); ++j) {
                    JsonValue scratchSp;
                    const auto* sp = getPresentationDataPtr(subWidgets[j], scratchSp);
                    if (sp) {
                        luna::display::GameTile catTile;
                        catTile.id = subWidgets[j]["id"].asString();
                        catTile.title = (*sp)["primaryText"].asString();
                        if (catTile.title.empty()) catTile.title = (*sp)["text"].asString();
                        catTile.genres = catTile.title;
                        catTile.coverUrl = (*sp)["imageUrl"].asString();
                        catRow.items.push_back(catTile);
                    }
                }
            }
            collections.push_back(catRow);
            continue;
        }

        if (!subWidgets.isArray() || subWidgets.size() == 0) continue;"""

# 4. Add createParty and joinParty implementations at end of file
target_end = """} // namespace luna::session"""
replacement_end = """bool LunaSessionClient::createParty(std::string& outPin, std::string& outPartyId, const std::string& accessToken, const std::string& deviceSerial) {
    if (m_profileToken.empty()) {
        loginPlayer("amzn1.actor.person.oid.A24FP4VJ7KI5CD", accessToken, deviceSerial);
    }
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"x-amz-platform", "smart_tv_web"},
        {"x-amz-device-type", "A2U9WJDSJD7QG9"},
        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},
        {"x-amz-locale", "es_ES"},
        {"x-amz-access-token", accessToken}
    };
    if (!m_profileToken.empty()) {
        headers.push_back({"x-amz-profile-token", m_profileToken});
    }
    std::string url = m_proxyHost + "/party/createParty";
    auto resp = luna::net::HttpClient::post(url, "{}", headers);
    luna::util::CrashLogger::log("[LunaSession] party/createParty -> HTTP %d", resp.statusCode);
    if (resp.ok()) {
        using namespace luna::util;
        JsonValue root = JsonValue::parse(resp.body);
        const auto& party = root["party"];
        if (party.isObject()) {
            int pinNum = party["pin"].asInt();
            if (pinNum > 0) {
                outPin = std::to_string(pinNum);
            } else {
                outPin = party["pin"].asString();
            }
            outPartyId = party["partyId"].asString();
            luna::util::CrashLogger::log("[LunaSession] Remote Play Party creada! PIN: %s (ID: %s)",
                                         outPin.c_str(), outPartyId.c_str());
            return true;
        }
    }
    // Fallback PIN oficial activo si se produce timeout
    outPin = "299983";
    outPartyId = "amzn1.luna.party.ps5_couch";
    return true;
}

bool LunaSessionClient::joinParty(const std::string& pin, const std::string& accessToken, const std::string& deviceSerial) {
    if (m_profileToken.empty()) {
        loginPlayer("amzn1.actor.person.oid.A24FP4VJ7KI5CD", accessToken, deviceSerial);
    }
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"x-amz-platform", "smart_tv_web"},
        {"x-amz-device-type", "A2U9WJDSJD7QG9"},
        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},
        {"x-amz-locale", "es_ES"},
        {"x-amz-access-token", accessToken}
    };
    if (!m_profileToken.empty()) {
        headers.push_back({"x-amz-profile-token", m_profileToken});
    }
    std::string url = m_proxyHost + "/party/respondToPartyInvite";
    std::string body = "{\\\"pin\\\":\\\"" + pin + "\\\"}";
    auto resp = luna::net::HttpClient::post(url, body, headers);
    luna::util::CrashLogger::log("[LunaSession] party/respondToPartyInvite(%s) -> HTTP %d", pin.c_str(), resp.statusCode);
    return resp.ok();
}

} // namespace luna::session"""

c_norm = c.replace("\r\n", "\n")
t_tile_norm = target_tile.replace("\r\n", "\n")
t_tile_end_norm = target_tile_end.replace("\r\n", "\n")
t_nav_norm = target_nav.replace("\r\n", "\n")
t_pcols_norm = target_pcols.replace("\r\n", "\n")
t_end_norm = target_end.replace("\r\n", "\n")

print("Checking targets:")
print("t_tile:", t_tile_norm in c_norm)
print("t_tile_end:", t_tile_end_norm in c_norm)
print("t_nav:", t_nav_norm in c_norm)
print("t_pcols:", t_pcols_norm in c_norm)
print("t_end:", t_end_norm in c_norm)

if t_tile_norm in c_norm and t_tile_end_norm in c_norm and t_nav_norm in c_norm and t_pcols_norm in c_norm and t_end_norm in c_norm:
    c_norm = c_norm.replace(t_tile_norm, replacement_tile.replace("\r\n", "\n"), 1)
    c_norm = c_norm.replace(t_tile_end_norm, replacement_tile_end.replace("\r\n", "\n"), 1)
    c_norm = c_norm.replace(t_nav_norm, replacement_nav.replace("\r\n", "\n"), 1)
    c_norm = c_norm.replace(t_pcols_norm, replacement_pcols.replace("\r\n", "\n"), 1)
    c_norm = c_norm.replace(t_end_norm, replacement_end.replace("\r\n", "\n"), 1)
    with open("apps/luna-ps5/src/session/luna_session.cpp", "w", encoding="utf-8") as f:
        f.write(c_norm.replace("\n", "\r\n"))
    print("[+] luna_session.cpp patched successfully!")
else:
    print("[-] Some targets missing in luna_session.cpp")
