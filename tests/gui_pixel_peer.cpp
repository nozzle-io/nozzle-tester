#include "gui/pixel_copy.hpp"
#include <cstdio>
#include <string>
#include <unistd.h>

namespace {
struct resources {
    NozzleSender *sender = nullptr;
    NozzleReceiver *receiver = nullptr;
    NozzleFrame *write = nullptr;
    NozzleFrame *read = nullptr;
    NozzlePixelMapping *write_map = nullptr;
    NozzlePixelMapping *read_map = nullptr;
    bool committed = false;
    ~resources() {
        nozzle_pixel_mapping_unlock(&read_map);
        nozzle_pixel_mapping_unlock(&write_map);
        if(read) nozzle_frame_release(read);
        if(write) {
            if(!committed) (void)nozzle_sender_discard_frame(sender, write);
            nozzle_frame_release(write);
        }
        if(receiver) nozzle_receiver_destroy(receiver);
        if(sender) nozzle_sender_destroy(sender);
    }
};

bool ok(NozzleErrorCode code, const char *operation) {
    if(code == NOZZLE_OK) return true;
    std::fprintf(stderr, "%s error=%d\n", operation, static_cast<int>(code));
    return false;
}

bool run_case(nozzle_tester::tester_format format, uint32_t width, uint32_t height) {
    using nozzle_tester::tester_format;
    const bool byte_format = format == tester_format::rgba8_unorm || format == tester_format::bgra8_unorm;
    const NozzleTextureFormat requested = format == tester_format::rgba8_unorm ? NOZZLE_FORMAT_RGBA8_UNORM :
        format == tester_format::bgra8_unorm ? NOZZLE_FORMAT_BGRA8_UNORM :
        format == tester_format::rgba16_float ? NOZZLE_FORMAT_RGBA16_FLOAT : NOZZLE_FORMAT_RGBA32_FLOAT;
    const NozzleTextureFormat expected_storage = byte_format ? NOZZLE_FORMAT_BGRA8_UNORM : requested;
    const uint32_t bpp = byte_format ? 4 : format == tester_format::rgba16_float ? 8 : 16;
    nozzle_tester::image_buffer image{width, height, format, std::vector<uint8_t>(width * height * bpp)};
    for(uint32_t y = 0; y < height; ++y) {
        for(uint32_t x = 0; x < width; ++x) {
            auto *pixel = image.bytes.data() + (y * width + x) * bpp;
            if(byte_format) {
                const uint8_t r = 190 + y % 31, g = 20 + x % 193, b = 40 + y % 79, a = 149 + x % 73;
                pixel[0] = format == tester_format::bgra8_unorm ? b : r;
                pixel[1] = g;
                pixel[2] = format == tester_format::bgra8_unorm ? r : b;
                pixel[3] = a;
            } else if(format == tester_format::rgba16_float) {
                const uint16_t values[] = {static_cast<uint16_t>(0x3c01 + y % 17),
                    static_cast<uint16_t>(0x3801 + x % 13), static_cast<uint16_t>(0x3401 + y % 7),
                    static_cast<uint16_t>(0x3a01 + x % 5)};
                std::memcpy(pixel, values, sizeof(values));
            } else {
                // Low mantissa bits detect unwanted float32 -> float16 conversion.
                const uint32_t values[] = {0x3f800001u + y % 17, 0x3f000001u + x % 13,
                    0x3e800001u + y % 7, 0x3f400001u + x % 5};
                std::memcpy(pixel, values, sizeof(values));
            }
        }
    }
    const std::string label = std::string(nozzle_tester::format_to_string(format)) + "-" + std::to_string(width);
    const std::string name = "g223-" + std::to_string(getpid()) + "-" + label;
    resources peer;
    NozzleSenderDesc desc{};
    desc.name = name.c_str();
    desc.application_name = "GUI pixel peer";
    desc.ring_buffer_size = 3;
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS;
    if(!ok(nozzle_sender_create(&desc, &peer.sender), "sender create")) return false;
    NozzleSenderInfoSnapshot sender_info{};
    if(!ok(nozzle_sender_get_info_snapshot(peer.sender, &sender_info), "sender info") ||
       sender_info.backend != NOZZLE_BACKEND_METAL) {
        std::fprintf(stderr, "Metal required, no SKIP\n");
        return false;
    }
    if(!ok(nozzle_sender_acquire_writable_frame(peer.sender, width, height, requested, &peer.write), "writable acquire")) return false;
    NozzleMappedPixels mapped{};
    if(!ok(nozzle_frame_lock_writable_pixels_mapping_with_origin(peer.write, NOZZLE_ORIGIN_TOP_LEFT,
        &peer.write_map, &mapped), "write map")) return false;
    if(!mapped.data || mapped.width != width || mapped.height != height ||
       mapped.format != expected_storage || mapped.row_stride_bytes < static_cast<int64_t>(width) * bpp) return false;
    std::printf("%s native format=%d stride=%lld\n", label.c_str(), (int)mapped.format, (long long)mapped.row_stride_bytes);
    nozzle_tester::copy_gui_image_to_mapping(image, mapped);
    if(!ok(nozzle_pixel_mapping_unlock_checked(&peer.write_map), "write unlock") ||
       !ok(nozzle_sender_commit_frame(peer.sender, peer.write), "commit")) return false;
    peer.committed = true;
    NozzleReceiverDesc receiver_desc{};
    receiver_desc.name = name.c_str();
    receiver_desc.receive_mode = NOZZLE_RECEIVE_LATEST_ONLY;
    if(!ok(nozzle_receiver_create(&receiver_desc, &peer.receiver), "receiver create")) return false;
    NozzleAcquireDesc acquire{3000};
    if(!ok(nozzle_receiver_acquire_frame(peer.receiver, &acquire, &peer.read), "native acquire") ||
       !ok(nozzle_frame_lock_pixels_mapping_with_origin(peer.read, NOZZLE_ORIGIN_TOP_LEFT,
           &peer.read_map, &mapped), "native map")) return false;
    if(!mapped.data || mapped.width != width || mapped.height != height ||
       mapped.format != expected_storage || mapped.row_stride_bytes < static_cast<int64_t>(width) * bpp) return false;
    // Independent native read: no GUI receive, CLI conversion or pattern verifier.
    for(uint32_t y = 0; y < height; ++y) {
        const auto *row = static_cast<const uint8_t *>(mapped.data) + y * mapped.row_stride_bytes;
        for(uint32_t x = 0; x < width; ++x) {
            if(byte_format) {
                const uint8_t expected[] = {static_cast<uint8_t>(40 + y % 79), static_cast<uint8_t>(20 + x % 193),
                    static_cast<uint8_t>(190 + y % 31), static_cast<uint8_t>(149 + x % 73)};
                for(uint32_t c = 0; c < 4; ++c) {
                    if(row[4*x+c] != expected[c]) {
                        std::fprintf(stderr, "%s x=%u y=%u c=%u actual=%u expected=%u\n",
                            label.c_str(), x, y, c, row[4*x+c], expected[c]);
                        return false;
                    }
                }
            } else if(std::memcmp(row + x*bpp, image.bytes.data() + (y*width+x)*bpp, bpp) != 0) {
                std::fprintf(stderr, "%s float bits changed x=%u y=%u\n", label.c_str(), x, y);
                return false;
            }
        }
    }
    return ok(nozzle_pixel_mapping_unlock_checked(&peer.read_map), "native unlock");
}
} // namespace

int main() {
    int failed = 0;
    for(uint32_t width : {65u, 641u}) {
        for(auto format : {nozzle_tester::tester_format::rgba8_unorm, nozzle_tester::tester_format::bgra8_unorm,
                          nozzle_tester::tester_format::rgba16_float, nozzle_tester::tester_format::rgba32_float}) {
            const bool pass = run_case(format, width, 3);
            std::printf("GUI native peer %s %ux3 %s\n", nozzle_tester::format_to_string(format), width, pass ? "PASS" : "FAIL");
            if(!pass) ++failed;
        }
    }
    std::printf("GUI_PIXEL_PEER failed=%d total=8\n", failed);
    return failed == 0 ? 0 : 1;
}
