#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "util/stb_image.h"
#include "util/image_loader.h"

namespace luna::util {

bool decodeImageRgba(const uint8_t* data, size_t size, std::vector<uint8_t>& outRgba, int& outW, int& outH) {
    if (!data || size == 0) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* decoded = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
    if (!decoded) return false;
    outW = w;
    outH = h;
    outRgba.assign(decoded, decoded + (static_cast<size_t>(w) * static_cast<size_t>(h) * 4));
    stbi_image_free(decoded);
    return true;
}

} // namespace luna::util
