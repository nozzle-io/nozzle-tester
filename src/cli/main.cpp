#include "core/evidence.hpp"
#include "core/pattern.hpp"

#include <nozzle/nozzle_c.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

struct cli_options {
    std::string command;
    std::string input_path;
    std::string output_path;
    std::string evidence_path;
    std::string sender_name{"nozzle_tester"};
    uint32_t width{320};
    uint32_t height{240};
    uint32_t expected_width{320};
    uint32_t expected_height{240};
    nozzle_tester::tester_format format{nozzle_tester::tester_format::rgba8_unorm};
    uint64_t frame_index{0};
    uint32_t frames{2};
    uint32_t timeout_ms{2000};
    uint32_t delay_ms{16};
};

void print_usage(const char *program) {
    std::fprintf(stderr, "Usage: %s <pattern|verify|self-test|sender|receiver> [options]\n", program);
    std::fprintf(stderr, "Common options:\n");
    std::fprintf(stderr, "  --width N --height N --format rgba8_unorm|bgra8_unorm|rgba16_float|rgba32_float\n");
    std::fprintf(stderr, "  --frame N / --expected-frame N\n");
    std::fprintf(stderr, "  --output PATH --input PATH --evidence PATH\n");
    std::fprintf(stderr, "Sender/receiver options:\n");
    std::fprintf(stderr, "  --name NAME --frames N --timeout-ms N --delay-ms N\n");
}

bool parse_u32(const char *text, uint32_t &out_value) {
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if(end == text || *end != '\0' || 0xfffffffful < value) return false;
    out_value = (uint32_t)value;
    return true;
}

bool parse_u64(const char *text, uint64_t &out_value) {
    char *end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if(end == text || *end != '\0') return false;
    out_value = (uint64_t)value;
    return true;
}

bool parse_options(int argc, char **argv, cli_options &options) {
    if(argc < 2) return false;
    options.command = argv[1];
    options.expected_width = options.width;
    options.expected_height = options.height;
    for(int index = 2; index < argc; index++) {
        const char *arg = argv[index];
        auto require_value = [&](const char *name) -> const char * {
            if(index + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            return argv[++index];
        };
        if(std::strcmp(arg, "--width") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !parse_u32(value, options.width)) return false;
            options.expected_width = options.width;
        } else if(std::strcmp(arg, "--height") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !parse_u32(value, options.height)) return false;
            options.expected_height = options.height;
        } else if(std::strcmp(arg, "--expected-width") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !parse_u32(value, options.expected_width)) return false;
        } else if(std::strcmp(arg, "--expected-height") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !parse_u32(value, options.expected_height)) return false;
        } else if(std::strcmp(arg, "--format") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !nozzle_tester::parse_format(value, options.format)) return false;
        } else if(std::strcmp(arg, "--frame") == 0 || std::strcmp(arg, "--expected-frame") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !parse_u64(value, options.frame_index)) return false;
        } else if(std::strcmp(arg, "--frames") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !parse_u32(value, options.frames)) return false;
        } else if(std::strcmp(arg, "--timeout-ms") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !parse_u32(value, options.timeout_ms)) return false;
        } else if(std::strcmp(arg, "--delay-ms") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr || !parse_u32(value, options.delay_ms)) return false;
        } else if(std::strcmp(arg, "--input") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr) return false;
            options.input_path = value;
        } else if(std::strcmp(arg, "--output") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr) return false;
            options.output_path = value;
        } else if(std::strcmp(arg, "--evidence") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr) return false;
            options.evidence_path = value;
        } else if(std::strcmp(arg, "--name") == 0) {
            const char *value = require_value(arg);
            if(value == nullptr) return false;
            options.sender_name = value;
        } else if(std::strcmp(arg, "--help") == 0) {
            return false;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg);
            return false;
        }
    }
    return true;
}

NozzleTextureFormat to_nozzle_format(nozzle_tester::tester_format format) {
    switch(format) {
        case nozzle_tester::tester_format::rgba8_unorm: return NOZZLE_FORMAT_RGBA8_UNORM;
        case nozzle_tester::tester_format::bgra8_unorm: return NOZZLE_FORMAT_BGRA8_UNORM;
        case nozzle_tester::tester_format::rgba16_float: return NOZZLE_FORMAT_RGBA16_FLOAT;
        case nozzle_tester::tester_format::rgba32_float: return NOZZLE_FORMAT_RGBA32_FLOAT;
        default: return NOZZLE_FORMAT_UNKNOWN;
    }
}

void emit_evidence(const nozzle_tester::evidence_record &record, const std::string &path) {
    const std::string json = nozzle_tester::make_evidence_json(record);
    if(path.empty()) {
        std::printf("%s", json.c_str());
        return;
    }
    if(!nozzle_tester::write_text_file(path, json)) {
        std::fprintf(stderr, "failed to write evidence: %s\n", path.c_str());
    }
}

nozzle_tester::test_case make_test(const cli_options &options) {
    nozzle_tester::test_case test{};
    test.id = "default";
    test.width = options.width;
    test.height = options.height;
    test.format = options.format;
    test.frame_index = options.frame_index;
    return test;
}

int run_pattern(const cli_options &options) {
    const nozzle_tester::test_case test = make_test(options);
    const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);
    if(!options.output_path.empty() && !nozzle_tester::write_binary_file(options.output_path, image.bytes)) {
        std::fprintf(stderr, "failed to write output: %s\n", options.output_path.c_str());
        return 1;
    }

    nozzle_tester::evidence_record record{};
    record.role = "pattern";
    record.backend = "cpu";
    record.test = test;
    record.observed_width = image.width;
    record.observed_height = image.height;
    record.observed_frame_index = test.frame_index;
    record.observed_frame_count = 1;
    record.changed_across_observations = true;
    record.verification = nozzle_tester::verify_pattern(image, test);
    if(!options.output_path.empty()) record.artifact_paths.push_back(options.output_path);
    emit_evidence(record, options.evidence_path);
    return record.verification.result == nozzle_tester::verdict::pass ? 0 : 1;
}

int run_verify(const cli_options &options) {
    std::vector<uint8_t> bytes;
    if(options.input_path.empty() || !nozzle_tester::read_binary_file(options.input_path, bytes)) {
        std::fprintf(stderr, "failed to read input: %s\n", options.input_path.c_str());
        return 1;
    }
    nozzle_tester::image_buffer image{};
    image.width = options.expected_width;
    image.height = options.expected_height;
    image.format = options.format;
    image.bytes = bytes;

    nozzle_tester::test_case test{};
    test.id = "verify";
    test.width = options.expected_width;
    test.height = options.expected_height;
    test.format = options.format;
    test.frame_index = options.frame_index;

    nozzle_tester::evidence_record record{};
    record.role = "verify";
    record.backend = "cpu";
    record.test = test;
    record.observed_width = image.width;
    record.observed_height = image.height;
    record.observed_frame_index = options.frame_index;
    record.observed_frame_count = 1;
    record.changed_across_observations = true;
    record.artifact_paths.push_back(options.input_path);
    record.verification = nozzle_tester::verify_pattern(image, test);
    emit_evidence(record, options.evidence_path);
    return record.verification.result == nozzle_tester::verdict::pass ? 0 : 1;
}

int run_self_test(const cli_options &options) {
    nozzle_tester::test_case test{};
    test.id = "self-test";
    test.width = 641;
    test.height = 479;
    test.format = nozzle_tester::tester_format::rgba8_unorm;
    test.frame_index = 3;
    const nozzle_tester::image_buffer good = nozzle_tester::generate_pattern(test);

    struct case_result {
        std::string name;
        nozzle_tester::verify_result result;
        bool expected_pass{false};
    };
    std::vector<case_result> cases;
    cases.push_back({"good", nozzle_tester::verify_pattern(good, test), true});
    cases.push_back({"vertical_flip", nozzle_tester::verify_pattern(nozzle_tester::make_vertical_flip_fixture(good), test), false});
    cases.push_back({"rb_swap", nozzle_tester::verify_pattern(nozzle_tester::make_rb_swap_fixture(good), test), false});
    cases.push_back({"alpha_zero", nozzle_tester::verify_pattern(nozzle_tester::make_alpha_zero_fixture(good), test), false});
    nozzle_tester::test_case stale_test = test;
    stale_test.frame_index = 4;
    cases.push_back({"stale_previous", nozzle_tester::verify_pattern(good, stale_test), false});

    bool ok = true;
    std::string reasons;
    for(const auto &item : cases) {
        const bool passed = item.result.result == nozzle_tester::verdict::pass;
        if(passed != item.expected_pass) {
            ok = false;
            reasons += item.name + " did not produce expected verdict;";
        }
    }

    nozzle_tester::evidence_record record{};
    record.role = "self-test";
    record.backend = "cpu";
    record.test = test;
    record.observed_width = test.width;
    record.observed_height = test.height;
    record.observed_frame_index = test.frame_index;
    record.observed_frame_count = (uint64_t)cases.size();
    record.changed_across_observations = true;
    record.verification = cases.front().result;
    if(!ok) {
        record.verification.result = nozzle_tester::verdict::fail;
        record.verification.failure_reasons.push_back(reasons.empty() ? "self_test_failure" : reasons);
    }
    emit_evidence(record, options.evidence_path);
    return ok ? 0 : 1;
}

int run_sender(const cli_options &options) {
    NozzleSenderDesc desc{};
    desc.name = options.sender_name.c_str();
    desc.application_name = "nozzle-tester";
    desc.ring_buffer_size = 3;

    NozzleSender *sender = nullptr;
    NozzleErrorCode error = nozzle_sender_create(&desc, &sender);
    nozzle_tester::evidence_record record{};
    record.role = "sender";
    record.backend = "auto";
    record.sender_name = options.sender_name;
    record.test = make_test(options);
    record.observed_width = options.width;
    record.observed_height = options.height;

    if(error != NOZZLE_OK || sender == nullptr) {
        record.verification.result = nozzle_tester::verdict::fail;
        record.verification.failure_reasons.push_back("sender_create_failed");
        emit_evidence(record, options.evidence_path);
        return 1;
    }

    uint32_t published = 0;
    for(uint32_t frame = 0; frame < options.frames; frame++) {
        NozzleFrame *writable = nullptr;
        error = nozzle_sender_acquire_writable_frame(sender, options.width, options.height, to_nozzle_format(options.format), &writable);
        if(error != NOZZLE_OK || writable == nullptr) break;

        NozzlePixelMapping *mapping = nullptr;
        NozzleMappedPixels pixels{};
        error = nozzle_frame_lock_writable_pixels_mapping_with_origin(writable, NOZZLE_ORIGIN_TOP_LEFT, &mapping, &pixels);
        if(error != NOZZLE_OK || mapping == nullptr || pixels.data == nullptr) {
            nozzle_sender_discard_frame(sender, writable);
            nozzle_frame_release(writable);
            break;
        }

        nozzle_tester::test_case frame_test = make_test(options);
        frame_test.frame_index = frame;
        const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(frame_test);
        const uint32_t row_bytes = options.width * nozzle_tester::bytes_per_pixel(options.format);
        uint8_t *target = (uint8_t*)pixels.data;
        for(uint32_t y = 0; y < options.height; y++) {
            std::memcpy(target + (int64_t)y * pixels.row_stride_bytes, image.bytes.data() + (size_t)y * row_bytes, row_bytes);
        }

        nozzle_pixel_mapping_unlock(&mapping);
        error = nozzle_sender_commit_frame(sender, writable);
        nozzle_frame_release(writable);
        if(error != NOZZLE_OK) break;
        published += 1;
        if(0 < options.delay_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.delay_ms));
        }
    }

    nozzle_sender_destroy(sender);
    record.observed_frame_count = published;
    record.observed_frame_index = published == 0 ? 0 : published - 1u;
    record.changed_across_observations = 1 < published;
    record.verification.result = published == options.frames ? nozzle_tester::verdict::pass : nozzle_tester::verdict::fail;
    if(record.verification.result != nozzle_tester::verdict::pass) {
        record.verification.failure_reasons.push_back("publish_frame_failed");
    }
    record.verification.dimensions_ok = true;
    record.verification.orientation_ok = true;
    record.verification.channel_order_ok = true;
    record.verification.alpha_ok = true;
    record.verification.stale_frame_ok = 1 < published;
    emit_evidence(record, options.evidence_path);
    return record.verification.result == nozzle_tester::verdict::pass ? 0 : 1;
}

int run_receiver(const cli_options &options) {
    NozzleReceiverDesc desc{};
    desc.name = options.sender_name.c_str();
    desc.application_name = "nozzle-tester";
    desc.receive_mode = NOZZLE_RECEIVE_SEQUENTIAL_BEST_EFFORT;

    NozzleReceiver *receiver = nullptr;
    NozzleErrorCode error = nozzle_receiver_create(&desc, &receiver);
    nozzle_tester::evidence_record record{};
    record.role = "receiver";
    record.backend = "auto";
    record.sender_name = options.sender_name;
    record.receiver_name = "nozzle-tester";
    record.test = make_test(options);
    record.test.width = options.expected_width;
    record.test.height = options.expected_height;

    if(error != NOZZLE_OK || receiver == nullptr) {
        record.verification.result = nozzle_tester::verdict::fail;
        record.verification.failure_reasons.push_back("receiver_create_failed");
        emit_evidence(record, options.evidence_path);
        return 1;
    }

    uint32_t observed = 0;
    bool all_passed = true;
    for(uint32_t frame = 0; frame < options.frames; frame++) {
        NozzleAcquireDesc acquire_desc{};
        acquire_desc.timeout_ms = options.timeout_ms;
        NozzleFrame *nozzle_frame = nullptr;
        error = nozzle_receiver_acquire_frame(receiver, &acquire_desc, &nozzle_frame);
        if(error != NOZZLE_OK || nozzle_frame == nullptr) {
            all_passed = false;
            break;
        }
        NozzleFrameInfo info{};
        error = nozzle_frame_get_info(nozzle_frame, &info);
        if(error != NOZZLE_OK) {
            nozzle_frame_release(nozzle_frame);
            all_passed = false;
            break;
        }
        const uint32_t bpp = nozzle_tester::bytes_per_pixel(options.format);
        std::vector<uint8_t> copied((size_t)info.width * info.height * bpp);
        NozzleMappedPixels copied_pixels{};
        error = nozzle_frame_copy_pixels_with_origin(nozzle_frame, NOZZLE_ORIGIN_TOP_LEFT, copied.data(), copied.size(), &copied_pixels);
        nozzle_frame_release(nozzle_frame);
        if(error != NOZZLE_OK) {
            all_passed = false;
            break;
        }

        nozzle_tester::image_buffer image{};
        image.width = info.width;
        image.height = info.height;
        image.format = options.format;
        image.bytes = copied;
        nozzle_tester::test_case expected = record.test;
        expected.frame_index = frame;
        const nozzle_tester::verify_result verify = nozzle_tester::verify_pattern(image, expected);
        if(verify.result != nozzle_tester::verdict::pass) {
            all_passed = false;
            record.verification = verify;
        }
        record.observed_width = info.width;
        record.observed_height = info.height;
        record.observed_frame_index = info.frame_index;
        observed += 1;
    }

    nozzle_receiver_destroy(receiver);
    record.observed_frame_count = observed;
    record.changed_across_observations = 1 < observed;
    if(observed == 0) {
        record.verification.result = nozzle_tester::verdict::fail;
        record.verification.failure_reasons.push_back("missing_frame");
    } else if(all_passed) {
        record.verification.result = nozzle_tester::verdict::pass;
        record.verification.dimensions_ok = true;
        record.verification.orientation_ok = true;
        record.verification.channel_order_ok = true;
        record.verification.alpha_ok = true;
        record.verification.stale_frame_ok = 1 < observed;
    }
    emit_evidence(record, options.evidence_path);
    return record.verification.result == nozzle_tester::verdict::pass ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    cli_options options{};
    if(!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }

    if(options.command == "pattern") return run_pattern(options);
    if(options.command == "verify") return run_verify(options);
    if(options.command == "self-test") return run_self_test(options);
    if(options.command == "sender") return run_sender(options);
    if(options.command == "receiver") return run_receiver(options);

    std::fprintf(stderr, "unknown command: %s\n", options.command.c_str());
    print_usage(argv[0]);
    return 2;
}
