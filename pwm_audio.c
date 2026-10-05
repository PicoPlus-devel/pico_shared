// pwm_audio.c - stereo PWM audio output fed with the HDMI samples, see pwm_audio.h
#include "pwm_audio.h"

#if PWM_AUDIO_IS_ENABLED
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#define RING_SIZE 2048u                 /* stereo frames, power of two */
#define RING_MASK (RING_SIZE - 1u)

static uint32_t ring[RING_SIZE];        /* duty values: left low 16 bits, right high 16 */
static volatile uint32_t wr_idx;        /* written by the producer only */
static volatile uint32_t rd_idx;        /* written by the interrupt only */
static uint32_t wrap_top;               /* PWM counter top: clk_sys / sample rate - 1 */
static uint slice_l, slice_r;
static volatile bool playing;           /* false: holding, waiting for half a ring; read by the producer */
static bool initialized;

static void __not_in_flash_func(pwm_audio_irq)(void)
{
    pwm_clear_irq(slice_l);
    uint32_t r = rd_idx;
    uint32_t fill = wr_idx - r;
    if (!playing)
    {
        if (fill < RING_SIZE / 2)
            return;                     /* keep holding the last value */
        playing = true;
    }
    if (fill == 0)
    {
        playing = false;                /* ran dry: hold, refill to half */
        return;
    }
    uint32_t v = ring[r & RING_MASK];
    rd_idx = r + 1;
    /* One write sets both channels of each slice. The other channel of each
       slice drives a pin that is not in PWM function, so it has no effect. */
    const uint32_t swapped = (v << 16) | (v >> 16);
    pwm_hw->slice[slice_l].cc = (PWM_AUDIO_PIN_L & 1) ? swapped : v;
    if (slice_r != slice_l)
        pwm_hw->slice[slice_r].cc = (PWM_AUDIO_PIN_R & 1) ? v : swapped;
}

static inline uint32_t to_duty(int s)
{
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return ((uint32_t)(s + 32768) * wrap_top) >> 16;
}

void __not_in_flash_func(pwm_audio_push)(int left, int right)
{
    if (!initialized)
        return;
    uint32_t w = wr_idx;
    uint32_t fill = w - rd_idx;
    if (fill >= RING_SIZE * 3 / 4)
        return;                         /* producer ahead: drop this sample */
    /* left duty in the low half, right in the high half (see the irq) */
    uint32_t v = to_duty(left) | (to_duty(right) << 16);
    ring[w & RING_MASK] = v;
    w++;
    if (playing && fill < RING_SIZE / 4)
    {
        ring[w & RING_MASK] = v;        /* producer behind: repeat this sample */
        w++;
    }
    __dmb();
    wr_idx = w;
}

void pwm_audio_init(uint32_t sample_rate)
{
    if (initialized)
        return;
#if defined(PWM_AUDIO_SMPS_PIN) && PWM_AUDIO_SMPS_PIN >= 0
    /* Pico / Pico 2: GPIO23 high puts the SMPS in PWM mode; its default
       power-save mode adds audible hiss to the PWM output. */
    gpio_init(PWM_AUDIO_SMPS_PIN);
    gpio_set_dir(PWM_AUDIO_SMPS_PIN, GPIO_OUT);
    gpio_put(PWM_AUDIO_SMPS_PIN, 1);
#endif
    wrap_top = clock_get_hz(clk_sys) / sample_rate - 1;
    slice_l = pwm_gpio_to_slice_num(PWM_AUDIO_PIN_L);
    slice_r = pwm_gpio_to_slice_num(PWM_AUDIO_PIN_R);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_wrap(&cfg, wrap_top);
    pwm_init(slice_l, &cfg, false);
    if (slice_r != slice_l)
        pwm_init(slice_r, &cfg, false);
    pwm_set_gpio_level(PWM_AUDIO_PIN_L, wrap_top / 2);
    pwm_set_gpio_level(PWM_AUDIO_PIN_R, wrap_top / 2);
    gpio_set_function(PWM_AUDIO_PIN_L, GPIO_FUNC_PWM);
    gpio_set_function(PWM_AUDIO_PIN_R, GPIO_FUNC_PWM);
    wr_idx = rd_idx = 0;
    playing = false;
    pwm_clear_irq(slice_l);
    pwm_set_irq_enabled(slice_l, true);
    irq_add_shared_handler(PWM_DEFAULT_IRQ_NUM(), pwm_audio_irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    initialized = true;
    /* Start both slices in the same cycle without touching the enable bits of
       other slices (pwm_set_mask_enabled() would clear them). */
    hw_set_bits(&pwm_hw->en, (1u << slice_l) | (1u << slice_r));
}

#endif // PWM_AUDIO_IS_ENABLED
