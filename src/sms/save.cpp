#pragma GCC optimize ("Os")

#include "save.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <unistd.h>
#include "share/game_save.h"
#include "share/emu_log_cpp.h"

static uint8_t* g_sram = NULL;
static size_t   g_sram_len = 0;
static char*    g_save_path = nullptr;
static uint32_t g_crc_last = 0;
static TickType_t g_next_check = 0;
static TickType_t g_next_allowed_write = 0;
static bool     g_owns_sram = false;
static bool     g_alloc_failed_logged = false;

static void ensure_dir(void)
{
  mkdir("/sd/sms_saves", 0777);
}

static bool flush_now(void)
{
  if (!g_sram || !g_sram_len) return false;
  if (!g_save_path) return false;

  if (share::gameSaveIsTrivialSram(g_sram, g_sram_len)) {
    EMU_LOG("SMS save: skip trivial SRAM, no write.\n");
    return false;
  }

  share::setGameIsSaving(true);

  ensure_dir();

  char tmp_path[PATH_MAX];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_save_path);
  tmp_path[sizeof(tmp_path) - 1] = '\0';

  FILE* f = fopen(tmp_path, "wb");
  if (!f) {
    EMU_LOG("SMS save: fopen tmp fail %s\n", tmp_path);
    share::setGameIsSaving(false);
    return false;
  }
  setvbuf(f, NULL, _IONBF, 0);

  size_t w = 0;
  uint8_t chunk[512];
  while (w < g_sram_len) {
    size_t n = g_sram_len - w;
    if (n > sizeof(chunk)) n = sizeof(chunk);
    memcpy(chunk, g_sram + w, n);
    size_t part = fwrite(chunk, 1, n, f);
    w += part;
    if (part != n) break;
  }
  fflush(f);
  fsync(fileno(f));
  fclose(f);

  if (w != g_sram_len) {
    EMU_LOG("SMS save: short write %u/%u to %s\n",
            (unsigned)w, (unsigned)g_sram_len, tmp_path);
    share::setGameIsSaving(false);
    return false;
  }

  unlink(g_save_path);
  if (rename(tmp_path, g_save_path) != 0) {
    EMU_LOG("SMS save: rename failed %s -> %s\n", tmp_path, g_save_path);
    share::setGameIsSaving(false);
    return false;
  }

  EMU_LOG("SMS save: wrote %u/%u -> %s \n",
          (unsigned)w, (unsigned)g_sram_len, g_save_path);

  share::setGameIsSaving(false);
  return true;
}

void sms_save_prepare(const char* romName, size_t sramLen)
{
  if (!g_save_path) {
    g_save_path = (char*)malloc(PATH_MAX);
    if (!g_save_path) abort();
  }

  g_sram_len = sramLen;
  share::gameSaveBuildPath(g_save_path, PATH_MAX, "/sd/sms_saves", romName, "rom.sms");
  g_crc_last = 0;
  g_next_check = g_next_allowed_write = 0;
  g_alloc_failed_logged = false;
}

uint8_t* sms_save_ensure_sram(void)
{
  if (g_sram) return g_sram;
  if (!g_save_path || !g_sram_len) return nullptr;

  g_sram = (uint8_t*)heap_caps_aligned_alloc(
      32, g_sram_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!g_sram) {
    if (!g_alloc_failed_logged) {
      EMU_LOG("SMS save: SRAM alloc failed (%u bytes), save RAM disabled.\n",
              (unsigned)g_sram_len);
      g_alloc_failed_logged = true;
    }
    return nullptr;
  }

  g_owns_sram = true;
  memset(g_sram, 0xFF, g_sram_len);
  EMU_LOG("SMS save: SRAM allocated lazily: %u bytes\n", (unsigned)g_sram_len);
  sms_save_load();
  return g_sram;
}

void sms_save_init(const char* romName, uint8_t* sramPtr, size_t sramLen)
{
  sms_save_prepare(romName, sramLen);
  g_sram = sramPtr;
  g_owns_sram = false;
  g_crc_last = (g_sram && g_sram_len) ? share::gameSaveCrc32Update(0, g_sram, g_sram_len) : 0;
}

void sms_save_load(void)
{
  if (!g_sram || !g_sram_len) return;
  memset(g_sram, 0xFF, g_sram_len);

  ensure_dir();

  char tmp_path[PATH_MAX];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_save_path);
  tmp_path[sizeof(tmp_path) - 1] = '\0';

  const char* loaded_path = g_save_path;

  FILE* f = fopen(g_save_path, "rb");
  if (!f) {
    f = fopen(tmp_path, "rb");
    if (!f) {
      EMU_LOG("SMS load: no save, %s nor %s\n", g_save_path, tmp_path);
      g_crc_last = share::gameSaveCrc32Update(0, g_sram, g_sram_len);
      return;
    }

    EMU_LOG("SMS load: .sav missing, using tmp %s\n", tmp_path);
    loaded_path = tmp_path;
  }

  size_t n = fread(g_sram, 1, g_sram_len, f);
  fclose(f);

  if (n < g_sram_len)
    memset(g_sram + n, 0xFF, g_sram_len - n);

  g_crc_last = share::gameSaveCrc32Update(0, g_sram, g_sram_len);

  EMU_LOG("SMS load: read %u/%u from %s\n",
          (unsigned)n, (unsigned)g_sram_len, loaded_path);
}

void sms_save_tick(void)
{
  if (!g_sram || !g_sram_len) return;

  TickType_t now = xTaskGetTickCount();
  if (now < g_next_check) return;
  g_next_check = now + pdMS_TO_TICKS(CHECK_MS);

  uint32_t crc = share::gameSaveCrc32Update(0, g_sram, g_sram_len);
  if (crc == g_crc_last) return;

  g_crc_last = crc;
  if (share::gameSaveIsTrivialSram(g_sram, g_sram_len)) {
    EMU_LOG("SMS save: trivial after change, skip write.\n");
    return;
  }

  if (now >= g_next_allowed_write) {
    if (flush_now()) {
      g_next_allowed_write = xTaskGetTickCount() + pdMS_TO_TICKS(GAP_MS);
    } else {
      g_crc_last = 0xFFFFFFFFu;
      EMU_LOG("SMS save: flush failed, will retry on next tick.\n");
    }
  }
}

void sms_save_force_flush(void)
{
  flush_now();
}

void sms_save_shutdown(void)
{
  free(g_save_path);
  g_save_path = nullptr;

  if (g_owns_sram && g_sram) {
    heap_caps_free(g_sram);
  }

  g_sram = nullptr;
  g_sram_len = 0;
  g_crc_last = 0;
  g_owns_sram = false;
  g_alloc_failed_logged = false;
}
