const fs = require('fs');

let content = fs.readFileSync('apps/luna-ps5/src/main.cpp', 'utf8');

const target = `            case AppState::LoginCBL: {
                cbl.tick();`;

const replacement = `            case AppState::LoginCBL: {
                cbl.tick();

                if (pressedNow & luna::input::PAD_TRIANGLE) {
                    luna::util::AppSettings::instance().cycleRegion();
                    cbl.setRegionDomain(luna::util::I18n::getCurrentRegion().amazonDomain);
                    luna::util::AppSettings::instance().save(settingsFilePath);
                    g_lastStatusText = "Region: " + std::string(luna::util::I18n::getCurrentRegion().name);
                }`;

if (content.includes(target)) {
  content = content.replace(target, replacement);
} else {
  content = content.replace(target.replace(/\n/g, '\r\n'), replacement.replace(/\n/g, '\r\n'));
}

fs.writeFileSync('apps/luna-ps5/src/main.cpp', content, 'utf8');
console.log('Added Triangle region switch to LoginCBL');
