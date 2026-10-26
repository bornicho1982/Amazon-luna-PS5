with open('apps/luna-ps5/assets/luna_premium_hero.jpg', 'rb') as f:
    data = f.read()

out_header = f"""#pragma once
#include <cstdint>
#include <cstddef>

namespace luna::assets {{
constexpr size_t kLunaPremiumHeroJpgSize = {len(data)};
extern const uint8_t kLunaPremiumHeroJpg[{len(data)}];
}}
"""

with open('apps/luna-ps5/src/display/hero_banner_asset.h', 'w', encoding='utf-8') as f:
    f.write(out_header)

out_cpp = f"""#include "display/hero_banner_asset.h"

namespace luna::assets {{
alignas(16) const uint8_t kLunaPremiumHeroJpg[{len(data)}] = {{
"""

chunks = []
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    chunks.append('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n')

out_cpp += ''.join(chunks)
out_cpp += '};\n}\n'

with open('apps/luna-ps5/src/display/hero_banner_asset.cpp', 'w', encoding='utf-8') as f:
    f.write(out_cpp)

print(f'Generated hero_banner_asset.h and hero_banner_asset.cpp ({len(data)} bytes)')
