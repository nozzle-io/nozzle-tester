// Independent Metal publisher for the production CLI receiver contract.
#include "core/received_image.hpp"

#include <cstdio>
#include <cstring>

namespace {
bool succeeded(NozzleErrorCode error, const char *operation) {
    if(error == NOZZLE_OK) return true;
    std::fprintf(stderr, "publisher %s failed: %d\n", operation, (int)error);
    return false;
}

bool publish(NozzleSender *sender, uint64_t payload_index) {
    nozzle_tester::test_case test{};
    test.width = 65;
    test.height = 33;
    test.frame_index = payload_index;
    NozzleFrame *frame = nullptr;
    if(!succeeded(nozzle_sender_acquire_writable_frame(sender, test.width, test.height,
        NOZZLE_FORMAT_RGBA8_UNORM, &frame), "acquire")) return false;
    NozzlePixelMapping *mapping = nullptr;
    NozzleMappedPixels pixels{};
    bool ok = succeeded(nozzle_frame_lock_writable_pixels_mapping_with_origin(frame, NOZZLE_ORIGIN_TOP_LEFT,
        &mapping, &pixels), "map");
    if(ok) {
        const auto row_size = test.width * 4u;
        ok = pixels.data && pixels.width == test.width && pixels.height == test.height &&
            pixels.row_stride_bytes >= (int64_t)row_size && nozzle_tester::received_format(pixels.format, test.format) &&
            nozzle_tester::bytes_per_pixel(test.format) == 4;
        if(!ok) std::fprintf(stderr, "publisher invalid mapped layout\n");
        if(ok) {
            const auto pattern = nozzle_tester::generate_pattern(test);
            for(uint32_t y = 0; y < test.height; ++y) {
                std::memcpy((uint8_t *)pixels.data + y * pixels.row_stride_bytes,
                    pattern.bytes.data() + y * row_size, row_size);
            }
        }
        if(!succeeded(nozzle_pixel_mapping_unlock_checked(&mapping), "unlock")) ok = false;
    }
    if(ok) ok = succeeded(nozzle_sender_commit_frame(sender, frame), "commit");
    if(!ok) (void)nozzle_sender_discard_frame(sender, frame);
    nozzle_frame_release(frame);
    return ok;
}
}

int main(int argc, char **argv) {
    if(argc != 3) return 2;
    NozzleSenderDesc desc{};
    desc.name = argv[1];
    desc.ring_buffer_size = 3;
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS;
    NozzleSender *sender = nullptr;
    if(!succeeded(nozzle_sender_create(&desc, &sender), "create")) return 1;
    bool ok = publish(sender, std::strcmp(argv[2], "wrong") == 0 ? 0 : 1);
    if(ok) {
        std::puts("ready");
        std::fflush(stdout);
        uint64_t index = 1;
        while(std::getchar() == 'p') {
            if(!publish(sender, ++index)) { ok = false; break; }
        }
    }
    nozzle_sender_destroy(sender);
    return ok ? 0 : 1;
}
