with open('apps/luna-ps5/src/main.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

target = """#include "util/image_loader.h"
#include "util/image_loader.h"

#if defined(LUNA_TARGET_PS5)
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
}"""

replacement = """#include "util/image_loader.h" """

assert target in text, "target not found"
text = text.replace(target, replacement)
with open('apps/luna-ps5/src/main.cpp', 'w', encoding='utf-8') as f:
    f.write(text)
print("Removed launchPs5WebBrowser cleanly")
