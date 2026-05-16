#include <Arduino.h> 
#include <string.h>
#include <M5Cardputer.h>
#include "run_ngp.h"
#include "../race/race-memory.h"
#include "../race/types.h"
#include "../race/tlcs900h.h"
#include "../race/input.h"
#include "../race/flash.h"
#include "ngc_sound.h"
#include "ngc_input.h"
#include "ngc_display.h"
#include "ngc_scheduler.h"
#include "ngc_save.h"
// #include "ngc_bios.h"
#include "share/emu_log_cpp.h"

#define NGP_LANG_EN 1
#define NGP_LANG    NGP_LANG_EN  // 0 = JP, 1 = EN
char retro_save_directory[3] = "/";
int tipo_consola = 0;

extern "C" {

int m_bIsActive = 1;
int gfx_hacks = 0;

extern int finscan;
extern "C" unsigned int gen_regsPC;
#define tlcsPc gen_regsPC
extern EMUINFO m_emuInfo;
extern int tipo_consola;
extern uint16_t *totalpalette;
__attribute__((weak)) void tlcs_reset(void) {}

unsigned char *rasterY = 0;
unsigned char  *frame0Pri = 0;
unsigned char  *frame1Pri = 0;
unsigned char  *color_switch = 0;
unsigned char *scanlineY = 0;
unsigned char *scrollSpriteX = 0;
unsigned char *scrollSpriteY = 0;
unsigned char *sprite_palette_numbers = 0;
unsigned char *sprite_table = 0;
unsigned short *patterns = 0;
unsigned char *oowSelect = 0;
unsigned short *oowTable = 0;
unsigned char *wndTopLeftY = 0;
unsigned char *wndSizeY = 0;
unsigned char *wndSizeX = 0;
unsigned char *bgSelect = 0;
unsigned char *bw_palette_table = 0;
unsigned short *palette_table = 0;
unsigned short *bgTable = 0;
unsigned char *wndTopLeftX = 0;
unsigned char *scrollFrontY = 0;
unsigned char *scrollFrontX = 0;
unsigned short *tile_table_front = 0;
unsigned char *scrollBackY = 0;
unsigned short *tile_table_back = 0;
unsigned char *scrollBackX = 0;
unsigned char  *pattern_table = NULL;

void ngpSoundOff(void);
void ngpSoundExecute(void) __attribute__((weak));
void ngpSoundExecute(void) {}
void audio_dac_init(void);
int Cz80_allocate_flag_tables(void);

}

#ifdef NGP_TRACE_LOGS
#define NGP_TRACE(...) EMU_LOG(__VA_ARGS__)

static uint8_t ngp_trace_u8(const unsigned char* p)
{
  return p ? *p : 0xFF;
}

static uint16_t ngp_trace_u16(const unsigned short* p)
{
  return p ? *p : 0xFFFF;
}

static void ngp_trace_heap(const char* stage)
{
  NGP_TRACE("[NGP][HEAP] %-14s heap=%u largest8=%u largestInternal=%u\n",
            stage,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static void ngp_trace_rom(const uint8_t* rom_base, size_t rom_size, const char* rom_name, int machine)
{
  NGP_TRACE("[NGP][BOOT] rom=%s ptr=%p len=%u machine=%d\n",
            rom_name ? rom_name : "(null)", rom_base, (unsigned)rom_size, machine);
  if (rom_base && rom_size >= 0x30) {
    NGP_TRACE("[NGP][BOOT] rom[20..2F]= %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
              rom_base[0x20], rom_base[0x21], rom_base[0x22], rom_base[0x23],
              rom_base[0x24], rom_base[0x25], rom_base[0x26], rom_base[0x27],
              rom_base[0x28], rom_base[0x29], rom_base[0x2A], rom_base[0x2B],
              rom_base[0x2C], rom_base[0x2D], rom_base[0x2E], rom_base[0x2F]);
  }
}

static void ngp_trace_vdp(const char* stage)
{
  const uint8_t lcd = tlcsMemReadB(0x00004000);
  const uint8_t irq = tlcsMemReadB(0x00008000);
  const uint8_t ifr = tlcsMemReadB(0x00008010);
  const uint8_t color = tlcsMemReadB(0x00006F91);

  NGP_TRACE("[NGP][VDP] %-14s PC=%06X LCD=0x%02X IRQ=0x%02X IFR=0x%02X scan=%u raster=%u color=0x%02X finscan=%d machine=%d\n",
            stage, (unsigned)tlcsPc, lcd, irq, ifr,
            ngp_trace_u8(scanlineY), ngp_trace_u8(rasterY), color, finscan, m_emuInfo.machine);
  NGP_TRACE("[NGP][VDP] ptrs sprite=%p pattern=%p front=%p back=%p pal=%p bwPal=%p bg=%p oow=%p draw=%p totalpal=%p\n",
            sprite_table, pattern_table, tile_table_front, tile_table_back,
            palette_table, bw_palette_table, bgTable, oowTable, drawBuffer, totalpalette);
  NGP_TRACE("[NGP][VDP] regs win=(%u,%u %ux%u) scrollF=(%u,%u) scrollB=(%u,%u) scrollS=(%u,%u) bgSel=0x%02X oowSel=0x%02X f0=0x%02X f1=0x%02X\n",
            ngp_trace_u8(wndTopLeftX), ngp_trace_u8(wndTopLeftY),
            ngp_trace_u8(wndSizeX), ngp_trace_u8(wndSizeY),
            ngp_trace_u8(scrollFrontX), ngp_trace_u8(scrollFrontY),
            ngp_trace_u8(scrollBackX), ngp_trace_u8(scrollBackY),
            ngp_trace_u8(scrollSpriteX), ngp_trace_u8(scrollSpriteY),
            ngp_trace_u8(bgSelect), ngp_trace_u8(oowSelect),
            ngp_trace_u8(frame0Pri), ngp_trace_u8(frame1Pri));

  if (palette_table) {
    NGP_TRACE("[NGP][PAL] raw=%04X %04X %04X %04X %04X %04X %04X %04X\n",
              palette_table[0], palette_table[1], palette_table[2], palette_table[3],
              palette_table[4], palette_table[5], palette_table[6], palette_table[7]);
  }
  if (bgTable) {
    NGP_TRACE("[NGP][BG] table=%04X %04X %04X %04X tileF=%04X %04X tileB=%04X %04X pattern=%04X %04X\n",
              bgTable[0], bgTable[1], bgTable[2], bgTable[3],
              ngp_trace_u16(tile_table_front), ngp_trace_u16(tile_table_front ? tile_table_front + 1 : nullptr),
              ngp_trace_u16(tile_table_back), ngp_trace_u16(tile_table_back ? tile_table_back + 1 : nullptr),
              ngp_trace_u16(patterns), ngp_trace_u16(patterns ? patterns + 1 : nullptr));
  }
  if (drawBuffer) {
    NGP_TRACE("[NGP][FB] pix=%04X %04X %04X %04X %04X\n",
              drawBuffer[0], drawBuffer[1], drawBuffer[80],
              drawBuffer[(size_t)76 * 160 + 80], drawBuffer[(size_t)151 * 160 + 159]);
  }
}

static void ngp_trace_frame(unsigned long frame, uint32_t emuUs)
{
  const uint16_t pal0 = palette_table ? palette_table[0] : 0xFFFF;
  const uint16_t pal1 = palette_table ? palette_table[1] : 0xFFFF;
  const uint16_t pix0 = drawBuffer ? drawBuffer[0] : 0xFFFF;
  const uint16_t pix1 = drawBuffer ? drawBuffer[80] : 0xFFFF;
  const uint16_t pix2 = drawBuffer ? drawBuffer[(size_t)76 * 160 + 80] : 0xFFFF;

  NGP_TRACE("[NGP][FRAME %lu] PC=%06X us=%u scan=%u ready=%u painted=%u LCD=%02X IRQ=%02X IFR=%02X win=%u,%u,%u,%u bg=%02X f0=%02X f1=%02X pal=%04X/%04X pix=%04X/%04X/%04X scrollF=%u,%u scrollB=%u,%u\n",
            frame, (unsigned)tlcsPc, (unsigned)emuUs,
            ngp_trace_u8(scanlineY), (unsigned)g_frame_ready, (unsigned)g_frame_counter,
            tlcsMemReadB(0x00004000), tlcsMemReadB(0x00008000), tlcsMemReadB(0x00008010),
            ngp_trace_u8(wndTopLeftX), ngp_trace_u8(wndTopLeftY),
            ngp_trace_u8(wndSizeX), ngp_trace_u8(wndSizeY),
            ngp_trace_u8(bgSelect), ngp_trace_u8(frame0Pri), ngp_trace_u8(frame1Pri),
            pal0, pal1, pix0, pix1, pix2,
            ngp_trace_u8(scrollFrontX), ngp_trace_u8(scrollFrontY),
            ngp_trace_u8(scrollBackX), ngp_trace_u8(scrollBackY));
}
#else
#define NGP_TRACE(...) ((void)0)
static void ngp_trace_heap(const char*) {}
static void ngp_trace_rom(const uint8_t*, size_t, const char*, int) {}
static void ngp_trace_vdp(const char*) {}
#endif

static void set_defaults_after_boot(void)
{
  // Couleur/mono selon ROM
  tlcsMemWriteB(0x00006F91, tlcsMemReadB(0x00200023));
  if (tipo_consola == 1) tlcsMemWriteB(0x00006F91, 0x00); // forcer mono

  // Langue ROM
  tlcsMemWriteB(0x00006F87, (NGP_LANG == NGP_LANG_EN) ? 0x01 : 0x00);

  // IRQ video
  tlcsMemWriteB(0x00004000, tlcsMemReadB(0x00004000) | 0xC0);

  // Bits Power
  tlcsMemWriteB(0x00006F84, 0x40);
  tlcsMemWriteB(0x00006F85, 0x00);
  tlcsMemWriteB(0x00006F86, 0x00);
}

static void map_vdp_tables_full()
{
  sprite_table           = (unsigned char*)  get_address(0x00008800);
  pattern_table          = (unsigned char*)  get_address(0x0000A000);
  patterns               = (unsigned short*) pattern_table;
  tile_table_front       = (unsigned short*) get_address(0x00009000);
  tile_table_back        = (unsigned short*) get_address(0x00009800);
  palette_table          = (unsigned short*) get_address(0x00008200);
  bw_palette_table       = (unsigned char*)  get_address(0x00008100);
  sprite_palette_numbers = (unsigned char*)  get_address(0x00008C00);

  scanlineY              = (unsigned char*)  get_address(0x00008009);
  frame0Pri              = (unsigned char*)  get_address(0x00008000);
  frame1Pri              = (unsigned char*)  get_address(0x00008030);

  wndTopLeftX            = (unsigned char*)  get_address(0x00008002);
  wndTopLeftY            = (unsigned char*)  get_address(0x00008003);
  wndSizeX               = (unsigned char*)  get_address(0x00008004);
  wndSizeY               = (unsigned char*)  get_address(0x00008005);

  scrollSpriteX          = (unsigned char*)  get_address(0x00008020);
  scrollSpriteY          = (unsigned char*)  get_address(0x00008021);
  scrollFrontX           = (unsigned char*)  get_address(0x00008032);
  scrollFrontY           = (unsigned char*)  get_address(0x00008033);
  scrollBackX            = (unsigned char*)  get_address(0x00008034);
  scrollBackY            = (unsigned char*)  get_address(0x00008035);

  bgSelect               = (unsigned char*)  get_address(0x00008118);
  bgTable                = (unsigned short*) get_address(0x000083E0);
  oowSelect              = (unsigned char*)  get_address(0x00008012);
  oowTable               = (unsigned short*) get_address(0x000083F0);

  color_switch           = (unsigned char*)  get_address(0x00006F91);

  static unsigned char s_dummy_scan = 0;
  if (!scanlineY) scanlineY = &s_dummy_scan;
  rasterY = scanlineY;
}

void run_ngp(const uint8_t* rom_base, size_t rom_size, const char* rom_name, int machine)
{
  ngp_trace_heap("entry");
  ngp_trace_rom(rom_base, rom_size, rom_name, machine);

  // Load ROM
  ngp_mem_set_rom(rom_base, rom_size);
  setFlashSize(rom_size);
  ngp_trace_heap("rom mapped");

  // Sys info
  m_emuInfo.machine = machine;
  m_emuInfo.romSize = (int)rom_size;
  tipo_consola      = 0;      // 0 = NGPC, 1 = NGP (mono)

  // Core init
  Cz80_allocate_flag_tables();
  ngp_mem_init();
  ngp_trace_heap("mem init");
  ngc_save_init(rom_name);
  ngc_save_load();
  ngp_trace_heap("save load");

  // Map VRAM/regs
  map_vdp_tables_full();
  ngp_trace_vdp("tables mapped");

  // default BG palette + enable
  if (bgTable)   bgTable[0] = 0xFFFF;
  if (bgSelect) *bgSelect |= 0x80;  // enable bgTable[index]
  ngp_trace_vdp("bg default");

  // CPU
  tlcs_init();
  tlcs_reset();
  Z80_Init(); 
  Z80_Reset();

  // Flags post boot
  set_defaults_after_boot();
  ngp_trace_vdp("boot defaults");

  // Init video
  ngc_display_init();
  graphics_init();
  ngp_trace_heap("video init");
  ngp_trace_vdp("video init");

  // Fullscreen si vide
  if (wndTopLeftX && wndTopLeftY && wndSizeX && wndSizeY) {
    if (*wndSizeX == 0 || *wndSizeY == 0) {
      *wndTopLeftX = 0; *wndTopLeftY = 0;
      *wndSizeX    = 160; *wndSizeY  = 152;
    }
  }
  if (bgSelect) *bgSelect |= 0x80;

  // LCD ON + priorité frame0
  if (frame0Pri) *frame0Pri |= 0x80 | 0x40;

  // Master IE (VBlank/HBlank)
  tlcsMemWriteB(0x00004000, tlcsMemReadB(0x00004000) | 0xC0);
  ngp_trace_vdp("lcd forced");

  // Fin ecran selon rom
  finscan = 198;
  if (mainrom[0x000020] == 0x65 || mainrom[0x000020] == 0x93) finscan = 199;

  audio_dac_init();
  ngc_sound_init();
  ngc_sound_frame();
  ngc_sound_frame();
  ngc_input_init();
  ngc_scheduler_start();
  ngp_trace_heap("tasks start");
  ngp_trace_vdp("run start");

  // Go
  EMU_LOG("[NGPC_RUN] entering ngpc_run() ... m_bIsActive=%d\n", m_bIsActive);
  EMU_LOG("[FORCE] Enabling LCD + VBLANK IRQ @ 0x4000 = 0xC0\n");
  tlcsMemWriteB(0x00004000, 0xC0);   // bit 7 = LCD ON, bit 6 = VBlank IRQ enable
  m_bIsActive = 1;

  EMU_LOG("[NGPC_RUN] starting core loop\n");

  #ifdef FRAMESKIP
      const int skipFrames = 1;
  #endif

  unsigned long status_last = millis();
  unsigned long frames = 0;
  unsigned long total_frames = 0;
  unsigned long frame_time_total = 0;
  unsigned long frame_time_min = ULONG_MAX;
  unsigned long frame_time_max = 0;
  const uint32_t TARGET_US = 16667; // 60 Hz
  const uint32_t CPU_CLOCK_HZ = 5700000; // 6 MHz downclocked by 5% (smooth perfs)
  
  // Kludges ROM
  switch (tlcsMemReadW(0x00200020)) {
    case 0x0059:   // Sonic
    case 0x0061:   // Metal Slug 2nd
        tlcsMemWriteB(0x0020001F, 0xFF);
        // ngc_patch_snk_logo();
      break;
  }

  while (m_bIsActive)
  {
      uint32_t t0 = micros();  

      // Execute one frame
      #ifdef FRAMESKIP
              tlcs_execute((CPU_CLOCK_HZ) / 60, skipFrames);
      #else
              tlcs_execute((CPU_CLOCK_HZ) / 60);
      #endif
      
      // Pacing 60 Hz
      uint32_t emuUs = micros() - t0;
      frame_time_total += emuUs;
      if (emuUs < frame_time_min) frame_time_min = emuUs;
      if (emuUs > frame_time_max) frame_time_max = emuUs;
      
      int32_t remaining = TARGET_US - emuUs;
      if (remaining > 0) {
        delayMicroseconds(remaining);
      }
      
      // Log framerate and do save tick
      frames++;
      total_frames++;
#ifdef NGP_TRACE_LOGS
      if (total_frames <= 5 || (total_frames % 60) == 0) {
        ngp_trace_frame(total_frames, emuUs);
      }
#endif
      if (millis() - status_last >= 2000)
      {
          ngc_save_tick();

          size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
          float avg_ms = frame_time_total / (float)frames / 1000.0f;
          float min_ms = frame_time_min / 1000.0f;
          float max_ms = frame_time_max / 1000.0f;
          EMU_LOG("[NGP_RUN] %lu frames / 2s (~%lu FPS) | HEAP: %u bytes (%.1f KB) | AVG %.2fms | MIN %.2fms | MAX %.2fms\n",
          frames,
          frames / 2,
          (unsigned int)heap_free,
          heap_free / 1024.0f,
          avg_ms,
          min_ms,
          max_ms);
          frame_time_total = 0;
          frame_time_min = ULONG_MAX;
          frame_time_max = 0;
          frames = 0;
          status_last = millis();
      }
  }
}
