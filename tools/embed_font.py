with open('apps/luna-ps5/assets/fonts/Inter-Bold.ttf', 'rb') as f:
    data = f.read()

print(f'Font size: {len(data)} bytes')

with open('apps/luna-ps5/src/display/font_inter_bold.h', 'w') as out:
    out.write('// Auto-generated Inter-Bold font data\n')
    out.write('#pragma once\n\n')
    out.write('#include <cstddef>\n\n')
    out.write(f'constexpr size_t kInterBoldFontSize = {len(data)};\n')
    out.write('extern const unsigned char kInterBoldFontData[];\n')

with open('apps/luna-ps5/src/display/font_inter_bold.cpp', 'w') as out:
    out.write('#include "display/font_inter_bold.h"\n\n')
    out.write('alignas(16) const unsigned char kInterBoldFontData[] = {\n')
    for i in range(0, len(data), 24):
        chunk = data[i:i+24]
        out.write('  ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n')
    out.write('};\n')

print('Generated font_inter_bold.h and .cpp successfully')
