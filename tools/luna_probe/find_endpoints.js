const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/25FnWx2xCo40Q-y.js_name_initial_app-ad52a362', 'utf8');

const regex = /requestWithToken\("([^"]+)"/g;
let m;
const endpoints = new Set();
while ((m = regex.exec(content)) !== null) {
  endpoints.add(m[1]);
}
console.log('Endpoints found:', endpoints.size);
console.log(Array.from(endpoints).filter(e => 
  e.toLowerCase().includes('profile') || 
  e.toLowerCase().includes('player') || 
  e.toLowerCase().includes('session') || 
  e.toLowerCase().includes('auth') ||
  e.toLowerCase().includes('account')
));
