const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/ynBYY5UuOxPBd0D.js_name_initial_app-730a55cb', 'utf8');

const idx = content.indexOf('playerLogin(');
console.log(content.slice(idx - 100, idx + 1200));
