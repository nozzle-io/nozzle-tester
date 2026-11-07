#pragma once

#include "gui/pixel_copy.hpp"
#include "gui/runtime_tick.hpp"

namespace nozzle_tester {

struct gui_publication {
    test_case test;
    image_buffer image;
    NozzleErrorCode error{NOZZLE_OK};
    const char *failed_stage{nullptr};
    gui_publish_status status{gui_publish_status::failed};
    bool commit_attempted{false};
};

// Non-UI portion of publish_one_frame, also exercised by the native peer.
// A fresh sender starts at frame 1. A timed-out commit advances the recorded
// core's sequence, but is not a successful publication; acquisition does neither.
inline gui_publication publish_gui_pattern(NozzleSender *sender, test_case test, uint64_t commit_attempts) {
    gui_publication result{};
    test.frame_index = commit_attempts + 1u;
    result.test = test;
    result.image = generate_pattern(test);
    NozzleFrame *writable = nullptr;
    result.failed_stage = "acquire_writable_frame_failed";
    result.error = nozzle_sender_acquire_writable_frame(sender, test.width, test.height, gui_image_format(test.format), &writable);
    if(result.error != NOZZLE_OK || writable == nullptr) {
        if(result.error == NOZZLE_ERROR_TIMEOUT) result.status = gui_publish_status::retry_later;
        return result;
    }

    NozzlePixelMapping *mapping = nullptr;
    NozzleMappedPixels pixels{};
    result.failed_stage = "writable_mapping_failed";
    result.error = nozzle_frame_lock_writable_pixels_mapping_with_origin(writable, NOZZLE_ORIGIN_TOP_LEFT, &mapping, &pixels);
    if(result.error != NOZZLE_OK || mapping == nullptr) {
        if(mapping != nullptr) nozzle_pixel_mapping_unlock(&mapping);
        nozzle_sender_discard_frame(sender, writable);
        nozzle_frame_release(writable);
        return result;
    }
    const auto copy_error = copy_gui_image_to_mapping(result.image, pixels);
    const auto unlock_error = nozzle_pixel_mapping_unlock_checked(&mapping);
    if(copy_error != NOZZLE_OK || unlock_error != NOZZLE_OK) {
        nozzle_sender_discard_frame(sender, writable);
        nozzle_frame_release(writable);
        result.error = copy_error != NOZZLE_OK ? copy_error : unlock_error;
        result.failed_stage = copy_error != NOZZLE_OK ? "copy_pixels_failed" : "unlock_pixels_failed";
        return result;
    }
    result.failed_stage = "commit_frame_failed";
    result.commit_attempted = true;
    result.error = nozzle_sender_commit_frame(sender, writable);
    if(result.error != NOZZLE_OK) nozzle_sender_discard_frame(sender, writable);
    nozzle_frame_release(writable);
    if(result.error == NOZZLE_OK) {
        result.failed_stage = nullptr;
        result.status = gui_publish_status::published;
    } else if(result.error == NOZZLE_ERROR_TIMEOUT) {
        result.status = gui_publish_status::retry_later;
    }
    return result;
}

} // namespace nozzle_tester
