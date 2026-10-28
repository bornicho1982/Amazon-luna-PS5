def load(p):
    return open(p, 'rb').read().decode('latin-1').replace('\r\r\n', '\n').replace('\r\n', '\n')


def save(p, s):
    open(p, 'wb').write(s.replace('\n', '\r\n').encode('latin-1'))


def rep(s, old, new):
    assert old in s, 'NOT FOUND: ' + old[:70]
    return s.replace(old, new, 1)


h = load('apps/luna-ps5/src/util/settings.h')
h = rep(h, 'HudMode hudMode = HudMode::Auto;', 'HudMode hudMode = HudMode::Hidden;')
save('apps/luna-ps5/src/util/settings.h', h)

c = load('apps/luna-ps5/src/util/settings.cpp')
c = rep(c, '    int hm = root["hudMode"].asInt(0);\n',
        '    int hm = root["hudMode"].asInt(2);\n'
        '    // Legacy settings files (saved before the HUD default changed) are migrated to HUD hidden\n'
        '    if (root["hudDefaultV2"].asInt(0) == 0) hm = 2;\n')
c = rep(c, '    json += "  \\"hudMode\\": " + std::to_string(static_cast<int>(hudMode)) + ",\\n";\n',
        '    json += "  \\"hudMode\\": " + std::to_string(static_cast<int>(hudMode)) + ",\\n";\n'
        '    json += "  \\"hudDefaultV2\\": 1,\\n";\n')
save('apps/luna-ps5/src/util/settings.cpp', c)
print('settings OK')
