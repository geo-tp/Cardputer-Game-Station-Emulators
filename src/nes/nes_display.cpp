#pragma GCC optimize ("Os")

extern "C" {
#include <nes/nes.h>
}

#include <M5Unified.h>

int16_t frame_scaling;
static uint16_t* s_nesLineBuf = nullptr;
static int       s_nesLineBufLen = 0;  // en pixels
static int16_t frame_x, frame_y, frame_x_offset, frame_y_offset, frame_width, frame_height, frame_line_pixels;
extern uint16_t *myPalette;
extern bool fullscreenMode;
extern int nesZoomPercent;

extern void display_begin() {}

extern "C" {

void display_init() {
    frame_scaling = 0;

    frame_x = 0;
    frame_y = 0;
    frame_x_offset = 0;
    frame_y_offset = 90;

    frame_width  = M5.Lcd.width();   // typ 240
    frame_height = M5.Lcd.height();  // typ 135
    frame_line_pixels = frame_width;

    if (!s_nesLineBuf || s_nesLineBufLen != frame_width) {
        if (s_nesLineBuf) {
            heap_caps_free(s_nesLineBuf);
            s_nesLineBuf = nullptr;
            s_nesLineBufLen = 0;
        }

        size_t bytes = frame_width * sizeof(uint16_t);
        s_nesLineBuf = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_nesLineBuf) s_nesLineBuf = (uint16_t*)malloc(bytes); // fallback


        if (s_nesLineBuf) {
            memset(s_nesLineBuf, 0, bytes);
            s_nesLineBufLen = frame_width;
        }
    }
}

void display_write_frame_square(const uint8_t *data[]) {
    M5.Lcd.startWrite();
    M5.Lcd.setWindow(frame_x, frame_y, frame_width - 1, frame_height - 1);

    const int srcW = NES_SCREEN_WIDTH;   // 256
    const int srcH = NES_SCREEN_HEIGHT;  // 240

    int outH = frame_height;
    int desiredW = 220; 
    int outW = desiredW;
    if (outW > frame_width) outW = frame_width;

    const int x_start = (frame_width - outW) / 2;
    const int x_end   = x_start + outW;
    const int virtW   = (srcH * 4) / 3;
    const int virtOff = (virtW - srcW) / 2;

    for (int32_t y = 0; y < frame_height; ++y) {
        int srcY = (y * srcH) / outH;
        if (srcY < 0) srcY = 0; else if (srcY >= srcH) srcY = srcH - 1;

        for (int32_t x = 0; x < x_start; ++x) {
            s_nesLineBuf[x] = TFT_BLACK;
        }

        int virtX_accum = 0;
        int virtX_step = (virtW << 16) / outW;
        for (int32_t x = x_start; x < x_end; ++x) {
            int virtX = virtX_accum >> 16;
            virtX_accum += virtX_step;
            int srcX  = virtX - virtOff;
            s_nesLineBuf[x] = (unsigned)srcX < (unsigned)srcW
                ? myPalette[data[srcY][srcX]]
                : TFT_BLACK;
        }
        for (int32_t x = x_end; x < frame_width; ++x) {
            s_nesLineBuf[x] = TFT_BLACK;
        }
        M5.Lcd.pushPixels(s_nesLineBuf, frame_width);
    }

    M5.Lcd.endWrite();
}

static inline int clampi(int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); }

void display_write_frame_zoom(const uint8_t *data[]) {
    M5.Lcd.startWrite();
    M5.Lcd.setWindow(frame_x, frame_y, frame_width - 1, frame_height - 1);

    const int srcW = NES_SCREEN_WIDTH;   // 256
    const int srcH = NES_SCREEN_HEIGHT;  // 240

    uint32_t zp = (nesZoomPercent < 100) ? 100u : (uint32_t)nesZoomPercent;
    int roiW = (int)((uint32_t)srcW * 100u / zp);
    int roiH = (int)((uint32_t)srcH * 100u / zp);
    roiW = clampi(roiW, 16, srcW);
    roiH = clampi(roiH, 16, srcH);

    const int roiX0 = (srcW - roiW) / 2;
    const int roiY0 = (srcH - roiH) / 2;

    for (int32_t y = 0; y < frame_height; y++) {
        int srcY = roiY0 + (int)((int32_t)y * roiH / frame_height);
        srcY = clampi(srcY, 0, srcH - 1);

        // bord gauche
        s_nesLineBuf[0] = s_nesLineBuf[1] = s_nesLineBuf[2] = s_nesLineBuf[3] = BLACK;

        int srcX_accum = 0;
        int srcX_step = (roiW << 16) / (frame_width - 5);
        for (int32_t x = 4; x < frame_width - 1; x++) {
            int srcX = roiX0 + (srcX_accum >> 16);
            srcX_accum += srcX_step;
            srcX = clampi(srcX, 0, srcW - 1);
            s_nesLineBuf[x] = myPalette[data[srcY][srcX]];
        }

        // bord droit
        s_nesLineBuf[frame_width - 2] = BLACK;
        s_nesLineBuf[frame_width - 1] = BLACK;

        M5.Lcd.pushPixels(s_nesLineBuf, frame_width);
    }

    M5.Lcd.endWrite();
}

void display_write_frame(const uint8_t *data[]) {
    if (!fullscreenMode) {
        display_write_frame_square(data);
        return;
    }
    if (nesZoomPercent != 100) {
        display_write_frame_zoom(data);
        return;
    }

    M5.Lcd.startWrite();
    M5.Lcd.setWindow(frame_x, frame_y, frame_width - 1, frame_height - 1);

    for (int32_t y = 0; y < frame_height; y++) {
        int srcY = (y * NES_SCREEN_HEIGHT) / frame_height;

        s_nesLineBuf[0] = s_nesLineBuf[1] = s_nesLineBuf[2] = s_nesLineBuf[3] = BLACK;

        int srcX_accum = (4 * NES_SCREEN_WIDTH << 16) / frame_width;
        int srcX_step = (NES_SCREEN_WIDTH << 16) / frame_width;
        for (int32_t x = 4; x < frame_width - 1; x++) {
            int srcX = (srcX_accum >> 16) - 4;
            srcX_accum += srcX_step;
            s_nesLineBuf[x] = myPalette[data[srcY][srcX]];
        }

        s_nesLineBuf[frame_width - 2] = BLACK;
        s_nesLineBuf[frame_width - 1] = BLACK;

        M5.Lcd.pushPixels(s_nesLineBuf, frame_width);
    }
    M5.Lcd.endWrite();
}

void display_clear() { 
    M5.Lcd.fillScreen(TFT_BLACK); 
}


}
