with open('apps/luna-ps5/src/session/luna_session.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Update logoUrl in parseGameTileFromWidget
old_logo = '''    std::string logoUrl = pdata["productIcon"].asString();
    if (logoUrl.empty()) logoUrl = pdata["iconUrl"].asString();
    if (logoUrl.empty()) logoUrl = pdata["logoUrl"].asString();
    if (logoUrl.empty()) logoUrl = pdata["titleLogoUrl"].asString();'''

new_logo = '''    std::string logoUrl = pdata["productIcon"].asString();
    if (logoUrl.empty()) logoUrl = pdata["titleLogo"].asString();
    if (logoUrl.empty()) logoUrl = pdata["logoImage"].asString();
    if (logoUrl.empty()) logoUrl = pdata["productLogo"].asString();
    if (logoUrl.empty()) logoUrl = pdata["iconUrl"].asString();
    if (logoUrl.empty()) logoUrl = pdata["logoUrl"].asString();
    if (logoUrl.empty()) logoUrl = pdata["titleLogoUrl"].asString();'''

assert old_logo in content, 'old_logo not found'
content = content.replace(old_logo, new_logo)

# 2. Filter out non-games in rankings
old_rank_end = '''        if (!row.items.empty()) {
            collections.push_back(std::move(row));
        }'''

new_rank_end = '''        if (row.isRankings) {
            auto it = std::remove_if(row.items.begin(), row.items.end(), [](const luna::display::GameTile& t) {
                return t.title.find("Luna Premium") != std::string::npos ||
                       t.id.find("luna_premium") != std::string::npos ||
                       t.id.find("channel.") != std::string::npos ||
                       t.id.find("promo_") != std::string::npos;
            });
            row.items.erase(it, row.items.end());
        }

        if (!row.items.empty()) {
            collections.push_back(std::move(row));
        }'''

assert old_rank_end in content, 'old_rank_end not found'
content = content.replace(old_rank_end, new_rank_end, 1)

# 3. Logo in resolveGameDetails
old_res_logo = '''                    std::string logo = pdata["productIcon"].asString();
                    if (logo.empty()) logo = pdata["iconUrl"].asString();
                    if (!logo.empty()) inoutTile->logoUrl = logo;'''

new_res_logo = '''                    std::string logo = pdata["productIcon"].asString();
                    if (logo.empty()) logo = pdata["titleLogo"].asString();
                    if (logo.empty()) logo = pdata["logoImage"].asString();
                    if (logo.empty()) logo = pdata["productLogo"].asString();
                    if (logo.empty()) logo = pdata["iconUrl"].asString();
                    if (logo.empty()) logo = pdata["logoUrl"].asString();
                    if (logo.empty()) logo = pdata["titleLogoUrl"].asString();
                    if (!logo.empty()) inoutTile->logoUrl = logo;'''

assert old_res_logo in content, 'old_res_logo not found'
content = content.replace(old_res_logo, new_res_logo)

# 4. Preload images in resolveGameDetails
old_preload_req = '''        luna::util::CrashLogger::log("[LunaSession] resolveGameDetails exito: playToken len=%zu, buildId=%s, profileId=%s, entitled=%d",'''

new_preload_req = '''        if (inoutTile) {
            if (!inoutTile->logoUrl.empty()) {
                luna::util::ImageLoader::instance().requestImage(inoutTile->logoUrl, luna::util::ImagePreset::ProductLogo);
            }
            if (!inoutTile->backdropUrl.empty()) {
                luna::util::ImageLoader::instance().requestImage(inoutTile->backdropUrl, luna::util::ImagePreset::PanoramicDetail);
            }
            for (const auto& sUrl : inoutTile->screenshotUrls) {
                luna::util::ImageLoader::instance().requestImage(sUrl, luna::util::ImagePreset::ScreenshotThumbnail);
            }
        }

        luna::util::CrashLogger::log("[LunaSession] resolveGameDetails exito: playToken len=%zu, buildId=%s, profileId=%s, entitled=%d",'''

assert old_preload_req in content, 'old_preload_req not found'
content = content.replace(old_preload_req, new_preload_req)

with open('apps/luna-ps5/src/session/luna_session.cpp', 'w', encoding='utf-8') as f:
    f.write(content)

print('Successfully patched luna_session.cpp')
