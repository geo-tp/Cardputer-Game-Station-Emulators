/*
** Nofrendo MMC5 — puNES-alignes
** Notes:
** - Some advanced MMC5 behaviors (EXRAM mode 1 CHR indirection for BG and dynamic split CHR bank)
**   require PPU fetch-time redirection that Nofrendo doesn't expose via this drop-in API.
*/

#include <string.h>
#include <noftypes.h>
#include "../nes/nes_mmc.h"
#include "../nes/nes_ppu.h"
#include "../nes/nes.h"
#include "../libsnss/libsnss.h"
#include <log.h>
#if AUDIO
#include "mmc5_snd.h"
#endif

/* ---------- State ---------- */

typedef struct {
  /* modes */
  uint8  prg_mode;      /* $5100 & 3: 0=32K,1=16K,2=8K,3=8K */
  uint8  chr_mode;      /* $5101 & 3: 0=8K,1=4K,2=2K,3=1K */
  uint8  ext_mode;      /* $5104 & 3 (puNES uses for advanced EXRAM modes) */

  /* banks */
  uint8  prg[5];        /* $5113..$5117 */
  uint16 chr[12];       /* $5120..$512B (A:0..7 / B:8..11), 10-bit using chr_high2 */
  uint8  chr_high2;     /* $5130 low 2 bits → CHR bits 8–9 (stored 0..3) */
  uint8  chr_last;      /* last $512x written (for some BG/sprite heuristics) */

  /* WRAM protect */
  uint8  wram_protect[2]; /* $5102,$5103 */

  /* Nametable control ($5105) */
  uint8  nmt;           /* 2 bits per quadrant */

  /* FILL / EXRAM (nametable source=2/3) */
  uint8  fill_tile;        /* $5106 (0x000..0x3BF) */
  uint8  fill_attr;        /* $5107 (2 bits -> {00,55,AA,FF}) */
  uint8  *fill_table;
  uint8  *exram;           /* $5C00..$5FFF */

  /* split (kept; dynamic CHR swap by split not applied in this drop-in) */
  uint8  split;            /* $5200 bit7 */
  uint8  split_side;       /* $5200 bit6 */
  uint8  split_st_tile;    /* $5200 low5 */
  uint8  split_scrl;       /* $5201 */
  uint32 split_bank;       /* $5202 (4K unit << 12) */

  /* frame/scanline state (for $5204 and scanline IRQ latch) */
  uint8  in_frame;     /* 0/1 -> $5204 bit7 */
  uint8  prev_vblank;  /* previous vblank state */
  int    scanline;     /* 0..239 during visible area */

  /* scanline IRQ (MMC5) */
  uint8  irq_latch;    /* $5203 */
  uint8  irq_enable;   /* $5204 bit7 */
  uint8  irq_pending;  /* $5204 bit6 */
  uint8  irq_line;     /* <-- nouveau: 1 = ligne “tenue” jusqu’à ACK */

  /* ... */

  uint8  timer_running;
  uint8  timer_irq;        /* latched flag for $5209 read */
  uint8  timer_line;       /* <-- nouveau: idem pour le timer */


  /* CHR set currently applied: 0 = Set A (sprites), 1 = Set B (background) */
  uint8  chr_set_active;
  uint16 chr_fetch_count;

  /* WRAM backing store (64K superset like puNES) */
  uint8  *wram;
  size_t wram_size;
  uint8  cpu_page_is_wram[16];

  /* math unit & timer (MMC5) */
  uint8  mul_a, mul_b;     /* $5205/$5206 → read low/high product */
  uint16 timer_count;      /* $5209/$520A */

} mmc5_t;

static mmc5_t m5;

/* puNES attribute filler values */
static const uint8 filler_attrib[4] = { 0x00, 0x55, 0xAA, 0xFF };

/* ---------- Internal helpers ---------- */

static int mmc5_wram_write_enabled(void)
{
    return (m5.wram_protect[0] == 0x02) && (m5.wram_protect[1] == 0x01);
}

/* --- CPU 8K helper mapping (direct page pointer install) --- */
static void cpu_map_8k(uint32 address, uint8 *src, int is_wram)
{
    nes6502_context c; nes6502_getcontext(&c);
    int page = (int)(address >> NES6502_BANKSHIFT);
    c.mem_page[page+0] = src + 0x0000;
    c.mem_page[page+1] = src + 0x1000;
    nes6502_setcontext(&c);
    m5.cpu_page_is_wram[page+0] = is_wram ? 1 : 0;
    m5.cpu_page_is_wram[page+1] = is_wram ? 1 : 0;
}

/* Map WRAM 8K window (bit7 ignored; bank wraps over wram_size) */
static void prg_map_wram8(uint32 address, uint8 bank7)
{
    uint8 *base = m5.wram;
    if (!base) { 
      static uint8 zero = 0;
      cpu_map_8k(address, &zero, 0);
      return;
    }
    size_t off = ((size_t)(bank7 & 0x7F) << 13) % (m5.wram ? m5.wram_size : 0x2000);
    cpu_map_8k(address, base + off, 1);
}

/* puNES maps $6000 via prg[0] with write-protect; we map the data here */
static void mmc5_apply_wram_6000(void)
{
    /* enable = (5102 == 0x02) && (5103 == 0x01) like puNES */
    int enable = (m5.wram_protect[0] == 0x02) && (m5.wram_protect[1] == 0x01);
    (void)enable; /* TODO: if you have page-level WP, apply it here */
    prg_map_wram8(0x6000, m5.prg[0]);
}

/* Map PRG ROM 8K — IMPORTANT: mask bit7 (ROM/RAM flag) from the bank index. */
static void prg_map_rom8(uint32 address, uint8 bank)
{
    int page = (int)(address >> NES6502_BANKSHIFT);
    mmc_bankrom(8, address, (bank & 0x7F));
    m5.cpu_page_is_wram[page+0] = 0;
    m5.cpu_page_is_wram[page+1] = 0;
}

static void prg_map_rom16(uint32 address, uint8 bank)
{
    int page = (int)(address >> NES6502_BANKSHIFT);
    mmc_bankrom(16, address, bank);
    m5.cpu_page_is_wram[page+0] = 0;
    m5.cpu_page_is_wram[page+1] = 0;
    m5.cpu_page_is_wram[page+2] = 0;
    m5.cpu_page_is_wram[page+3] = 0;
}

static void prg_map_rom32(uint32 address, uint8 bank)
{
    mmc_bankrom(32, address, bank);
    for (int page = 8; page < 16; page++)
        m5.cpu_page_is_wram[page] = 0;
}

/* MMC5: PRG regs carry a ROM/RAM flag in bit7; choose destination accordingly. */
static void prg_swap(uint32 address, uint8 val)
{
    if (val & 0x80) prg_map_rom8(address, val);
    else            prg_map_wram8(address, val);
}

static void mmc5_apply_prg(void);
static void mmc5_apply_chr_S(void);
static void mmc5_apply_chr_B(void);
static void mmc5_apply_nametable(void);

/* Map one nametable quadrant (0..3) to 0/1=internal, 2=EXRAM, 3=FILL. */
static void mmc5_map_nt_quadrant(int q, int mode)
{
  uint16 base = 0x2000 + q * 0x400;

  if (mode == 0 || mode == 1) {
    /* Let ppu_mirror cover internal A/B; we'll override 2/3 below. */
    return;
  }

  /* EXRAM or FILL: directly attach the backing buffer to PPU page. */
  uint8 *src = (mode == 2) ? m5.exram : m5.fill_table;

  /* ppu_setpage(size=1KB, page_num=8+q, location=src - base) */
  ppu_setpage(1, 8 + q, src - base);
}

/* Apply quadrant mirroring like puNES ($5105). */
static void mmc5_apply_nametable(void)
{
  int ntbits[4];
  for (int i = 0; i < 4; i++) ntbits[i] = (m5.nmt >> (i*2)) & 0x03;

  /* First, use ppu_mirror for 0/1 (internal A/B); force 0 elsewhere, corrected later. */
  int m0 = (ntbits[0] <= 1) ? ntbits[0] : 0;
  int m1 = (ntbits[1] <= 1) ? ntbits[1] : 0;
  int m2 = (ntbits[2] <= 1) ? ntbits[2] : 0;
  int m3 = (ntbits[3] <= 1) ? ntbits[3] : 0;

  ppu_mirror(m0, m1, m2, m3);

  /* Then override 2/3 quadrants with EXRAM/FILL buffers. */
  for (int q = 0; q < 4; q++) {
    if (ntbits[q] >= 2)
      mmc5_map_nt_quadrant(q, ntbits[q]);
  }

  ppu_mirrorhipages();
}

/* $5113 always selects PRG-RAM at $6000-$7FFF; bit 7 is ignored. */
static void mmc5_apply_6000(void)
{
    int enable = (m5.wram_protect[0] == 0x02) && (m5.wram_protect[1] == 0x01);
    (void)enable; /* TODO WP par page si ton core le supporte */

    prg_map_wram8(0x6000, m5.prg[0]);
}

/* ---------- PRG mapping (puNES prg_fix equivalent through mmc_bankrom()) ---------- */
static void mmc5_apply_prg(void)
{
    /* Always keep $6000 updated like puNES (banked WRAM + WP). */
    mmc5_apply_6000();

    switch (m5.prg_mode) {
    case 0: /* 32K @8000 : prg[4] >> 2 is always ROM */
        prg_map_rom32(0x8000, (m5.prg[4] >> 2));
        break;

    case 1: /* 8K @8000+A000 (even/odd of prg[2]), 16K ROM @C000 */
        prg_swap(0x8000, (m5.prg[2] & ~1));
        prg_swap(0xA000, (m5.prg[2] |  1));
        prg_map_rom16(0xC000, (m5.prg[4] >> 1));
        break;

    case 2: /* 8K @8000, A000, C000 (WRAM/ROM), fixed 8K ROM @E000 */
        prg_swap(0x8000, (m5.prg[2] & ~1));
        prg_swap(0xA000, (m5.prg[2] |  1));
        prg_swap(0xC000,  m5.prg[3]);
        prg_map_rom8(0xE000, (m5.prg[4] & 0x7F)); /* mask not strictly needed but harmless */
        break;

    case 3: /* 8K @8000, A000, C000 (WRAM/ROM), fixed 8K ROM @E000 */
        prg_swap(0x8000, m5.prg[1]);
        prg_swap(0xA000, m5.prg[2]);
        prg_swap(0xC000, m5.prg[3]);
        prg_map_rom8(0xE000, (m5.prg[4] & 0x7F));
        break;
    }
}

/* ---------- CHR mapping (Set A = sprites, Set B = background) ---------- */

static void mmc5_apply_chr_S(void)
{
    switch (m5.chr_mode) {
    case 0: /* 8K */
        mmc_bankvrom(8, 0x0000, (int)m5.chr[7]); break;
    case 1: /* 4K + 4K */
        mmc_bankvrom(4, 0x0000, (int)m5.chr[3]);
        mmc_bankvrom(4, 0x1000, (int)m5.chr[7]); break;
    case 2: /* 2K x4 */
        mmc_bankvrom(2, 0x0000, (int)m5.chr[1]);
        mmc_bankvrom(2, 0x0800, (int)m5.chr[3]);
        mmc_bankvrom(2, 0x1000, (int)m5.chr[5]);
        mmc_bankvrom(2, 0x1800, (int)m5.chr[7]); break;
    case 3: /* 1K x8 */
        mmc_bankvrom(1, 0x0000, (int)m5.chr[0]);
        mmc_bankvrom(1, 0x0400, (int)m5.chr[1]);
        mmc_bankvrom(1, 0x0800, (int)m5.chr[2]);
        mmc_bankvrom(1, 0x0C00, (int)m5.chr[3]);
        mmc_bankvrom(1, 0x1000, (int)m5.chr[4]);
        mmc_bankvrom(1, 0x1400, (int)m5.chr[5]);
        mmc_bankvrom(1, 0x1800, (int)m5.chr[6]);
        mmc_bankvrom(1, 0x1C00, (int)m5.chr[7]); break;
    }
    m5.chr_set_active = 0;
}

static void mmc5_apply_chr_B(void)
{
    switch (m5.chr_mode) {
    case 0:
        mmc_bankvrom(8, 0x0000, (int)m5.chr[11]); break;
    case 1:
        mmc_bankvrom(4, 0x0000, (int)m5.chr[11]);
        mmc_bankvrom(4, 0x1000, (int)m5.chr[11]); break;
    case 2:
        mmc_bankvrom(2, 0x0000, (int)m5.chr[9]);
        mmc_bankvrom(2, 0x0800, (int)m5.chr[11]);
        mmc_bankvrom(2, 0x1000, (int)m5.chr[9]);
        mmc_bankvrom(2, 0x1800, (int)m5.chr[11]); break;
    case 3:
        mmc_bankvrom(1, 0x0000, (int)m5.chr[8]);
        mmc_bankvrom(1, 0x0400, (int)m5.chr[9]);
        mmc_bankvrom(1, 0x0800, (int)m5.chr[10]);
        mmc_bankvrom(1, 0x0C00, (int)m5.chr[11]);
        mmc_bankvrom(1, 0x1000, (int)m5.chr[8]);
        mmc_bankvrom(1, 0x1400, (int)m5.chr[9]);
        mmc_bankvrom(1, 0x1800, (int)m5.chr[10]);
        mmc_bankvrom(1, 0x1C00, (int)m5.chr[11]); break;
    }
    m5.chr_set_active = 1;
}

/* MMC5 uses separate CHR register sets only for 8x16 sprites. Nofrendo's
   latch hook is tile-oriented, so count the 33 background fetches first and
   use set B for those; later sprite fetches use set A. */
static void mmc5_latchfunc(uint32 vram_base, uint8 tile)
{
    UNUSED(vram_base);
    UNUSED(tile);

    if (ppu_obj_8x16() && m5.chr_fetch_count < 33) {
        if (m5.chr_set_active != 1) mmc5_apply_chr_B();
    } else {
        if (m5.chr_set_active != 0) mmc5_apply_chr_S();
    }

    if (m5.chr_fetch_count < 0xFFFF)
        m5.chr_fetch_count++;
}

static bool mmc5_bgfunc(uint32 address, uint8 tile, uint8 fine_y,
                        uint8 *col_high, uint8 **data_ptr)
{
    rominfo_t *cart;
    uint16 ex_index;
    uint8 ex_attr;
    int bank4, bank_count;
    uint32 offset;

    if (m5.ext_mode != 1 || !m5.exram)
        return false;

    cart = mmc_getinfo();
    if (!cart || !cart->vrom || !cart->vrom_banks)
        return false;

    ex_index = (uint16)(address & 0x03FF);
    if (ex_index >= 0x03C0)
        return false;

    ex_attr = m5.exram[ex_index];
    bank4 = ((int)(m5.chr_high2 & 0x03) << 6) | (int)(ex_attr & 0x3F);
    bank_count = cart->vrom_banks * 2;
    offset = (uint32)((bank4 % bank_count) << 12) + ((uint32)tile << 4) + (fine_y & 7);

    *col_high = (uint8)((ex_attr >> 6) << 2);
    *data_ptr = &cart->vrom[offset];
    return true;
}

/* ---------- IRQ (scanline) & timer approximation ---------- */

static void map5_hblank(int vblank)
{
  int rendering = (!vblank && ppu_enabled());

  /* MMC5 only detects scanlines while the PPU is actively rendering. */
  if (!rendering) {
    m5.in_frame = 0;
    m5.prev_vblank = 1;
    m5.irq_line   = 0;
    m5.timer_line = 0;
    m5.chr_fetch_count = 0;
  } else if (!m5.in_frame) {
    m5.in_frame = 1;
    m5.prev_vblank = 0;
    m5.scanline = 0;
    m5.irq_pending = 0;        /* scanline 0 acknowledges pending IRQ */
    m5.irq_line = 0;
    m5.chr_fetch_count = 0;
  } else {
    m5.scanline = (m5.scanline + 1) & 0xFF;
    m5.chr_fetch_count = 0;
  }

   /* $5203 value 0 is a special case and never creates a new pending IRQ. */
   if (m5.irq_latch && (m5.scanline == m5.irq_latch) && m5.in_frame) {
      m5.irq_pending = 1;
      if (m5.irq_enable && !m5.irq_line) {          /* edge only */
         m5.irq_line = 1;
         nes_irq();               /* pulse unique */
      }
   }

   /* --- MMC5 general-purpose timer (approximation) --- */
   if (m5.timer_running && m5.timer_count) {
      const int approx_cpu_per_scanline = 114; /* ~1789773 / 15734 ≈ 113.56 */
      if (m5.timer_count > approx_cpu_per_scanline) {
         m5.timer_count -= approx_cpu_per_scanline;
      } else {
         m5.timer_count = 0;
         m5.timer_irq = 1;
         if (!m5.timer_line) {    /* edge only */
               m5.timer_line = 1;
               nes_irq();
         }
      }
   }
}

/* ---------- CPU writes/reads ---------- */

static void mmc5_apply_chr_highbits(void)
{
    /* No direct action needed here; we store chr_high2 and fold it on $512x writes. */
}

static void map5_write(uint32 address, uint8 value)
{
  switch (address) {
    /* $5000–$5015: MMC5 APU handled elsewhere if AUDIO; ignored here */

    case 0x5100: /* PRG mode */
      m5.prg_mode = (value & 0x03);
      mmc5_apply_prg();
      break;

    case 0x5101: /* CHR mode */
      m5.chr_mode = (value & 0x03);
      /* reprogram the active set immediately */
      if (m5.chr_set_active) mmc5_apply_chr_B(); else mmc5_apply_chr_S();
      break;

    case 0x5102: /* WRAM protect low  */
    case 0x5103: /* WRAM protect high */
      m5.wram_protect[address & 1] = (value & 0x03);
      /* Update $6000 mapping (and apply WP if your core supports it). */
      mmc5_apply_6000();
      break;

    case 0x5104: /* ext mode (advanced EXRAM) */
      m5.ext_mode = (value & 0x03);
      /* TODO: EXRAM mode 1 CHR indirection for BG requires PPU fetch redirection. */
      break;

    case 0x5105: /* quadrant mirroring */
      m5.nmt = value;
      mmc5_apply_nametable();
      break;

    case 0x5106: /* FILL tile */
      m5.fill_tile = value;
      memset(&m5.fill_table[0x000], m5.fill_tile, 0x3C0);
      break;

    case 0x5107: /* FILL attrib (2 bits) */
      m5.fill_attr = (value & 0x03);
      memset(&m5.fill_table[0x3C0], filler_attrib[m5.fill_attr], 0x40);
      break;

    /* PRG regs $5113–$5117 (0..4) */
    case 0x5113: case 0x5114: case 0x5115: case 0x5116: case 0x5117: {
      int idx = (int)(address - 0x5113);
      m5.prg[idx] = value;
      mmc5_apply_prg();
      /* If idx==0 ($6000), keep $6000 WRAM page current. */
      if (idx == 0) mmc5_apply_6000();
      break;
    }

    /* CHR regs A (0..7) + B (8..11) */
    case 0x5120: case 0x5121: case 0x5122: case 0x5123:
    case 0x5124: case 0x5125: case 0x5126: case 0x5127:
    case 0x5128: case 0x5129: case 0x512A: case 0x512B: {
      int idx = (int)(address - 0x5120);
      m5.chr_last = (uint8)idx;
      /* 1KB bank = (chr_high2<<8) | value  (10-bit index) */
      m5.chr[idx] = (uint16)((m5.chr_high2 & 0x03) << 8) | (uint16)value;
      /* reprogram the active set immediately */
      if (m5.chr_set_active) mmc5_apply_chr_B(); else mmc5_apply_chr_S();
      break;
    }

    case 0x5130:
      m5.chr_high2 = (value & 0x03); /* store 0..3 for bits 8–9 */
      mmc5_apply_chr_highbits();
      break;

    /* split regs (kept; dynamic CHR swap by split not applied in this drop-in) */
    case 0x5200:
      m5.split        = (value & 0x80) ? 1 : 0;
      m5.split_side   =  value & 0x40;
      m5.split_st_tile= (value & 0x1F);
      break;
    case 0x5201:
      m5.split_scrl = (value >= 240) ? (value - 16) : value;
      break;
    case 0x5202:
      /* CHR 4K bank for split BG; we store it (no dynamic apply here). */
      m5.split_bank = ((uint32)value) << 12;
      break;

    /* IRQ (scanline) */
    case 0x5203:
      m5.irq_latch = value;
      break;
    case 0x5204:
      m5.irq_enable = (value & 0x80) ? 1 : 0;
      if (!m5.irq_enable) {
         m5.irq_pending = 0;
         m5.irq_line = 0;    // <-- relâche si désactivé
      }
      break;

    /* Multiplier and timer */
    case 0x5205: m5.mul_a = value; break;
    case 0x5206: m5.mul_b = value; break;
    case 0x5209: m5.timer_count = (uint16)((m5.timer_count & 0xFF00) | value); m5.timer_running = 1; break;
    case 0x520A: m5.timer_count = (uint16)((m5.timer_count & 0x00FF) | (value << 8)); break;

    default:
      /* EXRAM writes $5C00–$5FFF are handled by map5_write_exram; ignore others. */
      break;
  }
}

static void map5_write_prg(uint32 address, uint8 value)
{
  int page = (int)(address >> NES6502_BANKSHIFT);

  if (page < 0 || page >= 16)
    return;

  if (!m5.cpu_page_is_wram[page] || !mmc5_wram_write_enabled())
    return;

  nes6502_context c;
  nes6502_getcontext(&c);
  if (!c.mem_page[page])
    return;
  c.mem_page[page][address & NES6502_BANKMASK] = value;
}

/* Low-range reads for MMC5 I/O */
static uint8 map5_read_low(uint32 address)
{
  switch (address) {
   case 0x5204: {
      uint8 v = (m5.irq_pending ? 0x80 : 0x00) | (m5.in_frame ? 0x40 : 0x00);
      m5.irq_pending = 0;
      m5.irq_line = 0;
      return v;
   }
    case 0x5205: {
      uint16 prod = (uint16)m5.mul_a * (uint16)m5.mul_b;
      return (uint8)(prod & 0x00FF);
    }
    case 0x5206: {
      uint16 prod = (uint16)m5.mul_a * (uint16)m5.mul_b;
      return (uint8)((prod >> 8) & 0x00FF);
    }
    case 0x5209: {
      uint8 v = m5.timer_irq ? 0x80 : 0x00;
      m5.timer_irq = 0;
      m5.timer_line = 0;
      /* If available, clear CPU IRQ line as puNES does. */
      return v;
    }
    default:
      break;
  }
  return 0xFF;
}

/* EXRAM ($5C00–$5FFF) handlers */
static void map5_write_exram(uint32 address, uint8 value)
{
  m5.exram[(address - 0x5C00) & 0x03FF] = value;
}

static uint8 map5_read_exram(uint32 address)
{
  return m5.exram[(address - 0x5C00) & 0x03FF];
}

/* ---------- Init / SNSS ---------- */

static void map5_init(void)
{
  memset(&m5, 0, sizeof(m5));

  m5.prg_mode = 3;
  m5.chr_mode = 0;

  m5.in_frame = 0;
  m5.prev_vblank = 1; /* start in VBlank */
  m5.scanline = 0;

  /* puNES-like initial PRG banks */
  m5.prg[0]=0xFB; m5.prg[1]=0xFC; m5.prg[2]=0xFD; m5.prg[3]=0xFE; m5.prg[4]=0xFF;

  /* CHR init 1..11 (like puNES); chr[0] left at 0 */
  for (int i=1; i<=11; i++) m5.chr[i] = (uint16)i;

  /* ALLOC SIMPLEMENT CES DEUX BUFFERS (1KB chacun) */
  m5.fill_table = (uint8*)malloc(0x400);
  m5.exram      = (uint8*)malloc(0x400);
  if (!m5.fill_table || !m5.exram) {
    free(m5.fill_table); m5.fill_table = NULL;
    free(m5.exram);      m5.exram      = NULL;
    nofrendo_log_printf("MMC5 init failed: out of memory (fill/exram)\n");
    return;
  }

  /* init FILL/EXRAM  */
  m5.fill_attr = 0;
  memset(&m5.fill_table[0x000], 0x00, 0x3C0);
  memset(&m5.fill_table[0x3C0], 0x00, 0x40); /* filler_attrib[0] */
  memset(m5.exram, 0x00, 0x400);

  /* MMC5 PRG-RAM: 64K is a compatible superset for commercial ExROM games. */
  m5.wram_size = 0x10000;
  m5.wram = (uint8*)NOFRENDO_MALLOC(m5.wram_size);
  if (m5.wram) memset(m5.wram, 0x00, m5.wram_size);

  /* Apply initial mappings and PPU hook */
  mmc5_apply_prg();          /* also maps $6000 via mmc5_apply_6000() */
  mmc5_apply_nametable();
  mmc5_apply_chr_S();        /* default to Set A active */
  ppu_setlatchfunc(mmc5_latchfunc);
  ppu_setbgfunc(mmc5_bgfunc);

  nofrendo_log_printf("MMC5 init (puNES-style drop-in)\n");
}

static void map5_getstate(SnssMapperBlock *state)
{
  /* Minimal/unchanged; extend if you serialize more fields. */
  state->extraData.mapper5.dummy = 0;
}

static void map5_setstate(SnssMapperBlock *state)
{
  UNUSED(state);
  /* Re-apply PRG/CHR/NT on load */
  mmc5_apply_prg();
  (m5.chr_set_active ? mmc5_apply_chr_B() : mmc5_apply_chr_S());
  mmc5_apply_nametable();
  m5.chr_fetch_count = 0;
}

/* ---------- Mapper tables ---------- */

static map_memwrite map5_memwrite[] = {
  /* $5000–$5015 MMC5 audio handled elsewhere */
  { 0x5016, 0x5BFF, map5_write },
  { 0x5C00, 0x5FFF, map5_write_exram },
  { 0x6000, 0xFFFF, map5_write_prg },
  { -1, -1, NULL }
};

static map_memread map5_memread[] = {
  { 0x5C00, 0x5FFF, map5_read_exram },
  { 0x5204, 0x5204, map5_read_low },
  { 0x5205, 0x5206, map5_read_low },
  { 0x5209, 0x5209, map5_read_low },
  { -1, -1, NULL }
};

mapintf_t map5_intf = {
  5,               /* mapper number */
  "MMC5 (puNES)",  /* name */
  map5_init,       /* init */
  NULL,            /* vblank */
  map5_hblank,     /* hblank */
  map5_getstate,   /* get state */
  map5_setstate,   /* set state */
  map5_memread,    /* mem reads */
  map5_memwrite,   /* mem writes */
#if AUDIO
  &mmc5_ext
#else
  NULL
#endif
};
