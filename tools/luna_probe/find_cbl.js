const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/t3qvjbojgaubNYh.js_name_initial_app-82d1cdcf', 'utf8');

const idx = content.indexOf('changeCustomerState(');
console.log(content.slice(idx, idx + 2500));
