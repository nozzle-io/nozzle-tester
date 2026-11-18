// Drive the production ImGui widgets without a window, GL renderer, or GPU.
#include "gui/controls.hpp"
#include <cstdio>
#include <cstring>

namespace {
bool exercise(bool running) {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    nozzle_tester::gui_settings settings;
    ImVec2 channel, next;
    bool advanced = false;
    const auto draw = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::Begin("test");
        const auto start = ImGui::GetCursorScreenPos();
        channel = {start.x + 24, start.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight() / 2};
        nozzle_tester::draw_session_inputs(settings, running);
        if(nozzle_tester::draw_preview_actions(running) == nozzle_tester::preview_action::next) advanced = true;
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        next = {(a.x + b.x) / 2, (a.y + b.y) / 2};
        ImGui::End();
        ImGui::Render();
    };
    const auto click = [&](ImVec2 pos) {
        io.AddMousePosEvent(pos.x, pos.y);
        draw();
        io.AddMouseButtonEvent(0, true);
        draw();
        io.AddMouseButtonEvent(0, false);
        draw();
    };
    draw();
    draw();
    click(channel);
    io.AddInputCharactersUTF8("X");
    draw();
    click(next);
    const bool edited = std::strcmp(settings.channel_name, "nozzle_tester_gui") != 0;
    std::printf("running=%d channel_edited=%d next_pressed=%d\n", running, edited, advanced);
    ImGui::DestroyContext();
    return edited == !running && advanced == !running;
}
}

int main() {
    const bool idle = exercise(false);
    const bool active = exercise(true);
    return idle && active ? 0 : 1;
}
