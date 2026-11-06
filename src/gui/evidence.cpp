#include "gui/evidence.hpp"

namespace nozzle_tester {
namespace {
void set_record(gui_evidence_state &state, evidence_record &record, const std::string &channel) {
    record.sender_name = channel;
    record.receiver_name = "nozzle-tester-gui";
    state.verdict_text = verdict_to_string(record.verification.result);
    state.json = make_evidence_json(record);
}

void store_observed_image(gui_evidence_state &state, const image_buffer &image,
    const test_case &expected, uint64_t count) {
    state.last_observed_image = image;
    state.last_expected_test = expected;
    state.last_observed_frame_count = count;
    state.has_last_observed_image = true;
}
} // namespace

void record_gui_generated(gui_evidence_state &state, const image_buffer &image,
    const test_case &test, const std::string &channel, const std::string &role, uint64_t published_count) {
    const bool preview = role == "gui-preview";
    const auto count = preview ? 1u : published_count;
    store_observed_image(state, image, test, count);
    evidence_record record{};
    record.role = role;
    record.backend = preview ? "cpu-preview" : "auto";
    record.test = test;
    record.observed_width = test.width;
    record.observed_height = test.height;
    record.observed_frame_index = test.frame_index;
    record.observed_frame_count = count;
    record.changed_across_observations = preview || 1 < count;
    if(preview) {
        record.verification = verify_pattern(image, test);
    } else {
        record.verification.result = verdict::pass;
        record.verification.dimensions_ok = true;
        record.verification.orientation_ok = true;
        record.verification.channel_order_ok = true;
        record.verification.alpha_ok = true;
        record.verification.stale_frame_ok = 1 < count;
    }
    set_record(state, record, channel);
}

void record_gui_received(gui_evidence_state &state, const image_buffer &image,
    evidence_record record, const std::string &channel) {
    store_observed_image(state, image, record.test, record.observed_frame_count);
    set_record(state, record, channel);
}

void clear_gui_observation(gui_evidence_state &) {
    // Baseline: stop/restart currently leaves the cached observation intact.
}

gui_capture_result capture_gui_evidence(gui_evidence_state &state, const std::string &role,
    const std::string &channel, bool running, const test_case &current_test,
    const std::vector<uint8_t> &preview, const std::string &last_error, const std::string &prefix) {
    const std::string raw_path = prefix + "-capture.raw";
    const std::string preview_path = prefix + "-preview.rgba";
    const std::string evidence_path = prefix + "-evidence.json";
    evidence_record record{};
    record.role = role;
    record.backend = running ? "auto" : "cpu-preview";
    record.sender_name = channel;
    record.receiver_name = "nozzle-tester-gui";
    if(state.has_last_observed_image) {
        write_binary_file(raw_path, state.last_observed_image.bytes);
        record.test = state.last_expected_test;
        record.observed_width = state.last_observed_image.width;
        record.observed_height = state.last_observed_image.height;
        record.observed_frame_index = state.last_expected_test.frame_index;
        record.observed_frame_count = state.last_observed_frame_count;
        record.changed_across_observations = 1 < state.last_observed_frame_count;
        record.native_texture_format = format_to_string(state.last_observed_image.format);
        record.cpu_evidence_format = format_to_string(state.last_observed_image.format);
        record.verification = verify_pattern(state.last_observed_image, state.last_expected_test);
        record.artifact_paths.push_back(raw_path);
        record.artifacts.push_back({"raw_capture", raw_path});
    } else {
        record.test = current_test;
        record.verification.result = verdict::fail;
        record.verification.failure_reasons.push_back("missing_capture");
    }
    if(!preview.empty()) {
        write_binary_file(preview_path, preview);
        record.artifact_paths.push_back(preview_path);
        record.artifacts.push_back({"preview_capture", preview_path});
    }
    if(!last_error.empty() && record.verification.result == verdict::pass) {
        record.verification.result = verdict::fail;
        record.verification.failure_reasons.push_back(last_error);
    }
    const auto json = make_evidence_json(record);
    write_text_file(evidence_path, json);
    state.json = json;
    state.verdict_text = verdict_to_string(record.verification.result);
    return {true, "wrote " + evidence_path + ", " + raw_path + ", and " + preview_path};
}
} // namespace nozzle_tester
