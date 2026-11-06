#include "gui/pixel_receive.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {
struct resources {
    NozzleSender *sender = nullptr;
    NozzleReceiver *receiver = nullptr;
    NozzleFrame *write = nullptr;
    NozzleFrame *read = nullptr;
    NozzlePixelMapping *mapping = nullptr;
    bool committed = false;
    ~resources() {
        nozzle_pixel_mapping_unlock(&mapping);
        if(read) nozzle_frame_release(read);
        if(write) {
            if(!committed) (void)nozzle_sender_discard_frame(sender, write);
            nozzle_frame_release(write);
        }
        if(receiver) nozzle_receiver_destroy(receiver);
        if(sender) nozzle_sender_destroy(sender);
    }
};

bool ok(NozzleErrorCode error, const char *where) {
    if(error == NOZZLE_OK) return true;
    std::fprintf(stderr, "GUI_RECEIVE_PEER %s error=%d\n", where, (int)error);
    return false;
}

bool run_case(NozzleTextureFormat requested, uint32_t width, bool bottom) {
    const bool mono = requested == NOZZLE_FORMAT_R8_UNORM;
    const bool byte = requested == NOZZLE_FORMAT_RGBA8_UNORM || requested == NOZZLE_FORMAT_BGRA8_UNORM;
    const uint32_t bpp = mono ? 1 : byte ? 4 : requested == NOZZLE_FORMAT_RGBA16_FLOAT ? 8 : 16;
    const NozzleTextureFormat storage = byte ? NOZZLE_FORMAT_BGRA8_UNORM : requested;
    const std::string name = "g230-" + std::to_string(getpid()) + "-" + std::to_string(requested)
        + "-" + std::to_string(width) + "-" + std::to_string(bottom);
    resources peer;
    NozzleSenderDesc desc{};
    desc.name = name.c_str();
    desc.application_name = "GUI receive native peer";
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
    if(!ok(nozzle_sender_acquire_writable_frame(peer.sender, width, 3, requested, &peer.write), "write acquire")) return false;
    NozzleMappedPixels mapped{};
    if(!ok(nozzle_frame_lock_writable_pixels_mapping_with_origin(peer.write,
        bottom ? NOZZLE_ORIGIN_BOTTOM_LEFT : NOZZLE_ORIGIN_TOP_LEFT, &peer.mapping, &mapped), "write map")) return false;
    if(!mapped.data || mapped.width != width || mapped.height != 3 || mapped.format != storage ||
       mapped.row_stride_bytes == 0) return false;
    const int64_t native_stride = mapped.row_stride_bytes;
    const int64_t absolute_stride = bottom ? -native_stride : native_stride;
    if(absolute_stride < static_cast<int64_t>(width) * bpp) return false;
    // Write actual native lanes directly. No GUI sender, pattern generator,
    // swizzle helper or receive conversion participates in the producer.
    for(uint32_t row_index = 0; row_index < 3; ++row_index) {
        const uint32_t y = bottom ? 2 - row_index : row_index;
        auto *row = static_cast<uint8_t *>(mapped.data) + row_index * native_stride;
        for(uint32_t x = 0; x < width; ++x) {
            if(mono) row[x] = static_cast<uint8_t>(40 + y);
            else if(byte) {
                const uint8_t lanes[] = {static_cast<uint8_t>(40+y), static_cast<uint8_t>(20+x%193),
                    static_cast<uint8_t>(190+y), static_cast<uint8_t>(149+x%73)};
                std::memcpy(row + x*bpp, lanes, sizeof(lanes));
            } else if(bpp == 8) {
                const uint16_t lanes[] = {static_cast<uint16_t>(0x3c01+y), static_cast<uint16_t>(0xb801+x%13),
                    static_cast<uint16_t>(0x4001+y), static_cast<uint16_t>(0x3801+x%7)};
                std::memcpy(row + x*bpp, lanes, sizeof(lanes));
            } else {
                const uint32_t lanes[] = {0x3f800001u+y, 0xbf000001u+x%13, 0x40000001u+y, 0x3f000001u+x%7};
                std::memcpy(row + x*bpp, lanes, sizeof(lanes));
            }
        }
    }
    if(!ok(nozzle_pixel_mapping_unlock_checked(&peer.mapping), "write unlock") ||
       !ok(nozzle_sender_commit_frame(peer.sender, peer.write), "commit")) return false;
    peer.committed = true;
    NozzleReceiverDesc receiver_desc{};
    receiver_desc.name = name.c_str();
    NozzleAcquireDesc acquire{3000};
    if(!ok(nozzle_receiver_create(&receiver_desc, &peer.receiver), "receiver create") ||
       !ok(nozzle_receiver_acquire_frame(peer.receiver, &acquire, &peer.read), "read acquire")) return false;
    NozzleFrameInfo info{};
    if(!ok(nozzle_frame_get_info(peer.read, &info), "frame info")) return false;
    nozzle_tester::image_buffer image{};
    NozzleMappedPixels copied{};
    const NozzleErrorCode error = nozzle_tester::copy_gui_received_image(peer.read, info, image, copied);
    std::printf("GUI_RECEIVE_METADATA requested=%d native=%d info=%d semantic=%d copied=%d native_stride=%lld copy_stride=%lld size=%ux3 bottom=%d error=%d\n",
        (int)requested, (int)storage, (int)info.format, (int)info.semantic_format, (int)copied.format,
        (long long)native_stride, (long long)copied.row_stride_bytes, width, bottom, (int)error);
    if(mono) return error == NOZZLE_ERROR_UNSUPPORTED_FORMAT && image.bytes.empty();
    if(!ok(error, "GUI copy") || info.width != width || info.height != 3 ||
       copied.format != storage || copied.width != width || copied.height != 3 ||
       copied.row_stride_bytes != static_cast<int64_t>(width)*bpp || copied.origin != NOZZLE_ORIGIN_TOP_LEFT ||
       image.width != width || image.height != 3 || image.bytes.size() != width*3*bpp ||
       copied.data != image.bytes.data()) return false;
    const auto expected_format = byte ? nozzle_tester::tester_format::bgra8_unorm : bpp == 8
        ? nozzle_tester::tester_format::rgba16_float : nozzle_tester::tester_format::rgba32_float;
    if(image.format != expected_format) {
        std::fprintf(stderr, "GUI image interpretation disagrees with native storage\n");
        return false;
    }
    // Independent expected components, exact float payload bits including HDR,
    // negatives and low float32 mantissa bits; no float-to-half roundtrip.
    for(uint32_t y = 0; y < 3; ++y) {
        for(uint32_t x = 0; x < width; ++x) {
            const uint8_t *pixel = image.bytes.data() + (y*width+x)*bpp;
            if(byte) {
                if(pixel[2] != 190+y || pixel[1] != 20+x%193 || pixel[0] != 40+y || pixel[3] != 149+x%73) return false;
            } else if(bpp == 8) {
                uint16_t actual[4];
                std::memcpy(actual, pixel, sizeof(actual));
                if(actual[0] != 0x3c01+y || actual[1] != 0xb801+x%13 || actual[2] != 0x4001+y || actual[3] != 0x3801+x%7) return false;
            } else {
                uint32_t actual[4];
                std::memcpy(actual, pixel, sizeof(actual));
                if(actual[0] != 0x3f800001u+y || actual[1] != 0xbf000001u+x%13 || actual[2] != 0x40000001u+y || actual[3] != 0x3f000001u+x%7) return false;
            }
        }
    }
    return true;
}
} // namespace

int main() {
    int failed = 0;
    for(uint32_t width : {65u, 641u}) {
        for(auto format : {NOZZLE_FORMAT_RGBA8_UNORM, NOZZLE_FORMAT_BGRA8_UNORM,
                           NOZZLE_FORMAT_RGBA16_FLOAT, NOZZLE_FORMAT_RGBA32_FLOAT}) {
            for(bool bottom : {false, true}) {
                const bool pass = run_case(format, width, bottom);
                std::printf("GUI_RECEIVE_CASE format=%d width=%u bottom=%d %s\n", (int)format, width, bottom, pass ? "PASS" : "FAIL");
                if(!pass) ++failed;
            }
        }
    }
    const bool unsupported = run_case(NOZZLE_FORMAT_R8_UNORM, 65, false);
    std::printf("GUI_RECEIVE_CASE unsupported R8 %s\n", unsupported ? "PASS" : "FAIL");
    if(!unsupported) ++failed;
    std::printf("GUI_RECEIVE_PEER failed=%d total=17\n", failed);
    return failed == 0 ? 0 : 1;
}
