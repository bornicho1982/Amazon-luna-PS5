const fs = require('fs');
let content = fs.readFileSync('apps/luna-ps5/src/display/display_manager.cpp', 'utf8');

const targetStr = 'bool isFirstSlide = (heroIdx == 0 || heroGame.title == "Luna Premium");';
const replacementStr = 'bool isFirstSlide = (heroCol.isHeroRotator && heroIdx == 0 && heroGame.title == "Luna Premium");';
content = content.replace(targetStr, replacementStr);

const targetElse = `if (!heroBannerRgba_.empty()) {
                        drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
                    } else {
                        fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                    }`;
const targetElseCRLF = targetElse.replace(/\n/g, '\r\n');
const replacementElse = 'fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);';

if (content.includes(targetElseCRLF)) {
  content = content.replace(targetElseCRLF, replacementElse);
} else if (content.includes(targetElse)) {
  content = content.replace(targetElse, replacementElse);
}

fs.writeFileSync('apps/luna-ps5/src/display/display_manager.cpp', content, 'utf8');
console.log('Fixed hero banner logic in display_manager.cpp');
