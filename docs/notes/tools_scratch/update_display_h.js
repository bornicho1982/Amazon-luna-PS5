const fs = require('fs');

let h = fs.readFileSync('apps/luna-ps5/src/display/display_manager.h', 'utf8');

// Add videoUrl to GameTile
h = h.replace('    std::vector<std::string> screenshotUrls;',
  '    std::vector<std::string> screenshotUrls;\n    std::string videoUrl;');

// Add drawSettingsScreen and drawMediaViewerScreen to DisplayManager class
const target = '    void drawStreamingStatusScreen(const GameTile& game,';
const addition = `    void drawSettingsScreen(int selectedItemIdx,
                            const std::string& statusText,
                            uint64_t frameCount);

    void drawMediaViewerScreen(const GameTile& game,
                               int currentMediaIdx,
                               uint64_t frameCount);

    void drawStreamingStatusScreen(const GameTile& game,`;

h = h.replace(target, addition);

fs.writeFileSync('apps/luna-ps5/src/display/display_manager.h', h, 'utf8');
console.log('Updated display_manager.h');
