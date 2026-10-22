const https = require('https');

function test(name, body, headers) {
  const payload = JSON.stringify(body);
  const req = https.request('https://proxy-prod.tempo.digital.a2z.com/player/playerLogin', {
    method: 'POST',
    headers: Object.assign({
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload)
    }, headers)
  }, (res) => {
    let data = '';
    res.on('data', chunk => data += chunk);
    res.on('end', () => console.log(name, '-> STATUS:', res.statusCode, 'BODY:', data));
  });
  req.on('error', err => console.error(name, 'ERR:', err));
  req.write(payload);
  req.end();
}

test('Empty payload', {}, {});
test('Dummy token empty body', {}, { 'x-amz-access-token': 'dummy' });
test('Dummy token with profileId', { profileId: 'default' }, { 'x-amz-access-token': 'dummy' });
