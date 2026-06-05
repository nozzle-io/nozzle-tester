#include "core/evidence.hpp"
#include "core/pattern.hpp"

#include <nozzle/nozzle_c.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

struct cli_options {
    std::string command;
    std::string input_path;
    std::string output_path;
    std::string evidence_path;
    std::string sender_name{"nozzle_tester"};
    bool sender_name_provided{false};
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
    std::fprintf(stderr, "Usage: %s <pattern|verify|self-test|sender|receiver|discover> [options]\n", program);
    std::fprintf(stderr, "Common options:\n");
    std::fprintf(stderr, "  --width N --height N --format rgba8_unorm|bgra8_unorm|rgba16_float|rgba32_float\n");
    std::fprintf(stderr, "  --frame N / --expected-frame N\n");
    std::fprintf(stderr, "  --output PATH --input PATH --evidence PATH\n");
    std::fprintf(stderr, "Sender/receiver options:\n");
    std::fprintf(stderr, "  --name NAME --frames N --timeout-ms N --delay-ms N\n");
    std::fprintf(stderr, "Discovery options:\n");
    std::fprintf(stderr, "  discover [--name NAME] [--timeout-ms N]\n");
}

const char *nozzle_error_name(NozzleErrorCode error) {
    switch(error) {
        case NOZZLE_OK: return "NOZZLE_OK";
        case NOZZLE_ERROR_UNKNOWN: return "NOZZLE_ERROR_UNKNOWN";
        case NOZZLE_ERROR_INVALID_ARGUMENT: return "NOZZLE_ERROR_INVALID_ARGUMENT";
        case NOZZLE_ERROR_UNSUPPORTED_BACKEND: return "NOZZLE_ERROR_UNSUPPORTED_BACKEND";
        case NOZZLE_ERROR_UNSUPPORTED_FORMAT: return "NOZZLE_ERROR_UNSUPPORTED_FORMAT";
        case NOZZLE_ERROR_DEVICE_MISMATCH: return "NOZZLE_ERROR_DEVICE_MISMATCH";
        case NOZZLE_ERROR_RESOURCE_CREATION_FAILED: return "NOZZLE_ERROR_RESOURCE_CREATION_FAILED";
        case NOZZLE_ERROR_SHARED_HANDLE_FAILED: return "NOZZLE_ERROR_SHARED_HANDLE_FAILED";
        case NOZZLE_ERROR_SENDER_NOT_FOUND: return "NOZZLE_ERROR_SENDER_NOT_FOUND";
        case NOZZLE_ERROR_SENDER_CLOSED: return "NOZZLE_ERROR_SENDER_CLOSED";
        case NOZZLE_ERROR_TIMEOUT: return "NOZZLE_ERROR_TIMEOUT";
        case NOZZLE_ERROR_BACKEND_ERROR: return "NOZZLE_ERROR_BACKEND_ERROR";
        case NOZZLE_ERROR_COMMAND_FAILED: return "NOZZLE_ERROR_COMMAND_FAILED";
        default: return "NOZZLE_ERROR_UNRECOGNIZED";
    }
}

const char *backend_name(NozzleBackendType backend) {
    switch(backend) {
        case NOZZLE_BACKEND_D3D11: return "D3D11";
        case NOZZLE_BACKEND_METAL: return "Metal";
        case NOZZLE_BACKEND_OPENGL: return "OpenGL";
        case NOZZLE_BACKEND_DMA_BUF: return "DMA-BUF";
        case NOZZLE_BACKEND_UNKNOWN: return "Unknown";
        default: return "Unknown";
    }
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
            options.sender_name_provided = true;
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

bool from_nozzle_format(NozzleTextureFormat format, nozzle_tester::tester_format &out_format) {
    switch(format) {
        case NOZZLE_FORMAT_RGBA8_UNORM: out_format = nozzle_tester::tester_format::rgba8_unorm; return true;
        case NOZZLE_FORMAT_BGRA8_UNORM: out_format = nozzle_tester::tester_format::bgra8_unorm; return true;
        case NOZZLE_FORMAT_RGBA16_FLOAT: out_format = nozzle_tester::tester_format::rgba16_float; return true;
        case NOZZLE_FORMAT_RGBA32_FLOAT: out_format = nozzle_tester::tester_format::rgba32_float; return true;
        default: return false;
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
    record.native_texture_format = nozzle_tester::format_to_string(test.format);
    record.cpu_evidence_format = nozzle_tester::format_to_string(test.format);
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
    record.native_texture_format = nozzle_tester::format_to_string(options.format);
    record.cpu_evidence_format = nozzle_tester::format_to_string(options.format);
    record.artifact_paths.push_back(options.input_path);
    record.verification = nozzle_tester::verify_pattern(image, test);
    emit_evidence(record, options.evidence_path);
    return record.verification.result == nozzle_tester::verdict::pass ? 0 : 1;
}

int run_self_test(const cli_options &options) {
    struct case_result {
        std::string name;
        nozzle_tester::verify_result result;
        bool expected_pass{false};
    };
    std::vector<case_result> cases;
    const nozzle_tester::tester_format formats[] = {
        nozzle_tester::tester_format::rgba8_unorm,
        nozzle_tester::tester_format::bgra8_unorm,
        nozzle_tester::tester_format::rgba16_float,
        nozzle_tester::tester_format::rgba32_float,
    };
    nozzle_tester::test_case first_test{};
    first_test.id = "self-test";
    first_test.width = 641;
    first_test.height = 479;
    first_test.format = nozzle_tester::tester_format::rgba8_unorm;
    first_test.frame_index = 3;
    for(auto format : formats) {
        nozzle_tester::test_case test = first_test;
        test.format = format;
        const nozzle_tester::image_buffer good = nozzle_tester::generate_pattern(test);
        const std::string prefix = std::string(nozzle_tester::format_to_string(format)) + ":";
        cases.push_back({prefix + "good", nozzle_tester::verify_pattern(good, test), true});
        cases.push_back({prefix + "vertical_flip", nozzle_tester::verify_pattern(nozzle_tester::make_vertical_flip_fixture(good), test), false});
        cases.push_back({prefix + "rb_swap", nozzle_tester::verify_pattern(nozzle_tester::make_rb_swap_fixture(good), test), false});
        cases.push_back({prefix + "alpha_zero", nozzle_tester::verify_pattern(nozzle_tester::make_alpha_zero_fixture(good), test), false});
        nozzle_tester::test_case stale_test = test;
        stale_test.frame_index += 1;
        cases.push_back({prefix + "stale_previous", nozzle_tester::verify_pattern(good, stale_test), false});
    }

    bool ok = true;
    std::string reasons;
    std::vector<std::string> covered_failure_reasons;
    for(const auto &item : cases) {
        const bool passed = item.result.result == nozzle_tester::verdict::pass;
        if(passed != item.expected_pass) {
            ok = false;
            reasons += item.name + " did not produce expected verdict;";
        }
        for(const auto &reason : item.result.failure_reasons) {
            bool found = false;
            for(const auto &covered : covered_failure_reasons) {
                if(covered == reason) {
                    found = true;
                    break;
                }
            }
            if(!found) covered_failure_reasons.push_back(reason);
        }
    }

    nozzle_tester::evidence_record record{};
    record.role = "self-test";
    record.backend = "cpu";
    record.test = first_test;
    record.observed_width = first_test.width;
    record.observed_height = first_test.height;
    record.observed_frame_index = first_test.frame_index;
    record.observed_frame_count = (uint64_t)cases.size();
    record.changed_across_observations = true;
    record.covered_failure_reasons = covered_failure_reasons;
    if(!options.evidence_path.empty()) {
        record.artifact_paths.push_back(options.evidence_path);
        record.artifacts.push_back({"self_test_evidence", options.evidence_path});
    }
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
    record.native_texture_format = nozzle_tester::format_to_string(options.format);
    record.cpu_evidence_format = nozzle_tester::format_to_string(options.format);

    if(error != NOZZLE_OK || sender == nullptr) {
        record.verification.result = nozzle_tester::verdict::fail;
        record.verification.failure_reasons.push_back(std::string("sender_create_failed:") + nozzle_error_name(error));
        std::fprintf(stderr, "sender_create_failed: %s (%d)\n", nozzle_error_name(error), (int)error);
        emit_evidence(record, options.evidence_path);
        return 1;
    }

    uint32_t published = 0;
    for(uint32_t frame = 0; frame < options.frames; frame++) {
        NozzleFrame *writable = nullptr;
        error = nozzle_sender_acquire_writable_frame(sender, options.width, options.height, to_nozzle_format(options.format), &writable);
        if(error != NOZZLE_OK || writable == nullptr) {
            std::fprintf(stderr, "acquire_writable_frame_failed: %s (%d)\n", nozzle_error_name(error), (int)error);
            break;
        }

        NozzlePixelMapping *mapping = nullptr;
        NozzleMappedPixels pixels{};
        error = nozzle_frame_lock_writable_pixels_mapping_with_origin(writable, NOZZLE_ORIGIN_TOP_LEFT, &mapping, &pixels);
        if(error != NOZZLE_OK || mapping == nullptr || pixels.data == nullptr) {
            std::fprintf(stderr, "writable_mapping_failed: %s (%d)\n", nozzle_error_name(error), (int)error);
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
        if(error != NOZZLE_OK) {
            std::fprintf(stderr, "commit_frame_failed: %s (%d)\n", nozzle_error_name(error), (int)error);
            break;
        }
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
        (void)frame;
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
        nozzle_tester::tester_format observed_format{};
        if(!from_nozzle_format(info.format, observed_format)) {
            nozzle_frame_release(nozzle_frame);
            all_passed = false;
            record.verification.result = nozzle_tester::verdict::fail;
            record.verification.failure_reasons.push_back("unsupported_observed_format");
            break;
        }
        const uint32_t bpp = nozzle_tester::bytes_per_pixel(observed_format);
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
        image.format = observed_format;
        image.bytes = copied;
        nozzle_tester::test_case expected = record.test;
        expected.format = options.format;
        expected.frame_index = info.frame_index;
        const nozzle_tester::verify_result verify = nozzle_tester::verify_pattern(image, expected);
        if(verify.result != nozzle_tester::verdict::pass) {
            all_passed = false;
            record.verification = verify;
        }
        record.observed_width = info.width;
        record.observed_height = info.height;
        record.observed_frame_index = info.frame_index;
        record.native_texture_format = nozzle_tester::format_to_string(observed_format);
        record.cpu_evidence_format = nozzle_tester::format_to_string(observed_format);
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

bool sender_array_contains(const NozzleSenderInfoArray &array, const std::string &name) {
    for(uint32_t index = 0; index < array.count; index++) {
        const NozzleSenderInfo &info = array.items[index];
        if(info.name != nullptr && name == info.name) {
            return true;
        }
    }
    return false;
}

void print_sender_array(const NozzleSenderInfoArray &array) {
    std::printf("senders: %u\n", array.count);
    for(uint32_t index = 0; index < array.count; index++) {
        const NozzleSenderInfo &info = array.items[index];
        std::printf(
            "sender[%u] name=%s application=%s id=%s backend=%s\n",
            index,
            info.name != nullptr ? info.name : "",
            info.application_name != nullptr ? info.application_name : "",
            info.id != nullptr ? info.id : "",
            backend_name(info.backend));
    }
}

int run_discover(const cli_options &options) {
    const auto start = std::chrono::steady_clock::now();
    while(true) {
        NozzleSenderInfoArray array{};
        const NozzleErrorCode error = nozzle_enumerate_senders(&array);
        if(error != NOZZLE_OK) {
            std::fprintf(stderr, "enumerate_senders_failed: %s (%d)\n", nozzle_error_name(error), (int)error);
            return 1;
        }

        const bool found = !options.sender_name_provided || sender_array_contains(array, options.sender_name);
        if(found || options.timeout_ms == 0) {
            print_sender_array(array);
            nozzle_free_sender_info_array(&array);
            if(options.sender_name_provided && !found) {
                std::fprintf(stderr, "sender_not_found: %s\n", options.sender_name.c_str());
                return 1;
            }
            return 0;
        }

        nozzle_free_sender_info_array(&array);
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if((uint64_t)elapsed >= options.timeout_ms) {
            NozzleSenderInfoArray final_array{};
            const NozzleErrorCode final_error = nozzle_enumerate_senders(&final_array);
            if(final_error == NOZZLE_OK) {
                print_sender_array(final_array);
                nozzle_free_sender_info_array(&final_array);
            }
            std::fprintf(stderr, "sender_not_found: %s\n", options.sender_name.c_str());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
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
    if(options.command == "discover") return run_discover(options);

    std::fprintf(stderr, "unknown command: %s\n", options.command.c_str());
    print_usage(argv[0]);
    return 2;
}
