#pragma once

#include <imgui.h>

namespace nozzle_tester {

struct gui_settings {
    int mode_index{0};
    char channel_name[128]{"nozzle_tester_gui"};
    int width{320};
    int height{240};
    int frame_index{0};
    int format_index{0};
    int timeout_ms{1};
};

inline bool draw_session_inputs(gui_settings &state, bool /*running*/) {
    static const char *mode_names[] = {
        "pattern preview",
        "sender",
        "receiver",
        "loopback",
    };
    bool changed = false;
    changed = ImGui::Combo("mode", &state.mode_index, mode_names, 4) || changed;
    changed = ImGui::InputText("channel", state.channel_name, sizeof(state.channel_name)) || changed;
    changed = ImGui::InputInt("width", &state.width) || changed;
    changed = ImGui::InputInt("height", &state.height) || changed;
    changed = ImGui::InputInt("expected/frame", &state.frame_index) || changed;
    changed = ImGui::InputInt("timeout ms", &state.timeout_ms) || changed;
    const char *format_names[] = {"rgba8_unorm", "bgra8_unorm", "rgba16_float", "rgba32_float"};
    changed = ImGui::Combo("format", &state.format_index, format_names, 4) || changed;
    return changed;
}

enum class preview_action { none, regenerate, next };

inline preview_action draw_preview_actions(bool /*running*/) {
    auto action = preview_action::none;
    if(ImGui::Button("Regenerate / verify")) action = preview_action::regenerate;
    ImGui::SameLine();
    if(ImGui::Button("Next frame")) action = preview_action::next;
    return action;
}

} // namespace nozzle_tester
