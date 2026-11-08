#pragma once

#include "core/pattern.hpp"
#include <nozzle/nozzle_c.h>

namespace nozzle_tester {

inline bool received_format(NozzleTextureFormat format, tester_format &out) {
    switch(format) {
        case NOZZLE_FORMAT_RGBA8_UNORM: out = tester_format::rgba8_unorm; return true;
        case NOZZLE_FORMAT_BGRA8_UNORM: out = tester_format::bgra8_unorm; return true;
        case NOZZLE_FORMAT_RGBA16_FLOAT: out = tester_format::rgba16_float; return true;
        case NOZZLE_FORMAT_RGBA32_FLOAT: out = tester_format::rgba32_float; return true;
        default: return false;
    }
}

// Shared GUI/CLI receive construction. Storage determines decoding; semantic
// format determines the requested-format check. Never relabel the copied bytes.
inline NozzleErrorCode copy_received_image(NozzleFrame *frame, const NozzleFrameInfo &info,
                                          image_buffer &image, NozzleMappedPixels &copied_pixels) {
    tester_format observed_format{};
    tester_format semantic_format{};
    NozzleTextureFormat semantic = info.semantic_format;
    // Preserve compatibility with legacy or unspecified UNKNOWN raw semantics.
    // Resolve that case through core, never overwrite explicit semantics.
    if(semantic == NOZZLE_FORMAT_UNKNOWN) {
        NozzleResolvedTextureFormat resolved{};
        const auto error = nozzle_frame_get_resolved_format(frame, &resolved);
        if(error != NOZZLE_OK) return error;
        semantic = resolved.semantic_format;
    }
    if(!received_format(info.format, observed_format) ||
       !received_format(semantic, semantic_format)) return NOZZLE_ERROR_UNSUPPORTED_FORMAT;
    const uint32_t bpp = bytes_per_pixel(observed_format);
    std::vector<uint8_t> copied(static_cast<size_t>(info.width) * info.height * bpp);
    const NozzleErrorCode error = nozzle_frame_copy_pixels_with_origin(
        frame, NOZZLE_ORIGIN_TOP_LEFT, copied.data(), copied.size(), &copied_pixels);
    if(error != NOZZLE_OK) return error;
    image.width = info.width;
    image.height = info.height;
    image.format = observed_format;
    image.semantic_format = semantic_format;
    image.bytes = copied;
    // The local copy vector is gone on return; never expose its borrowed pointer.
    copied_pixels.data = image.bytes.data();
    return NOZZLE_OK;
}

} // namespace nozzle_tester
