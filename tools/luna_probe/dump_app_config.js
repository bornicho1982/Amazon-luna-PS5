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
    const parsed = JSON.parse(data);
    console.log(JSON.stringify(parsed, null, 2).slice(0, 3000));
  });
});
req.write('{}');
req.end();
