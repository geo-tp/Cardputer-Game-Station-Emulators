#include "game_save.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
    // Indicates if a game save operation is in progress
    std::atomic<bool> g_gameIsSaving{false};

    // CRC32 table
    static uint32_t* s_crc32_tbl = nullptr;  // 256 * 4
    static int       s_crc32_ok  = 0;

    static void crc32_init_once()
    {
        if (s_crc32_ok != 0) return;

        uint32_t* T = (uint32_t*)std::malloc(256 * sizeof(uint32_t));
        if (!T) {
            s_crc32_ok = -1;
            return;
        }

        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            T[i] = c;
        }

        s_crc32_tbl = T;
        s_crc32_ok  = 1;
    }
} // namespace

namespace share
{
    bool gameIsSaving()
    {
        return g_gameIsSaving.load(std::memory_order_relaxed);
    }

    void setGameIsSaving(bool isSaving)
    {
        g_gameIsSaving.store(isSaving, std::memory_order_relaxed);
    }

    const char* gameSaveBasename(const char* path)
    {
        if (!path) return nullptr;

        const char* s = std::strrchr(path, '/');
        if (s) return s + 1;

        s = std::strrchr(path, '\\');
        if (s) return s + 1;

        return path;
    }

    bool gameSaveBuildPath(
        char* dst,
        size_t dstLen,
        const char* saveDir,
        const char* romPathOrName,
        const char* defaultName
    )
    {
        if (!dst || dstLen == 0 || !saveDir) {
            return false;
        }

        ::mkdir(saveDir, 0777);

        const char* base = gameSaveBasename(romPathOrName);
        char name[160] = {0};

        if (base && *base) {
            std::strncpy(name, base, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        } else if (defaultName && *defaultName) {
            std::strncpy(name, defaultName, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        } else {
            // Fallback 
            std::strncpy(name, "autosave.sav", sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        }

        size_t L = std::strlen(name);
        if (L >= 4) {
            name[L - 3] = 's';
            name[L - 2] = 'a';
            name[L - 1] = 'v';
        } else {
            const char* ext = ".sav";
            size_t extLen = std::strlen(ext);
            size_t curLen = std::strlen(name);
            size_t space  = sizeof(name) - 1 - curLen;
            if (space > 0) {
                size_t toCopy = (extLen < space) ? extLen : space;
                std::memcpy(name + curLen, ext, toCopy);
                name[curLen + toCopy] = '\0';
            }
        }

        int n = std::snprintf(dst, dstLen, "%s/%s", saveDir, name);
        if (n < 0 || static_cast<size_t>(n) >= dstLen) {
            dst[dstLen - 1] = '\0';
            return false;
        }

        return true;
    }

    bool gameSaveEnsureParentReady(const char* baseDir)
    {
        if (!baseDir) return false;

        struct stat st;

        if (::stat("/sd", &st) != 0) {
            return false;
        }

        if (::stat(baseDir, &st) != 0) {
            if (::mkdir(baseDir, 0777) != 0) {
                return false;
            }
        }

        return true;
    }

    bool gameSaveIsTrivialSram(const uint8_t* p, size_t n)
    {
        if (!p || n == 0) return true;

        // Test tout 0xFF
        size_t i = 0;
        for (; i < n; ++i) {
            if (p[i] != 0xFF) break;
        }
        if (i == n) return true;

        // Test tout 0x00
        for (i = 0; i < n; ++i) {
            if (p[i] != 0x00) break;
        }
        if (i == n) return true;

        return false;
    }

    uint32_t gameSaveCrc32Update(uint32_t crc, const uint8_t* buf, size_t len)
    {
        if (!buf || len == 0) return crc;

        if (s_crc32_ok == 0) {
            crc32_init_once();
        }

        crc ^= 0xFFFFFFFFu;

        if (s_crc32_ok > 0 && s_crc32_tbl) {
            const uint32_t* T = s_crc32_tbl;
            for (size_t i = 0; i < len; ++i) {
                crc = T[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
            }
        } else {
            // Fallback bitwise
            for (size_t i = 0; i < len; ++i) {
                uint32_t c = crc ^ buf[i];
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                crc = c ^ (crc >> 8);
            }
        }

        return crc ^ 0xFFFFFFFFu;
    }

} // namespace share
