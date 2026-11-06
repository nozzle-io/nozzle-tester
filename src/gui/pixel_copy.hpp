#pragma once

#include "core/pattern.hpp"
#include <nozzle/nozzle_c.h>
#include <cstddef>
#include <cstring>
#include <limits>

namespace nozzle_tester {

inline NozzleTextureFormat gui_image_format(tester_format format) {
    switch(format) {
        case tester_format::rgba8_unorm: return NOZZLE_FORMAT_RGBA8_UNORM;
        case tester_format::bgra8_unorm: return NOZZLE_FORMAT_BGRA8_UNORM;
        case tester_format::rgba16_float: return NOZZLE_FORMAT_RGBA16_FLOAT;
        case tester_format::rgba32_float: return NOZZLE_FORMAT_RGBA32_FLOAT;
        default: return NOZZLE_FORMAT_UNKNOWN;
    }
}

// Shared by the GUI publisher and the independent native-peer test. No implicit
// precision conversion: only identical storage or byte RGBA/BGRA permutation.
inline NozzleErrorCode copy_gui_image_to_mapping(const image_buffer &image, const NozzleMappedPixels &pixels) {
    const NozzleTextureFormat source_format = gui_image_format(image.format);
    if(source_format == NOZZLE_FORMAT_UNKNOWN) return NOZZLE_ERROR_UNSUPPORTED_FORMAT;
    const bool swap_rb = (source_format == NOZZLE_FORMAT_RGBA8_UNORM && pixels.format == NOZZLE_FORMAT_BGRA8_UNORM) ||
        (source_format == NOZZLE_FORMAT_BGRA8_UNORM && pixels.format == NOZZLE_FORMAT_RGBA8_UNORM);
    if(pixels.format != source_format && !swap_rb) return NOZZLE_ERROR_UNSUPPORTED_FORMAT;
    if(!pixels.data || image.width == 0 || image.height == 0 ||
       pixels.width != image.width || pixels.height != image.height ||
       (pixels.origin != NOZZLE_ORIGIN_TOP_LEFT && pixels.origin != NOZZLE_ORIGIN_BOTTOM_LEFT) ||
       pixels.row_stride_bytes == std::numeric_limits<int64_t>::min()) return NOZZLE_ERROR_INVALID_ARGUMENT;
    const uint64_t row_bytes = static_cast<uint64_t>(image.width) * bytes_per_pixel(image.format);
    if(row_bytes > image.bytes.size() / image.height || row_bytes > UINT32_MAX) return NOZZLE_ERROR_INVALID_ARGUMENT;
    const uint64_t stride = pixels.row_stride_bytes < 0 ? static_cast<uint64_t>(-pixels.row_stride_bytes)
                                                       : static_cast<uint64_t>(pixels.row_stride_bytes);
    const uint64_t max_offset = static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if(stride < row_bytes || row_bytes > max_offset ||
       (image.height > 1 && stride > (max_offset - row_bytes) / (image.height - 1))) return NOZZLE_ERROR_INVALID_ARGUMENT;
    const uint8_t permutation[4] = {2, 1, 0, 3};
    uint8_t *target = static_cast<uint8_t *>(pixels.data);
    for(uint32_t y = 0; y < image.height; y++) {
        const uint32_t source_y = pixels.origin == NOZZLE_ORIGIN_BOTTOM_LEFT ? image.height - 1 - y : y;
        const uint8_t *source_row = image.bytes.data() + static_cast<size_t>(source_y) * row_bytes;
        uint8_t *target_row = target + static_cast<int64_t>(y) * pixels.row_stride_bytes;
        if(swap_rb) {
            const NozzleErrorCode error = nozzle_swizzle_channels(source_row, target_row, image.width, 1,
                static_cast<int64_t>(row_bytes), static_cast<int64_t>(row_bytes), source_format, permutation);
            if(error != NOZZLE_OK) return error;
        } else {
            std::memcpy(target_row, source_row, static_cast<size_t>(row_bytes));
        }
    }
    return NOZZLE_OK;
}

} // namespace nozzle_tester
