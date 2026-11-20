/*
 * attract.h - pico-bootLoader attract mode: the contract between the loader and
 * the applications it starts. RP2350 only; nothing here exists in an RP2040 build.
 *
 * When the loader menu has been idle for a while, the loader picks a random
 * application that takes part, starts it, and tells it so through watchdog
 * scratch register 7 (scratch[6] keeps the "launched by the loader" magic):
 *
 *   scratch[7] = FRENS_ATTRACT_MAGIC | flags << 8 | minutes
 *
 * minutes   how long the application runs while nobody touches it (1..255)
 * flags     FRENS_ATTRACT_FLAG_STARTED: the application has started its game.
 *           It is set before the restart a board without PSRAM makes to load a
 *           rom, so that the next entry into the rom menu knows the game ended
 *           (or failed) and hands back to the loader instead of picking again.
 *
 * The application:
 *   - starts a game (or its own demo) without asking;
 *   - on the first input clears scratch[7]: a player took over, attract mode is
 *     off for the rest of this session and nothing returns by itself;
 *   - when the time is up without input, reboots with watchdog_reboot(0, 0, 0)
 *     and leaves scratch[7] as it is. The loader then shows its menu and picks
 *     the next application.
 *
 * Restarts an application makes on its own with watchdog_enable() (the loader
 * resumes those) keep scratch[7], so attract mode carries on across them.
 * Watchdog scratch registers survive a watchdog reboot and are cleared by a
 * power cycle, so attract mode never survives one.
 *
 * An application takes part by declaring it in its binary_info:
 *
 *   bi_decl(bi_program_feature("attract:/roms/NES"));
 *
 * The paths after "attract:" ('|'-separated) name what it needs on the card. The
 * loader only picks the application when one of them exists (a folder must not
 * be empty). An empty list means no check. An item starting with '@' is a
 * requirement instead of a path: "@psram" (attract:@psram|/roms/SNES) leaves the
 * application out on a board without PSRAM; a requirement the loader does not
 * know also leaves it out. pico_shared applications use FRENS_ATTRACT_DIRS() or
 * FRENS_ATTRACT_DIRS_PSRAM() from FrensHelpers.h.
 */
#ifndef FRENS_ATTRACT_H
#define FRENS_ATTRACT_H

#include <stdint.h>
#include <stdbool.h>
#include "pico.h"

#define FRENS_ATTRACT_FEATURE_PREFIX "attract:"

#if PICO_RP2350

#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#define FRENS_ATTRACT_SCRATCH       7
#define FRENS_ATTRACT_MAGIC         0xB007A000u
#define FRENS_ATTRACT_MAGIC_MASK    0xFFFFF000u
#define FRENS_ATTRACT_FLAG_STARTED  0x00000100u
#define FRENS_ATTRACT_MINUTES_MASK  0x000000FFu

#ifdef __cplusplus
extern "C" {
#endif

/* True when w is an attract word (magic and a non-zero game time). */
static inline bool frens_attract_word_valid(uint32_t w)
{
    return (w & FRENS_ATTRACT_MAGIC_MASK) == FRENS_ATTRACT_MAGIC &&
           (w & FRENS_ATTRACT_MINUTES_MASK) != 0;
}

/* Loader: right before starting an application. minutes == 0 marks a normal
 * start, which also removes an attract word left from an earlier start. */
static inline void frens_attract_mark_launch(uint32_t minutes)
{
    if (minutes > FRENS_ATTRACT_MINUTES_MASK)
        minutes = FRENS_ATTRACT_MINUTES_MASK;
    watchdog_hw->scratch[FRENS_ATTRACT_SCRATCH] = minutes ? (FRENS_ATTRACT_MAGIC | minutes) : 0;
}

/* Loader, at a boot that does not resume the application: true when the
 * application that ran handed back to attract mode. Clears the word. */
static inline bool frens_attract_consume_return(void)
{
    if (!frens_attract_word_valid(watchdog_hw->scratch[FRENS_ATTRACT_SCRATCH]))
        return false;
    watchdog_hw->scratch[FRENS_ATTRACT_SCRATCH] = 0;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* PICO_RP2350 */

/* The application side lives in bootloader builds only (pico_shared/CMakeLists.txt
 * sets BOOTLOADER_BUILD for bld.sh -b), which are RP2350 builds. */
#if defined(BOOTLOADER_BUILD) && BOOTLOADER_BUILD && PICO_RP2350
#define FRENS_ATTRACT_APP 1
#else
#define FRENS_ATTRACT_APP 0
#endif

#endif /* FRENS_ATTRACT_H */
