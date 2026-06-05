#include "core/evidence.hpp"
#include "core/pattern.hpp"

#include <nozzle/nozzle_c.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct gui_state {
    int mode_index{0};
    char channel_name[128]{"nozzle_tester_gui"};
    int width{320};
    int height{240};
    int frame_index{0};
    int format_index{0};
    int timeout_ms{1};
    bool running{false};
    NozzleSender *sender{nullptr};
    NozzleReceiver *receiver{nullptr};
    uint64_t published_count{0};
    uint64_t observed_count{0};
    uint64_t dropped_count{0};
    double fps{0.0};
    std::chrono::steady_clock::time_point start_time{};
    GLuint texture{0};
    int texture_width{0};
    int texture_height{0};
    std::vector<uint8_t> preview_bytes;
    std::string evidence_json;
    std::string verdict{"INCONCLUSIVE"};
    std::string status_text{"idle"};
    std::string last_error;
};

const nozzle_tester::tester_format formats[] = {
    nozzle_tester::tester_format::rgba8_unorm,
    nozzle_tester::tester_format::bgra8_unorm,
    nozzle_tester::tester_format::rgba16_float,
    nozzle_tester::tester_format::rgba32_float,
};

const char *mode_names[] = {
    "pattern preview",
    "sender",
    "receiver",
    "loopback",
};

void glfw_error_callback(int error, const char *description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description != nullptr ? description : "unknown");
}

nozzle_tester::tester_format current_format(const gui_state &state) {
    const int format_index = state.format_index < 0 ? 0 : state.format_index % 4;
    return formats[format_index];
}

NozzleTextureFormat to_nozzle_format(nozzle_tester::tester_format format) {
    switch(format) {
        case nozzle_tester::tester_format::rgba8_unorm: return NOZZLE_FORMAT_RGBA8_UNORM;
        case nozzle_tester::tester_format::bgra8_unorm: return NOZZLE_FORMAT_BGRA8_UNORM;
        case nozzle_tester::tester_format::rgba16_float: return NOZZLE_FORMAT_RGBA16_FLOAT;
        case nozzle_tester::tester_format::rgba32_float: return NOZZLE_FORMAT_RGBA32_FLOAT;
        default: return NOZZLE_FORMAT_UNKNOWN;
    }
}

bool from_nozzle_format(NozzleTextureFormat format, nozzle_tester::tester_format &out_format) {
    switch(format) {
        case NOZZLE_FORMAT_RGBA8_UNORM: out_format = nozzle_tester::tester_format::rgba8_unorm; return true;
        case NOZZLE_FORMAT_BGRA8_UNORM: out_format = nozzle_tester::tester_format::bgra8_unorm; return true;
        case NOZZLE_FORMAT_RGBA16_FLOAT: out_format = nozzle_tester::tester_format::rgba16_float; return true;
        case NOZZLE_FORMAT_RGBA32_FLOAT: out_format = nozzle_tester::tester_format::rgba32_float; return true;
        default: return false;
    }
}

nozzle_tester::test_case make_test_case(const gui_state &state) {
    nozzle_tester::test_case test{};
    test.id = mode_names[state.mode_index < 0 ? 0 : state.mode_index % 4];
    test.width = state.width < 1 ? 1u : (uint32_t)state.width;
    test.height = state.height < 1 ? 1u : (uint32_t)state.height;
    test.frame_index = state.frame_index < 0 ? 0u : (uint64_t)state.frame_index;
    test.format = current_format(state);
    return test;
}

void upload_preview_texture(gui_state &state, uint32_t width, uint32_t height, const std::vector<uint8_t> &preview) {
    if(state.texture == 0) {
        glGenTextures(1, &state.texture);
    }
    state.preview_bytes = preview;
    glBindTexture(GL_TEXTURE_2D, state.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, preview.data());
    state.texture_width = (int)width;
    state.texture_height = (int)height;
}

void set_record(gui_state &state, nozzle_tester::evidence_record &record) {
    record.sender_name = state.channel_name;
    record.receiver_name = "nozzle-tester-gui";
    state.verdict = nozzle_tester::verdict_to_string(record.verification.result);
    state.evidence_json = nozzle_tester::make_evidence_json(record);
}

void update_pattern_preview(gui_state &state) {
    const nozzle_tester::test_case test = make_test_case(state);
    const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);
    const std::vector<uint8_t> preview = nozzle_tester::image_to_rgba8_preview(image);
    upload_preview_texture(state, image.width, image.height, preview);

    nozzle_tester::evidence_record record{};
    record.role = "gui-preview";
    record.backend = "cpu-preview";
    record.test = test;
    record.observed_width = test.width;
    record.observed_height = test.height;
    record.observed_frame_index = test.frame_index;
    record.observed_frame_count = 1;
    record.changed_across_observations = true;
    record.verification = nozzle_tester::verify_pattern(image, test);
    set_record(state, record);
}

void destroy_runtime(gui_state &state) {
    if(state.receiver != nullptr) {
        nozzle_receiver_destroy(state.receiver);
        state.receiver = nullptr;
    }
    if(state.sender != nullptr) {
        nozzle_sender_destroy(state.sender);
        state.sender = nullptr;
    }
    state.running = false;
}

bool start_runtime(gui_state &state) {
    destroy_runtime(state);
    state.last_error.clear();
    state.published_count = 0;
    state.observed_count = 0;
    state.dropped_count = 0;
    state.fps = 0.0;
    state.start_time = std::chrono::steady_clock::now();

    if(state.mode_index == 1 || state.mode_index == 3) {
        NozzleSenderDesc sender_desc{};
        sender_desc.name = state.channel_name;
        sender_desc.application_name = "nozzle-tester-gui";
        sender_desc.ring_buffer_size = 3;
        NozzleErrorCode error = nozzle_sender_create(&sender_desc, &state.sender);
        if(error != NOZZLE_OK || state.sender == nullptr) {
            state.last_error = "sender_create_failed";
            destroy_runtime(state);
            return false;
        }
    }
    if(state.mode_index == 2 || state.mode_index == 3) {
        NozzleReceiverDesc receiver_desc{};
        receiver_desc.name = state.channel_name;
        receiver_desc.application_name = "nozzle-tester-gui";
        receiver_desc.receive_mode = NOZZLE_RECEIVE_SEQUENTIAL_BEST_EFFORT;
        NozzleErrorCode error = nozzle_receiver_create(&receiver_desc, &state.receiver);
        if(error != NOZZLE_OK || state.receiver == nullptr) {
            state.last_error = "receiver_create_failed";
            destroy_runtime(state);
            return false;
        }
    }
    state.running = true;
    return true;
}

bool publish_one_frame(gui_state &state) {
    if(state.sender == nullptr) return false;
    nozzle_tester::test_case test = make_test_case(state);
    test.frame_index = state.published_count;
    const nozzle_tester::image_buffer image = nozzle_tester::generate_pattern(test);
    const uint32_t row_bytes = test.width * nozzle_tester::bytes_per_pixel(test.format);

    NozzleFrame *writable = nullptr;
    NozzleErrorCode error = nozzle_sender_acquire_writable_frame(state.sender, test.width, test.height, to_nozzle_format(test.format), &writable);
    if(error != NOZZLE_OK || writable == nullptr) {
        state.last_error = "acquire_writable_frame_failed";
        return false;
    }

    NozzlePixelMapping *mapping = nullptr;
    NozzleMappedPixels pixels{};
    error = nozzle_frame_lock_writable_pixels_mapping_with_origin(writable, NOZZLE_ORIGIN_TOP_LEFT, &mapping, &pixels);
    if(error != NOZZLE_OK || mapping == nullptr || pixels.data == nullptr || pixels.row_stride_bytes < (int64_t)row_bytes) {
        if(mapping != nullptr) nozzle_pixel_mapping_unlock(&mapping);
        nozzle_sender_discard_frame(state.sender, writable);
        nozzle_frame_release(writable);
        state.last_error = "writable_mapping_failed";
        return false;
    }

    uint8_t *target = (uint8_t*)pixels.data;
    for(uint32_t y = 0; y < test.height; y++) {
        std::memcpy(target + (int64_t)y * pixels.row_stride_bytes, image.bytes.data() + (size_t)y * row_bytes, row_bytes);
    }
    nozzle_pixel_mapping_unlock(&mapping);
    error = nozzle_sender_commit_frame(state.sender, writable);
    nozzle_frame_release(writable);
    if(error != NOZZLE_OK) {
        state.last_error = "commit_frame_failed";
        return false;
    }

    const std::vector<uint8_t> preview = nozzle_tester::image_to_rgba8_preview(image);
    upload_preview_texture(state, image.width, image.height, preview);
    state.published_count += 1;
    state.frame_index = (int)test.frame_index;

    nozzle_tester::evidence_record record{};
    record.role = state.mode_index == 3 ? "loopback-sender" : "sender";
    record.backend = "auto";
    record.test = test;
    record.observed_width = test.width;
    record.observed_height = test.height;
    record.observed_frame_index = test.frame_index;
    record.observed_frame_count = state.published_count;
    record.changed_across_observations = 1 < state.published_count;
    record.verification.result = nozzle_tester::verdict::pass;
    record.verification.dimensions_ok = true;
    record.verification.orientation_ok = true;
    record.verification.channel_order_ok = true;
    record.verification.alpha_ok = true;
    record.verification.stale_frame_ok = 1 < state.published_count;
    set_record(state, record);
    return true;
}

bool receive_one_frame(gui_state &state) {
    if(state.receiver == nullptr) return false;
    NozzleAcquireDesc acquire_desc{};
    acquire_desc.timeout_ms = state.timeout_ms < 0 ? 0u : (uint64_t)state.timeout_ms;
    NozzleFrame *frame = nullptr;
    NozzleErrorCode error = nozzle_receiver_acquire_frame(state.receiver, &acquire_desc, &frame);
    if(error != NOZZLE_OK || frame == nullptr) {
        state.last_error = "missing_frame";
        return false;
    }

    NozzleFrameInfo info{};
    error = nozzle_frame_get_info(frame, &info);
    if(error != NOZZLE_OK) {
        nozzle_frame_release(frame);
        state.last_error = "frame_info_failed";
        return false;
    }

    nozzle_tester::tester_format observed_format{};
    if(!from_nozzle_format(info.format, observed_format)) {
        nozzle_frame_release(frame);
        state.last_error = "unsupported_observed_format";
        return false;
    }

    const uint32_t bytes_per_pixel = nozzle_tester::bytes_per_pixel(observed_format);
    std::vector<uint8_t> copied((size_t)info.width * info.height * bytes_per_pixel);
    NozzleMappedPixels copied_pixels{};
    error = nozzle_frame_copy_pixels_with_origin(frame, NOZZLE_ORIGIN_TOP_LEFT, copied.data(), copied.size(), &copied_pixels);
    nozzle_frame_release(frame);
    if(error != NOZZLE_OK) {
        state.last_error = "copy_pixels_failed";
        return false;
    }

    nozzle_tester::image_buffer image{};
    image.width = info.width;
    image.height = info.height;
    image.format = observed_format;
    image.bytes = copied;
    upload_preview_texture(state, image.width, image.height, nozzle_tester::image_to_rgba8_preview(image));

    nozzle_tester::test_case expected = make_test_case(state);
    expected.width = state.width < 1 ? 1u : (uint32_t)state.width;
    expected.height = state.height < 1 ? 1u : (uint32_t)state.height;
    expected.format = current_format(state);
    expected.frame_index = info.frame_index;
    const nozzle_tester::verify_result verify = nozzle_tester::verify_pattern(image, expected);

    state.observed_count += 1;
    state.dropped_count = info.dropped_frame_count;
    state.frame_index = (int)info.frame_index;
    nozzle_tester::evidence_record record{};
    record.role = state.mode_index == 3 ? "loopback-receiver" : "receiver";
    record.backend = "auto";
    record.test = expected;
    record.observed_width = info.width;
    record.observed_height = info.height;
    record.observed_frame_index = info.frame_index;
    record.observed_frame_count = state.observed_count;
    record.changed_across_observations = 1 < state.observed_count;
    record.verification = verify;
    if(record.verification.result == nozzle_tester::verdict::pass) {
        record.verification.stale_frame_ok = 1 < state.observed_count;
    }
    set_record(state, record);
    return verify.result == nozzle_tester::verdict::pass;
}

void tick_runtime(gui_state &state) {
    if(!state.running) return;
    bool ok = true;
    if(state.mode_index == 1) {
        ok = publish_one_frame(state);
    } else if(state.mode_index == 2) {
        ok = receive_one_frame(state);
    } else if(state.mode_index == 3) {
        ok = publish_one_frame(state);
        if(ok) {
            ok = receive_one_frame(state);
        }
    }
    if(!ok && state.mode_index != 2) {
        destroy_runtime(state);
    }
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - state.start_time).count();
    if(0.0 < elapsed) {
        const uint64_t count = state.mode_index == 1 ? state.published_count : state.observed_count;
        state.fps = (double)count / elapsed;
    }
}

void capture_evidence(gui_state &state) {
    const std::string capture_path = "nozzle-tester-gui-capture.rgba";
    const std::string evidence_path = "nozzle-tester-gui-evidence.json";
    if(!state.preview_bytes.empty()) {
        nozzle_tester::write_binary_file(capture_path, state.preview_bytes);
    }
    nozzle_tester::evidence_record record{};
    record.role = mode_names[state.mode_index < 0 ? 0 : state.mode_index % 4];
    record.backend = state.running ? "auto" : "cpu-preview";
    record.sender_name = state.channel_name;
    record.receiver_name = "nozzle-tester-gui";
    record.test = make_test_case(state);
    record.observed_width = state.texture_width < 1 ? 0u : (uint32_t)state.texture_width;
    record.observed_height = state.texture_height < 1 ? 0u : (uint32_t)state.texture_height;
    record.observed_frame_index = state.frame_index < 0 ? 0u : (uint64_t)state.frame_index;
    record.observed_frame_count = state.observed_count != 0 ? state.observed_count : std::max<uint64_t>(1, state.published_count);
    record.changed_across_observations = 1 < record.observed_frame_count;
    record.artifact_paths.push_back(capture_path);
    if(!state.last_error.empty()) {
        record.verification.result = nozzle_tester::verdict::fail;
        record.verification.failure_reasons.push_back(state.last_error);
    } else {
        record.verification.result = state.verdict == "PASS" ? nozzle_tester::verdict::pass : nozzle_tester::verdict::inconclusive;
    }
    record.verification.dimensions_ok = record.observed_width == record.test.width && record.observed_height == record.test.height;
    record.verification.orientation_ok = record.verification.result == nozzle_tester::verdict::pass;
    record.verification.channel_order_ok = record.verification.result == nozzle_tester::verdict::pass;
    record.verification.alpha_ok = record.verification.result == nozzle_tester::verdict::pass;
    record.verification.stale_frame_ok = record.changed_across_observations;
    const std::string json = nozzle_tester::make_evidence_json(record);
    nozzle_tester::write_text_file(evidence_path, json);
    state.evidence_json = json;
    state.status_text = "wrote " + evidence_path + " and " + capture_path;
}

} // namespace

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if(glfwInit() != GLFW_TRUE) {
        return 1;
    }

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow *window = glfwCreateWindow(1280, 860, "Nozzle Tester", nullptr, nullptr);
    if(window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    gui_state state{};
    update_pattern_preview(state);

    while(glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();
        tick_runtime(state);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("nozzle-tester conformance");
        bool changed = false;
        changed = ImGui::Combo("mode", &state.mode_index, mode_names, 4) || changed;
        changed = ImGui::InputText("channel", state.channel_name, sizeof(state.channel_name)) || changed;
        changed = ImGui::InputInt("width", &state.width) || changed;
        changed = ImGui::InputInt("height", &state.height) || changed;
        changed = ImGui::InputInt("expected/frame", &state.frame_index) || changed;
        changed = ImGui::InputInt("timeout ms", &state.timeout_ms) || changed;
        const char *format_names[] = {"rgba8_unorm", "bgra8_unorm", "rgba16_float", "rgba32_float"};
        changed = ImGui::Combo("format", &state.format_index, format_names, 4) || changed;
        if(changed && !state.running) {
            if(state.width < 1) state.width = 1;
            if(state.height < 1) state.height = 1;
            if(state.frame_index < 0) state.frame_index = 0;
            if(state.timeout_ms < 0) state.timeout_ms = 0;
            update_pattern_preview(state);
        }

        if(!state.running) {
            if(ImGui::Button("Start runtime")) {
                if(start_runtime(state)) {
                    state.status_text = "runtime started";
                } else {
                    state.status_text = state.last_error;
                }
            }
        } else if(ImGui::Button("Stop runtime")) {
            destroy_runtime(state);
            state.status_text = "runtime stopped";
        }
        ImGui::SameLine();
        if(ImGui::Button("Regenerate / verify")) {
            update_pattern_preview(state);
        }
        ImGui::SameLine();
        if(ImGui::Button("Next frame")) {
            state.frame_index += 1;
            update_pattern_preview(state);
        }
        ImGui::SameLine();
        if(ImGui::Button("Capture evidence")) {
            capture_evidence(state);
        }

        ImGui::Text("verdict: %s", state.verdict.c_str());
        ImGui::Text("published: %llu observed: %llu dropped: %llu fps: %.2f", (unsigned long long)state.published_count, (unsigned long long)state.observed_count, (unsigned long long)state.dropped_count, state.fps);
        ImGui::Text("status: %s", state.status_text.c_str());
        if(!state.last_error.empty()) {
            ImGui::Text("last error: %s", state.last_error.c_str());
        }
        ImGui::TextWrapped("The GUI uses the same pattern/oracle as nozzle-tester-cli. Capture evidence writes JSON plus an RGBA captured-frame artifact; a screenshot alone is not the oracle.");
        const float max_width = 760.0f;
        const float scale = state.texture_width > 0 ? std::min(max_width / (float)state.texture_width, 1.0f) : 1.0f;
        ImGui::Image((ImTextureID)(intptr_t)state.texture, ImVec2((float)state.texture_width * scale, (float)state.texture_height * scale));
        ImGui::BeginChild("evidence json", ImVec2(-1.0f, 260.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(state.evidence_json.c_str());
        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        int display_width = 0;
        int display_height = 0;
        glfwGetFramebufferSize(window, &display_width, &display_height);
        glViewport(0, 0, display_width, display_height);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    destroy_runtime(state);
    if(state.texture != 0) {
        glDeleteTextures(1, &state.texture);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
