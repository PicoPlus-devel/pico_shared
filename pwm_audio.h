#ifndef __PWM_AUDIO_H__
#define __PWM_AUDIO_H__
/*
 * pwm_audio - stereo PWM audio output, fed with the same samples as HDMI.
 *
 * Boards with an RC-filtered PWM audio jack (Olimex RP2040-PICO-PC: left on
 * GPIO28, right on GPIO27) set PWM_AUDIO_PIN_L / PWM_AUDIO_PIN_R in
 * BoardConfigs.cmake. hstx_push_audio_sample() hands every sample to
 * pwm_audio_push() as well, so HDMI and the jack play at the same time.
 *
 * Samples go through a single-producer ring (core1 resampler, or the menu
 * wav player on core0) to the PWM wrap interrupt on the core that called
 * pwm_audio_init(); the PWM carrier runs at the sample rate. The ring is
 * kept around half full by dropping or repeating a sample, which absorbs
 * the small rate difference between the HDMI audio clock and clk_sys.
 * When no samples arrive (menu, pause) the output holds the last value.
 */
#include <stdint.h>
#include <stdbool.h>

#ifndef PWM_AUDIO_PIN_L
#define PWM_AUDIO_PIN_L -1
#endif
#ifndef PWM_AUDIO_PIN_R
#define PWM_AUDIO_PIN_R -1
#endif
#define PWM_AUDIO_IS_ENABLED (PWM_AUDIO_PIN_L >= 0)

#ifdef __cplusplus
extern "C" {
#endif

/* Call on core0 after the system clock is final. */
void pwm_audio_init(uint32_t sample_rate);
/* Signed 16-bit samples. Safe from either core, one producer at a time. */
void pwm_audio_push(int left, int right);

#ifdef __cplusplus
}
#endif
#endif // __PWM_AUDIO_H__
