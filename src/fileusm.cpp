#include "fileusm.h"

#include "common.h"
#include "func_wrapper.h"
#include "log.h"
#include "utility.h"
#include "variables.h"

#include <cassert>
#include <cstdio>
#include <cstring>

VALIDATE_SIZE(FileUSM, 0x10);

Var<FileUSM *> g_fileUSM{0x0096190C};

void sub_81C5D0(char *a1, char *a2) {
    char v3;
    auto *v2 = a2;
    if (*a2) {
        do {
            if (*v2 == '.') {
                break;
            }

            v3 = (v2++)[1];
        } while (v3);
    }

    auto *v4 = a1;
    *v2 = '.';
    int v5 = v2 + 1 - a1;

    char v6;
    do {
        v6 = *v4;
        v4[v5] = *v4;
        ++v4;
    } while (v6);
}

void sub_81D0B0(char *a1, char *a2, int a3) {
    char *v3 = a1;
    if (a1 != nullptr) {
        if (a2 != nullptr) {
            char *v4 = a2;

            if (a3) {
                int v5 = a3;
                do {
                    *v3 ^= *v4;
                    char v6 = v4[1];
                    ++v3;
                    ++v4;
                    if (!v6) {
                        v4 = a2;
                    }

                    --v5;
                } while (v5);
            }
        }
    }
}

char *get_msg(FileUSM *a1, const char *a2) {
    char *result = nullptr;
    if constexpr (1) {
        if (a1 != nullptr) {
            result = a1->sub_81C4C0(a2);

            //sp_log("get_msg() = %s", result);
        }
    } else {
        result = (char *) CDECL_CALL(0x0081C580, a1, a2);
    }

    //sp_log("%s %s", result, a2);

    return result;
}

static void convert_buffer_utf8_to_cp1254(char *buf, int &len) {
    if (!buf || len <= 0) return;
    int src_i = 0;
    int dst_i = 0;
    const unsigned char *s = reinterpret_cast<const unsigned char *>(buf);
    while (src_i < len) {
        unsigned char c = s[src_i];
        if (c < 0x80) {
            buf[dst_i++] = c;
            src_i++;
        } else if (c == 0xC3 && src_i + 1 < len) {
            unsigned char c2 = s[src_i + 1];
            buf[dst_i++] = static_cast<char>(0x40 + c2);
            src_i += 2;
        } else if (c == 0xC4 && src_i + 1 < len) {
            unsigned char c2 = s[src_i + 1];
            if (c2 == 0x9E) buf[dst_i++] = static_cast<char>(0xD0); // Ğ
            else if (c2 == 0x9F) buf[dst_i++] = static_cast<char>(0xF0); // ğ
            else if (c2 == 0xB0) buf[dst_i++] = static_cast<char>(0xDD); // İ
            else if (c2 == 0xB1) buf[dst_i++] = static_cast<char>(0xFD); // ı
            else buf[dst_i++] = static_cast<char>(c2);
            src_i += 2;
        } else if (c == 0xC5 && src_i + 1 < len) {
            unsigned char c2 = s[src_i + 1];
            if (c2 == 0x90) buf[dst_i++] = static_cast<char>(0xDD); // İ
            else if (c2 == 0x9E) buf[dst_i++] = static_cast<char>(0xDE); // Ş
            else if (c2 == 0x9F) buf[dst_i++] = static_cast<char>(0xFE); // ş
            else buf[dst_i++] = static_cast<char>(c2);
            src_i += 2;
        } else {
            buf[dst_i++] = c;
            src_i++;
        }
    }
    buf[dst_i] = '\0';
    len = dst_i;
}

FileUSM::FileUSM(const char *a2, char *a3) {
    this->field_0 = nullptr;
    this->field_4 = nullptr;
    this->field_8 = 0;
    this->field_C = 0;

    FILE *v4 = nullptr;
    bool is_plain_text = false;

    // Check for mods/ override first, then local plain text files
    const char *candidates[] = {
        "mods/usm_ltr.usm",
        "mods/usm_ltr.txt",
        "usm_ltr.usm",
        "usm_ltr.txt",
        "mods/usm_lte.usm",
        "mods/usm_lte.txt",
        "usm_lte.usm",
        "usm_lte.txt",
        nullptr
    };

    for (int i = 0; candidates[i] != nullptr; ++i) {
        v4 = fopen(candidates[i], "rb");
        if (v4 != nullptr) {
            is_plain_text = true;
            printf("[FileUSM] Loaded plain text language file: '%s'\n", candidates[i]);
            break;
        }
    }

    if (v4 == nullptr) {
        char Filename[260];
        strcpy(Filename, a2);
        if (a3 != nullptr) {
            sub_81C5D0("dat", Filename);
        }
        v4 = fopen(Filename, "rb");
        if (v4 != nullptr) {
            printf("[FileUSM] Loaded default dat file: '%s'\n", Filename);
        }
    }

    FILE *v5 = v4;
    if (v4) {
        fseek(v5, 0, 2);
        int v7 = ftell(v5);
        fseek(v5, 0, 0);
        this->field_8 = v7;
        auto *v8 = static_cast<char *>(malloc(v7 + 2));
        size_t v9 = this->field_8;
        this->field_0 = v8;
        fread(v8, 1u, v9, v5);
        this->field_0[this->field_8] = 0;
        fclose(v5);

        if (!is_plain_text) {
            sub_81D0B0(this->field_0, a3, this->field_8);
        } else {
            // Strip UTF-8 BOM if present
            if (this->field_8 >= 3 &&
                static_cast<unsigned char>(this->field_0[0]) == 0xEF &&
                static_cast<unsigned char>(this->field_0[1]) == 0xBB &&
                static_cast<unsigned char>(this->field_0[2]) == 0xBF) {
                memmove(this->field_0, this->field_0 + 3, this->field_8 - 3);
                this->field_8 -= 3;
                this->field_0[this->field_8] = 0;
            }
            // Auto convert UTF-8 to Windows-1254
            convert_buffer_utf8_to_cp1254(this->field_0, this->field_8);
        }

        char *v12;
        for (int i{0}; i < this->field_8; ++i) {
            v12 = &this->field_0[i];
            if (*v12 == '\n') {
                ++this->field_C;
                *v12 = '\0';
            }

            if (this->field_0[i] == '\r') {
                this->field_0[i] = '\0';
            }
        }

        ++this->field_C;
        this->field_4 = static_cast<char **>(malloc(4 * this->field_C));

        bool v16 = false;
        int j = 0;
        int v22 = 0;

        if (this->field_8 > 0) {
            int v10 = 0;

            do {
                char *v18 = this->field_0;
                char v19 = this->field_0[j];
                int v20;

                if (v19 && (v20 = this->field_8 - 1, j != v20)) {
                    v16 = false;
                    if (v19 == '\\' && j < v20 && v18[j + 1] == 'n') {
                        v18[j] = '\r';
                        this->field_0[j + 1] = '\n';
                    }

                } else {
                    if (!v16) {
                        this->field_4[v22++] = &v18[v10];
                        v16 = true;
                    }

                    v10 = j + 1;
                }

                ++j;
            } while (j < this->field_8);
        }

        this->field_C = v22;
    }
}

char *FileUSM::sub_81C4C0(const char *a2) {
    int str_len = strlen(a2);
    auto *new_string = static_cast<char *>(malloc(str_len + 2));
    //strcpy_s(new_string, str_len, a2);
    strcpy(new_string, a2);

    new_string[str_len] = '=';
    new_string[str_len + 1] = '\0';

    if (this->field_0 != nullptr && this->field_4 != nullptr && this->field_8 > 0 &&
        this->field_C > 0) {
        for (int i = 0; i < this->field_C; ++i) {
            if (strncmp(this->field_4[i], new_string, str_len + 1) == 0) {
                free(new_string);
                auto *result = &this->field_4[i][str_len + 1];

                return result;
            }
        }
    }

    free(new_string);

    return nullptr;
}

//0x0081C7C0
FileUSM *create_usm_file(const char *a1, char *a2) {
    FileUSM *v3 = (FileUSM *) malloc(0x10u);
    if (v3 != nullptr) {
        *v3 = FileUSM{a1, a2};
    } else {
        assert(0);
    }

    if (v3->field_0 && v3->field_4 && v3->field_8 > 0 && v3->field_C > 0) {
        return v3;
    }

    free(v3->field_0);
    free(v3->field_4);
    free(v3);
    return nullptr;
}

void FileUSM_patch() {
    SET_JUMP(0x0081C7C0, create_usm_file);
    SET_JUMP(0x0081C580, get_msg);
    REDIRECT(0x005AC8A9, get_msg);
}
