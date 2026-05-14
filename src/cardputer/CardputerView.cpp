#pragma GCC optimize ("Os")

#include "CardputerView.h"
#include "CardputerInput.h"
#include "share/emu_log_cpp.h"
#include "Welcome.h"

M5GFX* CardputerView::Display = nullptr;

namespace {
constexpr uint32_t kSplashDurationMs = 3000;
constexpr uint32_t kSplashFrameMs = 90;
constexpr uint32_t kSplashColorStepMs = 120;
constexpr int kSplashToneChannel = 7;

constexpr uint16_t kSplashPalette[] = {
    0xB81F, // purple
    0xF9A0, // orange
    0xFBAE, // peach
    0xFFE0, // yellow
    0x7E60, // green
    0x07FF, // cyan
    0x435F, // blue
    0xD95F  // magenta
};

struct SplashNote {
    float freq;
    uint16_t durationMs;
};

constexpr SplashNote kSplashTune[] = {
    {261.63f, 170}, {329.63f, 170}, {392.00f, 220}, {329.63f, 150},
    {293.66f, 170}, {349.23f, 170}, {392.00f, 220}, {349.23f, 150},
    {261.63f, 170}, {329.63f, 170}, {392.00f, 170}, {440.00f, 220},
    {392.00f, 170}, {329.63f, 170}, {293.66f, 220}, {261.63f, 260}
};

void drawSplashTitle(M5GFX* display, const std::string& title, int x, int y, uint32_t elapsedMs) {
    const uint8_t paletteCount = sizeof(kSplashPalette) / sizeof(kSplashPalette[0]);
    const uint8_t phase = (elapsedMs / kSplashColorStepMs) % paletteCount;
    int cursorX = x;

    display->fillRect(0, y - 2, display->width(), display->fontHeight() + 6, BACKGROUND_COLOR);

    for (size_t i = 0; i < title.length(); ++i) {
        char c[2] = { title[i], '\0' };
        const uint8_t colorIndex = (i + paletteCount - phase) % paletteCount;
        display->setTextColor(kSplashPalette[colorIndex]);
        display->setCursor(cursorX, y);
        display->printf("%s", c);
        cursorX += display->textWidth(c);
    }
}
}

void CardputerView::initialize() {
    Display = &M5Cardputer.Display;

    // Boost M5GFX SPI
    auto* panel = Display->panel();
    if (panel) {
        auto* bus = (lgfx::Bus_SPI*)panel->bus();
        if (bus) {
            auto bcfg = bus->config();
            bcfg.freq_write  = 80000000;  // 80 MHz
            bcfg.freq_read   = 20000000;  // read, not critical
            bcfg.dma_channel = 1;         // DMA
            bus->config(bcfg);
        }

        auto pcfg = panel->config();
        pcfg.bus_shared = false;
        panel->config(pcfg);
    }

    Display->setRotation(1);
    Display->setTextColor(TEXT_COLOR);
    Display->fillScreen(BACKGROUND_COLOR);
    Display->setTextDatum(middle_center);
    Display->setFont(&fonts::Font0);
}

void CardputerView::showKeymapping(uint8_t numButtons) {
    // SNES = 6 boutons
    if (numButtons == 6) {
        showKeymapping6ButtonsSnes();
        return;
    }

    // layout standard: 2 ou 3
    showKeymappingStandard(numButtons);
}

void CardputerView::showKeymappingStandard(uint8_t numButtons) {
    clearMainView(5);

    Display->fillRoundRect(10, 35, Display->width() - 20, 90, DEFAULT_ROUND_RECT, TFT_BLACK);
    Display->drawRoundRect(10, 35, Display->width() - 20, 90, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    Display->setTextSize(TEXT_WIDE);
    Display->setTextColor(PRIMARY_COLOR);
    Display->drawCenterString("CONTROLS", Display->width() / 2, 45);

    auto keyBox = [&](int x, int y, int w, int h, const char* key) {
        Display->fillRoundRect(x, y, w, h, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
        Display->drawRoundRect(x, y, w, h, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
        const int textYOffset = -2;
        Display->setTextColor(TEXT_COLOR);
        Display->setTextSize(TEXT_MEDIUM);
        Display->drawCenterString(key, x + w/2, y + h/2 + textYOffset);
    };

    // D-PAD
    const int keyW = 22, keyH = 18, gap = 2;
    const int padX = 26, padY = 56;
    keyBox(padX + keyW + gap,   padY,                 keyW, keyH, "E");
    keyBox(padX,                padY + keyH + gap,    keyW, keyH, "A");
    keyBox(padX + keyW + gap,   padY + (keyH+gap)*2,  keyW, keyH, "S");
    keyBox(padX + (keyW+gap)*2, padY + keyH + gap,    keyW, keyH, "D");

    // Boutons
    const int abY = 70;
    const int abGap = gap + 5;

    if (numButtons == 3) {
        const int rightBlockW = keyW * 3 + abGap * 2;
        int abX = Display->width() - 20 - rightBlockW - 5;

        keyBox(abX,                      abY, keyW, keyH, "J");
        keyBox(abX + keyW + abGap,       abY, keyW, keyH, "K");
        keyBox(abX + (keyW + abGap) * 2, abY, keyW, keyH, "L");
    } else {
        const int rightBlockW = keyW * 2 + abGap;
        int abX = Display->width() - 20 - rightBlockW - 10 - 5;

        keyBox(abX,               abY, keyW, keyH, "K");
        keyBox(abX + keyW + abGap,abY, keyW, keyH, "L");
    }

    // START / SELECT
    const int centerX = Display->width() / 2;
    const int ssY = 103, ssW = 26, ssH = 16, ssShiftRight = 10;
    keyBox(centerX - ssW - 6 + ssShiftRight, ssY, ssW, ssH, "1");
    keyBox(centerX + 6 + ssShiftRight,       ssY, ssW, ssH, "2");
}

void CardputerView::showKeymapping6ButtonsSnes() {
    clearMainView(5);

    // Frame
    const int frameX = 10;
    const int frameY = 35;
    const int frameW = Display->width() - 20;
    const int frameH = 90;

    Display->fillRoundRect(frameX, frameY, frameW, frameH, DEFAULT_ROUND_RECT, TFT_BLACK);
    Display->drawRoundRect(frameX, frameY, frameW, frameH, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    // Title
    Display->setTextSize(TEXT_WIDE);
    Display->setTextColor(PRIMARY_COLOR);
    Display->drawCenterString("CONTROLS", Display->width() / 2, 45);

    auto keyBox = [&](int x, int y, int w, int h, const char* key) {
        Display->fillRoundRect(x, y, w, h, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
        Display->drawRoundRect(x, y, w, h, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
        const int textYOffset = -2;
        Display->setTextColor(TEXT_COLOR);
        Display->setTextSize(TEXT_MEDIUM);
        Display->drawCenterString(key, x + w/2, y + h/2 + textYOffset);
    };

    // D-PAD proportions
    const int keyW = 22, keyH = 18, gap = 2;

    // D-PAD
    const int padX = 26, padY = 56;
    keyBox(padX + keyW + gap,   padY,                 keyW, keyH, "E");
    keyBox(padX,                padY + keyH + gap,    keyW, keyH, "A");
    keyBox(padX + keyW + gap,   padY + (keyH+gap)*2,  keyW, keyH, "S");
    keyBox(padX + (keyW+gap)*2, padY + keyH + gap,    keyW, keyH, "D");

    // Shoulder buttons
    const int inset = 3;
    const int lrY   = frameY + inset;
    const int lrW   = 34;
    const int lrH   = 16;

    keyBox(frameX + inset,                 lrY, lrW, lrH, "I"); // L
    keyBox(frameX + frameW - inset - lrW,  lrY, lrW, lrH, "J"); // R

    // ABXY block 
    const int btnGapX = gap + 5;
    const int rowGapY = gap + 2;

    const int topRowY    = 66;
    const int bottomRowY = topRowY + keyH + rowGapY;

    const int rowShiftPx = 10;
    const int blockW     = keyW * 2 + btnGapX;

    const int blockRight = frameX + frameW - inset - 15 - 5;
    const int topRowX    = blockRight - blockW;
    const int botRowX    = topRowX + rowShiftPx;

    // Top row: Y / X
    keyBox(topRowX,                  topRowY,    keyW, keyH, "O");
    keyBox(topRowX + keyW + btnGapX, topRowY,    keyW, keyH, "P");

    // Bottom row: B / A
    keyBox(botRowX,                  bottomRowY, keyW, keyH, "K");
    keyBox(botRowX + keyW + btnGapX, bottomRowY, keyW, keyH, "L");

    // START / SELECT 
    const int centerX = Display->width() / 2 - 5;
    const int ssY = 103, ssW = 26, ssH = 16, ssShiftRight = 10;

    keyBox(centerX - ssW - 6 + ssShiftRight, ssY, ssW, ssH, "1");
    keyBox(centerX + 6 + ssShiftRight,       ssY, ssW, ssH, "2");
}

void CardputerView::welcome() {
    Display->setSwapBytes(true);

    // Remove the welcome image from the binary to save space in case REMOVE_PRINTF is not defined
    #ifdef REMOVE_PRINTF
        // Display->pushImage(0, 0, BGGAMESTATION_S_WIDTH, BGGAMESTATION_S_HEIGHT, bggamestation_s);
    #endif
   
    const std::string title = "Game Station 1.2";
    Display->setTextSize(TEXT_BIG);
    const int titleX = getCenterOffset(title);
    const int titleY = 65;

    if (!M5Cardputer.Speaker.isRunning()) {
        M5Cardputer.Speaker.begin();
    }
    M5Cardputer.Speaker.setVolume(70);

    uint32_t noteStart = 0;
    size_t noteIndex = 0;
    uint32_t start = millis();
    M5Cardputer.Speaker.tone(kSplashTune[0].freq, kSplashTune[0].durationMs, kSplashToneChannel, true);

    while ((millis() - start) < kSplashDurationMs) {
        const uint32_t elapsed = millis() - start;
        drawSplashTitle(Display, title, titleX, titleY, elapsed);

        if (noteIndex < (sizeof(kSplashTune) / sizeof(kSplashTune[0])) &&
            elapsed - noteStart >= kSplashTune[noteIndex].durationMs) {
            ++noteIndex;
            noteStart = elapsed;
            if (noteIndex < (sizeof(kSplashTune) / sizeof(kSplashTune[0]))) {
                M5Cardputer.Speaker.tone(kSplashTune[noteIndex].freq,
                                         kSplashTune[noteIndex].durationMs,
                                         kSplashToneChannel,
                                         true);
            }
        }

        delay(kSplashFrameMs);
    }

    drawSplashTitle(Display, title, titleX, titleY, kSplashDurationMs);
    M5Cardputer.Speaker.stop(kSplashToneChannel);

    Display->setSwapBytes(false);
}

void CardputerView::topBar(const std::string& title, bool submenu, bool searchBar) {
    uint8_t marginX = 4;
    uint8_t marginY = 17;
    int offsetX; // for text align
    size_t limiter; // char limitation
    float sizeText; // pixels offset depending on text size

    clearTopBar();
    Display->setTextSize(TEXT_MEDIUM);

    if (submenu) {
        drawSubMenuReturn(marginX+3, marginY); // for return <
        limiter = 20; // limit string size
    } else {
        limiter = 16;
    }
    
    if (searchBar) {
        Display->setTextColor(TEXT_COLOR);

        // Empty search query
        const std::string searchQuery = title.empty() ? "Type to search" : title.substr(0, limiter);
        drawSearchIcon(Display->width() - 20, marginY-2, 10, PRIMARY_COLOR);
        offsetX = getCenterOffset(searchQuery, Display->width());
        Display->setCursor(offsetX, marginY);
        Display->printf(searchQuery.c_str());
    } else {
        Display->setTextColor(TEXT_COLOR);
        Display->setTextSize(TEXT_WIDE);
        auto truncatedTitle = truncateString(title, 22);
        offsetX = getCenterOffset(truncatedTitle, Display->width());
        Display->setCursor(offsetX, marginY);
        Display->printf(truncatedTitle.c_str());
    }
}

void CardputerView::horizontalSelection(
    const std::vector<std::string>& options,
    uint16_t selectedIndex,
    const std::string& description1,
    const std::string& description2,
    const std::vector<std::string>& icons) {

    if (icons.empty()) {
        horizontalSelectionWithoutIcons(options, selectedIndex, description1, description2);
    } else {
        horizontalSelectionWithIcons(options, selectedIndex, icons);
    }
}

void CardputerView::horizontalSelectionWithIcons(
    const std::vector<std::string>& options,
    uint16_t selectedIndex,
    const std::vector<std::string>& icons) {

    // Clear
    clearMainView(5);

    // Icon
    if (icons[selectedIndex] == "Create Vault") {
        drawFileIcon();
    } else if (icons[selectedIndex] == "Load Vault") {
        drawVaultIcon();
    } else if (icons[selectedIndex] == "Load File") {
        drawSdCardIcon();
    } else if (icons[selectedIndex] == "Settings") {
        drawSettingsIcon();
    } else if (icons[selectedIndex] == "New Password") {
        drawPlusIcon();
    } else if (icons[selectedIndex] == "Del Password") {
        drawMinusIcon();
    } else if (icons[selectedIndex] == "My Passwords") {
        drawLockIcon();
    }

    // Name box
    Display->fillRoundRect(38, 93, Display->width() - 77, 35, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
    Display->drawRoundRect(38, 93, Display->width() - 77, 35, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    // Name
    Display->setTextSize(TEXT_BIG);
    Display->setTextColor(TEXT_COLOR);
    Display->setCursor(getCenterOffset(options[selectedIndex]), 110);
    Display->printf(options[selectedIndex].c_str());

    // Arrows for navigation
    Display->setTextColor(RECT_COLOR_DARK);
    Display->setCursor(17, 50);
    Display->printf("<");
    Display->setCursor(Display->width() - 30, 50);
    Display->printf(">");

    // Reset text color to default
    Display->setTextColor(PRIMARY_COLOR);
    Display->setTextSize(TEXT_MEDIUM);
}

void CardputerView::horizontalSelectionWithoutIcons(
    const std::vector<std::string>& options,
    uint16_t selectedIndex,
    const std::string& description1,
    const std::string& description2) {

    // Clear
    clearMainView(5);

    // Box frame
    Display->drawRoundRect(10, 35, Display->width() - 20, 90, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    // Infos
    Display->setTextSize(TEXT_WIDE);
    Display->setTextColor(PRIMARY_COLOR);
    Display->setCursor(getCenterOffset(description1), 48);
    Display->printf(description1.c_str());
    Display->setCursor(getCenterOffset(description2), 113);
    Display->printf(description2.c_str());

    // Name box
    Display->fillRoundRect(40, 63, Display->width() - 80, 35, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
    Display->drawRoundRect(40, 63, Display->width() - 80, 35, DEFAULT_ROUND_RECT, RECT_COLOR_LIGHT);

    // Name
    Display->setTextSize(TEXT_WIDE);
    Display->setTextColor(TEXT_COLOR);
    Display->setCursor(getCenterOffset(options[selectedIndex]), 80);
    Display->printf(options[selectedIndex].c_str());

    // Arrows for navigation
    Display->setCursor(20, 78);
    Display->printf("<");
    Display->setCursor(Display->width() - 27, 78);
    Display->printf(">");

    // Reset text color to default
    Display->setTextColor(PRIMARY_COLOR);
    Display->setTextSize(TEXT_MEDIUM);
}

void CardputerView::verticalSelection(
    const std::vector<std::string>& options,
    uint16_t selectedIndex,
    size_t visibleRows,
    const std::vector<std::string>& optionLabels,
    const std::vector<std::string>& shortcuts,
    bool visibleMention) {

    if (options.empty()) {
        clearMainView();
        Display->setTextSize(TEXT_BIG);
        Display->fillRoundRect(48, 59, 142, 36, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
        Display->drawRoundRect(48, 59, 142, 36, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
        Display->drawCenterString("No results", 120, 70);
        return;
    }

    if (!optionLabels.empty()) {
        verticalSelectionWithLabelsAndShortcuts(options, selectedIndex, visibleRows, optionLabels, shortcuts, visibleMention);
    } else {
        verticalSelectionSimple(options, selectedIndex, visibleRows);
    }
}

void CardputerView::verticalSelectionSimple(
    const std::vector<std::string>& options,
    uint16_t selectedIndex,
    size_t visibleRows) {

    size_t totalOptions = options.size();
    size_t currentStartRow = selectedIndex / visibleRows * visibleRows;

    clearMainView();

    for (size_t i = 0; i < visibleRows; ++i) {
        size_t index = currentStartRow + i;
        if (index >= totalOptions) break;

        bool isSelected = (index == selectedIndex);
        int x = DEFAULT_MARGIN;
        int y = TOP_BAR_HEIGHT + (i * 26);

        drawRect(isSelected, DEFAULT_MARGIN, y, Display->width() - 13, 22, 0);

        Display->setCursor(x + 10, y + 12);
        Display->setTextSize(TEXT_LARGE);

        // Color by extension
        std::string name = options[index];
        std::string ext;
        size_t dotPos = name.find_last_of('.');
        if (dotPos != std::string::npos) {
            ext = name.substr(dotPos + 1);
        }

        auto color = isSelected ? TEXT_COLOR : colorForExt(ext);

        if (ext.empty()) {
            name = "/" + name;
            color = isSelected ? TEXT_COLOR : FOLDER_COLOR;
        } 
        Display->setTextColor(color);

        Display->printf(truncateString(name, 20).c_str());
    }
}

void CardputerView::drawSelectedRowMarquee(const std::string& text,
                                           uint16_t rowInPage,
                                           size_t visibleRows) {
    const int rowH   = 26;
    const int boxH   = 22;
    const int boxX   = DEFAULT_MARGIN;
    const int boxW   = Display->width() - 13;
    const int padL   = 10;
    const int viewPadR = 3;

    const int y = TOP_BAR_HEIGHT + (int)rowInPage * rowH;

    drawRect(true, boxX, y, boxW, boxH, 0);

    const int viewX = boxX + padL;
    const int viewW = boxW  - padL - viewPadR;

    Display->setTextSize(TEXT_LARGE);
    Display->setTextColor(TEXT_COLOR);

    Display->setTextWrap(false);
    Display->setTextDatum(textdatum_t::top_left);

    Display->setClipRect(viewX, y, viewW, boxH);

    const int textY = y + (boxH - /*approx hauteur police*/16)/2;
    Display->drawString(text.c_str(), viewX, textY);

    Display->clearClipRect();
    Display->setTextDatum(middle_center);
}

void CardputerView::verticalSelectionWithLabelsAndShortcuts(
    const std::vector<std::string>& options,
    uint16_t selectedIndex,
    size_t visibleRows,
    const std::vector<std::string>& optionLabels,
    const std::vector<std::string>& shortcuts,
    bool visibleMention
    ) {

    size_t totalOptions = options.size();
    size_t currentStartRow = selectedIndex / visibleRows * visibleRows;

    clearMainView();

    for (size_t i = 0; i < visibleRows; ++i) {
        size_t index = currentStartRow + i;
        if (index >= totalOptions) break;

        bool isSelected = (index == selectedIndex);
        int x = DEFAULT_MARGIN;
        int y = TOP_BAR_HEIGHT + (i * 26);

        drawRect(isSelected, DEFAULT_MARGIN, y, Display->width() - 13, 22, 0);

        // Label
        Display->setTextSize(TEXT_SMALL);
        auto labelWidth = Display->textWidth(optionLabels[index].c_str());
        Display->fillRoundRect(x, y, labelWidth+15, 22, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
        Display->drawRoundRect(x, y, labelWidth+16, 22, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
        Display->setCursor(x + 8, y + 12);
        Display->setTextColor(PRIMARY_COLOR);
        Display->printf(optionLabels[index].c_str());

        // Option principale
        auto truncatedOption = truncateString(options[index], 18);
        truncatedOption = optionLabels[index] == "Pass" ? std::string(truncatedOption.size(), '*') : truncatedOption;
        Display->setCursor(labelWidth+28, y + 12);
        Display->setTextSize(TEXT_WIDE);
        Display->setTextColor(TEXT_COLOR);
        Display->printf(truncatedOption.c_str());

        // Raccourci
        if (!shortcuts.empty()) {
            Display->fillRoundRect(Display->width() - 44, y - 1, 39, 22, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
            Display->drawRoundRect(Display->width() - 45, y, 40, 22, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
            Display->setTextColor(TEXT_COLOR);
            Display->setTextSize(TEXT_WIDE);
            Display->setCursor(Display->width() - 28, y+12);
            Display->printf(shortcuts[index].c_str());
        }

    }
    
    // Keyboard mention
    if (visibleMention) {
        Display->setTextSize(TEXT_WIDE);
        Display->setTextColor(PRIMARY_COLOR);
        std::string mention = "Plug in USB as keyboard";
        Display->setCursor(getCenterOffset(mention), 122);
        Display->printf(mention.c_str());
    }
}

void CardputerView::subMessage(std::string message, int delayMs) {
    // Clear
    clearMainView(5);

    // Box frame
    Display->drawRoundRect(10, 35, Display->width() - 20, 90, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    // Description
    Display->setTextSize(TEXT_WIDE);
    Display->setTextColor(TEXT_COLOR);
    Display->setCursor(getCenterOffset(message), 80);
    Display->printf(message.c_str());
    Display->setTextSize(TEXT_MEDIUM);

    if (delayMs) {
        delay(delayMs);
    }
}

void CardputerView::stringPrompt(std::string label, std::string value, bool backButton, size_t minLength) {
    // Clear
    clearMainView(5);

    // Box frame
    Display->drawRoundRect(10, 35, Display->width() - 20, 90, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    // Description
    Display->setTextSize(TEXT_MEDIUM);
    Display->setTextColor(TEXT_COLOR);
    Display->setCursor(getCenterOffset(label), 48);
    Display->printf(label.c_str());

    // Check the length of the input and truncate if necessary
    std::string truncatedInput;
    if (value.length() > 20) {
        truncatedInput = value.substr(value.length() - 20); // Get the last characters
    } else {
        truncatedInput = value;
    }

    // input
    Display->setTextSize(TEXT_MEDIUM_LARGE);
    Display->fillRoundRect(42, 62, 155, 25, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
    Display->setCursor(51, 73);
    Display->printf(truncatedInput.c_str());

    size_t xPos = 110; 
    if (backButton) {
        // < button
        Display->fillRoundRect(53, 95, 40, 20, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
        Display->drawRoundRect(53, 95, 40, 20, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
        Display->setCursor(70, 105);
        Display->printf("<");
        xPos = 135;
    }

    // Button ok
    if (value.length() < minLength) {
        Display->fillRoundRect(xPos-30, 95, 80, 20, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
        Display->drawRoundRect(xPos-30, 95, 80, 20, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
    } else {
        Display->fillRoundRect(xPos-30, 95, 80, 20, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
    }

    Display->setTextSize(TEXT_MEDIUM);
    Display->setCursor(xPos+5, 106);
    Display->printf("OK");
}

void CardputerView::confirmationPrompt(std::string label) {
    // Clear
    clearMainView(5);

    // Box frame
    Display->drawRoundRect(8, 35, Display->width() - 20, 90, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    // Description
    Display->setTextSize(TEXT_WIDE);
    Display->setTextColor(TEXT_COLOR);
    auto truncatedLabel= truncateString(label, 24);
    Display->setCursor(getCenterOffset(truncatedLabel), 62);
    Display->printf(truncatedLabel.c_str());
    Display->setTextSize(TEXT_MEDIUM);

    // < button
    Display->fillRoundRect(65, 85, 40, 20, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
    Display->drawRoundRect(65, 85, 40, 20, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
    Display->setCursor(81, 96);
    Display->printf("<");
    
    // ok button
    Display->setTextSize(1.5);
    Display->fillRoundRect(128, 85, 40, 20, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
    Display->setCursor(140, 96);
    Display->printf("OK");
    Display->setTextSize(TEXT_WIDE);
}

void CardputerView::value(std::string label, std::string value) {
    clearMainView(5);

    // Initial text size
    float textSize = TEXT_BIG;

    // Adjust text size to fit the screen width
    adjustTextSizeToFit(value, Display->width() - 40, textSize);

    // Determine position
    auto x = getCenterOffset(value);
    auto y = 40;
    auto biggerThanScreen = Display->textWidth(value.c_str()) > Display->width();

    // Draw background if the text fits on the screen
    if (!biggerThanScreen) {
        y = 60;
        Display->fillRoundRect(x - 9, y - 15, Display->textWidth(value.c_str()) + 18, 32, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
        Display->drawRoundRect(x - 9, y - 15, Display->textWidth(value.c_str()) + 18, 32, DEFAULT_ROUND_RECT, RECT_COLOR_LIGHT);
    }

    Display->setCursor(x, y);
    Display->setTextColor(TEXT_COLOR);
    Display->printf(value.c_str());

    // Display "M" button
    Display->setTextSize(1.6);
    Display->fillRoundRect(38, 95, 20, 15, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
    Display->setTextColor(TEXT_COLOR);
    Display->setCursor(45, 102);
    Display->printf("m");

    Display->setCursor(68, 103);
    Display->setTextColor(PRIMARY_COLOR);
    Display->printf("to modify field");

    // Display "OK" button
    Display->fillRoundRect(33, 117, 30, 15, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
    Display->setTextColor(TEXT_COLOR);
    Display->setCursor(40, 125);
    Display->printf("ok");

    Display->setCursor(72, 125);
    Display->setTextColor(PRIMARY_COLOR);
    Display->printf("to send via USB");

    Display->setTextColor(TEXT_COLOR);
}

void CardputerView::drawRect(bool selected, uint8_t margin, uint16_t startY, uint16_t sizeX, uint16_t sizeY, uint16_t stepY) {
        // Draw rect
        if (selected) {
            Display->fillRoundRect(margin, startY + stepY, sizeX, sizeY, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
            Display->setTextColor(TEXT_COLOR);
        } else {
            Display->fillRoundRect(margin, startY + stepY , sizeX, sizeY, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
            Display->drawRoundRect(margin, startY + stepY, sizeX, sizeY, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
            Display->setTextColor(TEXT_COLOR);
        }
}

void CardputerView::drawSubMenuReturn(uint8_t x, uint8_t y) {
    Display->setTextSize(TEXT_WIDE);
    Display->setTextColor(PRIMARY_COLOR);
    Display->setCursor(x, y);
    Display->printf("<");
}

void CardputerView::drawSearchIcon(int x, int y, int size, uint16_t color) {
    int radius = size / 2;
    int handleLength = size / 2;

    // Dessiner cercle
    Display->drawCircle(x, y, radius, color);
    // Dessiner poignée
    Display->drawLine(x + radius, y + radius, x + radius + handleLength, y + radius + handleLength, color);
}

void CardputerView::clearMainView(uint8_t offsetY) {
    Display->fillRect(0, TOP_BAR_HEIGHT-offsetY, Display->width(), Display->height(), BACKGROUND_COLOR);
}

void CardputerView::clearTopBar() {
    Display->fillRect(0, 0, Display->width(), TOP_BAR_HEIGHT, BACKGROUND_COLOR);
}

void CardputerView::debug(const std::string& message) {
    Display->setTextSize(TEXT_MEDIUM);
    Display->fillScreen(TFT_BLACK);
    Display->setCursor(100, 10);
    Display->printf("DEBUG");
    Display->setCursor(10, 50);
    Display->printf(message.c_str());
    delay(3000);
}

void CardputerView::setBrightness(uint16_t brightness) {
    Display->setBrightness(brightness);
}

uint8_t CardputerView::getBrightness() {
    return Display->getBrightness();
}

int CardputerView::getCenterOffset(const std::string& text, int screenWidth) {
    // Measure the width of the text in pixels using the current font and size
    int textWidth = Display->textWidth(text.c_str());

    // Calculate the centered X position
    return (screenWidth - textWidth) / 2;
}

void CardputerView::drawVaultIcon(int x, int y, uint16_t color, size_t w, size_t h) {
    // Taille ajustée de l'icône
    int width = w;  // Largeur du coffre
    int height = h; // Hauteur de la base
    int lidHeight = 20; // Hauteur du couvercle

    // Fond de l'icône
    Display->fillRect(x - 5, y - 5, width + 10, height + lidHeight + 10, BACKGROUND_COLOR);

    // Dessin du coffre
    // Base rectangulaire du coffre
    Display->fillRect(x, y + lidHeight, width, height, PRIMARY_COLOR);

    // Dessin du couvercle
    Display->fillRect(x, y, width, lidHeight, PRIMARY_COLOR);

    // Dessin de la séparation entre le couvercle et la base
    Display->drawLine(x, y + lidHeight, x + width, y + lidHeight, RECT_COLOR_DARK);

    // Ajout d'une poignée rectangulaire sur le couvercle
    int handleWidth = 30;
    int handleHeight = 5;
    Display->fillRect(x + (width / 2) - (handleWidth / 2), y + 5, handleWidth, handleHeight, RECT_COLOR_DARK);

    // Serrure centrale
    int lockWidth = 12;
    int lockHeight = 18;
    int lockX = x + (width / 2) - (lockWidth / 2);
    int lockY = y + lidHeight + (height / 2) - (lockHeight / 2);
    Display->fillRect(lockX, lockY, lockWidth, lockHeight, RECT_COLOR_DARK);

    // Dessin de l'intérieur de la serrure (cylindre)
    int lockInnerWidth = 4;
    int lockInnerHeight = 6;
    int lockInnerX = lockX + (lockWidth / 2) - (lockInnerWidth / 2);
    int lockInnerY = lockY + 6;
    Display->fillRect(lockInnerX, lockInnerY, lockInnerWidth, lockInnerHeight, PRIMARY_COLOR);

    // Renforcement visuel avec des contours
    Display->drawRect(x, y, width, lidHeight, RECT_COLOR_DARK); // Contour du couvercle
    Display->drawRect(x, y + lidHeight, width, height, RECT_COLOR_DARK); // Contour de la base

    // Ajout de renforts sur le coffre
    int strapWidth = 4;
    Display->fillRect(x + 10, y + lidHeight, strapWidth, height, RECT_COLOR_DARK); // Renfort gauche
    Display->fillRect(x + width - 10 - strapWidth, y + lidHeight, strapWidth, height, RECT_COLOR_DARK); // Renfort droit
}

void CardputerView::drawSettingsIcon(int x, int y) {
  Display->fillRect(x,y,80,80,BACKGROUND_COLOR);
  int i=0;
  for(i=0;i<6;i++) {
    Display->fillArc(40 + x, 40 + y, 30, 20, 15 + 60 * i, 45 + 60 * i, PRIMARY_COLOR);
  }
  Display->fillArc(40 + x, 40 + y, 22, 8, 0, 360, PRIMARY_COLOR);
}

void CardputerView::drawFileIcon(int x, int y) {
    Display->fillRect(x-10, y, 90, 80, BACKGROUND_COLOR);

    int iconW = 58;  // Largeur de l'icône
    int iconH = 72;  // Hauteur de l'icône
    int foldSize = iconH / 4;  // Taille du pli en haut à droite

    // Dessin du fichier
    Display->fillRect(x, y, iconW, iconH, RECT_COLOR_DARK);
    Display->drawRect(x, y, iconW, iconH, PRIMARY_COLOR);

    // Effacer les coins pour le pli
    Display->fillRect(x + iconW - foldSize, y - 1, foldSize, 2, BACKGROUND_COLOR);
    Display->fillRect(x + iconW - 1, y, 2, foldSize, BACKGROUND_COLOR);
    Display->fillTriangle(x + iconW, y, x + iconW - foldSize, y, x + iconW, y + foldSize, BACKGROUND_COLOR);

    // Dessin du pli
    Display->drawTriangle(
        x + iconW - foldSize, y,
        x + iconW - foldSize, y + foldSize - 1,
        x + iconW - 1, y + foldSize - 1,
        PRIMARY_COLOR
    );

    // Dessin du petit coffre au centre
    int chestWidth = iconW / 2;  // Largeur du coffre
    int chestHeight = iconH / 4;  // Hauteur du coffre
    int chestX = x + (iconW / 2) - (chestWidth / 2);  // Centrage horizontal
    int chestY = y + (iconH / 2) - (chestHeight / 2);  // Centrage vertical
    int lidHeight = chestHeight / 3;  // Hauteur du couvercle

    // Base du coffre
    Display->fillRect(chestX, chestY + lidHeight, chestWidth, chestHeight - lidHeight, PRIMARY_COLOR);
    Display->drawRect(chestX, chestY + lidHeight, chestWidth, chestHeight - lidHeight, RECT_COLOR_DARK);

    // Couvercle du coffre
    Display->fillRect(chestX, chestY, chestWidth, lidHeight, PRIMARY_COLOR);
    Display->drawRect(chestX, chestY, chestWidth, lidHeight, RECT_COLOR_DARK);

    // Serrure du coffre
    int lockWidth = chestWidth / 6;
    int lockHeight = chestHeight / 6;
    int lockX = chestX + (chestWidth / 2) - (lockWidth / 2);
    int lockY = chestY + lidHeight + (chestHeight / 2) - (lockHeight / 2);
    Display->fillRect(lockX, lockY-4, lockWidth, lockHeight, RECT_COLOR_DARK);
}

void CardputerView::drawSdCardIcon(int x, int y) {
    Display->fillRect(x-10, y, 90, 80, BACKGROUND_COLOR);

    // Taille de l'icône
    int cardWidth = 60;  // Largeur de la carte SD
    int cardHeight = 70; // Hauteur de la carte SD
    int notchWidth = 12; // Largeur de l'encoche
    int notchHeight = 15; // Hauteur de l'encoche

    // Dessiner le corps principal de la carte SD
    Display->drawRoundRect(x, y, cardWidth, cardHeight, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    // Dessiner les contacts métalliques
    int contactWidth = 6; // Largeur de chaque contact
    int contactSpacing = 4; // Espacement entre les contacts
    int numContacts = 5; // Nombre de contacts
    int contactStartX = x + 7;
    int contactStartY = y + cardHeight - 10 + 2;

    // Ajouter des détails au centre de la carte SD
    int innerRectX = x + 8;
    int innerRectY = y + 8;
    int innerRectWidth = cardWidth - 16;
    int innerRectHeight = cardHeight - 20;
    Display->fillRoundRect(innerRectX, innerRectY, innerRectWidth, innerRectHeight, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
    Display->drawRoundRect(innerRectX, innerRectY, innerRectWidth, innerRectHeight, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    // // Dessiner "SD" au milieu du carré central
    int textX = innerRectX + innerRectWidth / 2;
    int textY = innerRectY + 10 + innerRectHeight / 2;

    Display->setTextSize(2); // Taille du texte
    Display->setTextColor(PRIMARY_COLOR);
    Display->setCursor(textX - (Display->textWidth("SD") / 2), textY - (Display->fontHeight() / 2));
    Display->print("SD");


    for (int i = 0; i < numContacts; i++) {
        Display->fillRect(contactStartX + i * (contactWidth + contactSpacing), contactStartY, contactWidth, 6, PRIMARY_COLOR);
    }
}

void CardputerView::drawLockIcon(int x, int y, uint16_t color, size_t w, size_t h) {
    Display->fillRect(x-10, y, 90, 80, BACKGROUND_COLOR);
    int lockWidth = w;     // Largeur du corps du cadenas
    int lockHeight = h;    // Hauteur du corps du cadenas
    int shackleWidth = 40;  // Largeur de l'anse
    int shackleHeight = 20; // Hauteur de l'anse
    int shackleOffset = 10; // Décalage vertical de l'anse par rapport au corps

    // Fond de l'icône
    Display->fillRect(x - lockWidth / 2 - 2, y - shackleHeight - lockHeight - 4, 
                      lockWidth + 4, lockHeight + shackleHeight + 6, BACKGROUND_COLOR);

    // Dessiner l'anse
    int shackleStartX = x - shackleWidth / 2;
    int shackleStartY = y - lockHeight - shackleOffset;
    Display->fillArc(shackleStartX + shackleWidth / 2, shackleStartY + shackleHeight / 2,
                     shackleWidth / 2, shackleWidth / 2 - 4, 180, 360, color);

    // Dessiner le corps du cadenas
    int lockX = x - lockWidth / 2;
    int lockY = y - lockHeight;
    Display->fillRect(lockX, lockY, lockWidth, lockHeight, color);

    // Dessiner l'intérieur du cadenas
    int innerLockWidth = lockWidth - 4;
    int innerLockHeight = lockHeight - 4;
    Display->fillRect(lockX + 2, lockY + 2, innerLockWidth, innerLockHeight, RECT_COLOR_DARK);

    // Ajouter un cercle pour représenter la serrure
    int lockCenterX = lockX + lockWidth / 2;
    int lockCenterY = lockY + lockHeight / 2;
    int lockHoleRadius = 5;

    Display->drawCircle(lockCenterX, lockCenterY, lockHoleRadius, RECT_COLOR_DARK);
    Display->fillCircle(lockCenterX, lockCenterY, lockHoleRadius - 2, color);
}

void CardputerView::drawPlusIcon(int x, int y, uint16_t color) {
    Display->fillRect(x-30, y-90, 120, 90, BACKGROUND_COLOR);
    int size = 50;
    int thickness = size / 5; // Épaisseur des lignes
    int halfSize = size / 2;

    // Fond de l'icône
    Display->fillRect(x - size / 2 - 2, y - size / 2 - 2, size + 4, size + 4, BACKGROUND_COLOR);

    // Ligne horizontale
    Display->fillRect(x - halfSize, y - thickness / 2, size, thickness, RECT_COLOR_DARK);
    Display->drawRect(x - halfSize, y - thickness / 2, size, thickness, color);

    // Ligne verticale
    Display->fillRect(x - thickness / 2, y - halfSize, thickness, size, RECT_COLOR_DARK);
    Display->drawRect(x - thickness / 2, y - halfSize, thickness, size, color);
}

void CardputerView::drawMinusIcon(int x, int y, uint16_t color) {
    Display->fillRect(x-30, y-90, 120, 90, BACKGROUND_COLOR);
    int size = 50;
    int thickness = size / 5;
    int halfSize = size / 2;

    // Fond de l'icône
    Display->fillRect(x - size / 2 - 2, y - size / 2 - 2, size + 4, size + 4, BACKGROUND_COLOR);

    // Ligne horizontale
    Display->fillRect(x - halfSize, y - thickness / 2, size, thickness, RECT_COLOR_DARK);
    Display->drawRect(x - halfSize, y - thickness / 2, size, thickness, color);
}


std::string CardputerView::truncateString(const std::string& input, size_t maxLength) {
    const std::string ellipsis = "...";

    if (input.length() <= maxLength) {
        return input;
    }

    // Calcul char number each side
    size_t halfLength = (maxLength - ellipsis.length()) / 2;

    // Start of the fist string, end of the second
    std::string firstPart = input.substr(0, halfLength);
    std::string secondPart = input.substr(input.length() - halfLength);

    // Concat with "..."
    return firstPart + ellipsis + secondPart;
}

void CardputerView::adjustTextSizeToFit(const std::string& text, uint16_t maxWidth, float textSize) {
    Display->setTextSize(textSize);
    
    while (Display->textWidth(text.c_str()) > maxWidth && textSize > TEXT_WIDE) {
        textSize-= 0.01; // Réduit progressivement la taille du texte
        Display->setTextSize(textSize);
    }
}

uint16_t CardputerView::colorForExt(const std::string& extRaw) const {
    std::string ext = extRaw;
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;

    if (ext == ".nes") return NES_COLOR;
    if (ext == ".sms") return SMS_COLOR;
    if (ext == ".gg")  return GAMEGEAR_COLOR;
    if (ext == ".sg" || ext == ".sc") return SG1000_COLOR;
    if (ext == ".col" || ext == ".cv") return COLECO_COLOR;
    if (ext == ".ngp" || ext == ".ngc") return NEOGEO_COLOR;
    if (ext == ".md") return GENESIS_COLOR;
    if (ext == ".ws" || ext == ".wsc") return WS_COLOR;
    if (ext == ".pce") return PCE_COLOR;
    if (ext == ".gb" || ext == ".gbc") return GAMEBOY_COLOR;
    if (ext == ".lnx") return LYNX_COLOR;
    if (ext == ".sfc" || ext == ".smc") return SNES_COLOR;
    if (ext == ".mx1" || ext == ".rom") return MSX1_COLOR;
    if (ext == ".a78") return ATARI_COLOR;
    if (ext == ".a26") return ATARI_COLOR;
    if (ext == ".cpr") return GX4000_COLOR;

    return TEXT_COLOR;
}

void CardputerView::showValidExt(const std::vector<std::string>& exts) {
    clearMainView(5);

    const int boxX = 10;
    const int boxY = 35;
    const int boxW = Display->width() - 20;
    const int boxH = 90;

    Display->fillRoundRect(boxX, boxY, boxW, boxH, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
    Display->drawRoundRect(boxX, boxY, boxW, boxH, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

    const int innerPad = 8;
    const int innerW = boxW - innerPad * 2;
    const int footerH = 12;
    const int maxBottom = boxY + boxH - footerH;
    const int colGap = 5;
    const int rowGap = 4;
    const int badgeRadius = 4;
    const int badgeHPadding = 7;
    const int badgeVPadding = 2;

    Display->setTextSize(TEXT_SMALL);
    Display->setTextColor(TEXT_COLOR);

    struct Badge { std::string raw; std::string txt; int w; int h; };
    std::vector<Badge> badges;
    badges.reserve(exts.size());

    for (const auto& raw : exts) {
        bool already = false;
        for (const auto& b : badges) {
            if (b.raw == raw) {
                already = true;
                break;
            }
        }
        if (already) continue;

        std::string txt = raw;
        for (auto& c : txt) c = (char)std::toupper((unsigned char)c);

        const int textW = Display->textWidth(txt.c_str());
        const int textH = Display->fontHeight();
        badges.push_back(Badge{raw, txt, textW + badgeHPadding * 2, textH + badgeVPadding * 2});
    }

    std::vector<std::vector<Badge>> rows;
    std::vector<Badge> current;
    int currentW = 0;

    for (const auto& b : badges) {
        const int extra = current.empty() ? 0 : colGap;
        if (!current.empty() && currentW + extra + b.w > innerW) {
            rows.push_back(current);
            current.clear();
            currentW = 0;
        }
        current.push_back(b);
        currentW += (current.size() == 1 ? 0 : colGap) + b.w;
    }
    if (!current.empty()) rows.push_back(current);

    const int lineH = Display->fontHeight() + badgeVPadding * 2;
    int totalH = (int)rows.size() * lineH + ((int)rows.size() - 1) * rowGap;
    if (totalH < 0) totalH = 0;

    int y = boxY + 3 + (maxBottom - (boxY + 8) - totalH) / 2;
    if (y < boxY + 7) y = boxY + 7;

    for (const auto& row : rows) {
        int rowW = 0;
        for (size_t i = 0; i < row.size(); ++i) {
            rowW += row[i].w;
            if (i + 1 < row.size()) rowW += colGap;
        }

        int x = boxX + innerPad + (innerW - rowW) / 2;

        for (const auto& b : row) {
            const uint16_t accent = colorForExt(b.raw);
            const uint16_t stroke = (accent == TEXT_COLOR) ? PRIMARY_COLOR : accent;

            Display->fillRoundRect(x, y, b.w, b.h, badgeRadius, RECT_COLOR_DARK);
            Display->drawRoundRect(x, y, b.w, b.h, badgeRadius, stroke);

            const int textW = Display->textWidth(b.txt.c_str());
            const int textH = Display->fontHeight();
            const int textX = x + (b.w - textW) / 2;
            const int textY = y + (b.h - textH) / 2 + textH - 3;

            Display->setCursor(textX, textY);
            Display->setTextColor(TEXT_COLOR);
            Display->printf("%s", b.txt.c_str());

            x += b.w + colGap;
        }

        y += lineH + rowGap;
        if (y > maxBottom) break;
    }

    Display->setTextSize(TEXT_SMALL);
    Display->setTextColor(PRIMARY_COLOR);
    Display->drawCenterString("Press any key", Display->width() / 2, boxY + boxH - 15);

    Display->setTextSize(TEXT_MEDIUM);
    Display->setTextColor(TEXT_COLOR);
}

void CardputerView::copyProgress(size_t total, size_t current, void* userCtx) {
    if (!Display) return;

    static uint32_t lastDraw = 0;
    static bool initialized = false;
    static int lastFilledW = 0;

    uint32_t now = millis();
    if (now - lastDraw < 50 && current != total) return;
    lastDraw = now;

    float ratio = (total > 0 ? (float)current / (float)total : 0.0f);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    // Cadre
    const int boxX = 10;
    const int boxY = 35;
    const int boxW = Display->width() - 20;
    const int boxH = 90;

    // Barre
    const int barH = 18;
    const int barW = boxW - 40;
    const int barX = boxX + (boxW - barW) / 2;
    const int barY = boxY + (boxH - barH) / 2;

    // Texte taille
    const int sizeY = boxY + boxH - 18;
    char sizeStr[32];
    snprintf(sizeStr, sizeof(sizeStr), "%.0f KB", total / 1024.0f);

    // init
    if (!initialized || current == 0) {
        initialized = true;
        lastFilledW = 0;

        Display->fillRect(
            0,
            TOP_BAR_HEIGHT,
            Display->width(),
            Display->height(),
            BACKGROUND_COLOR
        );

        // Cadre
        Display->fillRoundRect(boxX, boxY, boxW, boxH, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);
        Display->drawRoundRect(boxX, boxY, boxW, boxH, DEFAULT_ROUND_RECT, PRIMARY_COLOR);

        // Titre
        Display->setTextSize(TEXT_WIDE);
        Display->setTextColor(PRIMARY_COLOR);
        Display->drawCenterString("Writing ROM...", Display->width() / 2, boxY + 16);

        // Fond barre
        Display->fillRoundRect(barX, barY, barW, barH, DEFAULT_ROUND_RECT, RECT_COLOR_DARK);

        // Taille
        Display->setTextSize(TEXT_SMALL);
        Display->setTextColor(TEXT_COLOR);
        Display->drawCenterString(sizeStr, Display->width() / 2, sizeY);
    }

    // Only delta
    int filledW = (int)(barW * ratio);
    if (filledW > lastFilledW) {
        Display->fillRect(barX + lastFilledW, barY, filledW - lastFilledW, barH, PRIMARY_COLOR);
        lastFilledW = filledW;
    }
}

void CardputerView::displaySnesInfo() {
    Display->fillScreen(BACKGROUND_COLOR);

    // Box frame
    Display->drawRect(1, 1, Display->width() - 1, Display->height() - 1, PRIMARY_COLOR);

    // Main title
    Display->setTextSize(TEXT_BIG);
    Display->setTextColor(PRIMARY_COLOR);

    {
        std::string title = "SNES EMULATOR";
        auto x = getCenterOffset(title, Display->width());
        Display->setCursor(x, 22);
        Display->printf("%s", title.c_str());
    }

    // Sub title (line 1)
    Display->setTextSize(TEXT_MEDIUM_WIDE);
    Display->setTextColor(TEXT_COLOR);
    {
        std::string l1 = "SNES emulation is experimental";
        auto x = getCenterOffset(l1, Display->width());
        Display->setCursor(x, 46);
        Display->printf("%s", l1.c_str());
    }

    // Text line 2
    {
        std::string l2 = "and often cannot run properly";
        auto x = getCenterOffset(l2, Display->width());
        Display->setCursor(x, 65);
        Display->printf("%s", l2.c_str());
    }

    // Highlight line (line 3)
    Display->setTextColor(PRIMARY_COLOR);
    Display->setTextSize(TEXT_MEDIUM_LARGE);
    {
        std::string warn = "Hardware limits on Cardputer";
        auto x = getCenterOffset(warn, Display->width());
        Display->setCursor(x, 88);
        Display->printf("%s", warn.c_str());
    }

    // Button OK
    Display->fillRoundRect(70, 105, 100, 20, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
    Display->setTextColor(TEXT_COLOR);
    Display->setTextSize(TEXT_MEDIUM_WIDE);
    Display->setCursor(82, 115);
    Display->printf("OK to start");
}

void CardputerView::displayMsxInfo() {
    Display->fillScreen(BACKGROUND_COLOR);

    // Box frame
    Display->drawRect(1, 1, Display->width() - 1, Display->height() - 1, PRIMARY_COLOR);

    // Main title
    Display->setTextSize(TEXT_BIG);
    Display->setTextColor(PRIMARY_COLOR);

    {
        std::string title = "MSX EMULATOR";
        auto x = getCenterOffset(title, Display->width());
        Display->setCursor(x, 18);
        Display->printf("%s", title.c_str());
    }

    // Text lines
    Display->setTextSize(TEXT_MEDIUM_WIDE);
    Display->setTextColor(TEXT_COLOR);
    {
        std::string l1 = "An open BIOS is built-in";
        auto x = getCenterOffset(l1, Display->width());
        Display->setCursor(x, 40);
        Display->printf("%s", l1.c_str());
    }
    {
        std::string l2 = "You can use MSX.ROM from SD";
        auto x = getCenterOffset(l2, Display->width());
        Display->setCursor(x, 56);
        Display->printf("%s", l2.c_str());
    }

    Display->setTextColor(PRIMARY_COLOR);
    Display->setTextSize(TEXT_MEDIUM);
    {
        std::string l3 = "You can type keys and numbers";
        auto x = getCenterOffset(l3, Display->width());
        Display->setCursor(x, 76);
        Display->printf("%s", l3.c_str());
    }

    Display->setTextColor(TEXT_COLOR);
    {
        std::string l4 = "Use FN+key for MSX keyboard";
        auto x = getCenterOffset(l4, Display->width());
        Display->setCursor(x, 92);
        Display->printf("%s", l4.c_str());
    }

    // Button OK
    Display->fillRoundRect(70, 105, 100, 20, DEFAULT_ROUND_RECT, PRIMARY_COLOR);
    Display->setTextColor(TEXT_COLOR);
    Display->setTextSize(TEXT_MEDIUM_WIDE);
    Display->setCursor(82, 115);
    Display->printf("OK to start");
}
