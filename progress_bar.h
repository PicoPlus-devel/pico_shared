/*
 * progress_bar.h - SRAM-resident horizontal progress bar for the bootloader.
 *
 * Why it lives in its own translation unit: this function is called from
 * flashProgress() during a flash write, and any flash-resident code on
 * core1's hot path or in the callback itself can hard-fault when XIP is
 * disabled. Putting the renderer in src/progress_bar.c (and marking it
 * __not_in_flash_func) keeps the whole code path SRAM-clean.
 *
 * The bar is drawn directly into the active video framebuffer:
 *   - HSTX builds   : hstx_getframebuffer()        (SRAM static array)
 *   - PicoDVI+FB    : Frens::framebuffer           (SRAM static array)
 *   - PicoDVI line  : function is a no-op (RP2040 line-stream mode has no
 *                     persistent framebuffer to update from outside the
 *                     scanline callback).
 *
 * All colour values must be passed in by the caller (pre-fetched from
 * NesMenuPalette[] before the flash op starts), so this function never
 * reads anything from flash itself.
 */
#ifndef PROGRESS_BAR_H
#define PROGRESS_BAR_H

#include <stdint.h>

/*
 * PROGRESS_BAR_IN_SRAM (default 0)
 *
 * 1 keeps the renderer and its glyph table in SRAM, which is what a caller
 * needs if it draws while XIP is disabled -- pico-bootLoader's uf2 loader
 * calls back from inside the flash write, so pico-bootLoader's CMakeLists
 * sets it to 1. That costs ~2 KB of SRAM (760 B glyph table + the code).
 *
 * 0 (the default) is right when every call happens with XIP up, and that is
 * the case for every emulator: Frens::flashProgress*() draws between
 * flashEraseSafe()/flashProgramSafe() calls, which put QMI M0 back inside
 * their own interrupts-off window, and the romflash.cpp writers of
 * pico_snesPlus and pico-genesisPlus restore M0 the same way. The ~2 KB of
 * SRAM is better spent on the emulator.
 *
 * Getting this wrong is a hard fault during a flash write, not a warning.
 */
#ifndef PROGRESS_BAR_IN_SRAM
#define PROGRESS_BAR_IN_SRAM 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Draw / update the progress bar. Pass the numerator and denominator of the
 * progress fraction in arbitrary units (e.g. numer=37, denom=100). The bar's
 * geometry is fixed; only the fill width changes.
 *
 * A "nn%" label (numer/denom as a rounded-down percentage) is drawn in a
 * fixed-width field immediately below the bar, at pixel rows 182..189.
 * Callers must leave that band free.
 *
 * col_fill   - 16-bit pixel value for the filled part of the bar
 * col_empty  - 16-bit pixel value for the empty part of the bar, and for the
 *              background of the percentage label
 * col_border - 16-bit pixel value for the 1-px border around the bar, and for
 *              the percentage label's text
 *
 * Pixel format follows whatever the active backend uses (RGB444 for
 * PicoDVI, RGB555 for HSTX). The values are written as opaque uint16_t,
 * so the caller is responsible for picking the right encoding.
 */
void progress_bar_draw(uint32_t numer, uint32_t denom,
                       uint16_t col_fill,
                       uint16_t col_empty,
                       uint16_t col_border);

/*
 * Status line drawn centred just above the bar, in a fixed-width field that
 * repaints every cell -- a shorter string never leaves the tail of a longer
 * one behind. Same SRAM-only guarantees as progress_bar_draw(), so it is safe
 * to call from a flash-write progress callback: the glyph table for the whole
 * printable ASCII range lives in .data and nothing here touches flash.
 *
 * col_text / col_bg are 16-bit pixel values, pre-fetched by the caller.
 */
void progress_bar_draw_status(const char *text, uint16_t col_text, uint16_t col_bg);

/*
 * One line of text across the full screen width (40 cells), centred, with
 * its top at pixel row y. Every cell is painted, so the line replaces
 * whatever was drawn there before; text longer than 40 characters is cut
 * off. For the message lines above the bar. Same SRAM-only guarantees as
 * progress_bar_draw_status().
 */
void progress_bar_draw_text(int y, const char *text, uint16_t col_text, uint16_t col_bg);

#ifdef __cplusplus
}
#endif

#endif /* PROGRESS_BAR_H */
