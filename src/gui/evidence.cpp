#include "gui/evidence.hpp"
#include <sstream>
#include <utility>

#ifndef NOZZLE_TESTER_REPO_SHA
#define NOZZLE_TESTER_REPO_SHA "unknown"
#endif
#ifndef NOZZLE_TESTER_NOZZLE_CORE_SHA
#define NOZZLE_TESTER_NOZZLE_CORE_SHA "unknown"
#endif

namespace nozzle_tester {
namespace {
std::string quoted(const std::string &value) { return "\"" + json_escape(value) + "\""; }

void strings(std::ostringstream &out, const std::vector<std::string> &values) {
    out << "[";
    for(size_t i = 0; i < values.size(); ++i) out << (i ? "," : "") << quoted(values[i]);
    out << "]";
}

std::string make_gui_json(const gui_record &value) {
    const auto &r = value.evidence;
    const auto &v = r.verification;
    const auto observed = [&](uint64_t n) { return value.received ? std::to_string(n) : "null"; };
    const auto check = [&](bool pass) { return !value.received ? "INCONCLUSIVE" : pass ? "PASS" : "FAIL"; };
    std::ostringstream out;
    out << "{\n\"schema_version\":\"0.2.0\",\"tool\":\"nozzle-tester\",\"tool_version\":\"0.1.0\",\n";
    out << "\"repo_sha\":" << quoted(NOZZLE_TESTER_REPO_SHA)
        << ",\"nozzle_core_sha\":" << quoted(NOZZLE_TESTER_NOZZLE_CORE_SHA)
        << ",\"os\":" << quoted(detect_os_name()) << ",\"backend\":" << quoted(r.backend) << ",\n";
    out << "\"role\":" << quoted(r.role) << ",\"sender_name\":" << quoted(r.sender_name)
        << ",\"receiver_name\":" << quoted(r.receiver_name) << ",\n";
    out << "\"test_case_id\":" << quoted(make_case_id(r.test)) << ",\"format\":" << quoted(format_to_string(r.test.format))
        << ",\"native_texture_format\":\"unknown\",\"cpu_evidence_format\":"
        << quoted(value.received ? r.cpu_evidence_format : "unknown") << ",\n";
    out << "\"provenance\":" << quoted(value.received ? "received" : "no_received_observation") << ",\n";
    out << "\"publication\":";
    if(value.publication_succeeded.has_value()) {
        out << "{\"succeeded\":" << (*value.publication_succeeded ? "true" : "false")
            << ",\"published_count\":" << value.published_count << "}";
    } else out << "null";
    out << ",\n\"dimensions\":{\"expected_width\":" << r.test.width << ",\"expected_height\":" << r.test.height
        << ",\"observed_width\":" << observed(r.observed_width) << ",\"observed_height\":" << observed(r.observed_height) << "},\n";
    out << "\"frame\":{\"expected_index\":" << r.test.frame_index << ",\"observed_index\":" << observed(r.observed_frame_index)
        << ",\"observed_count\":" << observed(r.observed_frame_count) << ",\"changed_across_observations\":null},\n";
    out << "\"checks\":{\"orientation\":" << quoted(check(v.orientation_ok))
        << ",\"channel_order\":" << quoted(check(v.channel_order_ok)) << ",\"alpha\":" << quoted(check(v.alpha_ok))
        << ",\"dimensions\":" << quoted(check(v.dimensions_ok)) << ",\"format\":" << quoted(check(v.format_ok))
        << ",\"stale_frame\":" << quoted(check(v.stale_frame_ok))
        << ",\"stride_or_stretch\":" << quoted(value.received && v.result == verdict::pass ? "PASS" : "INCONCLUSIVE") << "},\n";
    out << "\"mismatch_count\":" << observed(v.mismatch_count)
        << ",\"verdict\":" << quoted(value.received ? verdict_to_string(v.result) : "INCONCLUSIVE")
        << ",\"unmeasured_reason\":" << (value.received ? "null" : quoted("no_received_observation")) << ",\n";
    out << "\"temporal_progress_reason\":\"not_measured\",\"failure_reasons\":";
    strings(out, v.failure_reasons);
    out << ",\"capture_errors\":";
    strings(out, value.capture_errors);
    out << ",\"artifacts\":[";
    for(size_t i = 0; i < r.artifacts.size(); ++i) {
        const auto &a = r.artifacts[i];
        out << (i ? "," : "") << "{\"role\":" << quoted(a.role) << ",\"path\":" << quoted(a.path) << "}";
    }
    out << "],\"artifact_paths\":";
    strings(out, r.artifact_paths);
    out << "}\n";
    return out.str();
}

void display(gui_evidence_state &state, const gui_record &record) {
    state.json = make_gui_json(record);
    state.verdict_text = record.received ? verdict_to_string(record.evidence.verification.result) : "INCONCLUSIVE";
}
} // namespace

void record_gui_generated(gui_evidence_state &state, const image_buffer &,
    const test_case &test, const std::string &channel, const std::string &role, uint64_t published_count) {
    gui_record value;
    value.evidence.role = role;
    value.evidence.backend = role == "gui-preview" ? "cpu-preview" : "auto";
    value.evidence.sender_name = channel;
    value.evidence.receiver_name = "nozzle-tester-gui";
    value.evidence.test = test;
    if(role != "gui-preview") {
        value.publication_succeeded = true;
        value.published_count = published_count;
    }
    state.current = value;
    display(state, value);
}

void record_gui_received(gui_evidence_state &state, const image_buffer &image,
    evidence_record record, const std::string &channel) {
    record.sender_name = channel;
    record.receiver_name = "nozzle-tester-gui";
    record.cpu_evidence_format = format_to_string(image.format);
    gui_record value;
    value.evidence = std::move(record);
    value.received = true;
    value.publication_succeeded = state.current.publication_succeeded;
    value.published_count = state.current.published_count;
    state.received = gui_snapshot{image, value};
    state.current = value;
    display(state, value);
}

void record_gui_publication_failure(gui_evidence_state &state, const test_case &test,
    const std::string &channel, const std::string &role, uint64_t published_count, const std::string &error) {
    record_gui_generated(state, {}, test, channel, role, published_count);
    state.current.publication_succeeded = false;
    state.current.evidence.verification.failure_reasons.push_back(error);
    display(state, state.current);
}

void clear_gui_observation(gui_evidence_state &state) {
    state.received.reset();
    state.current = {};
    // Keep the last displayed diagnostic, but no cached pixels or capture metadata.
}

gui_capture_result capture_gui_evidence(gui_evidence_state &state, const std::string &prefix) {
    gui_record value = state.received ? state.received->record : state.current;
    value.evidence.artifact_paths.clear();
    value.evidence.artifacts.clear();
    value.capture_errors.clear();
    if(state.received) {
        const auto write = [&](const std::string &path, const std::string &role, const std::vector<uint8_t> &bytes) {
            if(write_binary_file(path, bytes)) {
                value.evidence.artifact_paths.push_back(path);
                value.evidence.artifacts.push_back({role, path});
            } else value.capture_errors.push_back("write_failed: " + path);
        };
        write(prefix + "-capture.raw", "received_raw", state.received->image.bytes);
        // Preview derives from the same frozen pixels, never from the current GL preview.
        write(prefix + "-preview.rgba", "received_preview", image_to_rgba8_preview(state.received->image));
    }
    const auto path = prefix + "-evidence.json";
    if(!write_text_file(path, make_gui_json(value))) value.capture_errors.push_back("write_failed: " + path);
    display(state, value);
    if(!value.capture_errors.empty()) return {false, value.capture_errors.front()};
    return {true, "wrote " + path};
}
} // namespace nozzle_tester
