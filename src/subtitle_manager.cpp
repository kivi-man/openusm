#include "subtitle_manager.h"

#include "ngl.h"
#include "ngl_font.h"
#include "femanager.h"
#include "utility.h"
#include "func_wrapper.h"

#include <windows.h>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace subtitle_manager {

static bool s_enabled = false;
static bool s_initialized = false;

// 32-bit sound hash -> transcript text
static std::unordered_map<uint32_t, std::string> s_subtitles;

// Cutscene name -> transcript text (loaded for future expansion, currently inactive)
static std::unordered_map<std::string, std::string> s_cutscenes;

// Set of Peter/Spider-Man sound hashes for priority management
static std::unordered_set<uint32_t> s_peter_hashes;

// Set of NPC/citizen ambient voice hashes (CIT_ prefix) - never shown as subtitles
static std::unordered_set<uint32_t> s_npc_hashes;

struct ActiveSubtitle {
    uint32_t sound_hash;
    std::vector<std::string> lines;
    float timer;
    float total_duration;
    bool is_peter;
};

// Queue of currently active subtitles (max 2 concurrent, newer sounds stacked above older)
static std::vector<ActiveSubtitle> s_active;
static DWORD s_last_tick = 0;

bool is_enabled() {
    return s_enabled;
}

// Fast JSON parser specifically designed for audio_transcripts.json
static void load_transcripts_from_file(const char *filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    size_t pos = 0;
    const size_t len = content.length();

    while (pos < len) {
        // Find next key start: '"'
        size_t key_start = content.find('"', pos);
        if (key_start == std::string::npos) break;

        size_t key_end = content.find('"', key_start + 1);
        if (key_end == std::string::npos) break;

        std::string key = content.substr(key_start + 1, key_end - key_start - 1);

        // Check if this is an entry key followed by ':' and '{'
        size_t colon = content.find(':', key_end + 1);
        if (colon == std::string::npos) break;

        size_t brace = content.find('{', colon + 1);
        // If there's non-whitespace between colon and brace (e.g. a normal value), skip
        bool is_object = true;
        for (size_t i = colon + 1; i < brace; ++i) {
            if (!isspace((unsigned char)content[i])) {
                is_object = false;
                break;
            }
        }

        if (!is_object || brace == std::string::npos) {
            pos = key_end + 1;
            continue;
        }

        // Find the closing brace for this object
        size_t obj_end = content.find('}', brace + 1);
        if (obj_end == std::string::npos) break;

        // Inside this object, extract "wbk"
        std::string wbk_name;
        size_t wbk_pos = content.find("\"wbk\"", brace);
        if (wbk_pos != std::string::npos && wbk_pos < obj_end) {
            size_t wbk_colon = content.find(':', wbk_pos + 5);
            if (wbk_colon != std::string::npos && wbk_colon < obj_end) {
                size_t w_start = content.find('"', wbk_colon + 1);
                if (w_start != std::string::npos && w_start < obj_end) {
                    size_t w_end = content.find('"', w_start + 1);
                    if (w_end != std::string::npos && w_end < obj_end) {
                        wbk_name = content.substr(w_start + 1, w_end - w_start - 1);
                    }
                }
            }
        }

        // Filter out SFX and MUSIC banks from subtitles (case-insensitive)
        std::string wbk_upper = wbk_name;
        for (char &c : wbk_upper) c = (char)toupper((unsigned char)c);
        if (wbk_upper.find("SFX") != std::string::npos ||
            wbk_upper.find("MUSIC") != std::string::npos) {
            pos = obj_end + 1;
            continue;
        }

        // Inside this object, search for "transcript"
        size_t tr_pos = content.find("\"transcript\"", brace);
        if (tr_pos != std::string::npos && tr_pos < obj_end) {
            size_t tr_colon = content.find(':', tr_pos + 12);
            if (tr_colon != std::string::npos && tr_colon < obj_end) {
                size_t val_start = content.find('"', tr_colon + 1);
                if (val_start != std::string::npos && val_start < obj_end) {
                    // Find closing quote, handling escaped quotes
                    size_t val_end = val_start + 1;
                    while (val_end < obj_end) {
                        if (content[val_end] == '"' && content[val_end - 1] != '\\') {
                            break;
                        }
                        ++val_end;
                    }

                    if (val_end < obj_end) {
                        std::string transcript = content.substr(val_start + 1, val_end - val_start - 1);
                        // Clean unescaped quotes if needed
                        std::string clean_tr;
                        clean_tr.reserve(transcript.size());
                        for (size_t ci = 0; ci < transcript.size(); ++ci) {
                            if (transcript[ci] == '\\' && ci + 1 < transcript.size() && transcript[ci + 1] == '"') {
                                clean_tr += '"';
                                ++ci;
                            } else {
                                clean_tr += transcript[ci];
                            }
                        }

                        // Filter out empty or error placeholder transcripts
                        if (clean_tr.empty() || clean_tr == "ERROR" ||
                            clean_tr.find("ERROR") != std::string::npos ||
                            clean_tr.find("error") != std::string::npos) {
                            pos = obj_end + 1;
                            continue;
                        }

                        // Store into map
                        if (key.length() == 8) {
                            char *end_ptr = nullptr;
                            uint32_t hash = (uint32_t)strtoul(key.c_str(), &end_ptr, 16);
                            if (end_ptr != nullptr && *end_ptr == '\0') {
                                s_subtitles[hash] = clean_tr;
                            } else {
                                s_cutscenes[key] = clean_tr;
                            }
                        } else {
                            s_cutscenes[key] = clean_tr;
                        }
                    }
                }
            }
        }

        pos = obj_end + 1;
    }
}

static void load_peter_hashes(const char *dict_path) {
    FILE *f = fopen(dict_path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) {
            char *tab = strchr(line, '\t');
            if (tab) {
                char *name = tab + 1;
                bool is_spidey = false;
                for (char *p = name; *p; ++p) {
                    if ((*p == '_' || *p == 'S' || *p == 's' || *p == 'P' || *p == 'p') &&
                        (strstr(p, "_SPI_") != nullptr || strstr(p, "_spi_") != nullptr ||
                         strstr(p, "SPIDEY") != nullptr || strstr(p, "spidey") != nullptr ||
                         strstr(p, "SPIDER") != nullptr || strstr(p, "spider") != nullptr ||
                         strstr(p, "PETER")  != nullptr || strstr(p, "peter")  != nullptr)) {
                        is_spidey = true;
                        break;
                    }
                }
                if (is_spidey) {
                    char *end_ptr = nullptr;
                    uint32_t hash = (uint32_t)strtoul(line, &end_ptr, 16);
                    if (hash != 0) {
                        s_peter_hashes.insert(hash);
                    }
                }
            }
        }
    }
    fclose(f);
}

// Load all NPC/citizen ambient sound hashes from the dictionary.
// Any hash whose name starts with "CIT_" or "ALIASED_SOUND_CIT_" is a
// pedestrian ambient voice and must never appear as a subtitle.
// EXCEPTION: Sounds that contain a named-character speaker code in their
// name (e.g. _SPI_ = Spider-Man, _JOH_ = Johnny Storm, _VEN_ = Venom,
// _CAR_ = Carnage, _RHI_ = Rhino) are gameplay dialogue and ARE kept.
static void load_npc_hashes(const char *dict_path) {
    FILE *f = fopen(dict_path, "r");
    if (!f) return;

    // Named-character codes that should NOT be filtered even under CIT_ prefix
    static const char *s_hero_tags[] = {
        "_SPI_", "_JOH_", "_VEN_", "_CAR_", "_RHI_", "_ELE_", "_SHO_",
        nullptr
    };

    char line[512];
    int added = 0, skipped_hero = 0;
    while (fgets(line, sizeof(line), f)) {
        // Format: 0xHASHVALUE<TAB>SOUND_NAME
        if (line[0] != '0' || (line[1] != 'x' && line[1] != 'X')) continue;

        char *tab = strchr(line, '\t');
        if (!tab) continue;

        const char *name = tab + 1;

        // Check if the sound name indicates a citizen/NPC ambient voice
        bool is_npc = (
            strncmp(name, "CIT_",               4) == 0 ||
            strncmp(name, "ALIASED_SOUND_CIT_", 18) == 0
        );

        if (!is_npc) continue;

        // Skip named-character sounds – they are story/gameplay dialogue, not ambient
        bool is_named_char = false;
        for (int i = 0; s_hero_tags[i]; ++i) {
            if (strstr(name, s_hero_tags[i])) {
                is_named_char = true;
                break;
            }
        }
        if (is_named_char) { ++skipped_hero; continue; }

        char *end_ptr = nullptr;
        uint32_t hash = (uint32_t)strtoul(line, &end_ptr, 16);
        if (hash != 0) {
            s_npc_hashes.insert(hash);
            ++added;
        }
    }
    fclose(f);
    sp_log("[Subtitles] NPC hash filter: %d ambient, %d named-char kept", added, skipped_hero);
}

static inline void get_text_dimensions(nglFont *font, const char *str, int *w, int *h, float scale_x = 1.0f, float scale_y = 1.0f) {
    uint32_t uw = 0, uh = 0;
    typedef void (*nglGetStringDimensions_fn)(nglFont*, const char*, uint32_t*, uint32_t*, float, float);
    auto pfn = (nglGetStringDimensions_fn)0x007798E0;
    if (pfn && font && str) {
        pfn(font, str, &uw, &uh, scale_x, scale_y);
    }
    if (w) *w = (int)uw;
    if (h) *h = (int)uh;
}

static std::vector<std::string> wrap_text(const std::string &text, size_t max_line_len = 48) {
    std::vector<std::string> lines;
    if (text.length() <= max_line_len) {
        lines.push_back(text);
        return lines;
    }

    size_t start = 0;
    while (start < text.length()) {
        if (text.length() - start <= max_line_len) {
            lines.push_back(text.substr(start));
            break;
        }

        size_t split_pos = start + max_line_len;
        size_t space_pos = text.rfind(' ', split_pos);
        if (space_pos != std::string::npos && space_pos > start) {
            lines.push_back(text.substr(start, space_pos - start));
            start = space_pos + 1;
        } else {
            lines.push_back(text.substr(start, max_line_len));
            start += max_line_len;
        }
    }

    return lines;
}

void init() {
    if (s_initialized) return;
    s_initialized = true;

    // Check command line parameter
    const char *cmdLine = GetCommandLineA();
    if (cmdLine == nullptr || strstr(cmdLine, "-subtitles") == nullptr) {
        sp_log("[Subtitles] Disabled (launch with -subtitles to enable)");
        s_enabled = false;
        return;
    }

    s_enabled = true;
    sp_log("[Subtitles] Activated via -subtitles command line argument!");

    // Load Peter/Spider-Man hash dictionary for dialogue prioritization
    // and NPC/citizen hashes for ambient voice filtering
    const char *dict_paths[] = {
        "data/packs/pc/string_hash_dictionary.txt",
        "./data/packs/pc/string_hash_dictionary.txt",
        "string_hash_dictionary.txt"
    };
    for (const char *dp : dict_paths) {
        load_peter_hashes(dp);
        load_npc_hashes(dp);
        if (!s_peter_hashes.empty()) {
            sp_log("[Subtitles] Loaded %zu Peter/Spidey hashes and %zu NPC ambient hashes from %s",
                   s_peter_hashes.size(), s_npc_hashes.size(), dp);
            break;
        }
    }

    // Try loading audio_transcripts.json
    const char *paths[] = {
        "audio_transcripts.json",
        "./audio_transcripts.json",
        "data/audio_transcripts.json"
    };

    for (const char *path : paths) {
        load_transcripts_from_file(path);
        if (!s_subtitles.empty()) {
            sp_log("[Subtitles] Successfully loaded %s: %zu voice lines, %zu cutscenes cached.",
                   path, s_subtitles.size(), s_cutscenes.size());
            break;
        }
    }

    if (s_subtitles.empty()) {
        sp_log("[Subtitles] WARNING: audio_transcripts.json not found or empty in game directory!");
    }
}

void on_sound_triggered(uint32_t sound_hash, uint32_t caller) {
    if (!s_enabled || s_subtitles.empty() || sound_hash == 0) return;

    // Silently drop NPC/citizen ambient voices (CIT_ prefix in sound dictionary).
    // Peter's own voice always bypasses this filter even if the sound name is CIT_ prefixed
    // (e.g. CIT_SPI_* combat monologues are Spider-Man, not random pedestrian chatter).
    bool is_peter = (s_peter_hashes.find(sound_hash) != s_peter_hashes.end());
    if (!is_peter && s_npc_hashes.count(sound_hash)) return;

    auto it = s_subtitles.find(sound_hash);
    if (it == s_subtitles.end() || it->second.empty()) return;

    // Check if this sound is already active in the queue
    for (auto &act : s_active) {
        if (act.sound_hash == sound_hash) {
            act.timer = act.total_duration;
            return;
        }
    }

    // Duration: 2.0s base + 0.055s per char
    float duration = 2.0f + (float)it->second.length() * 0.055f;
    if (is_peter) {
        if (duration < 3.5f) duration = 3.5f;
        if (duration > 8.0f) duration = 8.0f;
    } else {
        if (duration < 2.5f) duration = 2.5f;
        if (duration > 6.0f) duration = 6.0f;
    }

    ActiveSubtitle entry;
    entry.sound_hash = sound_hash;
    entry.lines = wrap_text(it->second, 48);
    entry.timer = duration;
    entry.total_duration = duration;
    entry.is_peter = is_peter;

    // Multi-sound queue management (max 2 active dialogue items)
    const size_t MAX_CONCURRENT = 2;
    if (s_active.size() >= MAX_CONCURRENT) {
        if (is_peter) {
            // Peter ALWAYS gets in: remove first non-Peter line to make room
            int discard_idx = -1;
            for (size_t i = 0; i < s_active.size(); ++i) {
                if (!s_active[i].is_peter) {
                    discard_idx = (int)i;
                    break;
                }
            }
            if (discard_idx != -1) {
                s_active.erase(s_active.begin() + discard_idx);
            } else {
                s_active.erase(s_active.begin());
            }
        } else {
            // Non-Peter line: drop first non-Peter line if exists
            int discard_idx = -1;
            for (size_t i = 0; i < s_active.size(); ++i) {
                if (!s_active[i].is_peter) {
                    discard_idx = (int)i;
                    break;
                }
            }
            if (discard_idx != -1) {
                s_active.erase(s_active.begin() + discard_idx);
            } else {
                // All active entries are Peter! Protect Peter unless nearly finished (<0.8s)
                if (s_active[0].timer < 0.8f) {
                    s_active.erase(s_active.begin());
                } else {
                    return; // Drop NPC shout to protect Peter's active dialogue
                }
            }
        }
    }

    s_active.push_back(entry);
    sp_log("[Subtitles] 0x%08X (Peter=%d, caller=0x%08X): \"%s\" (%.1fs)", sound_hash, is_peter, caller, it->second.c_str(), duration);
}

void render() {
    if (!s_enabled) return;

    // Update timers
    DWORD now = GetTickCount();
    if (s_last_tick != 0) {
        float dt = (float)(now - s_last_tick) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;

        for (auto it = s_active.begin(); it != s_active.end(); ) {
            it->timer -= dt;
            if (it->timer <= 0.0f) {
                it = s_active.erase(it);
            } else {
                ++it;
            }
        }
    }
    s_last_tick = now;

    if (s_active.empty()) return;

    nglFont *font = nullptr;
    float font_scale = 1.0f;

    // Use the iconic Ultimate Spider-Man font: i_upupandaway
    font = g_femanager.GetFont(static_cast<font_index>(1));
    if (font == nullptr) {
        tlFixedString font_name{"i_upupandaway"};
        font = nglLoadFont(font_name);
    }

    if (font != nullptr) {
        font_scale = 0.72f; // Scaled down comic font as requested
    } else {
        font = nglSysFont();
        font_scale = 1.0f;
    }

    if (font == nullptr) return;

    // Measure font line height with scale
    int sample_w = 0, sample_h = 0;
    get_text_dimensions(font, "M", &sample_w, &sample_h, font_scale, font_scale);
    float line_h = (sample_h > 0) ? (float)sample_h + 3.0f : (20.0f * font_scale);

    const float gap_between_subs = 6.0f;
    const float base_bottom_y = 452.0f; // Screen bottom margin (closer to bottom of 480p screen)
    const uint32_t white_color = 0xFFFFFFFF;
    const uint32_t black_color = 0xFF000000;
    const float s_off = 1.5f; // Crisp pixel outline
    const float z_depth = -9999.0f; // Top-most visual layer (guaranteed depth = 0.0 in front of all 3D/2D geometry)

    // Vertical layout:
    // s_active[0] (earlier sound) is rendered at the bottom.
    // s_active[1] (newer sound, "sonra gelen ses") is stacked ABOVE s_active[0].
    float sub_bottom_y[2] = { base_bottom_y, 0.0f };

    float sub0_lines_count = (float)s_active[0].lines.size();
    float sub0_top_y = sub_bottom_y[0] - (sub0_lines_count - 1.0f) * line_h;

    if (s_active.size() > 1) {
        sub_bottom_y[1] = sub0_top_y - line_h - gap_between_subs;
    }

    // Render active subtitles
    for (size_t sub_idx = 0; sub_idx < s_active.size() && sub_idx < 2; ++sub_idx) {
        const auto &entry = s_active[sub_idx];
        const size_t num_lines = entry.lines.size();

        for (size_t i = 0; i < num_lines; ++i) {
            const std::string &line = entry.lines[i];
            if (line.empty()) continue;

            float draw_y = sub_bottom_y[sub_idx] - (float)(num_lines - 1 - i) * line_h;

            int text_w = 0, text_h = 0;
            get_text_dimensions(font, line.c_str(), &text_w, &text_h, font_scale, font_scale);
            if (text_w <= 0) text_w = (int)((float)line.length() * 10.0f * font_scale);

            // Centered horizontally on 640-pixel screen
            float draw_x = (640.0f - (float)text_w) * 0.5f;
            if (draw_x < 20.0f) draw_x = 20.0f;

            // 8-directional black outline shadow for sharp readability
            nglListAddString(font, draw_x + s_off, draw_y, z_depth, black_color, font_scale, font_scale, "%s", line.c_str());
            nglListAddString(font, draw_x - s_off, draw_y, z_depth, black_color, font_scale, font_scale, "%s", line.c_str());
            nglListAddString(font, draw_x, draw_y + s_off, z_depth, black_color, font_scale, font_scale, "%s", line.c_str());
            nglListAddString(font, draw_x, draw_y - s_off, z_depth, black_color, font_scale, font_scale, "%s", line.c_str());
            nglListAddString(font, draw_x + s_off, draw_y + s_off, z_depth, black_color, font_scale, font_scale, "%s", line.c_str());
            nglListAddString(font, draw_x - s_off, draw_y + s_off, z_depth, black_color, font_scale, font_scale, "%s", line.c_str());
            nglListAddString(font, draw_x + s_off, draw_y - s_off, z_depth, black_color, font_scale, font_scale, "%s", line.c_str());
            nglListAddString(font, draw_x - s_off, draw_y - s_off, z_depth, black_color, font_scale, font_scale, "%s", line.c_str());

            // Pure white text
            nglListAddString(font, draw_x, draw_y, z_depth, white_color, font_scale, font_scale, "%s", line.c_str());
        }
    }
}

} // namespace subtitle_manager

// Hook to capture sound stream plays
static int __cdecl hook_play_stream(uint32_t sound_hash) {
    if (subtitle_manager::is_enabled()) {
        // Read caller of 0x00520490 from stack
        // In 0x00520490, [esp + 0x14] before push ebx holds the caller's return address.
        // Inside hook_play_stream, (&sound_hash)[5] corresponds exactly to that return address.
        uint32_t caller = (&sound_hash)[5];

        // Specific non-dialogue caller addresses in USM.exe:
        // 0x004BBBDC: 0x004BBD10 (ambient pedestrian / entity running & screaming voices)
        // 0x00557CBB: 0x00557B80 (UI button confirm sounds)
        // 0x00558E50, 0x00559013: 0x00558E20 / 0x00558FE0 (Music playback streams)
        bool is_npc_ambient = (caller == 0x004BBBDC);
        bool is_music = (caller == 0x00558E50 || caller == 0x00559013);
        bool is_ui = (caller == 0x00557CBB);

        if (!is_npc_ambient && !is_music && !is_ui) {
            subtitle_manager::on_sound_triggered(sound_hash, caller);
        } else {
            sp_log("[Subtitles] Filtered out sound 0x%08X (caller=0x%08X, npc=%d, music=%d, ui=%d)",
                   sound_hash, caller, is_npc_ambient, is_music, is_ui);
        }
    }
    return CDECL_CALL(0x00798190, sound_hash);
}

void subtitle_manager_patch() {
    subtitle_manager::init();

    if (subtitle_manager::is_enabled()) {
        REDIRECT(0x005204B3, hook_play_stream);
        REDIRECT(0x005204ED, hook_play_stream);
        REDIRECT(0x00798EC5, hook_play_stream);
        sp_log("[Subtitles] Sound stream hooks installed successfully.");
    }
}
