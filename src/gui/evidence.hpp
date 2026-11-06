#pragma once

#include "core/evidence.hpp"

namespace nozzle_tester {

struct gui_record {
    evidence_record evidence;
    bool received{false};
    std::optional<bool> publication_succeeded;
    uint64_t published_count{0};
    std::vector<std::string> capture_errors;
};

struct gui_snapshot {
    image_buffer image;
    gui_record record;
};

struct gui_evidence_state {
    gui_record current;
    std::optional<gui_snapshot> received;
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
void record_gui_publication_failure(gui_evidence_state &state, const test_case &test,
    const std::string &channel, uint64_t published_count, const std::string &error);
void clear_gui_observation(gui_evidence_state &state);
gui_capture_result capture_gui_evidence(gui_evidence_state &state,
    const std::string &prefix = "nozzle-tester-gui");

} // namespace nozzle_tester
