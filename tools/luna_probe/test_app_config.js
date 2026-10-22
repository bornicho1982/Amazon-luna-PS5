const https = require('https');

const req = https.request('https://proxy-prod.tempo.digital.a2z.com/getAppConfig', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
    'Content-Length': 2,
    'x-amz-device-type': 'A2U9WJDSJD7QG9',
    'x-amz-platform': 'smart_tv_web',
    'x-amz-locale': 'es-ES'
  }
}, (res) => {
  let data = '';
  res.on('data', chunk => data += chunk);
  res.on('end', () => {
    try {
      const parsed = JSON.parse(data);
      console.log('Top-level keys in getAppConfig:', Object.keys(parsed));
      console.log('tdi:', parsed.tdi);
      console.log('endpoints:', parsed.endpoints);
    } catch(e) {
      console.log('Error parsing:', e);
    }
  });
});
req.write('{}');
req.end();
