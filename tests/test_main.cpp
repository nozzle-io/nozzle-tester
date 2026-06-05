#include "core/evidence.hpp"
#include "core/pattern.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if(!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        failures += 1;
    }
}

bool has_reason(const nozzle_tester::verify_result &result, const std::string &reason) {
    for(const auto &item : result.failure_reasons) {
        if(item.find(reason) != std::string::npos) return true;
    }
    return false;
}

void test_good_patterns() {
    const nozzle_tester::tester_format formats[] = {
        nozzle_tester::tester_format::rgba8_unorm,
        nozzle_tester::tester_format::bgra8_unorm,
        nozzle_tester::tester_format::rgba16_float,
        nozzle_tester::tester_format::rgba32_float,
    };
    for(auto format : formats) {
        nozzle_tester::test_case test{};
        test.id = "unit-good";
        test.width = 641;
        test.height = 479;
        test.format = format;
        test.frame_index = 9;
        const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);
        const nozzle_tester::verify_result result = nozzle_tester::verify_pattern(image, test);
        check(result.result == nozzle_tester::verdict::pass, "generated pattern verifies for every initial format");
    }
}

void test_bad_fixtures() {
    const nozzle_tester::tester_format formats[] = {
        nozzle_tester::tester_format::rgba8_unorm,
        nozzle_tester::tester_format::bgra8_unorm,
        nozzle_tester::tester_format::rgba16_float,
        nozzle_tester::tester_format::rgba32_float,
    };
    for(auto format : formats) {
        nozzle_tester::test_case test{};
        test.id = "unit-bad";
        test.width = 641;
        test.height = 479;
        test.format = format;
        test.frame_index = 3;
        const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);

        const nozzle_tester::verify_result flipped = nozzle_tester::verify_pattern(nozzle_tester::make_vertical_flip_fixture(image), test);
        check(flipped.result == nozzle_tester::verdict::fail, "vertical flip fixture fails for every initial format");
        check(has_reason(flipped, "vertical_flip"), "vertical flip reason is structured for every initial format");

        const nozzle_tester::verify_result swapped = nozzle_tester::verify_pattern(nozzle_tester::make_rb_swap_fixture(image), test);
        check(swapped.result == nozzle_tester::verdict::fail, "R/B swap fixture fails for every initial format");
        check(has_reason(swapped, "rb_swap"), "R/B swap reason is structured for every initial format");

        const nozzle_tester::verify_result alpha = nozzle_tester::verify_pattern(nozzle_tester::make_alpha_zero_fixture(image), test);
        check(alpha.result == nozzle_tester::verdict::fail, "alpha fixture fails for every initial format");
        check(has_reason(alpha, "alpha_mismatch"), "alpha reason is structured for every initial format");

        nozzle_tester::test_case stale = test;
        stale.frame_index = 4;
        const nozzle_tester::verify_result stale_result = nozzle_tester::verify_pattern(image, stale);
        check(stale_result.result == nozzle_tester::verdict::fail, "stale frame fixture fails for every initial format");
        check(has_reason(stale_result, "stale_frame"), "stale frame reason is structured for every initial format");
    }
}

void test_byte_size_validation() {
    nozzle_tester::test_case test{};
    test.id = "unit-byte-size";
    test.width = 641;
    test.height = 479;
    test.format = nozzle_tester::tester_format::rgba16_float;
    test.frame_index = 5;
    const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);

    nozzle_tester::image_buffer truncated = image;
    truncated.bytes.resize(truncated.bytes.size() - 1u);
    const nozzle_tester::verify_result truncated_result = nozzle_tester::verify_pattern(truncated, test);
    check(truncated_result.result == nozzle_tester::verdict::fail, "truncated capture fails deterministically");
    check(has_reason(truncated_result, "byte_size_mismatch"), "truncated capture reports byte_size_mismatch");

    nozzle_tester::image_buffer oversized = image;
    oversized.bytes.push_back(0);
    const nozzle_tester::verify_result oversized_result = nozzle_tester::verify_pattern(oversized, test);
    check(oversized_result.result == nozzle_tester::verdict::fail, "oversized capture fails deterministically");
    check(has_reason(oversized_result, "byte_size_mismatch"), "oversized capture reports byte_size_mismatch");

    nozzle_tester::image_buffer wrong_bytes_per_pixel = image;
    wrong_bytes_per_pixel.format = nozzle_tester::tester_format::rgba8_unorm;
    nozzle_tester::test_case declared = test;
    declared.format = nozzle_tester::tester_format::rgba8_unorm;
    const nozzle_tester::verify_result wrong_bpp_result = nozzle_tester::verify_pattern(wrong_bytes_per_pixel, declared);
    check(wrong_bpp_result.result == nozzle_tester::verdict::fail, "wrong bytes-per-pixel capture fails deterministically");
    check(has_reason(wrong_bpp_result, "byte_size_mismatch"), "wrong bytes-per-pixel reports byte_size_mismatch");
}


void test_dimension_and_format_mismatch_reasons() {
    nozzle_tester::test_case expected{};
    expected.id = "unit-mismatch-reasons";
    expected.width = 320;
    expected.height = 240;
    expected.format = nozzle_tester::tester_format::rgba16_float;
    expected.frame_index = 1;

    nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(expected);
    image.format = nozzle_tester::tester_format::rgba8_unorm;
    const nozzle_tester::verify_result format_result = nozzle_tester::verify_pattern(image, expected);
    check(format_result.result == nozzle_tester::verdict::fail, "format mismatch fails deterministically");
    check(format_result.dimensions_ok, "format mismatch does not poison dimension check");
    check(!format_result.format_ok, "format mismatch exposes format check");
    check(has_reason(format_result, "format_mismatch"), "format mismatch reports format_mismatch");
    check(!has_reason(format_result, "dimension_mismatch"), "format mismatch is not reported as dimension_mismatch");

    nozzle_tester::image_buffer wrong_dimensions = nozzle_tester::generate_pattern(expected);
    wrong_dimensions.width = expected.width + 1u;
    const nozzle_tester::verify_result dimension_result = nozzle_tester::verify_pattern(wrong_dimensions, expected);
    check(dimension_result.result == nozzle_tester::verdict::fail, "dimension mismatch fails deterministically");
    check(!dimension_result.dimensions_ok, "dimension mismatch exposes dimension check");
    check(dimension_result.format_ok, "dimension mismatch does not poison format check");
    check(has_reason(dimension_result, "dimension_mismatch"), "dimension mismatch reports dimension_mismatch");
    check(!has_reason(dimension_result, "format_mismatch"), "dimension mismatch is not reported as format_mismatch");
}

void test_non_zero_frame_index_oracle() {
    nozzle_tester::test_case test{};
    test.id = "unit-frame-index";
    test.width = 320;
    test.height = 240;
    test.format = nozzle_tester::tester_format::rgba8_unorm;
    test.frame_index = 42;
    const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);
    const nozzle_tester::verify_result observed_index_result = nozzle_tester::verify_pattern(image, test);
    check(observed_index_result.result == nozzle_tester::verdict::pass, "non-zero observed frame index verifies");

    nozzle_tester::test_case loop_counter_test = test;
    loop_counter_test.frame_index = 0;
    const nozzle_tester::verify_result loop_counter_result = nozzle_tester::verify_pattern(image, loop_counter_test);
    check(loop_counter_result.result == nozzle_tester::verdict::fail, "local loop counter would reject non-zero observed frame");
    check(has_reason(loop_counter_result, "stale_frame") || has_reason(loop_counter_result, "pixel_mismatch"), "non-zero frame mismatch is structured");
}

void test_evidence_json() {
    nozzle_tester::test_case test{};
    test.id = "unit-json";
    test.width = 320;
    test.height = 240;
    test.format = nozzle_tester::tester_format::rgba8_unorm;
    const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);
    nozzle_tester::evidence_record record{};
    record.role = "verify";
    record.backend = "cpu";
    record.test = test;
    record.observed_width = test.width;
    record.observed_height = test.height;
    record.observed_frame_count = 1;
    record.artifacts.push_back({"raw_capture", "capture.raw"});
    record.covered_failure_reasons.push_back("vertical_flip");
    record.verification = nozzle_tester::verify_pattern(image, test);
    const std::string json = nozzle_tester::make_evidence_json(record);
    check(json.find("\"schema_version\": \"0.1.0\"") != std::string::npos, "evidence has schema version");
    check(json.find("\"verdict\": \"PASS\"") != std::string::npos, "evidence has verdict");
    check(json.find("\"format\": \"rgba8_unorm\"") != std::string::npos, "evidence has format");
    check(json.find("\"repo_sha\": \"unknown\"") == std::string::npos, "evidence has build repo sha");
    check(json.find("\"nozzle_core_sha\": \"unknown\"") == std::string::npos, "evidence has nozzle core sha");
    check(json.find("\"format\": \"PASS\"") != std::string::npos, "evidence has format check");
    check(json.find("\"artifacts\"") != std::string::npos, "evidence has artifact roles");
    check(json.find("\"covered_failure_reasons\"") != std::string::npos, "evidence has covered failure reasons");
}

} // namespace

int main() {
    test_good_patterns();
    test_bad_fixtures();
    test_byte_size_validation();
    test_dimension_and_format_mismatch_reasons();
    test_non_zero_frame_index_oracle();
    test_evidence_json();
    if(failures != 0) {
        std::fprintf(stderr, "%d test failure(s)\n", failures);
        return 1;
    }
    std::printf("nozzle-tester unit tests passed\n");
    return 0;
}
