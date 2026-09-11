#include "subtitle_manager.h"

#include "ngl.h"
#include "ngl_font.h"
#include "femanager.h"
#include "utility.h"
#include "func_wrapper.h"
#include "cut_scene_player.h"
#include "string_hash.h"

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

// Set to true only while a Bink movie is actively playing (between on_movie_start and on_movie_end).
// Prevents stale Bink frame numbers from a previous or parallel video from corrupting elapsed_time.
static bool s_movie_active = false;
// The last Bink time value accepted, used to detect non-monotonic (stale) updates.
static float s_movie_last_bink_time = -1.0f;

struct SubtitleCue {
    float start_time = 0.0f;
    float end_time = -1.0f;
    std::string text;
    std::vector<std::string> lines;
};

struct TranscriptData {
    std::vector<SubtitleCue> cues;
    bool has_timestamps = false;
    bool is_cutscene = false;
    std::string full_text;
};

// 32-bit sound hash -> parsed transcript data
static std::unordered_map<uint32_t, TranscriptData> s_subtitles;

// Cutscene name -> parsed transcript data (loaded for future expansion, currently inactive)
static std::unordered_map<std::string, TranscriptData> s_cutscenes;

// Set of Peter/Spider-Man sound hashes for priority management
static std::unordered_set<uint32_t> s_peter_hashes;

// Set of NPC/citizen ambient voice hashes (CIT_ prefix) - never shown as subtitles
static std::unordered_set<uint32_t> s_npc_hashes;

struct ActiveSubtitle {
    uint32_t sound_hash = 0;
    int stream_handle = -1;
    TranscriptData transcript;
    float elapsed_time = 0.0f;
    float total_duration = 0.0f;
    bool is_peter = false;
    bool is_cutscene = false;
    bool is_movie_synced = false;
    int frames_alive = 0;
    bool has_started = false;
};

// Queue of currently active subtitles (max 2 concurrent, newer sounds stacked above older)
static std::vector<ActiveSubtitle> s_active;
static DWORD s_last_tick = 0;

// Direct check against game sound engine: returns true only while the stream is actively playing!
static bool is_sound_stream_playing(int stream_handle) {
    if (stream_handle == -1) return false;

    // 0x007982E0: nslIsValidStream(handle) - returns non-zero if stream handle/slot is allocated and valid
    int valid = CDECL_CALL(0x007982E0, stream_handle);
    if (!valid) return false;

    // 0x007983A0: nslIsStreamPlaying(handle) - returns 1 if actively playing, 0 if stopped/finished/paused
    int playing = CDECL_CALL(0x007983A0, stream_handle);
    if (!playing) return false;

    return true;
}

// Query current playback position in seconds directly from the game's audio engine
static float get_sound_stream_time(int stream_handle) {
    if (stream_handle == -1) return -1.0f;

    // 0x00798370: nslGetStreamTime(handle) - returns stream playback time in milliseconds
    int ms = CDECL_CALL(0x00798370, stream_handle);
    if (ms < 0) return -1.0f;
    return (float)ms / 1000.0f;
}

void clear() {
    s_active.clear();
}

void clear_gameplay() {
    for (auto it = s_active.begin(); it != s_active.end(); ) {
        if (!it->is_cutscene) {
            it = s_active.erase(it);
        } else {
            ++it;
        }
    }
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

// Parses a single timestamp string (e.g. "00:00.6", "01:23.5", "00:05", "12.5", "00:04,500")
static bool parse_single_time_str(const std::string &raw, float &out_seconds) {
    size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    size_t last = raw.find_last_not_of(" \t\r\n");
    std::string tag = raw.substr(first, last - first + 1);

    bool has_digit = false;
    for (char c : tag) {
        if (isdigit((unsigned char)c)) {
            has_digit = true;
        } else if (c == ':' || c == '.' || c == ',' || c == ' ' || c == '\t') {
            // allowed punctuation
        } else {
            return false;
        }
    }

    if (!has_digit) return false;

    // Split by colon ':'
    std::vector<std::string> parts;
    std::string cur;
    for (char c : tag) {
        if (c == ':') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(cur);

    if (parts.size() == 1) {
        std::string s = parts[0];
        std::replace(s.begin(), s.end(), ',', '.');
        out_seconds = (float)atof(s.c_str());
        return true;
    } else if (parts.size() == 2) {
        std::string s_min = parts[0];
        std::string s_sec = parts[1];
        std::replace(s_sec.begin(), s_sec.end(), ',', '.');
        float min = (float)atof(s_min.c_str());
        float sec = (float)atof(s_sec.c_str());
        out_seconds = min * 60.0f + sec;
        return true;
    } else if (parts.size() == 3) {
        std::string s_hour = parts[0];
        std::string s_min = parts[1];
        std::string s_sec = parts[2];
        std::replace(s_sec.begin(), s_sec.end(), ',', '.');
        float hour = (float)atof(s_hour.c_str());
        float min = (float)atof(s_min.c_str());
        float sec = (float)atof(s_sec.c_str());
        out_seconds = hour * 3600.0f + min * 60.0f + sec;
        return true;
    }

    return false;
}

// Parses a timestamp tag: supports single "[00:00.6]" or range "[00:00.6 - 00:02.6]", "[00:00.6 -> 00:02.6]"
static bool parse_timestamp_tag(const std::string &raw_tag, float &out_start, float &out_end) {
    out_start = 0.0f;
    out_end = -1.0f;

    // Check for range separators: "->", " - ", or "-" between digits
    size_t sep_pos = std::string::npos;
    size_t sep_len = 0;

    if ((sep_pos = raw_tag.find("->")) != std::string::npos) {
        sep_len = 2;
    } else if ((sep_pos = raw_tag.find(" - ")) != std::string::npos) {
        sep_len = 3;
    } else if ((sep_pos = raw_tag.find(',')) != std::string::npos) {
        // Only treat comma as range separator if colons exist after it (e.g. 00:01, 00:05)
        if (raw_tag.find(':', sep_pos + 1) != std::string::npos) {
            sep_len = 1;
        } else {
            sep_pos = std::string::npos;
        }
    } else if ((sep_pos = raw_tag.find('-')) != std::string::npos && sep_pos > 0 && isdigit((unsigned char)raw_tag[sep_pos - 1])) {
        sep_len = 1;
    }

    if (sep_pos != std::string::npos) {
        std::string part1 = raw_tag.substr(0, sep_pos);
        std::string part2 = raw_tag.substr(sep_pos + sep_len);
        float s = 0.0f, e = 0.0f;
        if (parse_single_time_str(part1, s) && parse_single_time_str(part2, e)) {
            out_start = s;
            out_end = e;
            return true;
        }
    }

    return parse_single_time_str(raw_tag, out_start);
}

// Parses raw transcript string into cues.
// If SRT-style inline tags like [00:00.0] or [00:00.0 - 00:02.5] are present, extracts timestamps and splits lines.
// If no timestamp tags exist, returns 1 cue starting at 0.0s for 100% backward compatibility.
static TranscriptData parse_transcript_data(const std::string &raw) {
    TranscriptData data;
    data.full_text = raw;

    struct RawCue {
        float start_time;
        float end_time;
        std::string text;
    };
    std::vector<RawCue> raw_cues;

    float current_start = 0.0f;
    float current_end = -1.0f;
    std::string current_text;
    bool found_any_timestamp = false;

    size_t i = 0;
    const size_t n = raw.length();

    while (i < n) {
        if (raw[i] == '[') {
            size_t close_bracket = raw.find(']', i + 1);
            if (close_bracket != std::string::npos) {
                std::string tag_content = raw.substr(i + 1, close_bracket - i - 1);
                float parsed_start = 0.0f, parsed_end = -1.0f;
                if (parse_timestamp_tag(tag_content, parsed_start, parsed_end)) {
                    found_any_timestamp = true;

                    // If text was accumulated prior to this tag, save it
                    size_t f_pos = current_text.find_first_not_of(" \t\r\n");
                    if (f_pos != std::string::npos) {
                        size_t l_pos = current_text.find_last_not_of(" \t\r\n");
                        std::string trimmed = current_text.substr(f_pos, l_pos - f_pos + 1);
                        if (!trimmed.empty()) {
                            raw_cues.push_back({ current_start, current_end, trimmed });
                        }
                    }

                    current_start = parsed_start;
                    current_end = parsed_end;
                    current_text.clear();
                    i = close_bracket + 1;
                    continue;
                }
            }
        }

        current_text += raw[i];
        ++i;
    }

    // Save final remaining text
    size_t f_pos = current_text.find_first_not_of(" \t\r\n");
    if (f_pos != std::string::npos) {
        size_t l_pos = current_text.find_last_not_of(" \t\r\n");
        std::string trimmed = current_text.substr(f_pos, l_pos - f_pos + 1);
        if (!trimmed.empty()) {
            raw_cues.push_back({ current_start, current_end, trimmed });
        }
    }

    data.has_timestamps = found_any_timestamp;

    if (!raw_cues.empty()) {
        std::sort(raw_cues.begin(), raw_cues.end(), [](const RawCue &a, const RawCue &b) {
            return a.start_time < b.start_time;
        });

        for (const auto &rc : raw_cues) {
            SubtitleCue cue;
            cue.start_time = rc.start_time;
            cue.end_time = rc.end_time;
            cue.text = rc.text;
            cue.lines = wrap_text(rc.text, 48);
            data.cues.push_back(std::move(cue));
        }
    } else {
        SubtitleCue cue;
        cue.start_time = 0.0f;
        cue.end_time = -1.0f;
        cue.text = raw;
        cue.lines = wrap_text(raw, 48);
        data.cues.push_back(std::move(cue));
    }

    return data;
}

static float calc_cue_duration(const std::string &text, bool /*is_peter*/) {
    if (text.empty()) return 0.0f;
    int words = 1;
    for (char c : text) {
        if (c == ' ' || c == '\t') words++;
    }
    // Natural speaking / reading pace: ~0.36s per word + 0.8s base reading reaction buffer
    float d = 0.8f + (float)words * 0.36f;
    // Character density check (~16 characters per second)
    float char_d = 0.6f + (float)text.length() * 0.055f;
    if (char_d > d) d = char_d;

    if (d < 1.4f) d = 1.4f; // Minimum 1.4s so player has time to read
    if (d > 5.5f) d = 5.5f; // Maximum 5.5s for a single subtitle line
    return d;
}

static const std::vector<std::string> *get_active_lines(const ActiveSubtitle &act) {
    if (act.transcript.cues.empty()) return nullptr;

    if (!act.transcript.has_timestamps) {
        float max_show = 1.8f + (float)act.transcript.full_text.length() * 0.06f;
        if (max_show > 6.0f) max_show = 6.0f;
        if (act.elapsed_time < max_show) {
            return &act.transcript.cues[0].lines;
        }
        return nullptr;
    }

    const auto &cues = act.transcript.cues;
    float t = act.elapsed_time;

    for (size_t i = 0; i < cues.size(); ++i) {
        const auto &cue = cues[i];
        if (t >= cue.start_time) {
            float cue_end = 0.0f;
            if (cue.end_time > cue.start_time) {
                cue_end = cue.end_time;
            } else {
                float dur = calc_cue_duration(cue.text, act.is_peter);
                if (i + 1 < cues.size()) {
                    float next_start = cues[i + 1].start_time;
                    float gap = next_start - cue.start_time;
                    if (gap <= dur + 0.35f) {
                        cue_end = next_start - 0.08f;
                    } else {
                        cue_end = cue.start_time + dur;
                    }
                } else {
                    cue_end = cue.start_time + dur;
                }
            }

            if (t < cue_end) {
                return &cue.lines;
            }
        }
    }

    return nullptr; // In pause / silence between dialogue lines
}

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

        bool is_wbk_cutscene = (wbk_upper.find("IGC") != std::string::npos ||
                                wbk_upper.find("SCENE") != std::string::npos);

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
                        // Unescape all standard JSON escape sequences
                        std::string clean_tr;
                        clean_tr.reserve(transcript.size());
                        for (size_t ci = 0; ci < transcript.size(); ++ci) {
                            if (transcript[ci] == '\\' && ci + 1 < transcript.size()) {
                                char next = transcript[ci + 1];
                                if      (next == 'n')  { clean_tr += '\n'; ++ci; }
                                else if (next == 't')  { clean_tr += '\t'; ++ci; }
                                else if (next == 'r')  { clean_tr += '\r'; ++ci; }
                                else if (next == '\\') { clean_tr += '\\'; ++ci; }
                                else if (next == '"')  { clean_tr += '"';  ++ci; }
                                else { clean_tr += transcript[ci]; }
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
                        TranscriptData tdata = parse_transcript_data(clean_tr);
                        tdata.is_cutscene = is_wbk_cutscene;

                        uint32_t hash = 0;
                        if (key.length() == 8) {
                            char *end_ptr = nullptr;
                            hash = (uint32_t)strtoul(key.c_str(), &end_ptr, 16);
                            if (end_ptr == nullptr || *end_ptr != '\0') {
                                hash = to_hash(key.c_str());
                            }
                        } else {
                            hash = to_hash(key.c_str());
                        }

                        if (hash != 0) {
                            s_subtitles[hash] = tdata;
                        }
                        s_cutscenes[key] = std::move(tdata);
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

void on_sound_triggered(uint32_t sound_hash, uint32_t caller, int stream_handle, uint32_t resolved_hash) {
    if (!s_enabled || s_subtitles.empty() || (sound_hash == 0 && resolved_hash == 0)) return;

    // Find transcript: check sound_hash first, then resolved_hash
    auto it = s_subtitles.find(sound_hash);
    if (it == s_subtitles.end() && resolved_hash != 0) {
        it = s_subtitles.find(resolved_hash);
    }
    if (it == s_subtitles.end() || it->second.cues.empty()) {
        return;
    }

    const TranscriptData &data = it->second;

    bool is_cs = data.is_cutscene || (caller == 0x007418DF);
    if (!is_cs) {
        try {
            cut_scene_player *csp = g_cut_scene_player();
            if (csp != nullptr && csp->is_playing()) {
                is_cs = true;
            }
        } catch (...) {}
    }

    // Check speaker priority
    bool is_peter = (s_peter_hashes.count(sound_hash) > 0) ||
                    (resolved_hash != 0 && s_peter_hashes.count(resolved_hash) > 0);

    // Silently drop NPC/citizen ambient voices (never drop cutscene dialogue or Peter)
    if (!is_cs && !is_peter) {
        if (s_npc_hashes.count(sound_hash) > 0 || (resolved_hash != 0 && s_npc_hashes.count(resolved_hash) > 0)) {
            return;
        }
    }

    // If a cutscene subtitle is currently active, ignore non-cutscene sounds so dialogue is never interrupted
    if (!is_cs) {
        for (const auto &act : s_active) {
            if (act.is_cutscene) {
                return;
            }
        }
    }

    // In cutscenes, clear ALL previous subtitles (gameplay or old cutscene line) so dialogue flows cleanly
    if (is_cs) {
        s_active.clear();
    }

    // Check if this sound is already active in the queue
    for (auto &act : s_active) {
        if (act.sound_hash == it->first) {
            act.stream_handle = stream_handle;
            return;
        }
    }

    float total_duration = 0.0f;
    if (data.has_timestamps && !data.cues.empty()) {
        const auto &last_cue = data.cues.back();
        if (last_cue.end_time > last_cue.start_time) {
            total_duration = last_cue.end_time;
        } else {
            float last_start = last_cue.start_time;
            float last_cue_dur = calc_cue_duration(last_cue.text, is_peter);
            total_duration = last_start + last_cue_dur;
        }
    } else {
        float duration = 1.6f + (float)data.full_text.length() * 0.06f;
        if (is_peter) {
            if (duration < 2.5f) duration = 2.5f;
            if (duration > 7.0f) duration = 7.0f;
        } else {
            if (duration < 2.0f) duration = 2.0f;
            if (duration > 6.0f) duration = 6.0f;
        }
        total_duration = duration;
    }

    ActiveSubtitle entry;
    entry.sound_hash = it->first;
    entry.stream_handle = stream_handle;
    entry.transcript = data;
    entry.elapsed_time = 0.0f;
    entry.total_duration = total_duration;
    entry.is_peter = is_peter;
    entry.is_cutscene = is_cs;
    entry.frames_alive = 0;
    entry.has_started = false;

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
                if (s_active[0].total_duration - s_active[0].elapsed_time < 0.8f) {
                    s_active.erase(s_active.begin());
                } else {
                    return; // Drop NPC shout to protect Peter's active dialogue
                }
            }
        }
    }

    s_active.push_back(std::move(entry));
    sp_log("[Subtitles] 0x%08X (Peter=%d, CS=%d, stream=%d, timed=%d, cues=%zu): \"%s\" (%.1fs)",
           it->first, is_peter, is_cs, stream_handle, data.has_timestamps, data.cues.size(),
           data.cues[0].text.c_str(), total_duration);
}

void render() {
    if (!s_enabled) return;

    // Update timers
    DWORD now = GetTickCount();
    float dt = 0.0f;
    if (s_last_tick != 0) {
        dt = (float)(now - s_last_tick) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        if (dt < 0.0f) dt = 0.0f;
    }
    s_last_tick = now;

    // Advance and manage active subtitle lifecycles
    for (auto it = s_active.begin(); it != s_active.end(); ) {
        it->frames_alive++;

        bool advanced_by_hw = false;

        // 1. Audio stream playback: use nslIsStreamPlaying ONLY to detect stream start/end.
        // DO NOT use nslGetStreamTime to set elapsed_time — stream handles are recycled by the
        // audio pool and return stale positions (e.g. 25500ms) for the previous stream.
        // This caused elapsed_time to instantly jump to [00:25.5] on a fresh subtitle.
        // Since transcripts have [start -> end] timestamps, the dt-based clock below is sufficient.
        if (it->stream_handle != -1) {
            if (is_sound_stream_playing(it->stream_handle)) {
                it->has_started = true;
                // elapsed_time is advanced by the guaranteed dt fallback below.
            } else if (it->has_started) {
                // Audio stream finished playing — erase the subtitle immediately.
                it = s_active.erase(it);
                continue;
            }
        }


        // 2. Movie playback with set_movie_time (Bink frame sync)
        if (it->is_movie_synced) {
            advanced_by_hw = true;
            it->is_movie_synced = false; // Reset for next frame
        }

        // 3. GUARANTEED MONOTONIC FALLBACK: NEVER FREEZE AT 0.0s!
        // If hardware audio is buffering (0ms), or movie hook hasn't run, or stream_handle == -1:
        // ALWAYS advance smoothly with dt!
        if (!advanced_by_hw) {
            it->elapsed_time += dt;
            it->has_started = true;
        }

        if (it->is_cutscene) {
            // Cutscenes: erase if past total duration
            if (it->elapsed_time >= it->total_duration + 1.5f) {
                it = s_active.erase(it);
                continue;
            }
            ++it;
        } else {
            // Duration timeout check for gameplay dialogue
            if (it->elapsed_time >= it->total_duration + 0.3f) {
                it = s_active.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (s_active.empty()) return;

    // Find active lines for up to 2 items
    const std::vector<std::string> *active_lines[2] = { nullptr, nullptr };
    for (size_t sub_idx = 0; sub_idx < s_active.size() && sub_idx < 2; ++sub_idx) {
        active_lines[sub_idx] = get_active_lines(s_active[sub_idx]);
    }

    if (active_lines[0] == nullptr && active_lines[1] == nullptr) {
        return; // Nothing visible at this moment
    }

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
    float sub_bottom_y[2] = { 0.0f, 0.0f };
    if (active_lines[0] != nullptr && active_lines[1] != nullptr) {
        sub_bottom_y[0] = base_bottom_y;
        float sub0_lines_count = (float)active_lines[0]->size();
        float sub0_top_y = sub_bottom_y[0] - (sub0_lines_count - 1.0f) * line_h;
        sub_bottom_y[1] = sub0_top_y - line_h - gap_between_subs;
    } else if (active_lines[0] != nullptr) {
        sub_bottom_y[0] = base_bottom_y;
    } else if (active_lines[1] != nullptr) {
        sub_bottom_y[1] = base_bottom_y;
    }

    // Render active subtitles
    for (size_t sub_idx = 0; sub_idx < 2; ++sub_idx) {
        if (active_lines[sub_idx] == nullptr) continue;

        const auto &lines = *active_lines[sub_idx];
        const size_t num_lines = lines.size();

        for (size_t i = 0; i < num_lines; ++i) {
            const std::string &line = lines[i];
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

void on_movie_start(const char *movie_name) {
    if (!s_enabled || movie_name == nullptr || s_subtitles.empty()) return;

    // Clean filename if path or extension passed
    std::string clean_name = movie_name;
    size_t last_slash = clean_name.find_last_of("\\/");
    if (last_slash != std::string::npos) {
        clean_name = clean_name.substr(last_slash + 1);
    }
    size_t dot = clean_name.rfind('.');
    if (dot != std::string::npos) {
        clean_name = clean_name.substr(0, dot);
    }

    clear();

    // The movie name maps to the cutscene hash via to_hash (e.g. "S01_FaPr_IGC1" -> 0x88DD1E5F)
    uint32_t hash = to_hash(clean_name.c_str());
    auto it = s_subtitles.find(hash);
    if (it == s_subtitles.end()) {
        auto it_cs = s_cutscenes.find(clean_name);
        if (it_cs != s_cutscenes.end()) {
            it = s_subtitles.emplace(hash, it_cs->second).first;
        }
    }

    if (it == s_subtitles.end() || it->second.cues.empty()) {
        sp_log("[Subtitles] Movie \"%s\" (clean: \"%s\", 0x%08X) has no subtitles cached.",
               movie_name, clean_name.c_str(), hash);
        return;
    }

    const TranscriptData &data = it->second;

    float total_duration = 0.0f;
    if (data.has_timestamps && !data.cues.empty()) {
        const auto &last_cue = data.cues.back();
        if (last_cue.end_time > last_cue.start_time) {
            total_duration = last_cue.end_time;
        } else {
            total_duration = last_cue.start_time + calc_cue_duration(last_cue.text, true);
        }
    } else {
        total_duration = 2.0f + (float)data.full_text.length() * 0.065f;
    }

    ActiveSubtitle entry;
    entry.sound_hash = hash;
    entry.stream_handle = -1; // Audio is played directly by Bink video
    entry.transcript = data;
    entry.elapsed_time = 0.0f;
    entry.total_duration = total_duration;
    entry.is_peter = true;
    entry.is_cutscene = true;
    entry.frames_alive = 0;
    entry.has_started = true;

    // Activate movie mode: set_movie_time() now allowed to update elapsed_time.
    // IMPORTANT: Set last_bink_time to 0.0f (NOT -1.0f) so the FIRST Bink frame delta is checked.
    // If Bink reuses the same BINK* handle from a previous video (e.g. G4 documentary ended at
    // frame 750 = 25.0s), the first call would have delta = 25.0 - 0.0 = 25.0s > 2.0s → REJECTED.
    // The subtitle cleanly starts from 0.0s via the dt fallback instead.
    s_movie_active = true;
    s_movie_last_bink_time = 0.0f;

    // Reset tick timer so movie subtitles start cleanly from 0.0s
    s_last_tick = GetTickCount();

    s_active.push_back(std::move(entry));
    sp_log("[Subtitles] Movie \"%s\" (0x%08X) subtitle started! Cues: %zu, Duration: %.1fs",
           clean_name.c_str(), hash, data.cues.size(), total_duration);
}


void on_movie_end() {
    if (!s_enabled) return;
    sp_log("[Subtitles] Movie ended / skipped. Clearing subtitles.");
    s_movie_active = false;
    s_movie_last_bink_time = -1.0f;
    clear();
}

void set_movie_time(float current_time) {
    // GUARD: Only accept Bink time if a movie is actively playing (on_movie_start was called).
    // This prevents stale/leftover Bink frames from logo videos, menus, or other Bink handles
    // from jumping elapsed_time to a random timestamp like 25.5s the instant the real movie begins.
    if (!s_movie_active) return;

    // GUARD: Time must be monotonically increasing and plausible (not a huge jump > 2s per frame).
    // A jump of >2s per Bink frame means the frame counter reset or a different Bink handle is firing.
    if (s_movie_last_bink_time >= 0.0f) {
        float delta = current_time - s_movie_last_bink_time;
        // If time jumped backwards or jumped more than 2 seconds in one frame, it's a stale handle.
        if (delta < -0.5f || delta > 2.0f) {
            sp_log("[Subtitles] set_movie_time: rejected stale Bink time %.3f (last=%.3f, delta=%.3f)",
                   current_time, s_movie_last_bink_time, delta);
            return;
        }
    }
    s_movie_last_bink_time = current_time;

    for (auto &act : s_active) {
        if (act.is_cutscene && act.stream_handle == -1) {
            act.elapsed_time = current_time;
            act.has_started = true;
            act.is_movie_synced = true;
            if (current_time > act.total_duration) {
                act.total_duration = current_time + 1.0f;
            }
        }
    }
}


} // namespace subtitle_manager

struct SoundResult {
    int stream_handle;
    void *alias;
};

// Original 0x00520490:
// SoundResult* __cdecl play_sound_stream_internal(SoundResult *out, uint32_t sound_hash);
static SoundResult* (__cdecl *orig_play_sound_stream)(SoundResult *out, uint32_t sound_hash) =
    (SoundResult* (__cdecl *)(SoundResult*, uint32_t))0x00520490;

// Cutscene audio caller: 0x007418DF inside cut_scene_player::frame_advance
static SoundResult* __cdecl hook_cutscene_sound(SoundResult *out, uint32_t sound_hash) {
    SoundResult *res = orig_play_sound_stream(out, sound_hash);
    if (subtitle_manager::is_enabled() && res != nullptr) {
        int handle = res->stream_handle;
        void *alias = res->alias;
        uint32_t resolved_hash = 0;
        if (alias != nullptr) {
            resolved_hash = *(uint32_t*)((uint8_t*)alias + 4);
        }
        subtitle_manager::on_sound_triggered(sound_hash, 0x007418DF, handle, resolved_hash);
    }
    return res;
}

// Gameplay speech callers: 0x005516A8, 0x005561E6, 0x0060BAC1, 0x005D99CA, 0x004DE236, 0x005296D7
static SoundResult* __cdecl hook_gameplay_speech(SoundResult *out, uint32_t sound_hash) {
    SoundResult *res = orig_play_sound_stream(out, sound_hash);
    if (subtitle_manager::is_enabled() && res != nullptr) {
        int handle = res->stream_handle;
        void *alias = res->alias;
        uint32_t resolved_hash = 0;
        if (alias != nullptr) {
            resolved_hash = *(uint32_t*)((uint8_t*)alias + 4);
        }
        subtitle_manager::on_sound_triggered(sound_hash, 0, handle, resolved_hash);
    }
    return res;
}

// Hook cutscene user skip button to instantly clear subtitles
static void __fastcall hook_cutscene_user_skip(cut_scene_player *self, void *edx, cut_scene *cs) {
    sp_log("[Subtitles] Cutscene skipped by user (0x00742156). Clearing subtitles.");
    subtitle_manager::clear();
    THISCALL(0x00740660, self, cs);
}

// Hook mission restart / abort to clear any lingering subtitles
static void __fastcall hook_mission_abort(cut_scene_player *self, void *edx, cut_scene *cs) {
    sp_log("[Subtitles] Mission abort/restart (0x00568D69). Clearing subtitles.");
    subtitle_manager::clear();
    THISCALL(0x00740660, self, cs);
}

// Hook script stop cutscene to clear any lingering subtitles
static void __fastcall hook_script_stop(cut_scene_player *self, void *edx, cut_scene *cs) {
    sp_log("[Subtitles] Script stop cutscene (0x0064D659). Clearing subtitles.");
    subtitle_manager::clear();
    THISCALL(0x00740660, self, cs);
}

// Hook cutscene play to clear any old gameplay subtitles without wiping cutscenes
static void __fastcall hook_cut_scene_play(cut_scene_player *self, void *edx, cut_scene *cs) {
    subtitle_manager::clear_gameplay();
    THISCALL(0x00742190, self, cs);
}

typedef int32_t (__stdcall *BinkDoFrame_fn)(void *bink);
static BinkDoFrame_fn orig_BinkDoFrame = nullptr;

static int32_t __stdcall hook_BinkDoFrame(void *bink) {
    if (bink != nullptr && subtitle_manager::is_enabled()) {
        uint32_t *hdr = (uint32_t*)bink;
        uint32_t frame_num = hdr[3]; // 0x0C
        uint32_t rate = hdr[4];      // 0x10
        uint32_t scale = hdr[5];     // 0x14
        if (rate > 0 && scale > 0) {
            float fps = (float)rate / (float)scale;
            float current_time = (float)frame_num / fps;
            subtitle_manager::set_movie_time(current_time);
        }
    }
    if (orig_BinkDoFrame != nullptr) {
        return orig_BinkDoFrame(bink);
    }
    return 0;
}

// Hook movie frame present to render subtitles on top of the movie quad
static void hook_movie_frame_send(bool a3) {
    if (subtitle_manager::is_enabled()) {
        subtitle_manager::render();
    }
    CDECL_CALL(0x0076EA10, a3);
}

// Wrapper for load_and_play_movie callers in USM.exe (e.g. Chuck script play_movie)
static bool __cdecl hook_load_and_play_movie(const char *dir, const char *movie_name, bool skippable) {
    if (subtitle_manager::is_enabled() && movie_name != nullptr) {
        subtitle_manager::on_movie_start(movie_name);
    }

    bool res = (bool)CDECL_CALL(0x006299E0, dir, movie_name, skippable);

    if (subtitle_manager::is_enabled()) {
        subtitle_manager::on_movie_end();
    }
    return res;
}

void subtitle_manager_patch() {
    subtitle_manager::init();

    if (subtitle_manager::is_enabled()) {
        // Hook BinkDoFrame IAT (0x0086F51C) to capture exact frame timestamps directly from the Bink engine
        DWORD old_protect;
        if (VirtualProtect((void*)0x0086F51C, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
            orig_BinkDoFrame = *(BinkDoFrame_fn*)0x0086F51C;
            *(void**)0x0086F51C = (void*)hook_BinkDoFrame;
            VirtualProtect((void*)0x0086F51C, sizeof(void*), old_protect, &old_protect);
            sp_log("[Subtitles] Hooked BinkDoFrame IAT (0x0086F51C) successfully.");
        }

        // Intercept cutscene dialogue caller directly inside cut_scene_player
        REDIRECT(0x007418DF, hook_cutscene_sound);

        // Intercept stereo cutscene audio callers
        REDIRECT(0x00558E4B, hook_cutscene_sound);
        REDIRECT(0x0055900E, hook_cutscene_sound);

        // Intercept gameplay dialogue / speech callers
        REDIRECT(0x005516A8, hook_gameplay_speech);
        REDIRECT(0x005561E6, hook_gameplay_speech);
        REDIRECT(0x0060BAC1, hook_gameplay_speech);
        REDIRECT(0x005D99CA, hook_gameplay_speech);
        REDIRECT(0x004DE236, hook_gameplay_speech);
        REDIRECT(0x005296D7, hook_gameplay_speech);

        // Hook cutscene start to clear old gameplay subtitles (protects cutscenes!)
        REDIRECT(0x00670D17, hook_cut_scene_play);
        REDIRECT(0x0074245A, hook_cut_scene_play);

        // Hook explicit cutscene user skip button
        REDIRECT(0x00742156, hook_cutscene_user_skip);

        // Hook mission restart / abort & script cutscene stop
        REDIRECT(0x00568D69, hook_mission_abort);
        REDIRECT(0x0064D659, hook_script_stop);

        // Intercept movie playback callers to trigger video subtitles
        REDIRECT(0x0062DBF8, hook_load_and_play_movie);
        REDIRECT(0x0062DDCE, hook_load_and_play_movie);
        REDIRECT(0x00635D61, hook_load_and_play_movie);
        REDIRECT(0x00635D79, hook_load_and_play_movie);
        REDIRECT(0x00635D91, hook_load_and_play_movie);
        REDIRECT(0x00635DA6, hook_load_and_play_movie);
        REDIRECT(0x00663B7B, hook_load_and_play_movie); // Chuck script play_movie()

        // Hook movie frame presentation loop to render subtitles on top of video
        REDIRECT(0x00629DCA, hook_movie_frame_send);

        sp_log("[Subtitles] Dedicated dialogue, cutscene & movie lifecycle hooks installed successfully.");
    }
}
