#pragma once

#include "gui/evidence.hpp"

namespace nozzle_tester {

template<class Release>
void destroy_gui_runtime(gui_evidence_state &evidence, bool &running, Release release) {
    clear_gui_observation(evidence);
    release();
    running = false;
}

template<class Release>
void stop_gui_runtime(gui_evidence_state &evidence, bool &running, Release release) {
    destroy_gui_runtime(evidence, running, release);
}

// The production dispatch and failure teardown, with transport operations supplied
// by the GUI. Tests use the same sequence with CPU observations and tracked release.
template<class Publish, class Receive, class Release>
void tick_gui_runtime(gui_evidence_state &evidence, int mode, bool &running,
    Publish publish, Receive receive, Release release) {
    if(!running) return;
    bool ok = true;
    if(mode == 1) ok = publish();
    else if(mode == 2) ok = receive();
    else if(mode == 3) {
        ok = publish();
        if(ok) ok = receive();
    }
    if(!ok && mode != 2) destroy_gui_runtime(evidence, running, release);
}

} // namespace nozzle_tester
