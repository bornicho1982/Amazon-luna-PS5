import http.server, socketserver, threading, urllib.request, time, json, ssl

with open('docs/notes/scratch_archive/luna_official_page.html', 'r', encoding='utf-8') as f:
    official_html = f.read()

# Replace config in official_html to use local proxy
old_cfg = 'stage: "Prod",'
new_cfg = '''stage: "Beta",
            proxyDevEndpoint: "http://127.0.0.1:8899/tempo/",'''
patched_html = official_html.replace(old_cfg, new_cfg)

# Also add fetch hook to route any tempo or amazon calls to local proxy
fetch_hook = '''<script>
// Intercept fetch and XHR for tempo endpoints
const origFetch = window.fetch;
window.fetch = function(input, init) {
    let url = typeof input === 'string' ? input : (input && input.url ? input.url : '');
    if (url.includes('tempo.digital.a2z.com')) {
        url = url.replace(/https:\/\/[^\/]+/, 'http://127.0.0.1:8899/tempo');
        if (typeof input === 'string') input = url;
        else input = new Request(url, init);
    }
    return origFetch.call(this, input, init);
};
</script>
'''
patched_html = patched_html.replace('<head>', '<head>\n' + fetch_hook)

with open('docs/notes/scratch_archive/luna_bridge_test.html', 'w', encoding='utf-8') as f:
    f.write(patched_html)

print('Saved luna_bridge_test.html')
