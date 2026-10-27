const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/NHnqA85uo6j9fRd.js', 'utf8');
let idx = content.indexOf('getPageByTypeAndId');
console.log(content.slice(idx - 400, idx + 400));
