#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nozzle_tester {

enum class tester_format {
    rgba8_unorm,
    bgra8_unorm,
    rgba16_float,
    rgba32_float,
};

enum class verdict {
    pass,
    fail,
    skip,
    inconclusive,
};

struct rgba_float {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};
};

struct test_case {
    std::string id{"default"};
    uint32_t width{320};
    uint32_t height{240};
    tester_format format{tester_format::rgba8_unorm};
    uint64_t frame_index{0};
};

struct image_buffer {
    uint32_t width{0};
    uint32_t height{0};
    tester_format format{tester_format::rgba8_unorm};
    std::vector<uint8_t> bytes;
};

struct verify_result {
    verdict result{verdict::inconclusive};
    std::vector<std::string> failure_reasons;
    uint64_t mismatch_count{0};
    uint64_t flipped_mismatch_count{0};
    uint64_t rb_swapped_mismatch_count{0};
    uint64_t stale_previous_mismatch_count{0};
    bool dimensions_ok{false};
    bool orientation_ok{false};
    bool channel_order_ok{false};
    bool alpha_ok{false};
    bool stale_frame_ok{false};
};

const char *format_to_string(tester_format format);
bool parse_format(const std::string &text, tester_format &out_format);
const char *verdict_to_string(verdict value);
uint32_t bytes_per_pixel(tester_format format);
uint64_t expected_byte_size(uint32_t width, uint32_t height, tester_format format);
std::string make_case_id(const test_case &test);

rgba_float expected_rgba(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint64_t frame_index);
image_buffer generate_pattern(const test_case &test);
std::vector<uint8_t> image_to_rgba8_preview(const image_buffer &image);
verify_result verify_pattern(const image_buffer &observed, const test_case &expected);

image_buffer make_vertical_flip_fixture(const image_buffer &source);
image_buffer make_rb_swap_fixture(const image_buffer &source);
image_buffer make_alpha_zero_fixture(const image_buffer &source);

bool write_binary_file(const std::string &path, const std::vector<uint8_t> &bytes);
bool read_binary_file(const std::string &path, std::vector<uint8_t> &out_bytes);

} // namespace nozzle_tester
