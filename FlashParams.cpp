#include "FlashParams.h"
#include "settings.h"
#include "hardware/clocks.h"
#include <cstring>

#define FLASHPARAM_MIN_FREQ_KHZ 252000 // NES, GB, SMS
#define FLASHPARAM_MIN_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_30

// Genesis max settings
#if !HSTX
#define FLASHPARAM_MAX_FREQ_KHZ 324000 
// Because of high overclock, RP2450-Pizero needs high voltage for stable image. 
// THIS MAY OVERHEAT AND DAMAGE THE CPU, USE HEATSINK!!!
#if HW_CONFIG == 7   
// 1_90 2_00 : Unstable image during gameplay
// 2_35 : Stable image during gameplay, but random reboots.
#define FLASHPARAM_MAX_VOLTAGE vreg_voltage::VREG_VOLTAGE_2_50
#else
#define FLASHPARAM_MAX_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_30
#endif
#else
#define FLASHPARAM_MAX_FREQ_KHZ 378000 // May cause artifacts on some screens, 336000 seems stable 
                                       // https://github.com/fhoedemakers/retroJam/issues/7
#define FLASHPARAM_MAX_VOLTAGE vreg_voltage::VREG_VOLTAGE_1_50
#endif
// Helper functions to manage FlashParams in flash memory.
namespace Frens
{
    static vreg_voltage _minVoltage = FLASHPARAM_MIN_VOLTAGE;
    static vreg_voltage _maxVoltage = FLASHPARAM_MAX_VOLTAGE;
    static uint32_t _minFreq = FLASHPARAM_MIN_FREQ_KHZ;
    static uint32_t _maxFreq = FLASHPARAM_MAX_FREQ_KHZ;
    
    void setOverclockLimits(uint32_t minFreqKHz, uint32_t maxFreqKHz, vreg_voltage minVoltage, vreg_voltage maxVoltage)
    {
        _minFreq = minFreqKHz;
        _maxFreq = maxFreqKHz;
        _minVoltage = minVoltage;
        _maxVoltage = maxVoltage;
    }

    uint32_t getMinFreqKHz() { return _minFreq; }
    uint32_t getMaxFreqKHz() { return _maxFreq; }
    vreg_voltage getMinVoltage() { return _minVoltage; }
    vreg_voltage getMaxVoltage() { return _maxVoltage; }

    /// @brief Validate the given FlashParams structure.
    /// @param params
    /// @return true if valid, false otherwise.
    bool validateFlashParams(const FlashParams &params)
    {
        // Check magic string
        if (strncmp(params.magic, FLASHPARAM_MAGIC, sizeof(FLASHPARAM_MAGIC)) != 0)
        {
            // printf("Magic string mismatch in FlashParams\n");
            return false;
        }

        // Check CPU frequency & voltage
        if (params.cpuFreqKHz == _minFreq && params.voltage == _minVoltage)
        {
            // printf("Valid FlashParams: min freq/voltage\n");
            return true;
        }
        if (params.cpuFreqKHz == _maxFreq && params.voltage == _maxVoltage)
        {
            // printf("Valid FlashParams: max freq/voltage\n");
            return true;
        }

        return false;
    }

    /// @brief Get a pointer to the FlashParams stored in flash memory.
    /// @return Pointer to FlashParams in flash memory.
    FlashParams *getFlashParams()
    {
        return (FlashParams *)FLASHPARAM_ADDRESS;
    }

    /// @brief Get the FLASHPARAM_OPT_* bits stored in flash.
    /// Independent of the clock validation: a board that never reads the clock from
    /// FlashParams still honours the options.
    /// @return The options, or 0 when the sector holds none.
    uint32_t getFlashParamsOptions()
    {
        const FlashParams *params = getFlashParams();
        if (strncmp(params->magic, FLASHPARAM_MAGIC, sizeof(FLASHPARAM_MAGIC)) != 0 ||
            params->optionsMagic != FLASHPARAM_OPTIONS_MAGIC)
        {
            return 0;
        }
        return params->options;
    }

    /// @brief Write new FlashParams to flash memory and reboot.
    /// @param cpuFreqKHz The CPU frequency in KHz.
    /// @param voltage The voltage setting.
    /// @param options The FLASHPARAM_OPT_* bits.
    /// @return false when invalid params are provided; does not return otherwise.
    bool __not_in_flash_func(writeFlashParamsToFlash)(uint32_t cpuFreqKHz, vreg_voltage voltage, uint32_t options)
    {
        FlashParams params = {};
        params.cpuFreqKHz = cpuFreqKHz;
        params.voltage = voltage;
        params.optionsMagic = FLASHPARAM_OPTIONS_MAGIC;
        params.options = options;
        strncpy(params.magic, FLASHPARAM_MAGIC, sizeof(FLASHPARAM_MAGIC));
        auto ofs = FLASHPARAM_ADDRESS - XIP_BASE;
        printf("Erasing and programming flash at offset: 0x%08X\n", ofs);
        if (!validateFlashParams(params))
        {
            printf("Invalid FlashParams provided. Aborting flash operation.\n");
            return false; // Invalid params
        }

        printf("New FlashParams: cpuFreqKHz=%u, voltage=%u, options=0x%08X\n", params.cpuFreqKHz, params.voltage, params.options);
        printf("System will reboot after programming flash...\n");
        // Program the hardware watchdog timer to reboot and do this before writing to flash,
        // system will likely hang after flash write.
        // Must be time enough to complete flash write: erasing a 4 KB sector is
        // typically ~50 ms but several hundred ms worst case, so do not cut this fine.
        // This ensures the reboot even if the system crashes after flash write.
        // We will also reset core 1 to avoid it possibly interfering with the flash write.
        printf("Resetting core 1...\n");
        multicore_reset_core1();
        printf("Setting watchdog timer to reboot in 1000 ms\n");
        watchdog_enable(1000, 0);

        // Interrupts stay off across BOTH operations, not just inside each one.
        //
        // On RP2350 flash_range_erase()/flash_range_program() finish in the bootrom's
        // flash_enter_cmd_xip(), which resets qmi_hw->m[0] to its conservative default:
        // the CLKDIV/RXDELAY setClocksAndStartStdio() programmed for the current
        // (possibly overclocked) clock is gone, and XIP is back to a plain 03h serial
        // read with RXDELAY=0. The SDK only saves and restores QMI CS1 (the PSRAM), so
        // nothing puts M0 back. Code fetched from flash in that window is unreliable.
        //
        // flashEraseSafe() restores interrupts on the way out, so calling the two
        // wrappers back to back leaves interrupts enabled while XIP is in that state,
        // and the next IRQ vectors into a flash-resident handler. It faulted or stalled
        // between the erase and the program, leaving the sector erased and never
        // written: validateFlashParams() then rejected it on the next boot and the box
        // came up at the default clock instead of the overclock the user had just
        // enabled. Holding interrupts off across both calls is what this function did
        // before the wrappers were introduced, and keeps every instruction from here to
        // the watchdog reboot running from RAM.
        uint32_t ints = save_and_disable_interrupts();
        flashEraseSafe(ofs, 4096);
        flashProgramSafe(ofs, (const uint8_t *)&params, sizeof(FlashParams));
        restore_interrupts(ints);
        // Will likely to crash here.
        while (1)
        {
            tight_loop_contents();
        };
        __unreachable();
        return true;
    }

    // Both keep the options already in flash: these only change the clock.
    bool WriteMaxValuesToFlash()
    {
        return writeFlashParamsToFlash(_maxFreq, _maxVoltage, getFlashParamsOptions());
    }

    bool WriteMinValuesToFlash()
    {
        return writeFlashParamsToFlash(_minFreq, _minVoltage, getFlashParamsOptions());
    }

    /// @brief Write new options to flash and reboot, keeping the clock the board runs at now.
    /// @param options The FLASHPARAM_OPT_* bits.
    /// @return false when the params could not be written; does not return otherwise.
    bool writeFlashParamsOptions(uint32_t options)
    {
        if (clock_get_hz(clk_sys) / 1000 == _maxFreq)
        {
            return writeFlashParamsToFlash(_maxFreq, _maxVoltage, options);
        }
        return writeFlashParamsToFlash(_minFreq, _minVoltage, options);
    }

    /// @brief Bring the settings file and FlashParams back in line. Call right after
    /// the settings are loaded.
    ///
    /// They can disagree after a FlashParams write that did not complete, a UF2 update
    /// that moved FlashParams (it sits right after the binary), or an SD card taken from
    /// another board, or after the settings were reset (a new SETTINGS_VERSION, or the
    /// file deleted) while FlashParams survived.
    ///
    /// A setting that is ON where FlashParams says off is turned off in the settings:
    /// switching something on is left to the menu, with its warnings. The other way
    /// round the settings win and FlashParams is rewritten, which reboots the board:
    /// - Overclock OFF but FlashParams at the overclock goes back to the normal clock.
    /// - A Video Clock Fix that is on in flash but off in the settings is turned off in
    ///   flash. That keeps a way back for someone whose only controller is a USB pad on
    ///   the built-in port: delete the settings file on a PC.
    void reconcileSettingsWithFlashParams()
    {
#if HW_CONFIG != 7
        bool dirty = false;
        const FlashParams *params = getFlashParams();
        // Left alone, the menu shows Overclock ON at the normal clock, and the next save
        // of any setting switches to the overclock without the warning.
        if (settings.flags.overclock && !(validateFlashParams(*params) && params->cpuFreqKHz == _maxFreq))
        {
            printf("Overclock is on in the settings but not in FlashParams, turning it off.\n");
            settings.flags.overclock = 0;
            dirty = true;
        }
#if HSTX && !CFG_TUH_RPI_PIO_USB
        const bool clockFixInFlash = (getFlashParamsOptions() & FLASHPARAM_OPT_HSTX_ON_PLL_USB) != 0;
        // Never the other way round: a moved SD card must not switch off the built-in
        // USB port of the board it is moved to.
        if (settings.flags.hstxClockFix && !clockFixInFlash)
        {
            printf("Video Clock Fix is on in the settings but not in FlashParams, turning it off.\n");
            settings.flags.hstxClockFix = 0;
            dirty = true;
        }
#endif
        if (dirty)
        {
            FrensSettings::savesettings();
        }

        // From here the settings win. Both changes go in one write.
        bool clockDown = false;
#if !SGX && !RETROJAM
        // Left alone, the board keeps running at the overclock with the menu showing it
        // OFF, until the next save of any setting. FM sound (SMS) needs the max clock
        // too. Not for SGX and retroJam, which pick the max clock themselves while
        // Overclock is off. With min == max there is nothing to go down to, and writing
        // it would reboot into the same state forever.
        if (!settings.flags.overclock && !settings.flags.useFM && _maxFreq != _minFreq &&
            validateFlashParams(*params) && params->cpuFreqKHz == _maxFreq)
        {
            printf("Overclock is off in the settings, turning it off in FlashParams.\n");
            clockDown = true;
        }
#endif
        const uint32_t oldOptions = getFlashParamsOptions();
        uint32_t newOptions = oldOptions;
#if HSTX && !CFG_TUH_RPI_PIO_USB
        if (clockFixInFlash && !settings.flags.hstxClockFix)
        {
            printf("Video Clock Fix is off in the settings, turning it off in FlashParams.\n");
            newOptions &= ~FLASHPARAM_OPT_HSTX_ON_PLL_USB;
        }
#endif
        if (clockDown || newOptions != oldOptions)
        {
            bool ok = clockDown ? writeFlashParamsToFlash(_minFreq, _minVoltage, newOptions)
                                : writeFlashParamsOptions(newOptions);
            if (!ok)
            {
                printf("Failed to write FlashParams\n");
            }
        }
#endif
    }
} // namespace Frens