#pragma once

#include "core/pattern.hpp"

#include <string>
#include <vector>

namespace nozzle_tester {

struct evidence_artifact {
    std::string role;
    std::string path;
};

enum class index_progression { insufficient, repeated, advanced, nonmonotonic };

const char *progression_text(index_progression progression);

struct frame_sequence {
    uint64_t observed_frame_index{0};
    uint64_t observed_frame_count{0};
    index_progression progression{index_progression::insufficient};
    void observe_index(uint64_t index);
};

struct evidence_record : frame_sequence {
    std::string role{"verify"};
    std::string backend{"cpu"};
    std::string sender_name;
    std::string receiver_name;
    std::string repo_sha{"unknown"};
    std::string nozzle_core_sha{"unknown"};
    test_case test;
    uint32_t observed_width{0};
    uint32_t observed_height{0};
    bool changed_across_observations{false};
    std::string native_texture_format{"unknown"};
    std::string cpu_evidence_format;
    std::vector<std::string> artifact_paths;
    std::vector<evidence_artifact> artifacts;
    std::vector<std::string> covered_failure_reasons;
    verify_result verification;
    std::string transport_error;
};

std::string json_escape(const std::string &text);
std::string make_evidence_json(const evidence_record &record);
bool write_text_file(const std::string &path, const std::string &text);
std::string detect_os_name();

} // namespace nozzle_tester
