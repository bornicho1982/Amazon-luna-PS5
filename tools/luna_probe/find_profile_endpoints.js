const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/VVbpQMlH-GS-d6Z.js_name_initial_app-85d7501d', 'utf8');

const idx = content.indexOf('_defaultProfileId');
console.log(content.slice(Math.max(0, idx - 500), idx + 1000));
