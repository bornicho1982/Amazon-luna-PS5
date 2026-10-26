import http.server, socketserver, threading, urllib.request, time, json, ssl, subprocess

class LunaBridgeHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', '*')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path == '/token':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"valid": true, "token": "dummy_test_token"}')
        elif self.path == '/cookie':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"interpretedStringState": "CONSENT_GIVEN"}')
        elif self.path == '/manifest-branded.json':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"name": "Amazon Luna", "short_name": "Luna", "start_url": "/home", "display": "standalone"}')
        elif self.path.startswith('/luna'):
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            with open('docs/notes/scratch_archive/luna_bridge_test.html', 'rb') as f:
                self.wfile.write(f.read())
        else:
            super().do_GET()

    def do_POST(self):
        print('[POST REQUEST]', self.path)
        content_len = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_len)
        print('[POST BODY]', body[:200])

        if '/tempo/' in self.path:
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"pageMemberGroups": {"mainContent": {"widgets": []}}}')
        else:
            self.send_response(200)
            self.end_headers()

server = socketserver.TCPServer(('127.0.0.1', 8899), LunaBridgeHandler)
t = threading.Thread(target=server.serve_forever, daemon=True)
t.start()
print('Started test bridge server on 8899')

cmd = [
    r'C:\Program Files\Google\Chrome\Application\chrome.exe',
    '--headless=new',
    '--disable-gpu',
    '--dump-dom',
    'http://127.0.0.1:8899/luna?isLGTV=true'
]
res = subprocess.run(cmd, capture_output=True, text=True, timeout=12)
print('Chrome return code:', res.returncode)
print('DOM Output length:', len(res.stdout))
print('DOM excerpt:\n', res.stdout[:1000])

server.shutdown()
