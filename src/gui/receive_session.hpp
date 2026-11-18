#pragma once

#include "gui/evidence.hpp"
#include <nozzle/nozzle_c.h>

namespace nozzle_tester {

// A closed receiver may reconnect on its next acquisition. Preserve the frozen
// capture, but never compare the replacement's first index to the old sender.
inline void note_gui_acquire_error(gui_evidence_state &state, NozzleErrorCode error) {
    if(error == NOZZLE_ERROR_SENDER_CLOSED) state.sequence = {};
}

} // namespace nozzle_tester
