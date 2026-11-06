#include "gui/evidence.hpp"
#include <cstdio>

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
    const auto preview = image_to_rgba8_preview(image);
    gui_evidence_state state;
    record_gui_generated(state, image, test, "source", "sender", 1);
    emit("sender", state);
    record_gui_generated(state, image, test, "source", "gui-preview", 0);
    auto capture = capture_gui_evidence(state, "receiver", "changed-channel", true, test, preview, "", "source");
    emit("generated_capture", state, capture.success);

    receive(state, image, test);
    auto changed = test;
    changed.width = 123;
    changed.frame_index = 90;
    capture = capture_gui_evidence(state, "sender", "changed-channel", false, changed, {}, "", "frozen");
    std::vector<uint8_t> raw;
    const bool raw_matches = read_binary_file("frozen-capture.raw", raw) && raw == image.bytes;
    emit("frozen_capture", state, capture.success, raw_matches);

    clear_gui_observation(state);
    capture = capture_gui_evidence(state, "receiver", "new-session", true, test, preview, "", "stopped");
    emit("stopped_capture", state, capture.success);

    receive(state, image, test);
    if(!write_text_file("blocked", "not a directory")) return 2;
    capture = capture_gui_evidence(state, "receiver", "original-channel", true, test, preview, "", "blocked/child");
    emit("failed_write", state, capture.success);

    receive(state, make_rb_swap_fixture(image), test);
    emit("bad_received", state);
    capture = capture_gui_evidence(state, "receiver", "original-channel", true, test, {}, "", "bad");
    emit("bad_capture", state, capture.success);
    return 0;
}
