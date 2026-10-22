const https = require('https');

function escapeJson(str) {
    let out = '';
    for (let i = 0; i < str.length; i++) {
        const c = str[i];
        if (c === '"') out += '\\"';
        else if (c === '\\') out += '\\\\';
        else if (c === '\r') out += '\\r';
        else if (c === '\n') out += '\\n';
        else if (c === '\t') out += '\\t';
        else out += c;
    }
    return out;
}

const sdp1 = "v=0\r\n"; // incomplete SDP
const sdp2 = [
  "v=0",
  "o=- 4962303333 1 IN IP4 127.0.0.1",
  "s=AmazonLunaPS5",
  "t=0 0",
  "a=group:BUNDLE 0 1 2",
  "a=msid-semantic: WMS",
  "a=ice-ufrag:luna0ps5ufrag",
  "a=ice-pwd:luna0ps5password00000000000000",
  "a=ice-options:trickle",
  "a=fingerprint:sha-256 B1:B4:6B:42:F2:F2:1C:18:31:36:5A:F3:A8:10:E2:03:62:3C:D5:41:0F:7D:61:D1:D1:17:F9:6B:19:D9:63:F6",
  "a=setup:actpass",
  "m=audio 33438 UDP/TLS/RTP/SAVPF 111",
  "c=IN IP4 0.0.0.0",
  "a=mid:0",
  "a=sendrecv",
  "a=rtcp-mux",
  "a=rtpmap:111 opus/48000/2",
  "a=fmtp:111 minptime=10;useinbandfec=1;stereo=1",
  "a=candidate:1 1 UDP 2130706431 127.0.0.1 33438 typ host",
  "m=video 33438 UDP/TLS/RTP/SAVPF 108 100 102",
  "c=IN IP4 0.0.0.0",
  "a=mid:1",
  "a=recvonly",
  "a=rtcp-mux",
  "a=rtpmap:108 H264/90000",
  "a=fmtp:108 level-asymmetry-allowed=0;packetization-mode=1;profile-level-id=42e01f",
  "a=rtcp-fb:108 nack",
  "a=rtcp-fb:108 nack pli",
  "a=rtcp-fb:108 ccm fir",
  "a=candidate:1 1 UDP 2130706431 127.0.0.1 33438 typ host",
  "m=application 33438 UDP/DTLS/SCTP webrtc-datachannel",
  "c=IN IP4 0.0.0.0",
  "a=mid:2",
  "a=sctp-port:5000",
  "a=candidate:1 1 UDP 2130706431 127.0.0.1 33438 typ host",
  ""
].join("\r\n");

function send(name, sdp) {
  const signalingInner = JSON.stringify({ type: "offer", sdp });
  const payload = JSON.stringify({
    gameId: "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16",
    signalingRequest: signalingInner,
    timeout: 15000,
    clientFeatures: { h264: true, opus: true, dataChannels: true },
    clientContext: { clientType: "BROWSER", deviceType: "SMART_TV", deviceModel: "LG TV" }
  });

  const req = https.request({
    hostname: 'proxy-prod.eu-west-1.tempo.digital.a2z.com',
    port: 443,
    path: '/createGameSessionJob',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload),
      'x-amz-access-token': 'dummy',
      'x-amz-marketplace-id': 'A1RKKUPIHCS9HS',
      'x-amz-country-of-residence': 'ES',
      'x-amz-locale': 'es-ES'
    }
  }, (res) => {
    let d = '';
    res.on('data', chunk => d += chunk);
    res.on('end', () => console.log(`[${name}] -> HTTP ${res.statusCode}: ${d}`));
  });
  req.on('error', e => console.error(name, e));
  req.write(payload);
  req.end();
}

send('incomplete SDP', sdp1);
send('full WebRTC SDP', sdp2);
