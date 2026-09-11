#pragma once

#include <cstdint>
#include <string>

namespace subtitle_manager {

// Initializes the subtitle manager if "-subtitles" is present in command-line arguments.
void init();

// Returns true if subtitles are enabled via "-subtitles".
bool is_enabled();

// Called when any stream / audio hash is played by the game sound engine.
void on_sound_triggered(uint32_t sound_hash, uint32_t caller = 0, int stream_handle = -1, uint32_t resolved_hash = 0);

// Immediately clears all active subtitles (e.g. when cutscene or dialogue is skipped/stopped).
void clear();

// Clears only gameplay dialogue subtitles, protecting active cutscene subtitles.
void clear_gameplay();

// Called during the 2D overlay rendering pass to render the active subtitle using nglListAddString.
void render();

// Called when a pre-rendered video/movie starts playback (e.g. S01_FaPr_IGC1).
void on_movie_start(const char *movie_name);

// Called when a pre-rendered video/movie ends or is skipped.
void on_movie_end();

// Updates current movie playback time directly from Bink frame counter.
void set_movie_time(float current_time);

} // namespace subtitle_manager

void subtitle_manager_patch();
