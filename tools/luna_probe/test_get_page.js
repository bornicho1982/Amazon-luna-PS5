const https = require('https');

function test(name, body, headers) {
  const payload = JSON.stringify(body);
  const req = https.request('https://proxy-prod.tempo.digital.a2z.com/getPage', {
    method: 'POST',
    headers: Object.assign({
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload),
      'x-amz-platform': 'smart_tv_web',
      'x-amz-device-type': 'A2U9WJDSJD7QG9',
      'x-amz-locale': 'es-ES'
    }, headers)
  }, (res) => {
    let data = '';
    res.on('data', chunk => data += chunk);
    res.on('end', () => console.log(name, '-> STATUS:', res.statusCode, 'BODY:', data.slice(0, 300)));
  });
  req.on('error', err => console.error(name, 'ERR:', err));
  req.write(payload);
  req.end();
}

test('getPage without token', { pageContext: { pageId: 'default', pageUri: 'home' } }, {});
test('getPage with dummy token', { pageContext: { pageId: 'default', pageUri: 'home' } }, { 'x-amz-access-token': 'dummy' });
