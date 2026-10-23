#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace luna::util {

bool decodeImageRgba(const uint8_t* data, size_t size, std::vector<uint8_t>& outRgba, int& outW, int& outH);

} // namespace luna::util
