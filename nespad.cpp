#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "nespad.h"

#define nespad_wrap_target 0

// Both program variants clock NESPAD_READ_BITS bits. 18 so SNES controllers
// (which share the NES latch/clock/data protocol but shift out 16 bits:
// B,Y,Select,Start,Up,Down,Left,Right,A,X,L,R + 4 ID bits) are read in full.
// Plain NES controllers simply shift out low for bits 9-16;
// nespad_read_finish() detects that and masks the result back to 8 bits.
// Clocks 17-18 tell an idle SNES pad from an empty port, see nespad_decode().
// 32 with NESPAD_SNES_MOUSE: the SNES Mouse sends its Y and X motion bytes in
// clocks 17-32.
#if NESPAD_SNES_MOUSE
#define NESPAD_READ_BITS 32
#else
#define NESPAD_READ_BITS 18
#endif
// "set x, NESPAD_READ_BITS - 1  side 1": the bit counter of both variants.
#define NESPAD_SET_X (0xf020 | (NESPAD_READ_BITS - 1))
#if HW_CONFIG == 12 || HW_CONFIG == 13 // Murmulator M1/M2 bothe controllers ahare latch and clock
static const uint16_t nespad_program_instructions[] = {
    //     .wrap_target
    // @javavi pull request
    // from https://github.com/fhoedemakers/pico-infonesPlus/pull/167
    0xD020, //  0: irq    wait 0          side 1
    0xFA01, //  1: set    pins, 1         side 1 [10]
    NESPAD_SET_X, //  2: set    x, NESPAD_READ_BITS - 1   side 1
    0xE000, //  3: set    pins, 0         side 0
    0x4401, //  4: in     pins, 1         side 0 [4]
    0xF400, //  5: set    pins, 0         side 1 [4]
    0x1043, //  6: jmp    x--, 3          side 1
    //     .wrap
  };
#else // Original nespad code, used as long as the above code is not tested on WaveShare boards.
static const uint16_t nespad_program_instructions[] = {
    //     .wrap_target
    0xd020, //  0: irq    wait 0          side 1
    0xf101, //  1: set    pins, 1         side 1 [1]
    NESPAD_SET_X, //  2: set    x, NESPAD_READ_BITS - 1   side 1
    0xf000, //  3: set    pins, 0         side 1
    0xf400, //  4: set    pins, 0         side 1 [4]
    0xe100, //  5: set    pins, 0         side 0 [1]
    0x5301, //  6: in     pins, 1         side 1 [3]
    0x1044, //  7: jmp    x--, 4          side 1
            //     .wrap
};
#endif
// Derive program length from instruction array (works for both variants)
#define NESPAD_PROG_LEN (sizeof(nespad_program_instructions) / sizeof(nespad_program_instructions[0]))
static const struct pio_program nespad_program = {
    .instructions = nespad_program_instructions,
    .length = NESPAD_PROG_LEN,
    .origin = -1,
};

static inline pio_sm_config nespad_program_get_default_config(uint offset)
{
  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, offset + nespad_wrap_target, offset + (NESPAD_PROG_LEN - 1));
  sm_config_set_sideset(&c, 1, false, false);
  return c;
}

#if defined(PICO_RP2350)
#define NES_NUM_PIOS 3
#elif defined(PICO_RP2040)
#define NES_NUM_PIOS 2
#else
#define NES_NUM_PIOS 2
#endif

static PIO pio[2];
static int8_t sm[2] = {-1, -1};
static bool second_pad = false;
static bool read_pending = false;
#if NESPAD_SNES_MOUSE
static uint8_t clk_pin[2];
static uint8_t lat_pin[2];
#endif

// Cache program once per PIO instance
static int program_offset[NES_NUM_PIOS];      // valid only if program_added[i] is true
static bool program_added[NES_NUM_PIOS] = {}; // default-initialized to false

uint8_t nespad_states[2] = {0, 0};
// Full 16-clock read in SNES serial order: bit0=B, 1=Y, 2=Select, 3=Start,
// 4=Up, 5=Down, 6=Left, 7=Right, 8=A, 9=X, 10=L, 11=R (1 = pressed).
// A plain NES controller only populates bits 0-7 (A,B,Select,Start,dpad) —
// for those pads this is identical to nespad_states[].
uint16_t nespad_states_ext[2] = {0, 0};
uint16_t nespad_raw_ext[2] = {0, 0};
uint8_t nespad_padtype[2] = {NESPAD_TYPE_UNKNOWN, NESPAD_TYPE_UNKNOWN};
nespad_mouse_state nespad_mouse[2] = {};
uint8_t nespad_state = 0;

#if NESPAD_SNES_MOUSE
// Reads without the mouse signature before a port stops being a mouse. One bad
// read (a wobbly plug) must not unplug the mouse from the game.
#define NESPAD_MOUSE_MISSES 4
// Most sensitivity pulses sent after the mouse is detected, see
// nespad_read_finish(). Three reach 0 from any setting; the fourth is spare
// for a mouse that powers up in an undefined state.
#define NESPAD_MOUSE_MAX_PULSES 4
static uint8_t mouse_misses[2];
static uint8_t mouse_pulses[2]; // sensitivity pulses sent since detection
static uint8_t mouse_speed[2];  // sensitivity the mouse reported last (0-2)
#endif
bool nespad_begin(uint8_t padnum, uint32_t cpu_khz, uint8_t clkPin, uint8_t dataPin,
                  uint8_t latPin, PIO _pio)
{
  if (padnum > 1)
    return false;
  if (padnum == 1)
    second_pad = true;

  pio[padnum] = _pio;
  int pio_idx = pio_get_index(_pio);
#if NESPAD_SNES_MOUSE
  clk_pin[padnum] = clkPin;
  lat_pin[padnum] = latPin;
#endif

  // Claim an SM first
  if ((sm[padnum] = pio_claim_unused_sm(pio[padnum], true)) < 0)
    return false;

  // Load program once per PIO instance
  if (!program_added[pio_idx])
  {
    if (!pio_can_add_program(pio[padnum], &nespad_program))
      return false;
    program_offset[pio_idx] = pio_add_program(pio[padnum], &nespad_program);
    program_added[pio_idx] = true;
  }
  uint offset = (uint)program_offset[pio_idx];

  pio_sm_config c = nespad_program_get_default_config(offset);
  sm_config_set_sideset_pins(&c, clkPin);
  sm_config_set_in_pins(&c, dataPin);
  sm_config_set_set_pins(&c, latPin, 1);
  pio_gpio_init(pio[padnum], clkPin);
  pio_gpio_init(pio[padnum], dataPin);
  pio_gpio_init(pio[padnum], latPin);
  gpio_set_pulls(dataPin, true, false);
  pio_sm_set_pindirs_with_mask(pio[padnum], sm[padnum],
                               (1u << clkPin) | (1u << latPin),
                               (1u << clkPin) | (1u << dataPin) | (1u << latPin));
  sm_config_set_in_shift(&c, true, true, NESPAD_READ_BITS);
  sm_config_set_clkdiv_int_frac(&c, cpu_khz / 1000, 0);

  pio_set_irq0_source_enabled(pio[padnum], (pio_interrupt_source)(pis_interrupt0 + sm[padnum]), false);
  pio_sm_clear_fifos(pio[padnum], sm[padnum]);

  pio_sm_init(pio[padnum], sm[padnum], offset, &c);
  pio_sm_set_enabled(pio[padnum], sm[padnum], true);
  return true;
}

// Initiate nespad read. Non-blocking; result will be available in ~220 uS
// (~390 uS with NESPAD_SNES_MOUSE) via nespad_read_finish(). Does nothing
// while a read is pending, so a second start cannot queue a second result in
// the FIFO. Must first call nespad_begin() once to set up PIO.
void nespad_read_start(void)
{
  if (read_pending)
    return;
  read_pending = true;
  pio_interrupt_clear(pio[0], 0);
  if (second_pad)
  {
    pio_interrupt_clear(pio[1], 0);
  }
}

#if NESPAD_SNES_MOUSE
// One SNES Mouse motion byte as it was clocked in: bit 0 the direction, bits
// 1-7 the magnitude MSB first. Sign-and-magnitude, not two's complement.
static int32_t nespad_mouse_axis(uint32_t bits)
{
  int32_t mag = 0;
  for (int i = 1; i < 8; i++)
  {
    mag = (mag << 1) | (int32_t)((bits >> i) & 1);
  }
  return (bits & 1) ? -mag : mag;
}

// Step a SNES Mouse to its next sensitivity (0 -> 1 -> 2 -> 0) the way a
// console does it: one clock pulse while latch is high. Only called between
// reads, while the SM waits on "irq wait 0" with latch low and clock high, so
// SIO takes the two pins over at those levels and hands them back unchanged;
// the SM does not notice. ~30 uS.
static void nespad_mouse_speed_pulse(int padnum)
{
  const uint clk = clk_pin[padnum];
  const uint lat = lat_pin[padnum];
  gpio_put(clk, 1);
  gpio_put(lat, 0);
  gpio_set_dir(clk, GPIO_OUT);
  gpio_set_dir(lat, GPIO_OUT);
  gpio_set_function(clk, GPIO_FUNC_SIO);
  gpio_set_function(lat, GPIO_FUNC_SIO);
  gpio_put(lat, 1);
  busy_wait_us_32(12);
  gpio_put(clk, 0);
  busy_wait_us_32(6);
  gpio_put(clk, 1);
  busy_wait_us_32(6);
  gpio_put(lat, 0);
  busy_wait_us_32(6);
  pio_gpio_init(pio[padnum], clk);
  pio_gpio_init(pio[padnum], lat);
}
#endif

// Finish nespad read. Ideally should be called ~220 uS after
// nespad_read_start(), but can be sooner (will block until ready), or later
// (will introduce latency). Does nothing when no read is pending. Sets
// nespad_states[] (legacy 8-bit view, 1 = pressed: 0x01=A/B, 0x02=B/Y,
// 0x04=Select, 0x08=Start, 0x10=Up, 0x20=Down, 0x40=Left, 0x80=Right — first
// name NES pad, second SNES pad) and nespad_states_ext[] (full SNES-order
// 12-button word, see declaration). Must first call nespad_begin() once to set
// up PIO. Result will be 0 if PIO failed to init (e.g. no free state machine).
static uint16_t nespad_decode(int padnum)
{
  if (sm[padnum] < 0)
    return 0;
  // Right-shift was used in sm config so bit order matches NES controller
  // bits used elsewhere in picones, but does require shifting down...
  uint32_t raw = (pio_sm_get_blocking(pio[padnum], sm[padnum]) >> (32 - NESPAD_READ_BITS)) ^
                 (0xFFFFFFFFu >> (32 - NESPAD_READ_BITS));
  nespad_raw_ext[padnum] = (uint16_t)raw;
#if NESPAD_SNES_MOUSE
  // SNES Mouse: ID nibble 0001, which no pad and no empty port sends. Checked
  // before the SNES test, which its buttons and sensitivity bits (clocks 9-12,
  // where a pad has A,X,L,R) would pass. Not a pad, so it reports through
  // nespad_mouse[] and leaves the button state at 0.
  if ((raw & 0xF000) == 0x8000)
  {
    if (nespad_padtype[padnum] != NESPAD_TYPE_MOUSE)
    {
      nespad_padtype[padnum] = NESPAD_TYPE_MOUSE;
      mouse_pulses[padnum] = 0;
    }
    mouse_misses[padnum] = 0;
    // Clock 9 right button, 10 left, 11-12 sensitivity (MSB first).
    mouse_speed[padnum] = (uint8_t)((((raw >> 10) & 1) << 1) | ((raw >> 11) & 1));
    nespad_mouse_state &m = nespad_mouse[padnum];
    m.buttons = (uint8_t)(((raw >> 9) & 1) | (((raw >> 8) & 1) << 1));
    // Motion only counts once the mouse is at its lowest sensitivity, see
    // nespad_read_finish(); until then the counts are scaled or garbage.
    if (mouse_pulses[padnum] > 0 &&
        (mouse_speed[padnum] == 0 || mouse_pulses[padnum] >= NESPAD_MOUSE_MAX_PULSES))
    {
      m.dx += nespad_mouse_axis(raw >> 24);          // clocks 25-32, 1 = left
      m.dy += nespad_mouse_axis((raw >> 16) & 0xFF); // clocks 17-24, 1 = up
    }
    return 0;
  }
  if (nespad_padtype[padnum] == NESPAD_TYPE_MOUSE)
  {
    nespad_mouse[padnum].buttons = 0;
    if (++mouse_misses[padnum] < NESPAD_MOUSE_MISSES)
    {
      return 0;
    }
    nespad_padtype[padnum] = NESPAD_TYPE_UNKNOWN; // unplugged: let the pad tests below decide
    nespad_mouse[padnum] = {};
  }
#endif
  // NES controller: the 4021's serial input is grounded, so clocks 9-16
  // read low on the wire = 1 after inversion. A SNES controller drives the
  // 4 ID bits (13-16) high = 0 after inversion, so they can never all be
  // set. Disconnected port reads high via pull-up = all zeros.
  if ((raw & 0xF000) == 0xF000)
  {
    nespad_padtype[padnum] = NESPAD_TYPE_NES;
    raw &= 0x00FF; // NES pad: keep the 8 real buttons
  }
  else if ((raw & 0x30000) == 0x30000 || (raw & 0x0F00))
  {
    // An official SNES pad pulls the line low after its 16 bits (as ares and
    // Mesen2 model it), so clocks 17-18 read 1 even while it is idle, where an
    // empty port and the 8-bit SNES->NES adapter cable leave the line high.
    // Pads that do not, are recognised by A, X, L or R being down: only a SNES
    // pad has those. Sticky, since without that an idle pad looks like an empty
    // port, whose first B press would otherwise read as NES A.
    nespad_padtype[padnum] = NESPAD_TYPE_SNES;
  }
  else if (nespad_padtype[padnum] == NESPAD_TYPE_NES)
  {
    nespad_padtype[padnum] = NESPAD_TYPE_UNKNOWN; // ID nibble gone: no longer a NES pad
  }
  return (uint16_t)(raw & 0x0FFF); // SNES pad: strip the ID bits
}

void nespad_read_finish(void)
{
  if (!read_pending)
    return;
  read_pending = false;
  nespad_states_ext[0] = nespad_decode(0);
  nespad_states_ext[1] = second_pad ? nespad_decode(1) : 0;
  nespad_states[0] = (uint8_t)nespad_states_ext[0];
  nespad_states[1] = (uint8_t)nespad_states_ext[1];
#if NESPAD_SNES_MOUSE
  // A fresh mouse gets at least one sensitivity pulse, because some revisions
  // report garbage until their sensitivity has been changed once, and then
  // pulses until it reports 0: linear counts, one per step of the sensor. A
  // clone that cannot be switched (Hyperkin) always reports 0 and gets its one
  // pulse. Only between reads, see the pulse.
  for (int i = 0; i < (second_pad ? 2 : 1); i++)
  {
    if (nespad_padtype[i] == NESPAD_TYPE_MOUSE && mouse_misses[i] == 0 &&
        mouse_pulses[i] < NESPAD_MOUSE_MAX_PULSES && (mouse_pulses[i] == 0 || mouse_speed[i] != 0))
    {
      nespad_mouse_speed_pulse(i);
      mouse_pulses[i]++;
    }
  }
#endif
}
