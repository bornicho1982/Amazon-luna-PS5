import json
import re

raw = r'{"gameSession":{"connectionId":"1997913608","gameId":"amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16","ipVersion":"IPV4","protocol":"WEB_RTC","publicIp":"15.217.4.14","region":"eu-south-2","routing":"TURN","sessionId":"6f2315cc-2792-41ca-a93d-d7db4ad13b6b","signalingResponse":"{\"type\":\"answer\",\"sdp\":\"v=0\\r\\nm=audio 33438 UDP/TLS/RTP/SAVPF 111 63 110\\r\\na=candidate:2986608634 1 udp 2122260223 15.217.4.14 33438 typ host generation 0 network-id 1\\r\\na=ice-ufrag:qiFK\\r\\na=ice-pwd:Et5SgEfOCKIBaQZfuXoK7xHA\\r\\n\"}","status":"AVAILABLE","transport":"UDP"},"gameSessionJobId":"amzn1.tempo.sessionjob.30e1eea5-0790-4c51-8dfa-9e217664940f","isReconnect":false,"status":"COMPLETED_SUCCEEDED"}'

data = json.loads(raw)
gs = data.get('gameSession', {})
print('publicIp:', gs.get('publicIp'))
sig = json.loads(gs.get('signalingResponse', '{}'))
sdp = sig.get('sdp', '')
print('sdp length:', len(sdp))

m_ip = re.search(r'"publicIp":\s*"([^"]+)"', raw)
print('regex ip:', m_ip.group(1) if m_ip else None)

m_port = re.search(r'm=audio\s+(\d+)', sdp)
print('port:', m_port.group(1) if m_port else None)

m_ufrag = re.search(r'a=ice-ufrag:([^\r\n]+)', sdp)
print('ufrag:', m_ufrag.group(1) if m_ufrag else None)

m_pwd = re.search(r'a=ice-pwd:([^\r\n]+)', sdp)
print('pwd:', m_pwd.group(1) if m_pwd else None)
