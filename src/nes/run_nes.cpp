#pragma GCC optimize ("Os")

#include "run_nes.h"

#include <M5Cardputer.h>
#include <stdio.h>
#include <string>

#include <esp_heap_caps.h>

#include "cardputer/CardputerView.h"
#include "cardputer/CardputerInput.h"
#include "share/emu_log_cpp.h"
#include "vfs/rom_xip.h"

#ifdef NES_DIAG_LOGS
static void nes_diag_log_heap(const char *stage)
{
  EMU_LOG("[NES][BOOT] %-12s heap=%u largest8=%u largestInternal=%u\n",
          stage,
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static void nes_diag_log_header(const uint8_t *rom, size_t len)
{
  EMU_LOG("[NES][BOOT] xip=%p len=%u\n", rom, (unsigned)len);
  if (!rom || len < 16) {
    EMU_LOG("[NES][BOOT] header unavailable\n");
    return;
  }

  EMU_LOG("[NES][BOOT] header=");
  for (size_t i = 0; i < 16; ++i) {
    EMU_LOG(" %02X", rom[i]);
  }
  EMU_LOG("\n");

  if (rom[0] == 'N' && rom[1] == 'E' && rom[2] == 'S' && rom[3] == 0x1A) {
    const uint8_t flags6 = rom[6];
    const uint8_t flags7 = rom[7];
    const unsigned mapper = (unsigned)((flags6 >> 4) | (flags7 & 0xF0));
    EMU_LOG("[NES][BOOT] ines prg=%uKB chr=%uKB mapper=%u mirror=%c battery=%u trainer=%u four=%u\n",
            (unsigned)rom[4] * 16U,
            (unsigned)rom[5] * 8U,
            mapper,
            (flags6 & 0x01) ? 'V' : 'H',
            (unsigned)((flags6 & 0x02) != 0),
            (unsigned)((flags6 & 0x04) != 0),
            (unsigned)((flags6 & 0x08) != 0));
  } else {
    EMU_LOG("[NES][BOOT] invalid iNES magic\n");
  }
}
#endif

void run_nes(const char* xipPath)
{
#ifdef NES_DIAG_LOGS
  EMU_LOG("[NES][BOOT] path=%s\n", xipPath ? xipPath : "(null)");
  nes_diag_log_heap("entry");
  nes_diag_log_header(get_rom_ptr(), get_rom_size());
#endif

  CardputerView display;
  display.initialize();

#ifdef NES_DIAG_LOGS
  nes_diag_log_heap("display");
#endif

  // Call the NES core
  // Note: the core expects argv to be the ROM path
  char* argv_[1] = { const_cast<char*>(xipPath) };
  nofrendo_main(1, argv_);

#ifdef NES_DIAG_LOGS
  nes_diag_log_heap("returned");
#endif
}
