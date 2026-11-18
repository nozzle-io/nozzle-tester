#include "core/evidence.hpp"

#include <cstdio>
#include <sstream>

#if defined(_WIN32)
#include <filesystem>
#endif

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

std::string json_string(const std::string &text) {
    return "\"" + json_escape(text) + "\"";
}

const char *check_json(bool measured, bool passed) {
    if(!measured) return "null";
    return passed ? "\"PASS\"" : "\"FAIL\"";
}

std::string number_json(bool measured, uint64_t value) {
    return measured ? std::to_string(value) : "null";
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

const char *progression_text(index_progression progression) {
    switch(progression) {
        case index_progression::insufficient: return "insufficient";
        case index_progression::repeated: return "repeated";
        case index_progression::advanced: return "advanced";
        case index_progression::nonmonotonic: return "nonmonotonic";
    }
    return "insufficient";
}

void frame_sequence::observe_index(uint64_t index) {
    if(observed_frame_count != 0) {
        const auto step = index == observed_frame_index ? index_progression::repeated :
            index > observed_frame_index ? index_progression::advanced : index_progression::nonmonotonic;
        progression = progression == index_progression::insufficient || progression == step
            ? step : index_progression::nonmonotonic;
    }
    observed_frame_index = index;
    ++observed_frame_count;
}

std::string make_evidence_json(const evidence_record &record) {
    const std::string repo_sha = record.repo_sha == "unknown" ? NOZZLE_TESTER_REPO_SHA : record.repo_sha;
    const std::string nozzle_core_sha = record.nozzle_core_sha == "unknown" ? NOZZLE_TESTER_NOZZLE_CORE_SHA : record.nozzle_core_sha;
    const bool receiver = record.role == "receiver";
    const bool evaluated = !receiver || record.observed_frame_count != 0;
    const auto &result = record.verification;
    const bool pixels = !receiver || (evaluated && result.pixels_compared);
    const bool previous = pixels && (!receiver || record.test.frame_index != 0);
    const auto run_result = record.transport_error.empty() ? result.result : verdict::fail;
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"schema_version\": \"" << (receiver ? "0.3.0" : "0.1.0") << "\",\n";
    stream << "  \"tool\": \"nozzle-tester\",\n";
    stream << "  \"tool_version\": \"0.1.0\",\n";
    stream << "  \"repo_sha\": \"" << json_escape(repo_sha) << "\",\n";
    stream << "  \"nozzle_core_sha\": \"" << json_escape(nozzle_core_sha) << "\",\n";
    stream << "  \"os\": \"" << json_escape(detect_os_name()) << "\",\n";
    stream << "  \"backend\": \"" << json_escape(record.backend) << "\",\n";
    stream << "  \"role\": \"" << json_escape(record.role) << "\",\n";
    stream << "  \"sender_name\": \"" << json_escape(record.sender_name) << "\",\n";
    stream << "  \"receiver_name\": \"" << json_escape(record.receiver_name) << "\",\n";
    stream << "  \"test_case_id\": " << (evaluated ? json_string(make_case_id(record.test)) : "null") << ",\n";
    stream << "  \"format\": \"" << format_to_string(record.test.format) << "\",\n";
    stream << "  \"native_texture_format\": " << (receiver ? "null" : json_string(record.native_texture_format)) << ",\n";
    const auto cpu_format = record.cpu_evidence_format.empty() ? format_to_string(record.test.format) : record.cpu_evidence_format;
    stream << "  \"cpu_evidence_format\": " << (evaluated ? json_string(cpu_format) : "null") << ",\n";
    stream << "  \"dimensions\": {\n";
    stream << "    \"expected_width\": " << record.test.width << ",\n";
    stream << "    \"expected_height\": " << record.test.height << ",\n";
    stream << "    \"observed_width\": " << number_json(evaluated, record.observed_width) << ",\n";
    stream << "    \"observed_height\": " << number_json(evaluated, record.observed_height) << "\n";
    stream << "  },\n";
    stream << "  \"frame\": {\n";
    stream << "    \"expected_index\": " << number_json(evaluated, record.test.frame_index) << ",\n";
    stream << "    \"observed_index\": " << number_json(evaluated, record.observed_frame_index) << ",\n";
    stream << "    \"observed_count\": " << record.observed_frame_count << ",\n";
    if(receiver) {
        stream << "    \"index_progression\": " << json_string(progression_text(record.progression)) << "\n";
    } else {
        stream << "    \"changed_across_observations\": " << (record.changed_across_observations ? "true" : "false") << "\n";
    }
    stream << "  },\n";
    stream << "  \"checks\": {\n";
    stream << "    \"orientation\": " << check_json(pixels, result.orientation_ok) << ",\n";
    stream << "    \"channel_order\": " << check_json(pixels, result.channel_order_ok) << ",\n";
    stream << "    \"alpha\": " << check_json(pixels, result.alpha_ok) << ",\n";
    stream << "    \"dimensions\": " << check_json(evaluated, result.dimensions_ok) << ",\n";
    stream << "    \"format\": " << check_json(evaluated, result.format_ok) << ",\n";
    stream << "    \"stale_frame\": " << check_json(previous, result.stale_frame_ok) << ",\n";
    const char *stride = receiver ? "null" : result.result == verdict::pass ? "\"PASS\"" : "\"INCONCLUSIVE\"";
    stream << "    \"stride_or_stretch\": " << stride << "\n";
    stream << "  },\n";
    stream << "  \"mismatch_count\": " << number_json(pixels, result.mismatch_count) << ",\n";
    stream << "  \"flipped_mismatch_count\": " << number_json(pixels, result.flipped_mismatch_count) << ",\n";
    stream << "  \"rb_swapped_mismatch_count\": " << number_json(pixels, result.rb_swapped_mismatch_count) << ",\n";
    stream << "  \"stale_previous_mismatch_count\": " << number_json(previous, result.stale_previous_mismatch_count) << ",\n";
    if(receiver) {
        stream << "  \"frame_verdict\": " << (evaluated ? json_string(verdict_to_string(result.result)) : "null") << ",\n";
        stream << "  \"transport_error\": " << (record.transport_error.empty() ? "null" : json_string(record.transport_error)) << ",\n";
    }
    stream << "  \"verdict\": \"" << verdict_to_string(run_result) << "\",\n";
    stream << "  \"failure_reasons\": ";
    if(record.transport_error.empty()) append_string_array(stream, result.failure_reasons);
    else append_string_array(stream, {record.transport_error});
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
#if defined(_WIN32)
    std::FILE *file = _wfopen(std::filesystem::u8path(path).c_str(), L"wb");
#else
    std::FILE *file = std::fopen(path.c_str(), "wb");
#endif
    if(file == nullptr) return false;
    const size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const int close_result = std::fclose(file);
    return written == text.size() && close_result == 0;
}

} // namespace nozzle_tester
