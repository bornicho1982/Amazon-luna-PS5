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

const sdpOffer = "v=0\r\no=- 12345 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n";

const signalingInner = '{"type":"offer","sdp":"' + escapeJson(sdpOffer) + '",' +
    '"extras":{"features":[],"requestedChannels":["display","audio","input","participant"],' +
    '"clientContext":{"browserClientRole":"BROWSER","browserType":"Chrome","browserVersion":"120.0.0.0",' +
    '"deviceModel":"LG TV","deviceType":"smarttv","osName":"webOS","osVersion":"9.0"}}}';

const payload = '{\n' +
    '  "gameId": "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16",\n' +
    '  "signalingRequest": "' + escapeJson(signalingInner) + '",\n' +
    '  "timeout": 15000,\n' +
    '  "clientFeatures": {\n' +
    '    "h264": true,\n' +
    '    "opus": true,\n' +
    '    "dataChannels": true\n' +
    '  },\n' +
    '  "clientContext": {\n' +
    '    "clientType": "BROWSER",\n' +
    '    "deviceType": "SMART_TV",\n' +
    '    "deviceModel": "LG TV"\n' +
    '  }\n' +
    '}';

console.log("Testing constructed payload:");
const outer = JSON.parse(payload);
console.log("Outer parse successful!");
const inner = JSON.parse(outer.signalingRequest);
console.log("Inner parse successful! type =", inner.type);
console.log("sdp verified:", inner.sdp === sdpOffer);
