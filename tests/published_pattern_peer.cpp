#include "core/received_image.hpp"
#include "gui/pattern_publish.hpp"
#include <cstdio>
#include <string>
#include <unistd.h>

namespace {
struct session {
    NozzleSender *sender{};
    NozzleReceiver *receiver{};
    NozzleFrame *read{};
    NozzleFrame *write{};
    NozzlePixelMapping *mapping{};
    ~session() {
        nozzle_pixel_mapping_unlock(&mapping);
        if(read) nozzle_frame_release(read);
        if(write) {
            (void)nozzle_sender_discard_frame(sender, write);
            nozzle_frame_release(write);
        }
        if(receiver) nozzle_receiver_destroy(receiver);
        if(sender) nozzle_sender_destroy(sender);
    }
};

bool check(NozzleErrorCode error, const char *stage) {
    if(error == NOZZLE_OK) return true;
    std::fprintf(stderr, "PUBLISHED_PATTERN %s error=%d\n", stage, (int)error);
    return false;
}

bool precommit_controls(session &s, const nozzle_tester::test_case &test) {
    const auto format = nozzle_tester::gui_image_format(test.format);
    const auto acquire_error = nozzle_sender_acquire_writable_frame(s.sender, test.width, test.height,
        NOZZLE_FORMAT_UNKNOWN, &s.write);
    if(acquire_error == NOZZLE_OK || s.write != nullptr) return false;
    if(!check(nozzle_sender_acquire_writable_frame(s.sender, test.width, test.height, format, &s.write), "discard acquire") ||
       !check(nozzle_sender_discard_frame(s.sender, s.write), "discard")) return false;
    nozzle_frame_release(s.write);
    s.write = nullptr;
    if(!check(nozzle_sender_acquire_writable_frame(s.sender, test.width, test.height, format, &s.write), "rejected commit acquire")) return false;
    NozzleMappedPixels pixels{};
    if(!check(nozzle_frame_lock_writable_pixels_mapping_with_origin(s.write, NOZZLE_ORIGIN_TOP_LEFT,
        &s.mapping, &pixels), "rejected commit map")) return false;
    const auto commit_error = nozzle_sender_commit_frame(s.sender, s.write);
    if(commit_error != NOZZLE_ERROR_INVALID_ARGUMENT) return false;
    if(!check(nozzle_pixel_mapping_unlock_checked(&s.mapping), "rejected commit unlock") ||
       !check(nozzle_sender_discard_frame(s.sender, s.write), "rejected commit discard")) return false;
    nozzle_frame_release(s.write);
    s.write = nullptr;
    std::printf("PUBLISHED_PATTERN_PRECOMMIT acquire_error=%d active_mapping_commit_error=%d discard=PASS\n",
        (int)acquire_error, (int)commit_error);
    return true;
}

bool run_session(const std::string &name, nozzle_tester::tester_format format, uint32_t width, int restart) {
    session s;
    NozzleSenderDesc desc{};
    desc.name = name.c_str();
    desc.ring_buffer_size = 3;
    desc.fallback_flags_valid = 1;
    desc.fallback_flags = NOZZLE_FALLBACK_SAFE_DEFAULTS;
    if(!check(nozzle_sender_create(&desc, &s.sender), "sender create")) return false;
    NozzleSenderInfoSnapshot sender_info{};
    if(!check(nozzle_sender_get_info_snapshot(s.sender, &sender_info), "sender info") ||
       sender_info.backend != NOZZLE_BACKEND_METAL) return false;
    nozzle_tester::test_case base{};
    base.width = width;
    base.height = 33;
    base.format = format;
    if(!precommit_controls(s, base)) return false;
    bool pass = true;
    for(uint64_t count = 0; count < 2; ++count) {
        const auto published = nozzle_tester::publish_gui_pattern(s.sender, base, count);
        if(published.failed_stage != nullptr) {
            std::fprintf(stderr, "GUI publication failed at %s error=%d\n", published.failed_stage, (int)published.error);
            return false;
        }
        if(!s.receiver) {
            NozzleReceiverDesc receiver_desc{};
            receiver_desc.name = name.c_str();
            if(!check(nozzle_receiver_create(&receiver_desc, &s.receiver), "receiver create")) return false;
        }
        NozzleAcquireDesc acquire{3000};
        NozzleFrameInfo info{};
        NozzleMappedPixels copied{};
        nozzle_tester::image_buffer image{};
        if(!check(nozzle_receiver_acquire_frame(s.receiver, &acquire, &s.read), "receive") ||
           !check(nozzle_frame_get_info(s.read, &info), "info") ||
           !check(nozzle_tester::copy_received_image(s.read, info, image, copied), "copy")) return false;
        auto expected = base;
        expected.frame_index = info.frame_index;
        const auto result = nozzle_tester::verify_pattern(image, expected);
        expected.frame_index += 1;
        const auto wrong_index = nozzle_tester::verify_pattern(image, expected);
        bool stale_reason = false;
        for(const auto &reason : wrong_index.failure_reasons) {
            if(reason == "stale_frame") stale_reason = true;
        }
        const bool frame_pass = published.test.frame_index == info.frame_index && info.frame_index == count + 1 &&
            result.result == nozzle_tester::verdict::pass && wrong_index.result == nozzle_tester::verdict::fail && stale_reason;
        std::printf("PUBLISHED_PATTERN format=%s width=%u restart=%d payload=%llu metadata=%llu verdict=%s wrong_index=%s reasons=",
            nozzle_tester::format_to_string(format), width, restart, (unsigned long long)published.test.frame_index,
            (unsigned long long)info.frame_index, nozzle_tester::verdict_to_string(result.result),
            nozzle_tester::verdict_to_string(wrong_index.result));
        for(const auto &reason : result.failure_reasons) std::printf("%s,", reason.c_str());
        std::printf("\n");
        pass = pass && frame_pass;
        nozzle_frame_release(s.read);
        s.read = nullptr;
    }
    return pass;
}
} // namespace

int main() {
    int failures = 0;
    for(uint32_t width : {65u, 641u}) {
        for(auto format : {nozzle_tester::tester_format::rgba8_unorm, nozzle_tester::tester_format::bgra8_unorm,
            nozzle_tester::tester_format::rgba16_float, nozzle_tester::tester_format::rgba32_float}) {
            const std::string name = "p233-" + std::to_string(getpid()) + "-" + std::to_string(width)
                + "-" + nozzle_tester::format_to_string(format);
            for(int restart = 0; restart < 2; ++restart) {
                if(!run_session(name, format, width, restart)) ++failures;
            }
        }
    }
    std::printf("PUBLISHED_PATTERN_PEER failed=%d total=16\n", failures);
    return failures == 0 ? 0 : 1;
}
