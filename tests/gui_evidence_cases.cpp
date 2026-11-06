#include "gui/evidence.hpp"
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
    record_gui_generated(state, image, test, "source", "sender", 1);
    emit("sender", state);
    record_gui_generated(state, image, test, "source", "gui-preview", 0);
    auto capture = capture_gui_evidence(state, "source");
    emit("generated_capture", state, capture.success);

    receive(state, image, test);
    auto changed = test;
    changed.width = 123;
    changed.frame_index = 90;
    record_gui_generated(state, generate_pattern(changed), changed, "changed-channel", "gui-preview", 0);
    capture = capture_gui_evidence(state, "frozen");
    std::vector<uint8_t> raw;
    std::vector<uint8_t> preview;
    const bool raw_matches = read_binary_file("frozen-capture.raw", raw) && raw == image.bytes
        && read_binary_file("frozen-preview.rgba", preview) && preview == image_to_rgba8_preview(image);
    emit("frozen_capture", state, capture.success, raw_matches);

    clear_gui_observation(state);
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
    return 0;
}
