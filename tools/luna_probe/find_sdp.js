const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/1sdrd-nOi60hQJ0.js_name_initial_app-ecbc9d4f', 'utf8');

const idx = content.indexOf('m.j.parseOffer');
const modStart = content.lastIndexOf('{', content.lastIndexOf('function', idx));
console.log(content.slice(modStart - 200, modStart + 300));
