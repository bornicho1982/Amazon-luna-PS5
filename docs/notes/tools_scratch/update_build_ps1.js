const fs = require('fs');
let content = fs.readFileSync('apps/luna-ps5/build_ps5.ps1', 'utf8');
content = content.replace('"util/image_loader",', '"util/image_loader",\n        "util/settings",');
fs.writeFileSync('apps/luna-ps5/build_ps5.ps1', content, 'utf8');
console.log('Added util/settings to build_ps5.ps1');
