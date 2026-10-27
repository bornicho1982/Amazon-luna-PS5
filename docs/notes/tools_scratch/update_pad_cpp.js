const fs = require('fs');

let c = fs.readFileSync('apps/luna-ps5/src/input/pad_manager.cpp', 'utf8');

c = c.replace('auto applyDeadzone = [](int val) -> int16_t {\n        if (val > -1800 && val < 1800) return 0;',
  'int dz = deadzoneThreshold_;\n    auto applyDeadzone = [dz](int val) -> int16_t {\n        if (val > -dz && val < dz) return 0;');

c = c.replace('auto applyDeadzone = [](int val) -> int16_t {\r\n        if (val > -1800 && val < 1800) return 0;',
  'int dz = deadzoneThreshold_;\r\n    auto applyDeadzone = [dz](int val) -> int16_t {\r\n        if (val > -dz && val < dz) return 0;');

c = c.replace('void PadManager::setVibration(uint8_t largeMotor, uint8_t smallMotor) {\n#if defined(LUNA_TARGET_PS5)\n    if (padHandle_ >= 0) {',
  'void PadManager::setVibration(uint8_t largeMotor, uint8_t smallMotor) {\n    if (!vibrationEnabled_) return;\n#if defined(LUNA_TARGET_PS5)\n    if (padHandle_ >= 0) {');

c = c.replace('void PadManager::setVibration(uint8_t largeMotor, uint8_t smallMotor) {\r\n#if defined(LUNA_TARGET_PS5)\r\n    if (padHandle_ >= 0) {',
  'void PadManager::setVibration(uint8_t largeMotor, uint8_t smallMotor) {\r\n    if (!vibrationEnabled_) return;\r\n#if defined(LUNA_TARGET_PS5)\r\n    if (padHandle_ >= 0) {');

fs.writeFileSync('apps/luna-ps5/src/input/pad_manager.cpp', c, 'utf8');
console.log('Updated pad_manager.cpp');
