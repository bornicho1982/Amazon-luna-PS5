import os

path = r'c:\proyectos antigravity\webkit PS5 v2\apps\luna-ps5\src\main.cpp'
with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

start = None
end = None
for i, line in enumerate(lines):
    if 'case AppState::Catalog: {' in line:
        start = i
    if 'case AppState::GameDetail: {' in line:
        end = i
        break

assert start is not None and end is not None

new_catalog_block = [
    '            case AppState::Catalog: {\n',
    '                if (pressedNow & luna::input::PAD_CROSS) {\n',
    '                    luna::util::CrashLogger::log("[Main] [X] pulsado: reabriendo Web de Amazon Luna en PS5");\n',
    '                    launchPs5WebBrowser(localWebUrl);\n',
    '                } else if (pressedNow & luna::input::PAD_SQUARE) {\n',
    '                    luna::util::CrashLogger::log("[Main] [CUADRADO] pulsado: desvinculando cuenta de Amazon Luna");\n',
    '                    cbl.logout();\n',
    '                    catalogFetched = false;\n',
    '                    g_catalogDiagnostic.clear();\n',
    '                    g_lastStatusText = "Cuenta desvinculada. Escanea el nuevo codigo QR para vincular.";\n',
    '                    appState = AppState::LoginCBL;\n',
    '                    break;\n',
    '                } else if (pressedNow & luna::input::PAD_OPTIONS) {\n',
    '                    luna::util::CrashLogger::log("[Main] [OPTIONS] pulsado: saliendo de la aplicacion");\n',
    '                    g_running = false;\n',
    '                    break;\n',
    '                }\n',
    '\n',
    '                std::string statusMsg = g_lastStatusText;\n',
    '                if (statusMsg.empty()) {\n',
    '                    const auto& prof = sessionClient.getPlayerProfile();\n',
    '                    if (!prof.accountName.empty()) {\n',
    '                        statusMsg = "Conectado a Amazon como " + prof.accountName + " | Motor nativo en espera";\n',
    '                    } else {\n',
    '                        statusMsg = "Conectado a Amazon Luna | Motor nativo en espera";\n',
    '                    }\n',
    '                }\n',
    '\n',
    '                display.drawNativeEngineWaitingScreen(localWebUrl, lanWebUrl, statusMsg, frameCount);\n',
    '                break;\n',
    '            }\n',
    '\n'
]

# Note: end is the line with '            case AppState::GameDetail: {'
# Before that line, lines[end-1] might be blank, lines[end-2] is '            }'
# Let's replace from start up to end
new_lines = lines[:start] + new_catalog_block + lines[end:]

with open(path, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print("Catalog state replaced with Direct Web Bridge Waiting Screen successfully.")
