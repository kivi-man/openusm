#pragma once

#include <cstdint>
#include <string>

namespace subtitle_manager {

// Initializes the subtitle manager if "-subtitles" is present in command-line arguments.
void init();

// Returns true if subtitles are enabled via "-subtitles".
bool is_enabled();

// Called when any stream / audio hash is played by the game sound engine.
void on_sound_triggered(uint32_t sound_hash, uint32_t caller = 0);

// Called during the 2D overlay rendering pass to render the active subtitle using nglListAddString.
void render();

} // namespace subtitle_manager

void subtitle_manager_patch();
