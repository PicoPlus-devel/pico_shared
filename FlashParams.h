#pragma once
#include "FrensHelpers.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include <cstddef>

#define FLASHPARAM_MAGIC "FRENS01"
#define FLASHPARAM_ADDRESS (((uintptr_t)&__flash_binary_end + 0xFFF) & ~0xFFF)
// Marks the options word as written by firmware that knows about it. Sectors
// written before it existed hold uninitialised stack bytes in the padding, so
// FLASHPARAM_MAGIC alone cannot tell a real options word from garbage.
#define FLASHPARAM_OPTIONS_MAGIC 0x3154504Fu // "OPT1"
// Option bits. Bit 0 was the Video Clock Fix (HSTX from PLL_USB), retired when
// 384 MHz replaced 378 MHz. Boards may still have it set: do not reuse it.
// Testing aid: run as a board without PSRAM, whether it has any or not. Read once
// at boot (see initPsram()), set from the settings menu, which reboots to apply it.
#define FLASHPARAM_OPT_DISABLE_PSRAM (1u << 1)



namespace Frens {
    
    
    typedef struct 
    {
        char magic[sizeof(FLASHPARAM_MAGIC)];    // "FRENS001"
        uint32_t cpuFreqKHz;
        vreg_voltage voltage;
        // vreg_voltage is a 1-byte enum on arm-none-eabi, so these start at offset 16.
        uint32_t optionsMagic;                   // FLASHPARAM_OPTIONS_MAGIC when options is valid
        uint32_t options;                        // FLASHPARAM_OPT_* bits
        // pad to 256 bytes
        char padding[256 - 24];
    } FlashParams;
    static_assert(offsetof(FlashParams, optionsMagic) == 16, "FlashParams options must start at offset 16");
    static_assert(sizeof(FlashParams) == 256, "FlashParams must stay 256 bytes");

    bool validateFlashParams(const FlashParams &params);
    uint32_t getFlashParamsOptions();
    bool writeFlashParamsToFlash(uint32_t cpuFreqKHz, vreg_voltage voltage, uint32_t options);
    void reconcileSettingsWithFlashParams();

    void setOverclockLimits(uint32_t minFreqKHz, uint32_t maxFreqKHz, vreg_voltage minVoltage, vreg_voltage maxVoltage);
    bool WriteMaxValuesToFlash();
    bool WriteMinValuesToFlash();
    uint32_t getMinFreqKHz() ;
    uint32_t getMaxFreqKHz() ;
    vreg_voltage getMinVoltage() ;
    vreg_voltage getMaxVoltage() ;
} // namespace Frens