#pragma once

#include "core/pattern.hpp"
#include <nozzle/nozzle_c.h>
#include <cstring>

namespace nozzle_tester {

// Shared by the GUI publisher and the independent native-peer test.
inline void copy_gui_image_to_mapping(const image_buffer &image, const NozzleMappedPixels &pixels) {
    const uint32_t row_bytes = image.width * bytes_per_pixel(image.format);
    uint8_t *target = static_cast<uint8_t *>(pixels.data);
    for(uint32_t y = 0; y < image.height; y++) {
        std::memcpy(target + static_cast<int64_t>(y) * pixels.row_stride_bytes,
            image.bytes.data() + static_cast<size_t>(y) * row_bytes, row_bytes);
    }
}

} // namespace nozzle_tester
