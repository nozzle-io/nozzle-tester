#include "core/pattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace nozzle_tester {

namespace {

uint8_t clamp_to_u8(float value) {
    if(value <= 0.0f) return 0;
    if(1.0f <= value) return 255;
    return (uint8_t)std::lround(value * 255.0f);
}

float clamp01(float value) {
    if(value < 0.0f) return 0.0f;
    if(1.0f < value) return 1.0f;
    return value;
}

uint16_t float_to_half(float value) {
    value = clamp01(value);
    uint32_t bits{0};
    std::memcpy(&bits, &value, sizeof(bits));
    uint32_t sign{(bits >> 16) & 0x8000u};
    int32_t exponent{(int32_t)((bits >> 23) & 0xffu) - 127 + 15};
    uint32_t mantissa{bits & 0x7fffffu};
    if(exponent <= 0) {
        if(exponent < -10) return (uint16_t)sign;
        mantissa = (mantissa | 0x800000u) >> (uint32_t)(1 - exponent);
        return (uint16_t)(sign | ((mantissa + 0x1000u) >> 13));
    }
    if(31 <= exponent) {
        return (uint16_t)(sign | 0x7c00u);
    }
    uint32_t half_mantissa = (mantissa + 0x1000u) >> 13;
    if(half_mantissa == 0x400u) {
        half_mantissa = 0;
        exponent += 1;
        if(31 <= exponent) {
            return (uint16_t)(sign | 0x7c00u);
        }
    }
    return (uint16_t)(sign | ((uint32_t)exponent << 10) | half_mantissa);
}

float half_to_float(uint16_t half) {
    const uint32_t sign{(uint32_t)(half & 0x8000u) << 16};
    int32_t exponent{(int32_t)((half >> 10) & 0x1fu)};
    uint32_t mantissa{(uint32_t)half & 0x03ffu};
    uint32_t bits{0};
    if(exponent == 0) {
        if(mantissa == 0) {
            bits = sign;
        } else {
            exponent = 1;
            while((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                exponent -= 1;
            }
            mantissa &= 0x03ffu;
            bits = sign | ((uint32_t)(exponent + 127 - 15) << 23) | (mantissa << 13);
        }
    } else if(exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((uint32_t)(exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float value{0.0f};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool in_rect(uint32_t x, uint32_t y, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom) {
    return left <= x && x < right && top <= y && y < bottom;
}

uint32_t marker_size(uint32_t width, uint32_t height) {
    uint32_t minimum = std::min(width, height);
    if(minimum == 0) return 0;
    uint32_t size = std::max<uint32_t>(1, minimum / 8);
    return std::min<uint32_t>(size, 24);
}

void encode_pixel(std::vector<uint8_t> &bytes, size_t offset, tester_format format, const rgba_float &rgba) {
    if(format == tester_format::rgba8_unorm) {
        bytes[offset + 0] = clamp_to_u8(rgba.r);
        bytes[offset + 1] = clamp_to_u8(rgba.g);
        bytes[offset + 2] = clamp_to_u8(rgba.b);
        bytes[offset + 3] = clamp_to_u8(rgba.a);
    } else if(format == tester_format::bgra8_unorm) {
        bytes[offset + 0] = clamp_to_u8(rgba.b);
        bytes[offset + 1] = clamp_to_u8(rgba.g);
        bytes[offset + 2] = clamp_to_u8(rgba.r);
        bytes[offset + 3] = clamp_to_u8(rgba.a);
    } else if(format == tester_format::rgba16_float) {
        uint16_t values[4] = {
            float_to_half(rgba.r),
            float_to_half(rgba.g),
            float_to_half(rgba.b),
            float_to_half(rgba.a),
        };
        std::memcpy(bytes.data() + offset, values, sizeof(values));
    } else {
        float values[4] = {clamp01(rgba.r), clamp01(rgba.g), clamp01(rgba.b), clamp01(rgba.a)};
        std::memcpy(bytes.data() + offset, values, sizeof(values));
    }
}

rgba_float decode_pixel(const std::vector<uint8_t> &bytes, size_t offset, tester_format format) {
    rgba_float result{};
    if(format == tester_format::rgba8_unorm) {
        result.r = bytes[offset + 0] / 255.0f;
        result.g = bytes[offset + 1] / 255.0f;
        result.b = bytes[offset + 2] / 255.0f;
        result.a = bytes[offset + 3] / 255.0f;
    } else if(format == tester_format::bgra8_unorm) {
        result.b = bytes[offset + 0] / 255.0f;
        result.g = bytes[offset + 1] / 255.0f;
        result.r = bytes[offset + 2] / 255.0f;
        result.a = bytes[offset + 3] / 255.0f;
    } else if(format == tester_format::rgba16_float) {
        uint16_t values[4]{};
        std::memcpy(values, bytes.data() + offset, sizeof(values));
        result.r = half_to_float(values[0]);
        result.g = half_to_float(values[1]);
        result.b = half_to_float(values[2]);
        result.a = half_to_float(values[3]);
    } else {
        float values[4]{};
        std::memcpy(values, bytes.data() + offset, sizeof(values));
        result.r = values[0];
        result.g = values[1];
        result.b = values[2];
        result.a = values[3];
    }
    return result;
}

bool close_enough(const rgba_float &a, const rgba_float &b, tester_format format, bool include_alpha) {
    const float tolerance = format == tester_format::rgba8_unorm || format == tester_format::bgra8_unorm ? 1.5f / 255.0f : (format == tester_format::rgba16_float ? 0.02f : 0.01f);
    if(tolerance < std::fabs(a.r - b.r)) return false;
    if(tolerance < std::fabs(a.g - b.g)) return false;
    if(tolerance < std::fabs(a.b - b.b)) return false;
    if(include_alpha && tolerance < std::fabs(a.a - b.a)) return false;
    return true;
}

uint64_t count_mismatches(const image_buffer &observed, const test_case &expected, bool flipped, bool rb_swapped, bool include_alpha) {
    const uint32_t bpp = bytes_per_pixel(observed.format);
    uint64_t mismatches{0};
    for(uint32_t y = 0; y < observed.height; y++) {
        for(uint32_t x = 0; x < observed.width; x++) {
            const size_t offset = ((size_t)y * observed.width + x) * bpp;
            rgba_float got = decode_pixel(observed.bytes, offset, observed.format);
            if(rb_swapped) {
                std::swap(got.r, got.b);
            }
            const uint32_t expected_y = flipped ? (observed.height - 1u - y) : y;
            rgba_float want = expected_rgba(x, expected_y, expected.width, expected.height, expected.frame_index);
            if(!close_enough(got, want, expected.format, include_alpha)) {
                mismatches += 1;
            }
        }
    }
    return mismatches;
}

image_buffer mutate_pixels(const image_buffer &source, rgba_float (*mutation)(rgba_float)) {
    image_buffer result = source;
    const uint32_t bpp = bytes_per_pixel(source.format);
    for(uint32_t y = 0; y < source.height; y++) {
        for(uint32_t x = 0; x < source.width; x++) {
            const size_t offset = ((size_t)y * source.width + x) * bpp;
            const rgba_float original = decode_pixel(source.bytes, offset, source.format);
            encode_pixel(result.bytes, offset, source.format, mutation(original));
        }
    }
    return result;
}

} // namespace

const char *format_to_string(tester_format format) {
    switch(format) {
        case tester_format::rgba8_unorm: return "rgba8_unorm";
        case tester_format::bgra8_unorm: return "bgra8_unorm";
        case tester_format::rgba16_float: return "rgba16_float";
        case tester_format::rgba32_float: return "rgba32_float";
        default: return "unknown";
    }
}

bool parse_format(const std::string &text, tester_format &out_format) {
    if(text == "rgba8_unorm") { out_format = tester_format::rgba8_unorm; return true; }
    if(text == "bgra8_unorm") { out_format = tester_format::bgra8_unorm; return true; }
    if(text == "rgba16_float") { out_format = tester_format::rgba16_float; return true; }
    if(text == "rgba32_float") { out_format = tester_format::rgba32_float; return true; }
    return false;
}

const char *verdict_to_string(verdict value) {
    switch(value) {
        case verdict::pass: return "PASS";
        case verdict::fail: return "FAIL";
        case verdict::skip: return "SKIP";
        case verdict::inconclusive: return "INCONCLUSIVE";
        default: return "INCONCLUSIVE";
    }
}

uint32_t bytes_per_pixel(tester_format format) {
    switch(format) {
        case tester_format::rgba8_unorm: return 4;
        case tester_format::bgra8_unorm: return 4;
        case tester_format::rgba16_float: return 8;
        case tester_format::rgba32_float: return 16;
        default: return 0;
    }
}

uint64_t expected_byte_size(uint32_t width, uint32_t height, tester_format format) {
    return (uint64_t)width * height * bytes_per_pixel(format);
}

std::string make_case_id(const test_case &test) {
    return test.id + "/" + std::to_string(test.width) + "x" + std::to_string(test.height) + "/" + format_to_string(test.format) + "/frame" + std::to_string(test.frame_index);
}

rgba_float expected_rgba(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint64_t frame_index) {
    if(width == 0 || height == 0) return {};

    const float fx = width <= 1 ? 0.0f : (float)x / (float)(width - 1u);
    const float fy = height <= 1 ? 0.0f : (float)y / (float)(height - 1u);
    rgba_float result{};
    result.r = std::fmod(0.11f + fx * 0.53f + (float)(frame_index % 17u) * 0.031f, 1.0f);
    result.g = std::fmod(0.17f + fy * 0.61f + (float)((x + frame_index) % 13u) * 0.019f, 1.0f);
    result.b = std::fmod(0.23f + (1.0f - fx) * 0.37f + (float)((y + frame_index) % 11u) * 0.023f, 1.0f);
    result.a = 1.0f;

    const uint32_t size = marker_size(width, height);
    if(size != 0 && in_rect(x, y, 0, 0, size, size)) {
        return {1.0f, 0.05f, 0.05f, 1.0f};
    }
    if(size != 0 && in_rect(x, y, width - size, 0, width, size)) {
        return {0.05f, 1.0f, 0.05f, 1.0f};
    }
    if(size != 0 && in_rect(x, y, 0, height - size, size, height)) {
        return {0.05f, 0.05f, 1.0f, 1.0f};
    }
    if(size != 0 && in_rect(x, y, width - size, height - size, width, height)) {
        return {1.0f, 1.0f, 0.05f, 1.0f};
    }

    const uint32_t red_left = width / 5u;
    const uint32_t red_right = std::min(width, red_left + std::max<uint32_t>(1, width / 12u));
    if(in_rect(x, y, red_left, 0, red_right, height)) {
        result.r = 1.0f;
        result.g *= 0.15f;
        result.b *= 0.15f;
    }

    const uint32_t blue_top = (height * 3u) / 5u;
    const uint32_t blue_bottom = std::min(height, blue_top + std::max<uint32_t>(1, height / 12u));
    if(in_rect(x, y, 0, blue_top, width, blue_bottom)) {
        result.r *= 0.15f;
        result.g *= 0.15f;
        result.b = 1.0f;
    }

    const uint32_t alpha_left = width / 2u;
    const uint32_t alpha_top = height / 3u;
    const uint32_t alpha_right = std::min(width, alpha_left + std::max<uint32_t>(1, width / 5u));
    const uint32_t alpha_bottom = std::min(height, alpha_top + std::max<uint32_t>(1, height / 5u));
    if(in_rect(x, y, alpha_left, alpha_top, alpha_right, alpha_bottom)) {
        result.r = 1.0f;
        result.g = 0.0f;
        result.b = 1.0f;
        result.a = 0.35f;
    }

    const uint32_t moving_size = std::max<uint32_t>(1, std::min(width, height) / 16u);
    const uint32_t travel_width = width <= moving_size ? 1u : width - moving_size;
    const uint32_t travel_height = height <= moving_size ? 1u : height - moving_size;
    const uint32_t moving_x = travel_width == 1u ? 0u : (uint32_t)((frame_index * 7u) % travel_width);
    const uint32_t moving_y = travel_height == 1u ? 0u : (uint32_t)((frame_index * 5u) % travel_height);
    if(in_rect(x, y, moving_x, moving_y, std::min(width, moving_x + moving_size), std::min(height, moving_y + moving_size))) {
        result.r = 1.0f;
        result.g = 1.0f;
        result.b = 1.0f;
        result.a = 1.0f;
    }

    return result;
}

image_buffer generate_pattern(const test_case &test) {
    image_buffer image{};
    image.width = test.width;
    image.height = test.height;
    image.format = test.format;
    const uint32_t bpp = bytes_per_pixel(test.format);
    image.bytes.resize((size_t)test.width * test.height * bpp);
    for(uint32_t y = 0; y < test.height; y++) {
        for(uint32_t x = 0; x < test.width; x++) {
            const size_t offset = ((size_t)y * test.width + x) * bpp;
            encode_pixel(image.bytes, offset, test.format, expected_rgba(x, y, test.width, test.height, test.frame_index));
        }
    }
    return image;
}

std::vector<uint8_t> image_to_rgba8_preview(const image_buffer &image) {
    std::vector<uint8_t> preview((size_t)image.width * image.height * 4u);
    const uint32_t source_bpp = bytes_per_pixel(image.format);
    for(uint32_t y = 0; y < image.height; y++) {
        for(uint32_t x = 0; x < image.width; x++) {
            const size_t source_offset = ((size_t)y * image.width + x) * source_bpp;
            const size_t target_offset = ((size_t)y * image.width + x) * 4u;
            const rgba_float rgba = decode_pixel(image.bytes, source_offset, image.format);
            preview[target_offset + 0] = clamp_to_u8(rgba.r);
            preview[target_offset + 1] = clamp_to_u8(rgba.g);
            preview[target_offset + 2] = clamp_to_u8(rgba.b);
            preview[target_offset + 3] = clamp_to_u8(rgba.a);
        }
    }
    return preview;
}

verify_result verify_pattern(const image_buffer &observed, const test_case &expected) {
    verify_result result{};
    result.dimensions_ok = observed.width == expected.width && observed.height == expected.height && observed.format == expected.format;
    if(!result.dimensions_ok) {
        result.result = verdict::fail;
        result.failure_reasons.push_back("dimension_mismatch");
        return result;
    }

    const uint64_t expected_size = expected_byte_size(observed.width, observed.height, observed.format);
    if((uint64_t)observed.bytes.size() != expected_size) {
        result.result = verdict::fail;
        result.failure_reasons.push_back("byte_size_mismatch");
        return result;
    }

    const uint64_t pixel_count = (uint64_t)observed.width * observed.height;
    result.mismatch_count = count_mismatches(observed, expected, false, false, true);
    result.flipped_mismatch_count = count_mismatches(observed, expected, true, false, true);
    result.rb_swapped_mismatch_count = count_mismatches(observed, expected, false, true, true);

    test_case previous = expected;
    if(0 < previous.frame_index) {
        previous.frame_index -= 1;
        result.stale_previous_mismatch_count = count_mismatches(observed, previous, false, false, true);
    } else {
        result.stale_previous_mismatch_count = std::numeric_limits<uint64_t>::max();
    }

    result.orientation_ok = result.mismatch_count <= result.flipped_mismatch_count;
    result.channel_order_ok = result.mismatch_count <= result.rb_swapped_mismatch_count;
    const uint64_t alpha_mismatches = count_mismatches(observed, expected, false, false, true) - count_mismatches(observed, expected, false, false, false);
    result.alpha_ok = alpha_mismatches == 0;
    result.stale_frame_ok = result.mismatch_count <= result.stale_previous_mismatch_count;

    if(result.mismatch_count == 0) {
        result.result = verdict::pass;
        return result;
    }

    result.result = verdict::fail;
    if(result.flipped_mismatch_count < result.mismatch_count && result.flipped_mismatch_count < pixel_count / 4u + 1u) {
        result.failure_reasons.push_back("vertical_flip");
    }
    if(result.rb_swapped_mismatch_count < result.mismatch_count && result.rb_swapped_mismatch_count < pixel_count / 4u + 1u) {
        result.failure_reasons.push_back("rb_swap");
    }
    if(!result.alpha_ok) {
        result.failure_reasons.push_back("alpha_mismatch");
    }
    if(result.stale_previous_mismatch_count < result.mismatch_count) {
        result.failure_reasons.push_back("stale_frame");
    }
    if(result.failure_reasons.empty()) {
        result.failure_reasons.push_back("pixel_mismatch");
    }
    return result;
}

image_buffer make_vertical_flip_fixture(const image_buffer &source) {
    image_buffer result = source;
    const uint32_t bpp = bytes_per_pixel(source.format);
    for(uint32_t y = 0; y < source.height; y++) {
        const uint32_t source_y = source.height - 1u - y;
        std::memcpy(result.bytes.data() + (size_t)y * source.width * bpp, source.bytes.data() + (size_t)source_y * source.width * bpp, (size_t)source.width * bpp);
    }
    return result;
}

image_buffer make_rb_swap_fixture(const image_buffer &source) {
    return mutate_pixels(source, [](rgba_float value) {
        std::swap(value.r, value.b);
        return value;
    });
}

image_buffer make_alpha_zero_fixture(const image_buffer &source) {
    return mutate_pixels(source, [](rgba_float value) {
        value.a = 0.0f;
        return value;
    });
}

bool write_binary_file(const std::string &path, const std::vector<uint8_t> &bytes) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if(file == nullptr) return false;
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    const int close_result = std::fclose(file);
    return written == bytes.size() && close_result == 0;
}

bool read_binary_file(const std::string &path, std::vector<uint8_t> &out_bytes) {
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if(file == nullptr) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    if(size < 0) {
        std::fclose(file);
        return false;
    }
    std::fseek(file, 0, SEEK_SET);
    out_bytes.resize((size_t)size);
    const size_t read = std::fread(out_bytes.data(), 1, out_bytes.size(), file);
    const int close_result = std::fclose(file);
    return read == out_bytes.size() && close_result == 0;
}

} // namespace nozzle_tester
