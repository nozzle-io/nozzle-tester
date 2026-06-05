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
    nozzle_tester::test_case test{};
    test.id = "unit-bad";
    test.width = 641;
    test.height = 479;
    test.format = nozzle_tester::tester_format::rgba8_unorm;
    test.frame_index = 3;
    const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);

    const nozzle_tester::verify_result flipped = nozzle_tester::verify_pattern(nozzle_tester::make_vertical_flip_fixture(image), test);
    check(flipped.result == nozzle_tester::verdict::fail, "vertical flip fixture fails");
    check(has_reason(flipped, "vertical_flip"), "vertical flip reason is structured");

    const nozzle_tester::verify_result swapped = nozzle_tester::verify_pattern(nozzle_tester::make_rb_swap_fixture(image), test);
    check(swapped.result == nozzle_tester::verdict::fail, "R/B swap fixture fails");
    check(has_reason(swapped, "rb_swap"), "R/B swap reason is structured");

    const nozzle_tester::verify_result alpha = nozzle_tester::verify_pattern(nozzle_tester::make_alpha_zero_fixture(image), test);
    check(alpha.result == nozzle_tester::verdict::fail, "alpha fixture fails");
    check(has_reason(alpha, "alpha_mismatch"), "alpha reason is structured");

    nozzle_tester::test_case stale = test;
    stale.frame_index = 4;
    const nozzle_tester::verify_result stale_result = nozzle_tester::verify_pattern(image, stale);
    check(stale_result.result == nozzle_tester::verdict::fail, "stale frame fixture fails");
    check(has_reason(stale_result, "stale_frame"), "stale frame reason is structured");
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
    record.verification = nozzle_tester::verify_pattern(image, test);
    const std::string json = nozzle_tester::make_evidence_json(record);
    check(json.find("\"schema_version\": \"0.1.0\"") != std::string::npos, "evidence has schema version");
    check(json.find("\"verdict\": \"PASS\"") != std::string::npos, "evidence has verdict");
    check(json.find("\"format\": \"rgba8_unorm\"") != std::string::npos, "evidence has format");
}

} // namespace

int main() {
    test_good_patterns();
    test_bad_fixtures();
    test_evidence_json();
    if(failures != 0) {
        std::fprintf(stderr, "%d test failure(s)\n", failures);
        return 1;
    }
    std::printf("nozzle-tester unit tests passed\n");
    return 0;
}
