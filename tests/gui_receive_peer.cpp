#include "core/received_image.hpp"
#include <nozzle/nozzle.hpp>
#include <nozzle/pixel_access.hpp>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

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
    const NozzleErrorCode error = nozzle_tester::copy_received_image(peer.read, info, image, copied);
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

bool run_cli_receiver(const char *cli, const std::string &name, uint32_t width,
                      nozzle_tester::tester_format expected_format, int expected_exit) {
    std::vector<std::string> args = {cli, "receiver", "--name", name, "--width", std::to_string(width),
        "--height", "33", "--format", nozzle_tester::format_to_string(expected_format),
        "--frames", "1", "--timeout-ms", "3000"};
    std::vector<char *> argv;
    for(auto &arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid{};
    const int spawn_error = posix_spawn(&pid, cli, nullptr, nullptr, argv.data(), environ);
    if(spawn_error != 0) {
        std::fprintf(stderr, "CLI receiver spawn error=%d\n", spawn_error);
        return false;
    }
    int status{};
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while(waited < 0 && errno == EINTR);
    const int exit_code = waited == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    std::printf("CLI_RECEIVE_VERDICT expected_format=%s width=%u exit=%d expected_exit=%d\n",
        nozzle_tester::format_to_string(expected_format), width, exit_code, expected_exit);
    return exit_code == expected_exit;
}

bool has_reason(const nozzle_tester::verify_result &result, const char *reason) {
    for(const auto &actual : result.failure_reasons) if(actual == reason) return true;
    return false;
}

bool run_verdict_case(NozzleTextureFormat requested, uint32_t width, const char *cli) {
    resources peer;
    const std::string name = "v230-" + std::to_string(getpid()) + "-" + std::to_string(requested)
        + "-" + std::to_string(width);
    NozzleSenderDesc desc{};
    desc.name = name.c_str();
    desc.ring_buffer_size = 3;
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS;
    if(!ok(nozzle_sender_create(&desc, &peer.sender), "verdict sender create") ||
       !ok(nozzle_sender_acquire_writable_frame(peer.sender, width, 33, requested, &peer.write), "verdict write acquire")) return false;
    NozzleMappedPixels mapped{};
    if(!ok(nozzle_frame_lock_writable_pixels_mapping_with_origin(peer.write, NOZZLE_ORIGIN_TOP_LEFT,
        &peer.mapping, &mapped), "verdict write map")) return false;
    nozzle_tester::test_case source{};
    source.width = width;
    source.height = 33;
    source.frame_index = 1; // Core's first successful commit is frame 1.
    if(!nozzle_tester::received_format(mapped.format, source.format)) return false;
    const auto pattern = nozzle_tester::generate_pattern(source);
    const size_t row_bytes = width * nozzle_tester::bytes_per_pixel(source.format);
    if(!mapped.data || mapped.width != width || mapped.height != source.height ||
       mapped.row_stride_bytes < static_cast<int64_t>(row_bytes)) return false;
    // Generate in the actual mapped layout, independently of the GUI sender.
    for(uint32_t y = 0; y < source.height; ++y) {
        std::memcpy(static_cast<uint8_t *>(mapped.data) + y * mapped.row_stride_bytes,
            pattern.bytes.data() + y * row_bytes, row_bytes);
    }
    if(!ok(nozzle_pixel_mapping_unlock_checked(&peer.mapping), "verdict write unlock") ||
       !ok(nozzle_sender_commit_frame(peer.sender, peer.write), "verdict commit")) return false;
    peer.committed = true;
    NozzleReceiverDesc receiver_desc{};
    receiver_desc.name = name.c_str();
    NozzleAcquireDesc acquire{3000};
    if(!ok(nozzle_receiver_create(&receiver_desc, &peer.receiver), "verdict receiver create") ||
       !ok(nozzle_receiver_acquire_frame(peer.receiver, &acquire, &peer.read), "verdict read acquire")) return false;
    NozzleFrameInfo info{};
    nozzle_tester::image_buffer image{};
    NozzleMappedPixels copied{};
    if(!ok(nozzle_frame_get_info(peer.read, &info), "verdict frame info") ||
       !ok(nozzle_tester::copy_received_image(peer.read, info, image, copied), "verdict copy")) return false;
    nozzle_tester::test_case expected = source;
    if(!nozzle_tester::received_format(requested, expected.format)) return false;
    expected.frame_index = info.frame_index; // Same as the GUI and CLI receiver.
    const auto result = nozzle_tester::verify_pattern(image, expected);
    std::printf("GUI_RECEIVE_VERDICT requested=%d storage=%d semantic=%d width=%u frame=%llu result=%s reasons=",
        (int)requested, (int)copied.format, (int)info.semantic_format, width,
        (unsigned long long)info.frame_index, nozzle_tester::verdict_to_string(result.result));
    for(const auto &reason : result.failure_reasons) std::printf("%s,", reason.c_str());
    std::printf("\n");
    bool pass = image.bytes == pattern.bytes && image.format == source.format && info.frame_index == source.frame_index &&
        image.semantic_format == expected.format && result.result == nozzle_tester::verdict::pass;
    const auto swapped = nozzle_tester::verify_pattern(nozzle_tester::make_rb_swap_fixture(image), expected);
    const auto flipped = nozzle_tester::verify_pattern(nozzle_tester::make_vertical_flip_fixture(image), expected);
    const auto alpha = nozzle_tester::verify_pattern(nozzle_tester::make_alpha_zero_fixture(image), expected);
    pass = pass && swapped.result == nozzle_tester::verdict::fail && has_reason(swapped, "rb_swap") &&
        flipped.result == nozzle_tester::verdict::fail && has_reason(flipped, "vertical_flip") &&
        alpha.result == nozzle_tester::verdict::fail && has_reason(alpha, "alpha_mismatch");
    auto wrong_format = expected;
    wrong_format.format = requested == NOZZLE_FORMAT_RGBA16_FLOAT ? nozzle_tester::tester_format::rgba8_unorm
        : nozzle_tester::tester_format::rgba16_float;
    const auto mismatch = nozzle_tester::verify_pattern(image, wrong_format);
    pass = pass && mismatch.result == nozzle_tester::verdict::fail && has_reason(mismatch, "format_mismatch");
    // Release the in-process frame before the real CLI receiver acquires it.
    nozzle_frame_release(peer.read);
    peer.read = nullptr;
    const bool cli_pass = run_cli_receiver(cli, name, width, expected.format, 0);
    const bool cli_rejects = run_cli_receiver(cli, name, width, wrong_format.format, 1);
    std::printf("GUI_RECEIVE_CONTROLS requested=%d width=%u corruption_and_format=%s\n",
        (int)requested, width, pass ? "PASS" : "FAIL");
    return pass && cli_pass && cli_rejects;
}

struct external_resources {
    nozzle::sender sender;
    nozzle::writable_frame frame;
    ~external_resources() {
        if(frame.valid()) (void)sender.discard_frame(frame);
    }
};

bool run_external_case(uint32_t width, bool mono, const char *cli) {
    const std::string name = "e230-" + std::to_string(getpid()) + "-" + std::to_string(width)
        + "-" + std::to_string(mono);
    external_resources source;
    nozzle::sender_desc desc;
    desc.name = name;
    auto sender = nozzle::sender::create(desc);
    if(!sender.ok()) return false;
    source.sender = std::move(sender.value());
    const auto storage = mono ? nozzle::texture_format::r8_unorm : nozzle::texture_format::bgra8_unorm;
    auto frame = source.sender.acquire_writable_frame({width, 33, storage});
    if(!frame.ok()) return false;
    source.frame = std::move(frame.value());
    auto mapping = nozzle::lock_writable_pixels_mapping_with_origin(source.frame, nozzle::texture_origin::top_left);
    if(!mapping.ok()) return false;
    const auto &pixels = mapping.value().pixels();
    const size_t row_bytes = width * (mono ? 1u : 4u);
    if(!pixels.data || pixels.format != storage || pixels.width != width || pixels.height != 33 ||
       pixels.row_stride_bytes < static_cast<int64_t>(row_bytes)) return false;
    nozzle_tester::test_case expected{};
    expected.width = width;
    expected.height = 33;
    expected.format = nozzle_tester::tester_format::bgra8_unorm;
    expected.frame_index = 1;
    const auto pattern = nozzle_tester::generate_pattern(expected);
    for(uint32_t y = 0; y < expected.height; ++y) {
        auto *row = static_cast<uint8_t *>(pixels.data) + y * pixels.row_stride_bytes;
        if(mono) std::memset(row, 41, row_bytes);
        else std::memcpy(row, pattern.bytes.data() + y * row_bytes, row_bytes);
    }
    if(!mapping.value().unlock_checked().ok() ||
       !source.sender.publish_external_texture(source.frame.get_texture()).ok()) return false;

    resources observer;
    NozzleReceiverDesc receiver_desc{};
    receiver_desc.name = name.c_str();
    NozzleAcquireDesc acquire{3000};
    NozzleFrameInfo info{};
    NozzleResolvedTextureFormat resolved{};
    if(!ok(nozzle_receiver_create(&receiver_desc, &observer.receiver), "external receiver create") ||
       !ok(nozzle_receiver_acquire_frame(observer.receiver, &acquire, &observer.read), "external acquire") ||
       !ok(nozzle_frame_get_info(observer.read, &info), "external info") ||
       !ok(nozzle_frame_get_resolved_format(observer.read, &resolved), "external resolved")) return false;
    const auto expected_storage = mono ? NOZZLE_FORMAT_R8_UNORM : NOZZLE_FORMAT_BGRA8_UNORM;
    std::printf("EXTERNAL_RECEIVE_METADATA width=%u raw=%d resolved=%d storage=%d frame=%llu\n",
        width, (int)info.semantic_format, (int)resolved.semantic_format, (int)resolved.storage_format,
        (unsigned long long)info.frame_index);
    // Current publication carries normalized semantics after core #226.
    if(info.semantic_format != expected_storage || resolved.semantic_format != expected_storage ||
       resolved.storage_format != expected_storage || info.frame_index != expected.frame_index) return false;
    nozzle_tester::image_buffer image{};
    NozzleMappedPixels copied{};
    const auto error = nozzle_tester::copy_received_image(observer.read, info, image, copied);
    bool pass;
    if(mono) {
        pass = error == NOZZLE_ERROR_UNSUPPORTED_FORMAT && image.bytes.empty();
    } else {
        pass = error == NOZZLE_OK && image.format == expected.format && image.semantic_format == expected.format &&
            image.bytes == pattern.bytes;
        if(error == NOZZLE_OK) {
            const auto correct = nozzle_tester::verify_pattern(image, expected);
            auto wrong = expected;
            wrong.format = nozzle_tester::tester_format::rgba16_float;
            const auto mismatch = nozzle_tester::verify_pattern(image, wrong);
            pass = pass && correct.result == nozzle_tester::verdict::pass &&
                mismatch.result == nozzle_tester::verdict::fail && has_reason(mismatch, "format_mismatch");
        }
    }
    std::printf("EXTERNAL_RECEIVE_HELPER width=%u mono=%d copy_error=%d %s\n", width, mono, (int)error, pass ? "PASS" : "FAIL");

    // Inject only the helper's metadata argument. The real frame, its resolved
    // format and shared publication are unchanged; this is not an old publisher.
    auto legacy_info = info;
    legacy_info.semantic_format = NOZZLE_FORMAT_UNKNOWN;
    nozzle_tester::image_buffer legacy_image{};
    NozzleMappedPixels legacy_copied{};
    const auto legacy_error = nozzle_tester::copy_received_image(
        observer.read, legacy_info, legacy_image, legacy_copied);
    const bool legacy_pass = mono
        ? legacy_error == NOZZLE_ERROR_UNSUPPORTED_FORMAT && legacy_image.bytes.empty()
        : legacy_error == NOZZLE_OK && legacy_image.format == expected.format &&
          legacy_image.semantic_format == expected.format && legacy_image.bytes == pattern.bytes &&
          legacy_copied.format == expected_storage && legacy_copied.data == legacy_image.bytes.data();
    std::printf("EXTERNAL_RECEIVE_LEGACY_INJECTED width=%u mono=%d raw=%d resolved=%d copy_error=%d %s\n",
        width, mono, (int)legacy_info.semantic_format, (int)resolved.semantic_format,
        (int)legacy_error, legacy_pass ? "PASS" : "FAIL");
    pass = pass && legacy_pass;

    if(!mono) {
        auto explicit_info = info;
        explicit_info.semantic_format = NOZZLE_FORMAT_RGBA8_UNORM;
        nozzle_tester::image_buffer explicit_image{};
        NozzleMappedPixels explicit_copied{};
        const auto explicit_error = nozzle_tester::copy_received_image(
            observer.read, explicit_info, explicit_image, explicit_copied);
        const bool explicit_pass = explicit_error == NOZZLE_OK &&
            explicit_image.format == expected.format &&
            explicit_image.semantic_format == nozzle_tester::tester_format::rgba8_unorm &&
            explicit_image.bytes == pattern.bytes && explicit_copied.format == expected_storage;
        std::printf("EXTERNAL_RECEIVE_EXPLICIT_INJECTED width=%u raw=%d resolved=%d copy_error=%d %s\n",
            width, (int)explicit_info.semantic_format, (int)resolved.semantic_format,
            (int)explicit_error, explicit_pass ? "PASS" : "FAIL");
        pass = pass && explicit_pass;
    }
    nozzle_frame_release(observer.read);
    observer.read = nullptr;
    // Run the real CLI even if the helper fails, so the negative CI proves both.
    const bool cli_correct = run_cli_receiver(cli, name, width, expected.format, mono ? 1 : 0);
    const bool cli_wrong = mono || run_cli_receiver(cli, name, width, nozzle_tester::tester_format::rgba16_float, 1);
    return pass && cli_correct && cli_wrong;
}
} // namespace

int main(int argc, char **argv) {
    if(argc != 2) {
        std::fprintf(stderr, "CLI executable path required\n");
        return 1;
    }
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
    for(uint32_t width : {65u, 641u}) {
        for(auto format : {NOZZLE_FORMAT_RGBA8_UNORM, NOZZLE_FORMAT_BGRA8_UNORM,
                           NOZZLE_FORMAT_RGBA16_FLOAT, NOZZLE_FORMAT_RGBA32_FLOAT}) {
            if(!run_verdict_case(format, width, argv[1])) ++failed;
        }
    }
    for(uint32_t width : {65u, 641u}) {
        if(!run_external_case(width, false, argv[1])) ++failed;
    }
    if(!run_external_case(65, true, argv[1])) ++failed;
    std::printf("GUI_RECEIVE_PEER failed=%d total=28\n", failed);
    return failed == 0 ? 0 : 1;
}
