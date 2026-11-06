#pragma once

#include "core/evidence.hpp"

namespace nozzle_tester {

struct gui_evidence_state {
    image_buffer last_observed_image;
    test_case last_expected_test;
    uint64_t last_observed_frame_count{0};
    bool has_last_observed_image{false};
    std::string json;
    std::string verdict_text{"INCONCLUSIVE"};
};

struct gui_capture_result {
    bool success{false};
    std::string status;
};

void record_gui_generated(gui_evidence_state &state, const image_buffer &image,
    const test_case &test, const std::string &channel, const std::string &role, uint64_t published_count);
void record_gui_received(gui_evidence_state &state, const image_buffer &image,
    evidence_record record, const std::string &channel);
void clear_gui_observation(gui_evidence_state &state);
gui_capture_result capture_gui_evidence(gui_evidence_state &state, const std::string &role,
    const std::string &channel, bool running, const test_case &current_test,
    const std::vector<uint8_t> &preview, const std::string &last_error,
    const std::string &prefix = "nozzle-tester-gui");

} // namespace nozzle_tester
