#include "core/evidence.hpp"

#include <cstdio>
#include <sstream>

namespace nozzle_tester {

#ifndef NOZZLE_TESTER_REPO_SHA
#define NOZZLE_TESTER_REPO_SHA "unknown"
#endif

#ifndef NOZZLE_TESTER_NOZZLE_CORE_SHA
#define NOZZLE_TESTER_NOZZLE_CORE_SHA "unknown"
#endif

std::string json_escape(const std::string &text) {
    std::string result;
    result.reserve(text.size() + 8);
    for(char c : text) {
        switch(c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if((unsigned char)c < 0x20u) {
                    result += "?";
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

std::string detect_os_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

namespace {

const char *check_text(bool value) {
    return value ? "PASS" : "FAIL";
}

void append_string_array(std::ostringstream &stream, const std::vector<std::string> &values) {
    stream << "[";
    for(size_t index = 0; index < values.size(); index++) {
        if(index != 0) stream << ",";
        stream << "\"" << json_escape(values[index]) << "\"";
    }
    stream << "]";
}

void append_artifacts(std::ostringstream &stream, const std::vector<evidence_artifact> &artifacts) {
    stream << "[";
    for(size_t index = 0; index < artifacts.size(); index++) {
        if(index != 0) stream << ",";
        stream << "{";
        stream << "\"role\":\"" << json_escape(artifacts[index].role) << "\",";
        stream << "\"path\":\"" << json_escape(artifacts[index].path) << "\"";
        stream << "}";
    }
    stream << "]";
}

} // namespace

std::string make_evidence_json(const evidence_record &record) {
    const std::string repo_sha = record.repo_sha == "unknown" ? NOZZLE_TESTER_REPO_SHA : record.repo_sha;
    const std::string nozzle_core_sha = record.nozzle_core_sha == "unknown" ? NOZZLE_TESTER_NOZZLE_CORE_SHA : record.nozzle_core_sha;
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schema_version\": \"0.1.0\",\n";
    stream << "  \"tool\": \"nozzle-tester\",\n";
    stream << "  \"tool_version\": \"0.1.0\",\n";
    stream << "  \"repo_sha\": \"" << json_escape(repo_sha) << "\",\n";
    stream << "  \"nozzle_core_sha\": \"" << json_escape(nozzle_core_sha) << "\",\n";
    stream << "  \"os\": \"" << json_escape(detect_os_name()) << "\",\n";
    stream << "  \"backend\": \"" << json_escape(record.backend) << "\",\n";
    stream << "  \"role\": \"" << json_escape(record.role) << "\",\n";
    stream << "  \"sender_name\": \"" << json_escape(record.sender_name) << "\",\n";
    stream << "  \"receiver_name\": \"" << json_escape(record.receiver_name) << "\",\n";
    stream << "  \"test_case_id\": \"" << json_escape(make_case_id(record.test)) << "\",\n";
    stream << "  \"format\": \"" << format_to_string(record.test.format) << "\",\n";
    stream << "  \"native_texture_format\": \"" << json_escape(record.native_texture_format) << "\",\n";
    stream << "  \"cpu_evidence_format\": \"" << json_escape(record.cpu_evidence_format.empty() ? format_to_string(record.test.format) : record.cpu_evidence_format) << "\",\n";
    stream << "  \"dimensions\": {\n";
    stream << "    \"expected_width\": " << record.test.width << ",\n";
    stream << "    \"expected_height\": " << record.test.height << ",\n";
    stream << "    \"observed_width\": " << record.observed_width << ",\n";
    stream << "    \"observed_height\": " << record.observed_height << "\n";
    stream << "  },\n";
    stream << "  \"frame\": {\n";
    stream << "    \"expected_index\": " << record.test.frame_index << ",\n";
    stream << "    \"observed_index\": " << record.observed_frame_index << ",\n";
    stream << "    \"observed_count\": " << record.observed_frame_count << ",\n";
    stream << "    \"changed_across_observations\": " << (record.changed_across_observations ? "true" : "false") << "\n";
    stream << "  },\n";
    stream << "  \"checks\": {\n";
    stream << "    \"orientation\": \"" << check_text(record.verification.orientation_ok) << "\",\n";
    stream << "    \"channel_order\": \"" << check_text(record.verification.channel_order_ok) << "\",\n";
    stream << "    \"alpha\": \"" << check_text(record.verification.alpha_ok) << "\",\n";
    stream << "    \"dimensions\": \"" << check_text(record.verification.dimensions_ok) << "\",\n";
    stream << "    \"format\": \"" << check_text(record.verification.format_ok) << "\",\n";
    stream << "    \"stale_frame\": \"" << check_text(record.verification.stale_frame_ok) << "\",\n";
    stream << "    \"stride_or_stretch\": \"" << (record.verification.result == verdict::pass ? "PASS" : "INCONCLUSIVE") << "\"\n";
    stream << "  },\n";
    stream << "  \"mismatch_count\": " << record.verification.mismatch_count << ",\n";
    stream << "  \"flipped_mismatch_count\": " << record.verification.flipped_mismatch_count << ",\n";
    stream << "  \"rb_swapped_mismatch_count\": " << record.verification.rb_swapped_mismatch_count << ",\n";
    stream << "  \"stale_previous_mismatch_count\": " << record.verification.stale_previous_mismatch_count << ",\n";
    stream << "  \"verdict\": \"" << verdict_to_string(record.verification.result) << "\",\n";
    stream << "  \"failure_reasons\": ";
    append_string_array(stream, record.verification.failure_reasons);
    stream << ",\n";
    stream << "  \"covered_failure_reasons\": ";
    append_string_array(stream, record.covered_failure_reasons);
    stream << ",\n";
    stream << "  \"artifacts\": ";
    append_artifacts(stream, record.artifacts);
    stream << ",\n";
    stream << "  \"artifact_paths\": ";
    append_string_array(stream, record.artifact_paths);
    stream << "\n";
    stream << "}\n";
    return stream.str();
}

bool write_text_file(const std::string &path, const std::string &text) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if(file == nullptr) return false;
    const size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const int close_result = std::fclose(file);
    return written == text.size() && close_result == 0;
}

} // namespace nozzle_tester
