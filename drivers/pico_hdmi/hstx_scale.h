#ifndef HSTX_SCALE_H
#define HSTX_SCALE_H
/*
 * Vertical scaling of a framebuffer of `rows` rows onto `lines` output lines
 * (rows <= lines), for hstx_setFrameSize(). Plain C without SDK dependencies,
 * so a host-side preview can produce the same picture as the scanline callback.
 *
 * An output line spans rows/lines of a row. A line inside one row shows that
 * row; a line across the boundary of two rows shows both, weighted by how much
 * of the line each covers, rounded to quarters. For example:
 *   288 rows on 480 lines, per three rows: A, 3/4 A + 1/4 B, B, 1/4 B + 3/4 C, C
 *   240 rows on 480 lines: every row on two lines, nothing blended
 *
 * The functions are always inlined: the scanline callback runs from RAM and
 * must not call into flash.
 */
#include <stdint.h>

#define HSTX_SCALE_INLINE static inline __attribute__((always_inline))

/// The row output line `line` shows. *q is the weight of the row below it, in
/// quarters: 0 shows the row alone, 1..3 blends it with the next row (which
/// then exists).
HSTX_SCALE_INLINE uint32_t hstx_scale_row(uint32_t line, uint32_t rows, uint32_t lines, uint32_t *q)
{
    // positions in 1/lines of a row
    const uint32_t start = line * rows;
    uint32_t row = start / lines;
    const uint32_t end = start + rows;
    const uint32_t boundary = (row + 1) * lines;
    uint32_t w = (end > boundary) ? ((end - boundary) * 4 + rows / 2) / rows : 0;
    if (w == 4)
    {
        row++;
        w = 0;
    }
    *q = w;
    return row;
}

/// Per-channel average of two pairs of RGB555 pixels, rounded down. Exact for
/// equal inputs, so a flat colour stays the same when blended.
HSTX_SCALE_INLINE uint32_t hstx_avg555x2(uint32_t a, uint32_t b)
{
    return (a & b) + (((a ^ b) & 0x7BDE7BDEu) >> 1);
}

/// Blends `pairs` pixel pairs of row a with the row below it, b, giving b the
/// weight q (1..3 quarters).
HSTX_SCALE_INLINE void hstx_scale_blend(uint32_t *dst, const uint32_t *a, const uint32_t *b, uint32_t pairs, uint32_t q)
{
    if (q == 2)
    {
        for (uint32_t i = 0; i < pairs; i++)
            dst[i] = hstx_avg555x2(a[i], b[i]);
        return;
    }
    // 1/4 or 3/4: the average once more with the nearer row
    const uint32_t *nearer = (q == 1) ? a : b;
    for (uint32_t i = 0; i < pairs; i++)
        dst[i] = hstx_avg555x2(nearer[i], hstx_avg555x2(a[i], b[i]));
}

#endif // HSTX_SCALE_H
