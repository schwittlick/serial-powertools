#pragma once

namespace serialpowertools {

class AppState;

namespace ui {

// Apply default left/right docking layout. Call once on first frame.
void applyDefaultLayout(unsigned dockspaceId);

// Draw the left "Controls" window. Reads/mutates `app` directly.
void drawControls(AppState& app);

// Draw the right "Output" log window.
void drawOutput(AppState& app);

}
}
