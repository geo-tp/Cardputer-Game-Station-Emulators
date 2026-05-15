#include "ws_save.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "share/game_save.h"
#include "share/emu_log_cpp.h"

#ifdef WS_LOGS_ENABLED
#define WS_LOG(...) EMU_LOG(__VA_ARGS__)
#else
#define WS_LOG(...) ((void)0)
#endif

extern "C" {
// Core WS 
extern int RAMSize;
extern int RAMBanks;
extern unsigned char** RAMMap;
extern unsigned char* MemDummy;
extern int CartKind;
extern int WsSramBackingActive(void);
extern int WsSramBackingDirty(void);
extern void WsSramBackingClearDirty(void);
extern unsigned int WsSramBackingDirtyPages(void);
extern void WsSramBackingClearDirtyPages(unsigned int mask);
extern unsigned char WsSramBackingRead(int offset);
extern void WsSramBackingWrite(int offset, unsigned char value);
}

#ifndef CK_EEP
#define CK_EEP 0x01
#endif

#define WS_SAVE_DIR "/sd/ws_saves"
#define WS_SAVE_PAGE_SIZE 2048

static uint8_t*     g_sram        = nullptr;   // pointer vers RAMMap
static size_t       g_sram_len    = 0;         // = RAMSize
static bool         g_sram_backed = false;     // SD/cache backed SRAM fallback
static char*        g_save_path   = nullptr;
static uint32_t     g_crc_last    = 0;
static TickType_t   g_next_check  = 0;
static bool         g_save_dirty  = false;

// ====================== FS utils ======================
static void ensure_dir(){ mkdir(WS_SAVE_DIR, 0777); }

static void make_save_path(const char* romPathOrName){
  ensure_dir();

  const char* base = share::gameSaveBasename(romPathOrName);
  char name[160] = {0};

  if (base && *base){
    strncpy(name, base, sizeof(name)-1);
    size_t L = strlen(name);
    if (L >= 4) { name[L-3]='s'; name[L-2]='a'; name[L-1]='v'; }
    else strncat(name, ".sav", sizeof(name)-strlen(name)-1);
  } else {
    strcpy(name, "ws_autosave.sav");
  }

  int n = snprintf(g_save_path, PATH_MAX, WS_SAVE_DIR "/%s", name);
  if (n < 0 || (size_t)n >= PATH_MAX) {
    g_save_path[PATH_MAX-1] = '\0';
  }
}

// ====================== I/O ======================
static bool ensure_file_size(FILE* f, size_t len){
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) return false;
  long cur = ftell(f);
  if (cur < 0) return false;
  if ((size_t)cur >= len) return true;

  const size_t CHUNK = 512;
  uint8_t buf[CHUNK];
  memset(buf, 0xFF, sizeof(buf));
  size_t remaining = len - (size_t)cur;
  while (remaining > 0) {
    size_t n = remaining < CHUNK ? remaining : CHUNK;
    if (fwrite(buf, 1, n, f) != n) return false;
    remaining -= n;
    taskYIELD();
  }
  return true;
}

static bool flush_backed_dirty_pages(){
  unsigned int dirtyMask = WsSramBackingDirtyPages();
  if (!dirtyMask && (WsSramBackingDirty() || g_save_dirty)) {
    const unsigned int pages = (unsigned int)((g_sram_len + WS_SAVE_PAGE_SIZE - 1) / WS_SAVE_PAGE_SIZE);
    dirtyMask = (pages >= 32) ? 0xFFFFFFFFu : ((1u << pages) - 1u);
  }
  if (!dirtyMask && !g_save_dirty) return true;

  FILE* f = fopen(g_save_path, "r+b");
  if (!f) f = fopen(g_save_path, "w+b");
  if (!f) return false;
  if (!ensure_file_size(f, g_sram_len)) {
    fclose(f);
    return false;
  }

  uint8_t buf[512];
  unsigned int pagesWritten = 0;
  size_t bytesWritten = 0;
  const unsigned int pages = (unsigned int)((g_sram_len + WS_SAVE_PAGE_SIZE - 1) / WS_SAVE_PAGE_SIZE);
  for (unsigned int page = 0; page < pages && page < 32; ++page) {
    if ((dirtyMask & (1u << page)) == 0) continue;
    size_t pageOffset = (size_t)page * WS_SAVE_PAGE_SIZE;
    size_t pageLen = g_sram_len - pageOffset;
    if (pageLen > WS_SAVE_PAGE_SIZE) pageLen = WS_SAVE_PAGE_SIZE;
    if (fseek(f, (long)pageOffset, SEEK_SET) != 0) {
      fclose(f);
      return false;
    }
    size_t done = 0;
    while (done < pageLen) {
      size_t n = pageLen - done;
      if (n > sizeof(buf)) n = sizeof(buf);
      for (size_t i = 0; i < n; ++i) {
        buf[i] = WsSramBackingRead((int)(pageOffset + done + i));
      }
      if (fwrite(buf, 1, n, f) != n) {
        fclose(f);
        WS_LOG("[WS][SAVE] write error saving %s\n", g_save_path);
        return false;
      }
      done += n;
      bytesWritten += n;
      taskYIELD();
    }
    pagesWritten++;
  }

  fflush(f);
  fsync(fileno(f));
  fclose(f);

  WsSramBackingClearDirtyPages(dirtyMask);
  g_save_dirty = WsSramBackingDirty() != 0;
  WS_LOG("[WS][SAVE] wrote %u dirty pages/%u bytes -> %s\n",
         pagesWritten, (unsigned)bytesWritten, g_save_path);
  return true;
}

static bool flush_now(){
  if ((!g_sram && !g_sram_backed) || !g_sram_len) return false;
  if (!share::gameSaveEnsureParentReady(WS_SAVE_DIR)) return false;
  if (g_sram_backed) return flush_backed_dirty_pages();

  uint32_t crc = share::gameSaveCrc32Update(0, g_sram, g_sram_len);
  if (!g_save_dirty && crc == g_crc_last) return true;
  if (!g_sram_backed && share::gameSaveIsTrivialSram(g_sram, g_sram_len)) return false;

  FILE* f = fopen(g_save_path, "r+b");
  if (!f) f = fopen(g_save_path, "w+b");
  if (!f) {
    return false;
  }

  const size_t CHUNK = 512;
  size_t remaining = g_sram_len;
  size_t offset = 0;
  uint8_t buf[CHUNK];

  while (remaining > 0) {
    size_t n = remaining < CHUNK ? remaining : CHUNK;
    const uint8_t* src = g_sram ? (g_sram + offset) : buf;
    if (g_sram_backed) {
      for (size_t i = 0; i < n; ++i) {
        buf[i] = WsSramBackingRead((int)(offset + i));
      }
    }
    size_t w = fwrite(src, 1, n, f);
    if (w != n) {
      // erreur write
      fclose(f);
      WS_LOG("[WS][SAVE] write error saving %s\n", g_save_path);
      return false;
    }
    offset    += n;
    remaining -= n;
    taskYIELD();
  }

  fflush(f);
  fsync(fileno(f));
  fclose(f);

  WS_LOG("[WS][SAVE] wrote %u bytes -> %s\n",
         (unsigned)g_sram_len, g_save_path);

  g_crc_last = crc;
  g_save_dirty = false;
  return true;
}

// ====================== API ======================
void ws_save_init(const char* romPathOrName){
  bool is_eep  = (CartKind & CK_EEP) != 0;

  // EEP : 128 / 1024 / 2048 octets
  // SRAM : entre 1 et 32 Ko
  #ifdef WS_SAVE_ENABLED
  bool ok_size = is_eep
      ? (RAMSize == 0x80 || RAMSize == 0x400 || RAMSize == 0x800)
      : (RAMSize > 0 && RAMSize <= 0x8000); // max 32KB SRAM
  #else
  bool ok_size = false;
  #endif

  const bool has_sram_ptr = RAMMap && RAMMap[0] && RAMMap[0] != MemDummy;
  const bool has_sram_backing = !is_eep && ok_size && WsSramBackingActive();

  if (!ok_size || RAMBanks < 1 || (!has_sram_ptr && !has_sram_backing)) {
    WS_LOG("[WS][SAVE] ignored (size=%d, banks=%d, kind=%s)\n",
           RAMSize, RAMBanks, is_eep ? "EEP" : "SRAM");
    g_sram      = nullptr;
    g_sram_backed = false;
    g_sram_len  = 0;
    g_crc_last  = 0;
    g_next_check = 0;
    g_save_dirty = false;
    return;
  }

  if (!g_save_path) {
    g_save_path = (char*)malloc(PATH_MAX);
    if (!g_save_path) {
      WS_LOG("[WS][SAVE] ignored (OOM on path alloc)\n");
      g_sram      = nullptr;
      g_sram_backed = false;
      g_sram_len  = 0;
      g_crc_last  = 0;
      g_next_check = 0;
      g_save_dirty = false;
      return;
    }
  }

  g_save_path[0] = '\0';
  g_sram         = has_sram_ptr ? RAMMap[0] : nullptr;
  g_sram_backed  = !has_sram_ptr && has_sram_backing;
  g_sram_len     = (size_t)RAMSize;

  // Construit chemin
  make_save_path(romPathOrName);

  // CRC initial
  g_crc_last    = g_sram_backed ? 0 : share::gameSaveCrc32Update(0, g_sram, g_sram_len);
  g_next_check  = 0;
  g_save_dirty  = false;
  if (g_sram_backed) WsSramBackingClearDirty();

  WS_LOG("[WS][SAVE] path=%s len=%u (%s)\n",
         g_save_path,
         (unsigned)g_sram_len,
         is_eep ? "EEP" : "SRAM");
}

void ws_save_load(void){
  if ((!g_sram && !g_sram_backed) || !g_sram_len) return;
  if (!share::gameSaveEnsureParentReady(WS_SAVE_DIR)) {
    WS_LOG("[WS][SAVE] skip load (storage not ready)\n");
    return;
  }

  if (g_sram) memset(g_sram, 0xFF, g_sram_len);

  FILE* f = fopen(g_save_path, "rb");
  if (!f){
    // .sav manquant
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_save_path);

    // Tenter le rename
    if (rename(tmp_path, g_save_path) == 0) {
      WS_LOG("[WS][SAVE] promoted temp -> sav: %s\n", g_save_path);
      f = fopen(g_save_path, "rb"); // rouvre le .sav
    } else {
      // lire .tmp
      f = fopen(tmp_path, "rb");
      if (f) {
        WS_LOG("[WS][SAVE] loading from temp (rename failed)\n");
      } else {
        WS_LOG("[WS][SAVE] no save and no temp: %s\n", g_save_path);
        return;
      }
    }
  }

  const size_t CHUNK = 512;
  size_t remaining = g_sram_len;
  size_t offset = 0;
  uint8_t* dst = g_sram;
  uint8_t buf[CHUNK];

  while (remaining > 0) {
    size_t want = remaining < CHUNK ? remaining : CHUNK;
    size_t got  = fread(g_sram_backed ? buf : dst, 1, want, f);
    if (got == 0) break;
    if (g_sram_backed) {
      for (size_t i = 0; i < got; ++i) {
        WsSramBackingWrite((int)(offset + i), buf[i]);
      }
    } else {
      dst += got;
    }
    offset    += got;
    remaining -= got;
    taskYIELD();
  }
  fclose(f);

  if (remaining && g_sram) memset(dst, 0xFF, remaining);

  g_crc_last = g_sram_backed ? 0 : share::gameSaveCrc32Update(0, g_sram, g_sram_len);
  if (g_sram_backed) WsSramBackingClearDirty();
  WS_LOG("[WS][SAVE] loaded %u/%u from %s\n",
         (unsigned)(g_sram_len - remaining), (unsigned)g_sram_len, g_save_path);
}

void ws_save_tick(void){
  if ((!g_sram && !g_sram_backed) || !g_sram_len) return;
  TickType_t now = xTaskGetTickCount();
  if (now < g_next_check) return;
  g_next_check = now + pdMS_TO_TICKS(CHECK_MS);

  if (g_sram_backed) {
    if (WsSramBackingDirty()) g_save_dirty = true;
    return;
  }

  uint32_t crc = share::gameSaveCrc32Update(0, g_sram, g_sram_len);
  if (crc != g_crc_last && !share::gameSaveIsTrivialSram(g_sram, g_sram_len)) {
    g_save_dirty = true;
  }
}

void ws_save_request_flush(void){
  if (!g_sram && !g_sram_backed) return;
  g_save_dirty = true;
}

void ws_save_force_flush(void){
  if (!g_sram && !g_sram_backed) return;
  if (g_sram_backed && WsSramBackingDirty()) g_save_dirty = true;
  if (!g_save_dirty && !g_sram_backed) {
    uint32_t crc = share::gameSaveCrc32Update(0, g_sram, g_sram_len);
    g_save_dirty = (crc != g_crc_last);
  }
  if (!g_save_dirty && !WsSramBackingDirty()) return;

  share::setGameIsSaving(true);
  bool ok = flush_now();
  share::setGameIsSaving(false);
  if (!ok) {
    WS_LOG("[WS][SAVE] final save failed, will retry if requested\n");
  }
}
