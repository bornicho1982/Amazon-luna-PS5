import os

path = r'c:\proyectos antigravity\webkit PS5 v2\apps\luna-ps5\src\main.cpp'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Add sceSystemServiceLaunchWebBrowser declaration and helper
launch_helper = '''#if defined(LUNA_TARGET_PS5)
extern "C" {
    int sceSystemServiceLaunchWebBrowser(const char *uri, void *reserved);
}
#endif

static void launchPs5WebBrowser(const std::string& url) {
#if defined(LUNA_TARGET_PS5)
    luna::util::CrashLogger::log("[Main] Lanzando navegador web de PS5: %s", url.c_str());
    int rc = sceSystemServiceLaunchWebBrowser(url.c_str(), nullptr);
    luna::util::CrashLogger::log("[Main] sceSystemServiceLaunchWebBrowser retorno: 0x%08X", rc);
#else
    (void)url;
#endif
}
'''

if 'launchPs5WebBrowser' not in content:
    content = content.replace('namespace {\nstd::atomic<bool> g_running{true};',
                              launch_helper + '\nnamespace {\nstd::atomic<bool> g_running{true};')

# 2. LAN IP discovery in main()
lan_ip_code = '''    // Obtener IP LAN local para mostrarla en la pantalla de espera
    std::string lanIp = "192.168.18.21";
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        sockaddr_in serv;
        std::memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = inet_addr("8.8.8.8");
        serv.sin_port = htons(53);
        if (connect(sock, (const sockaddr*)&serv, sizeof(serv)) == 0) {
            sockaddr_in name;
            socklen_t namelen = sizeof(name);
            if (getsockname(sock, (sockaddr*)&name, &namelen) == 0) {
                char buf[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &name.sin_addr, buf, sizeof(buf))) {
                    lanIp = buf;
                }
            }
        }
#if defined(_WIN32)
        closesocket(sock);
#else
        close(sock);
#endif
    }
    std::string localWebUrl = "http://127.0.0.1:8080/luna";
    std::string lanWebUrl = "http://" + lanIp + ":8080/luna";
'''

target_auth_server = '    // 8. Servidor HTTP local Web Bridge (http://127.0.0.1:8080/luna)\n    luna::auth::AuthServer authServer;\n    authServer.start(8080);'
if 'localWebUrl' not in content:
    replacement_auth_server = lan_ip_code + '\n' + target_auth_server
    content = content.replace(target_auth_server, replacement_auth_server)

# 3. Check startup browser launch when authenticated
target_startup_auth = '''    cbl.init(storageDir);
    if (cbl.isAuthenticated()) {
        appState = AppState::Catalog;
    } else {
        appState = AppState::LoginCBL;
    }'''

replacement_startup_auth = '''    cbl.init(storageDir);
    if (cbl.isAuthenticated()) {
        appState = AppState::Catalog;
    } else {
        appState = AppState::LoginCBL;
    }'''
# Notice loadCatalogIfAuthenticated() is called right below:
# if (cbl.isAuthenticated()) {
#     loadCatalogIfAuthenticated();
# }
# We want to call launchPs5WebBrowser(localWebUrl); there!

target_load_cat = '''    if (cbl.isAuthenticated()) {
        loadCatalogIfAuthenticated();
    }'''

replacement_load_cat = '''    if (cbl.isAuthenticated()) {
        loadCatalogIfAuthenticated();
        launchPs5WebBrowser(localWebUrl);
    }'''

if 'launchPs5WebBrowser(localWebUrl)' not in content:
    content = content.replace(target_load_cat, replacement_load_cat)

# 4. In AppState::LoginCBL, launch browser when auth succeeds
target_login_success = '''                if (cbl.isAuthenticated()) {
                    luna::util::CrashLogger::log("[Main] Usuario autenticado con exito. Cargando catalogo...");
                    display.drawLoginScreen(
                        cbl.getPublicCode(),
                        cbl.getVerificationUri(),
                        cbl.getQrSize(),
                        [&cbl](int x, int y) { return cbl.getQrModule(x, y); },
                        "Conectado a Amazon. Cargando tu catalogo de Luna...",
                        frameCount
                    );
                    loadCatalogIfAuthenticated();
                    appState = AppState::Catalog;
                    break;
                }'''

replacement_login_success = '''                if (cbl.isAuthenticated()) {
                    luna::util::CrashLogger::log("[Main] Usuario autenticado con exito. Abriendo Web...");
                    display.drawLoginScreen(
                        cbl.getPublicCode(),
                        cbl.getVerificationUri(),
                        cbl.getQrSize(),
                        [&cbl](int x, int y) { return cbl.getQrModule(x, y); },
                        "Conectado a Amazon. Abriendo interfaz Web de Luna...",
                        frameCount
                    );
                    loadCatalogIfAuthenticated();
                    launchPs5WebBrowser(localWebUrl);
                    appState = AppState::Catalog;
                    break;
                }'''

content = content.replace(target_login_success, replacement_login_success)

# 5. In teardownStreamingSession, re-launch browser when session ends
target_teardown = '''    auto teardownStreamingSession = [&](const std::string& reason) {
        luna::util::CrashLogger::log("[Main] teardownStreamingSession: %s", reason.c_str());
        rtcClient.stop();
        sessionClient.terminateSession(cbl.getAccessToken());
        clearMediaQueues();
        overlayOpen = false;
        streamingActiveFrames = 0;
        videoStreaming.store(false);
        g_lastStatusText = reason;
        appState = AppState::Catalog;
    };'''

replacement_teardown = '''    auto teardownStreamingSession = [&](const std::string& reason) {
        luna::util::CrashLogger::log("[Main] teardownStreamingSession: %s", reason.c_str());
        rtcClient.stop();
        sessionClient.terminateSession(cbl.getAccessToken());
        clearMediaQueues();
        overlayOpen = false;
        streamingActiveFrames = 0;
        videoStreaming.store(false);
        g_lastStatusText = reason;
        appState = AppState::Catalog;
        launchPs5WebBrowser(localWebUrl);
    };'''

content = content.replace(target_teardown, replacement_teardown)

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)

print("Patch step 1 applied successfully.")
