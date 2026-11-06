#include "gui/evidence.hpp"
#include "gui/runtime_tick.hpp"
#include <cstdio>
#include <filesystem>

using namespace nozzle_tester;

namespace {
void emit(const char *name, const gui_evidence_state &state, bool success = true, bool raw_matches = true) {
    std::printf("{\"case\":\"%s\",\"record\":\"%s\",\"capture_success\":%s,\"raw_matches\":%s}\n",
        name, json_escape(state.json).c_str(), success ? "true" : "false", raw_matches ? "true" : "false");
}

void receive(gui_evidence_state &state, const image_buffer &image, const test_case &test) {
    evidence_record record{};
    record.role = "loopback-receiver";
    record.backend = "auto";
    record.test = test;
    record.observed_width = image.width;
    record.observed_height = image.height;
    record.observed_frame_index = test.frame_index;
    record.observed_frame_count = 1;
    record.verification = verify_pattern(image, test);
    record_gui_received(state, image, record, "original-channel");
}
} // namespace

int main() {
    test_case test{};
    test.width = 65;
    test.height = 3;
    test.frame_index = 7;
    test.format = tester_format::rgba32_float;
    const auto image = generate_pattern(test);
    gui_evidence_state state;
    record_gui_generated(state, test, "source", "sender", 1);
    emit("sender", state);
    record_gui_generated(state, test, "source", "gui-preview", 0);
    auto capture = capture_gui_evidence(state, "source");
    emit("generated_capture", state, capture.success);

    receive(state, image, test);
    auto changed = test;
    changed.width = 123;
    changed.frame_index = 90;
    record_gui_generated(state, changed, "changed-channel", "gui-preview", 0);
    capture = capture_gui_evidence(state, "frozen");
    std::vector<uint8_t> raw;
    std::vector<uint8_t> preview;
    const bool raw_matches = read_binary_file("frozen-capture.raw", raw) && raw == image.bytes
        && read_binary_file("frozen-preview.rgba", preview) && preview == image_to_rgba8_preview(image);
    emit("frozen_capture", state, capture.success, raw_matches);

    bool stopped_running = true;
    stop_gui_runtime(state, stopped_running, [] {});
    capture = capture_gui_evidence(state, "stopped");
    emit("stopped_capture", state, capture.success);

    receive(state, image, test);
    if(!write_text_file("blocked", "not a directory")) return 2;
    capture = capture_gui_evidence(state, "blocked/child");
    emit("failed_write", state, capture.success);

    std::error_code error;
    if(!std::filesystem::create_directory("rawfail-capture.raw", error) || error) return 2;
    capture = capture_gui_evidence(state, "rawfail");
    emit("raw_write_failure", state, capture.success);
    if(!std::filesystem::create_directory("jsonfail-evidence.json", error) || error) return 2;
    capture = capture_gui_evidence(state, "jsonfail");
    emit("json_write_failure", state, capture.success);

    clear_gui_observation(state);
    record_gui_publication_failure(state, test, "failed-sender", "loopback-sender", 2, "commit_failed");
    emit("publication_failure", state);

    receive(state, make_rb_swap_fixture(image), test);
    emit("bad_received", state);
    capture = capture_gui_evidence(state, "bad");
    emit("bad_capture", state, capture.success);

    gui_evidence_state sequence;
    bool running = true;
    int released = 0;
    const auto bad = make_rb_swap_fixture(image);
    tick_gui_runtime(3, running, [&] {
        record_gui_generated(sequence, test, "original-channel", "loopback-sender", 1);
        return true;
    }, [&] {
        receive(sequence, bad, test);
        return false;
    }, [&] { ++released; });
    capture = capture_gui_evidence(sequence, "bad-sequence");
    raw.clear();
    emit("bad_sequence", sequence, capture.success,
        !running && released == 1 && read_binary_file("bad-sequence-capture.raw", raw) && raw == bad.bytes);

    clear_gui_observation(sequence);
    running = true;
    released = 0;
    tick_gui_runtime(1, running, [&] {
        record_gui_publication_failure(sequence, test, "failed-sender", "sender", 2, "commit_failed");
        return false;
    }, [] { return true; }, [&] { ++released; });
    capture = capture_gui_evidence(sequence, "publish-sequence");
    emit("publish_sequence", sequence, capture.success, !running && released == 1);

    clear_gui_observation(sequence);
    receive(sequence, image, test);
    record_gui_publication_failure(sequence, test, "original-channel", "loopback-sender", 2, "commit_failed");
    capture = capture_gui_evidence(sequence, "mixed");
    emit("mixed_publication", sequence, capture.success);

    running = true;
    released = 0;
    int receive_calls = 0;
    tick_gui_runtime(3, running, [&] {
        record_gui_publication_failure(sequence, test, "original-channel", "loopback-sender", 2, "commit_failed");
        return false;
    }, [&] { ++receive_calls; return true; }, [&] { ++released; });
    capture = capture_gui_evidence(sequence, "mixed-sequence");
    raw.clear();
    emit("mixed_sequence", sequence, capture.success, !running && released == 1 && receive_calls == 0
        && read_binary_file("mixed-sequence-capture.raw", raw) && raw == image.bytes);
    stop_gui_runtime(sequence, running, [] {});
    capture = capture_gui_evidence(sequence, "explicit-stop");
    emit("explicit_stop_after_failure", sequence, capture.success);

    auto mismatch = test;
    mismatch.width += 1;
    receive(sequence, image, mismatch);
    emit("dimension_early_return", sequence);
    mismatch = test;
    mismatch.format = tester_format::rgba16_float;
    receive(sequence, image, mismatch);
    emit("format_early_return", sequence);
    auto short_image = image;
    short_image.bytes.pop_back();
    receive(sequence, short_image, test);
    emit("bytes_early_return", sequence);
    return 0;
}
