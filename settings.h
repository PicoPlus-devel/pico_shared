#ifndef SETTINGS
#define SETTINGS
#include "ff.h"
#include "FrensHelpers.h"
#include <stdint.h>


extern struct settings settings;
#define SETTINGS_VERSION 115

struct settings
{
    unsigned short version = SETTINGS_VERSION; // version of settings structure

    ScreenMode screenMode;
    short firstVisibleRowINDEX;
    short selectedRow;
    short horzontalScrollIndex;
    unsigned short fgcolor;
    unsigned short bgcolor;
    // int reserved[3];
    char currentDir[FF_MAX_LFN];
    int8_t fruitjamVolumeLevel; // Volume level for Fruit Jam internal speaker in db
    uint8_t scanlineType;
    // 32-bit bitfield container: keeps the bits packed into a single storage
    // unit so adding new flags doesn't change layout per-bit. 16 bits used,
    // 16 spare. Bumping SETTINGS_VERSION above invalidates older saved files
    // whose layout used the 16-bit container.
    struct
    {
        uint32_t useExtAudio : 1;      // 0 = use DVIAudio, 1 = use external Audio
        uint32_t enableVUMeter : 1;    // 0 = disable VU meter, 1 = enable VU meter
        uint32_t borderMode : 2;       // BorderMode enum (2 bits)
        uint32_t dmgLCDPalette : 2;    // DMG LCD Palette (2 bits) 0=Green 1=Color 2=B&W
        uint32_t audioEnabled : 1;     // 1 = audio on, 0 = audio muted
        uint32_t displayFrameRate : 1; // 1 = show FPS overlay, 0 = do not show
        uint32_t frameSkip : 1;        // 1 = enable frame skipping, 0 = disable frame skipping
        uint32_t scanlineOn : 1;       // 1 = scanlines on, 0 = scanlines off
       // uint32_t fruitJamEnableInternalSpeaker : 1; // 1 = enable Fruit Jam internal speaker, 0 = disable
        uint32_t rapidFireOnA : 1;     // 1 = rapid fire on A button, 0 = off
        uint32_t rapidFireOnB : 1;     // 1 = rapid fire on B button, 0 = off
        uint32_t useDVIModeForHDMI : 1; // 1 = use DVI mode for HDMI output (lower latency, but no audio), 0 = use HDMI mode (required for audio, but slightly higher latency)
        uint32_t autoSwapFDS : 1;      // 1 = automatically swap FDS disk sides when loading a .fds file, 0 = do not auto swap (user must manually select "FDS Disk Swap" in settings menu to swap sides). Default to on, because it's less confusing for users if the correct disk side is automatically loaded.
        uint32_t autoInsertDiskA : 1;  // 1 = disk side A is pre-inserted at boot, 0 = disk starts ejected (user presses A to insert, allowing BIOS Mario/Luigi animation to play)
        uint32_t overclock : 1;        // 1 = boot/run at FLASHPARAM_MAX_FREQ_KHZ, 0 = FLASHPARAM_MIN_FREQ_KHZ
        uint32_t useFM : 1;            // SMS-only: 1 = YM2413 FM sound on (RP2350 only); 0 = PSG only
        uint32_t serialKeyboard : 1;   // TI-99/4A only: 1 = type characters arriving on the serial console into the machine. Off by default: an unconnected RX pin would otherwise frame noise into the emulated keyboard.
        uint32_t removeSpriteLimit : 1; // NES only: 1 = draw every sprite on a scanline (less flicker), 0 = hardware limit of 8. Inverted so that the spare bit (0) in older saved files keeps the limit on.
        uint32_t menuOverscan : 2;     // 0 = menu uses the whole screen, 1 = top and bottom text row blank, 2 = also first and last column blank. For TVs that cut off the edges.
        uint32_t genesisPad : 2;       // Genesis only: 0 = Auto (6 buttons when the cartridge header lists a 6-button pad), 1 = 3-button pad, 2 = 6-button pad
        uint32_t nesPalette : 4;       // NES only: index of the selected color palette. 0 = the board's default, which the spare bits (0) of older saved files select.
        uint32_t hstxClockFix : 1;     // HSTX builds without PIO USB: 1 = clock HSTX from PLL_USB (no dots on some displays at 378 MHz and up), which turns the built-in USB port off. Mirrored in FlashParams, see FLASHPARAM_OPT_HSTX_ON_PLL_USB.
        uint32_t snesButtonLayout : 1; // NES only, in game (not the menu or hotkeys): 1 = SNES layout, NES B/A on the left/bottom face button (SNES Y/B) of a pad with four, 0 = NES layout (SNES B/A). The spare bits (0) of older saved files select NES.
        uint32_t tateMode : 2;         // Vertical arcade games: 0 = Off, picture turned upright for a normal monitor; 1 = Bottom left, tate for a monitor turned clockwise (its bottom edge on the left); 2 = Bottom right, tate for a monitor turned counter-clockwise. The spare bits (0) of older saved files select 0.
        uint32_t reserved : 1;         // spare bits for future flags; reset to 0
    } flags; // 31 bits used + 1 reserved = full 32-bit container

};
namespace FrensSettings
{
   
    // Border rendering mode enumeration
    enum BorderMode
    {
        DEFAULTBORDER = 0, // Use default static border
        RANDOMBORDER = 1,  // Pick a random border each time (implementation dependent)
        THEMEDBORDER = 2   // Use a border that matches current theme/game
    };
    typedef enum {
        NES = 0,
        SMS = 1,
        GAMEBOY = 2,
        GENESIS = 3,
        MULTI = 4,
        PCE = 5,
        O2EM = 6,
        SNES = 7,
        TI99 = 8,
        OUTRUN = 9,
        PHOENIX = 10,
        MOONCRESTA = 11
    } emulators;
    static emulators emulatorType = NES;
    void initSettings(emulators emu) ;
    void setEmulatorType(const char * fileextension);
    void savesettings();
    void loadsettings();
    void resetsettings(struct settings *settings = nullptr);
    emulators getEmulatorType();
    const char *getEmulatorTypeString(bool forSettings = false);
    emulators getEmulatorTypeForSettings();
}
extern const int8_t *g_settings_visibility;
extern const uint8_t *g_available_screen_modes;
// Non-const so the FDS disk-swap entry can be flipped on at runtime
// when a .fds image is loaded (see main.cpp).
extern int8_t g_settings_visibility_nes[];
extern const int8_t g_settings_visibility_gb[];
extern const int8_t g_settings_visibility_sms[];
extern const int8_t g_settings_visibility_md[];
extern const int8_t g_settings_visibility_pce[];
extern const int8_t g_settings_visibility_o2em[];
extern int8_t g_settings_visibility_snes[];
extern const int8_t g_settings_visibility_ti99[];
extern const int8_t g_settings_visibility_main[];
#endif