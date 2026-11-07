#pragma once

#include "gui/evidence.hpp"

namespace nozzle_tester {

enum class gui_publish_status { published, retry_later, failed };

template<class Release>
void destroy_gui_runtime(bool &running, Release release) {
    release();
    running = false;
}

template<class Release>
void stop_gui_runtime(gui_evidence_state &evidence, bool &running, Release release) {
    destroy_gui_runtime(running, release);
    clear_gui_observation(evidence);
}

// The production dispatch and failure teardown, with transport operations supplied
// by the GUI. Tests use the same sequence with CPU observations and tracked release.
template<class Publish, class Receive, class Release>
void tick_gui_runtime(int mode, bool &running,
    Publish publish, Receive receive, Release release) {
    if(!running) return;
    bool ok = true;
    if(mode == 1 || mode == 3) {
        const auto status = publish();
        if(status == gui_publish_status::retry_later) return;
        ok = status == gui_publish_status::published;
        if(ok && mode == 3) ok = receive();
    } else if(mode == 2) {
        ok = receive();
    }
    if(!ok && mode != 2) destroy_gui_runtime(running, release);
}

} // namespace nozzle_tester
