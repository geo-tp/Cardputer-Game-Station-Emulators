#pragma GCC optimize ("Os")

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <sys/stat.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "esp_partition.h"
#include "esp_spi_flash.h"

/* Nofrendo / OSD headers */
#include <noftypes.h>
#include <event.h>
#include <gui.h>
#include <log.h>
#include <nes/nes.h>
#include <nes/nes_pal.h>
#include <nes/nesinput.h>
#include <nofconfig.h>
#include <osd.h>

#define HW_AUDIO_SAMPLERATE 22050
#define NTSC

TimerHandle_t nes_timer;
static uint8_t *fb = NULL;       /* framebuffer 8bpp */
static bitmap_t *myBitmap = NULL;
bool fullscreenMode = true;
int nesZoomPercent = 115;          /* 100% = fullscreen, 110% to 150% = zoom */

#define FB_WIDTH    NES_SCREEN_WIDTH     /* 256 */
#define FB_HEIGHT   256                  /* surallocate */
#define VISIBLE_H   NES_SCREEN_HEIGHT    /* 240 visible */

/* memory allocation */
void *mem_alloc(int size, bool prefer_fast_memory)
{
    (void)prefer_fast_memory;
    return malloc((size_t)size);
}

/* sound */
extern int  osd_init_sound(void);
extern void osd_stopsound(void);
extern void do_audio_frame(void);
extern void osd_getsoundinfo(sndinfo_t *info);

/* display */
extern void display_init(void);
extern void display_write_frame(const uint8_t *data[]);
extern void display_clear(void);

/* save */
extern void sram_autosave_tick(void);
extern void osd_set_rompath_for_saves(const char *p);

/* This runs on core 0 */
QueueHandle_t vidQueue;

#ifdef BENCHMARK_LOGS
static uint32_t s_benchCoreFrames = 0;
static uint32_t s_benchDrawnFrames = 0;
static uint32_t s_benchDroppedFrames = 0;
static uint64_t s_benchRenderUs = 0;
static int64_t  s_benchLastLogUs = 0;

static void nes_benchmark_log_if_due(void)
{
    int64_t now = esp_timer_get_time();
    if (s_benchLastLogUs == 0) {
        s_benchLastLogUs = now;
        return;
    }

    int64_t elapsed = now - s_benchLastLogUs;
    if (elapsed < 2000000LL) return;

    float seconds = (float)elapsed / 1000000.0f;
    float coreFps = (float)s_benchCoreFrames / seconds;
    float drawnFps = (float)s_benchDrawnFrames / seconds;
    float avgRenderUs = s_benchDrawnFrames
        ? (float)s_benchRenderUs / (float)s_benchDrawnFrames
        : 0.0f;
    float speedPct = (coreFps / 60.0f) * 100.0f;

    printf(
        "[NES][BENCH] core=%.2f fps (%.1f%%) | drawn=%.2f fps | render=%.1f us | drop=%lu | heap=%u\n",
        coreFps,
        speedPct,
        drawnFps,
        avgRenderUs,
        (unsigned long)s_benchDroppedFrames,
        (unsigned)esp_get_free_heap_size()
    );

    s_benchCoreFrames = 0;
    s_benchDrawnFrames = 0;
    s_benchDroppedFrames = 0;
    s_benchRenderUs = 0;
    s_benchLastLogUs = now;
}
#endif

static void displayTask(void *arg)
{
    bitmap_t *bmp = NULL;
    nofrendo_log_printf("OSD: displayTask started\n");
    while (1) {
        if (xQueueReceive(vidQueue, &bmp, portMAX_DELAY) == pdTRUE) {
            if (!bmp || !bmp->line[0]) { nofrendo_log_printf("OSD: bad bmp\n"); continue; }
#ifdef BENCHMARK_LOGS
            int64_t renderStart = esp_timer_get_time();
#endif
            display_write_frame((const uint8_t **)bmp->line);
#ifdef BENCHMARK_LOGS
            s_benchRenderUs += (uint64_t)(esp_timer_get_time() - renderStart);
            s_benchDrawnFrames++;
            nes_benchmark_log_if_due();
#endif
        }
    }
}

/* initialise/shutdown/setmode */
static int init(int width, int height)   { (void)width; (void)height; return 0; }
static void shutdown(void)               {}
static int  set_mode(int width, int height) { (void)width; (void)height; return 0; }

/* palette */
uint16 *myPalette;
static void set_palette(rgb_t *pal)
{
    if (!myPalette) {
        myPalette = (uint16 *)malloc(256 * sizeof(uint16));
        if (!myPalette) {
            nofrendo_log_printf("OSD: set_palette malloc FAILED\n");
            return;
        }
    }

    int i;
    for (i = 0; i < 256; i++) {
        uint16 c = (pal[i].b >> 3) + ((pal[i].g >> 2) << 5) + ((pal[i].r >> 3) << 11);
        myPalette[i] = (uint16)((c >> 8) | ((c & 0xff) << 8));
    }
}

/* clear */
static void clear(uint8 color)
{
    (void)color;
    display_clear();
}

/* lock/free write */
static bitmap_t *lock_write(void)
{
    if (!fb) {
        const size_t fb_size = (size_t)FB_WIDTH * FB_HEIGHT; /* 61 440 */
        nofrendo_log_printf("OSD: alloc fb %u bytes (%dx%d)\n",
                            (unsigned)fb_size, FB_WIDTH, FB_HEIGHT);
        fb = (uint8_t*) malloc(fb_size);
        if (!fb) { nofrendo_log_printf("OSD: alloc fb FAILED\n"); return NULL; }
        memset(fb, 0, fb_size);

        myBitmap = bmp_createhw((uint8 *)fb, FB_WIDTH, FB_HEIGHT, FB_WIDTH);
        if (!myBitmap) { nofrendo_log_printf("OSD: bmp_createhw FAILED\n"); return NULL; }

        nofrendo_log_printf("OSD: lock_write -> bmp %p\n", (void*)myBitmap);
    }
    return myBitmap; /* reuse */
}

static void free_write(int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties; (void)dirty_rects;
    /* no free: persistent fb */
}

/* blit */
static void custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties; (void)dirty_rects;
#ifdef BENCHMARK_LOGS
    s_benchCoreFrames++;
#endif
    if (xQueueSend(vidQueue, &bmp, 0) != pdPASS) {
        bitmap_t *tmp; xQueueReceive(vidQueue, &tmp, 0);
#ifdef BENCHMARK_LOGS
        s_benchDroppedFrames++;
#endif
        (void)xQueueSend(vidQueue, &bmp, 0);
    }
    do_audio_frame();
    sram_autosave_tick();
}

viddriver_t sdlDriver = {
    "Simple DirectMedia Layer",
    init, shutdown, set_mode, set_palette, clear,
    lock_write, free_write, custom_blit,
    false /* invalidate flag */
};

void osd_getvideoinfo(vidinfo_t *info)
{
    info->default_width  = NES_SCREEN_WIDTH;
    info->default_height = NES_SCREEN_HEIGHT;
    info->driver = &sdlDriver;
}

/* -------------------- Input -------------------- */

extern void     controller_init(void);
extern uint32_t controller_read_input(void);

static void osd_initinput(void) { controller_init(); }
static void osd_freeinput(void) {}

void osd_getinput(void)
{
    const int ev[32] = {
        event_joypad1_up,    event_joypad1_down,  event_joypad1_left,  event_joypad1_right,
        event_joypad1_select,event_joypad1_start, event_joypad1_a,     event_joypad1_b,
        event_state_save,    event_state_load,    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0
    };
    static int oldb = 0xffff;
    uint32_t b   = controller_read_input();
    uint32_t chg = b ^ (uint32_t)oldb;
    int x; oldb = (int)b;
    for (x = 0; x < 16; x++) {
        if (chg & 1U) {
            event_t evh = event_get(ev[x]);
            if (evh) evh((b & 1U) ? INP_STATE_BREAK : INP_STATE_MAKE);
        }
        chg >>= 1U;
        b   >>= 1U;
    }
}

void osd_getmouse(int *x, int *y, int *button)
{
    (void)x; (void)y; (void)button;
}

/* -------------------- Core init/shutdown -------------------- */

static int logprint(const char *string) { return printf("%s", string); }

int osd_init(void)
{
    nofrendo_log_chain_logfunc(logprint);
#ifdef BENCHMARK_LOGS
    printf("[NES][BENCH] enabled\n");
#endif
#ifdef NES_DIAG_LOGS
    printf("[NES][OSD] init heap=%u largest8=%u largestInternal=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
#endif

    if (osd_init_sound()) return -1;

    display_init();
    vidQueue = xQueueCreate(2, sizeof(bitmap_t *));
    xTaskCreatePinnedToCore(&displayTask, "displayTask", 4096, NULL, 3, NULL, 0);
    osd_initinput();
#ifdef NES_DIAG_LOGS
    printf("[NES][OSD] ready queue=%p fb=%p bmp=%p heap=%u largest8=%u\n",
           (void *)vidQueue,
           (void *)fb,
           (void *)myBitmap,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
    return 0;
}

void osd_shutdown(void)
{
    osd_stopsound();
    osd_freeinput();
}

/* -------------------- Entry points -------------------- */

char configfilename[] = "na";

int osd_main(int argc, char *argv[])
{
    (void)argc;
    config.filename = configfilename;
    if (argc > 0 && argv && argv[0]) osd_set_rompath_for_saves(argv[0]);
#ifdef NES_DIAG_LOGS
    printf("[NES][OSD] main argc=%d rom=%s\n",
           argc,
           (argc > 0 && argv && argv[0]) ? argv[0] : "(null)");
#endif
    return main_loop(argv[0], system_autodetect);
}

/* timer */
int osd_installtimer(int frequency, void *func, int funcsize, void *counter, int countersize)
{
    (void)funcsize; (void)counter; (void)countersize;
    nofrendo_log_printf("Timer install, configTICK_RATE_HZ=%d, freq=%d\n", configTICK_RATE_HZ, frequency);
    nes_timer = xTimerCreate("nes", configTICK_RATE_HZ / frequency, pdTRUE, NULL, (TimerCallbackFunction_t)func);
    xTimerStart(nes_timer, 0);
#ifdef NES_DIAG_LOGS
    printf("[NES][OSD] timer freq=%d handle=%p periodTicks=%u\n",
           frequency,
           (void *)nes_timer,
           (unsigned)(configTICK_RATE_HZ / frequency));
#endif
    return 0;
}

/* -------------------- Wrappers -------------------- */

/* Wrap all bitmap creation from nofrendo */
bitmap_t *__wrap_bmp_create(int width, int height, int pitch)
{
    (void)width; (void)height; (void)pitch;
    if (!myBitmap) lock_write();
    return myBitmap;
}

/* Wrap all bitmap destruction from nofrendo */
void __wrap_bmp_destroy(bitmap_t **bmp)
{
    (void)bmp; /* no-op */
}

// /* -------------------- XIP ROM access -------------------- */

// /* Mapped ROM */
// static const uint8_t *g_rom_ptr = NULL;
// static size_t g_rom_size = 0;
// static spi_flash_mmap_handle_t g_rom_mmap = 0;

// /* Map the ROM partition for XIP access */
// int osd_xip_map_rom_partition(const char *part_name, size_t rom_size_effective)
// {
//     const esp_partition_t *p;
//     const void *ptr = NULL;
//     if (part_name == NULL) part_name = "rom";

//     p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, part_name);
//     if (!p) return -1;

//     if (esp_partition_mmap(p, 0, p->size, SPI_FLASH_MMAP_DATA, &ptr, &g_rom_mmap) != ESP_OK)
//         return -2;

//     g_rom_ptr  = (const uint8_t *)ptr;
//     g_rom_size = (rom_size_effective > 0 && rom_size_effective <= p->size) ? rom_size_effective : p->size;
//     nofrendo_log_printf("OSD: XIP mapped '%s' size=%u\n", part_name, (unsigned)g_rom_size);
//     return 0;
// }

// /** Get pointer/size of mapped ROM */
// const uint8_t* _osd_get_rom_ptr(void){ return g_rom_ptr; }
// size_t _osd_get_rom_size(void){ return g_rom_size; }

// void osd_xip_unmap(void)
// {
//     if (g_rom_mmap) {
//         spi_flash_munmap(g_rom_mmap);
//         g_rom_mmap = 0;
//     }
//     g_rom_ptr = NULL;
//     g_rom_size = 0;
// }
