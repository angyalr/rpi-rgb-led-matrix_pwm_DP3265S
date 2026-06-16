// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
#include "spwm-panel-config.h"
#include "spwm-panel-ini.h"

#include <algorithm>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace rgb_matrix {
namespace internal {

namespace {

// Return true when the requested panel type begins with the expected profile
// name.
bool spwm_panel_type_matches(const char *spwm_panel_type,
                             const char *spwm_expected_panel_type) {
  if (spwm_panel_type == nullptr || spwm_expected_panel_type == nullptr) {
    return false;
  }

  return strncasecmp(spwm_panel_type, spwm_expected_panel_type,
                     strlen(spwm_expected_panel_type)) == 0;
}

// Copy a compile-time register payload array into a vector for runtime
// storage.
template <size_t N>
std::vector<uint16_t> spwm_make_words(const uint16_t (&spwm_words)[N]) {
  return std::vector<uint16_t>(spwm_words, spwm_words + N);
}

// Wrap a compile-time init-step array in the lightweight runtime view used by
// the SPWM upload code.
template <size_t N>
SPWM_Init_Sequence spwm_make_init_sequence(
    const SPWM_Init_Step (&spwm_steps)[N]) {
  SPWM_Init_Sequence spwm_init_sequence = {spwm_steps, N};
  return spwm_init_sequence;
}

// Wrap one register timing description in the runtime struct consumed by the
// upload helpers. The first LAT width stays aligned with the tail-latch window
// that overlaps the final data clocks, while any later entries become
// post-data LAT sections.
template <size_t N>
SPWM_Register_Timing spwm_make_register_timing(
    const uint8_t (&spwm_lat_clocks)[N],
    int spwm_lat_space_clocks = 0) {
  SPWM_Register_Timing spwm_timing = {
      spwm_lat_clocks,
      N,
      spwm_lat_space_clocks,
  };
  return spwm_timing;
}

// Apply one sparse physical-column layout to a profile when the resolved width
// matches. This keeps odd receiver-chain wiring quirks reusable across panel
// definitions instead of open-coding the same slot list in each profile.
template <size_t N>
void spwm_apply_missing_column_layout(
    SPWM_Panel_Settings *spwm_settings, int spwm_columns,
    int spwm_expected_columns, const int (&spwm_missing_columns)[N]) {
  if (spwm_settings == nullptr || spwm_columns != spwm_expected_columns) {
    return;
  }

  const int spwm_missing_column_count = std::min(
      static_cast<int>(N), SPWM_Panel_Settings::kMaxMissingColumnSlots);
  spwm_settings->missing_column_count = spwm_missing_column_count;
  for (int spwm_column = 0; spwm_column < spwm_missing_column_count;
       ++spwm_column) {
    spwm_settings->missing_column_positions[spwm_column] =
        spwm_missing_columns[spwm_column];
  }
}

static const int SPWM_SPARSE_172_COLUMN_LAYOUT[] = {20, 52, 100, 148};

// Build the shared SPWM settings baseline. Panel profiles can then override
// only the values that differ. Default based on FM6373, including the shared
// shift-register Channel A pulse geometry used by row-address types 1 and 2.
SPWM_Panel_Settings spwm_make_default_panel_settings() {
  SPWM_Panel_Settings spwm_settings = {};
  spwm_settings.default_rows = 64;
  spwm_settings.default_columns = 128;
  spwm_settings.upload_channels_per_chip = 16;
  spwm_settings.upload_word_bits = 16;
  spwm_settings.upload_chip_count = 0;  // derive from columns / channels_per_chip
  spwm_settings.auto_tune_oe_gaps = true;
  spwm_settings.auto_tune_frames = 20;
  spwm_settings.auto_tune_max_step_clks = 50;
  spwm_settings.first_oe_clk_length = 12;
  spwm_settings.end_of_frame_extra_row_cycles = 53;
  spwm_settings.frame_end_sleep_us = 300;
  spwm_settings.oe_during_upload_clk_count = 112;
  spwm_settings.oe_after_upload_clk_count = 112;
  spwm_settings.oe_clk_look_behind = 16;
  spwm_settings.oe_clk_length = 4;
  spwm_settings.shiftreg_row_select_a_pulse_clk_count = 2;
  spwm_settings.shiftreg_row_select_a_pulse_start_clk = 0;
  spwm_settings.shiftreg_row_select_a_pulse_centered = true;
  spwm_settings.oe_style = SPWM_OE_STYLE_FM6373;
  spwm_settings.missing_column_count = 0;
  return spwm_settings;
}

// Resolve how many cascaded driver chips a register block has to cover for the
// active panel width.
size_t spwm_resolve_register_repeat_count(
    const SPWM_Panel_Settings &spwm_settings, int spwm_columns) {
  if (spwm_settings.upload_chip_count > 0) {
    return static_cast<size_t>(spwm_settings.upload_chip_count);
  }

  const int spwm_resolved_columns =
      spwm_columns > 0 ? spwm_columns : spwm_settings.default_columns;
  const int spwm_channels_per_chip =
      spwm_settings.upload_channels_per_chip > 0
          ? spwm_settings.upload_channels_per_chip
          : 16;
  if (spwm_resolved_columns <= 0 || spwm_channels_per_chip <= 0) {
    return 0;
  }

  return static_cast<size_t>(
      (spwm_resolved_columns + spwm_channels_per_chip - 1) /
      spwm_channels_per_chip);
}

bool spwm_parse_u16_env(const char *spwm_env_name, uint16_t *spwm_value) {
  const char *spwm_text = getenv(spwm_env_name);
  if (spwm_text == nullptr || *spwm_text == '\0') {
    return false;
  }

  char *spwm_end = nullptr;
  errno = 0;
  unsigned long spwm_parsed = strtoul(spwm_text, &spwm_end, 0);
  if (errno != 0 || spwm_end == spwm_text || *spwm_end != '\0' ||
      spwm_parsed > 0xffffUL) {
    return false;
  }

  *spwm_value = static_cast<uint16_t>(spwm_parsed);
  return true;
}

uint16_t spwm_apply_u16_env(const char *spwm_env_name,
                            uint16_t spwm_default_value) {
  uint16_t spwm_value = 0;
  return spwm_parse_u16_env(spwm_env_name, &spwm_value)
             ? spwm_value
             : spwm_default_value;
}

bool spwm_env_enabled(const char *spwm_env_name) {
  const char *spwm_text = getenv(spwm_env_name);
  if (spwm_text == nullptr || *spwm_text == '\0') {
    return false;
  }
  return strcasecmp(spwm_text, "0") != 0 &&
         strcasecmp(spwm_text, "false") != 0 &&
         strcasecmp(spwm_text, "no") != 0 &&
         strcasecmp(spwm_text, "off") != 0;
}

int spwm_dp3265s_reg08_to_igain_tenths(uint16_t spwm_reg08) {
  const int spwm_data = spwm_reg08 & 0xff;
  const int spwm_coarse = (spwm_data >> 6) & 0x03;
  const int spwm_fine = spwm_data & 0x3f;
  return ((spwm_fine + 1) * (spwm_coarse + 1) * 1000 + 64) / 128;
}

uint16_t spwm_dp3265s_make_reg08_for_igain_tenths(
    int spwm_target_igain_tenths) {
  if (spwm_target_igain_tenths < 0) {
    spwm_target_igain_tenths = 0;
  } else if (spwm_target_igain_tenths > 2000) {
    spwm_target_igain_tenths = 2000;
  }

  int spwm_best_data = 0;
  int spwm_best_error = 1000000;
  for (int spwm_coarse = 0; spwm_coarse < 4; ++spwm_coarse) {
    for (int spwm_fine = 0; spwm_fine < 64; ++spwm_fine) {
      const int spwm_igain_tenths =
          ((spwm_fine + 1) * (spwm_coarse + 1) * 1000 + 64) / 128;
      const int spwm_delta = spwm_igain_tenths - spwm_target_igain_tenths;
      const int spwm_error = spwm_delta < 0 ? -spwm_delta : spwm_delta;
      if (spwm_error < spwm_best_error) {
        spwm_best_error = spwm_error;
        spwm_best_data = (spwm_coarse << 6) | spwm_fine;
      }
    }
  }
  return static_cast<uint16_t>(0x0800 | spwm_best_data);
}

uint16_t spwm_apply_dp3265s_igain_env(uint16_t spwm_reg08,
                                      long *spwm_brightness_percent,
                                      int *spwm_target_igain_tenths) {
  const char *spwm_text = getenv("SPWM_DP3265S_IGAIN_PERCENT");
  if (spwm_text == nullptr || *spwm_text == '\0') {
    return spwm_reg08;
  }

  char *spwm_end = nullptr;
  errno = 0;
  long spwm_percent = strtol(spwm_text, &spwm_end, 10);
  if (errno != 0 || spwm_end == spwm_text || *spwm_end != '\0') {
    return spwm_reg08;
  }

  if (spwm_percent < 0) {
    spwm_percent = 0;
  } else if (spwm_percent > 100) {
    spwm_percent = 100;
  }

  const int spwm_max_igain_tenths = 2000;
  const int spwm_target_tenths =
      static_cast<int>((spwm_max_igain_tenths * spwm_percent + 50) / 100);
  if (spwm_brightness_percent != nullptr) {
    *spwm_brightness_percent = spwm_percent;
  }
  if (spwm_target_igain_tenths != nullptr) {
    *spwm_target_igain_tenths = spwm_target_tenths;
  }
  return spwm_dp3265s_make_reg08_for_igain_tenths(spwm_target_tenths);
}

uint16_t spwm_sm16269s_make_gain_word(long spwm_percent) {
  if (spwm_percent < 0) {
    spwm_percent = 0;
  } else if (spwm_percent > 100) {
    spwm_percent = 100;
  }
  const int spwm_gain = static_cast<int>((63 * spwm_percent + 50) / 100);
  return static_cast<uint16_t>(spwm_gain & 0x3f);
}

uint16_t spwm_apply_sm16269s_gain_env(uint16_t spwm_gain,
                                      long *spwm_brightness_percent) {
  const char *spwm_text = getenv("SPWM_SM16269S_GAIN_PERCENT");
  if (spwm_text == nullptr || *spwm_text == '\0') {
    return spwm_gain;
  }

  char *spwm_end = nullptr;
  errno = 0;
  long spwm_percent = strtol(spwm_text, &spwm_end, 10);
  if (errno != 0 || spwm_end == spwm_text || *spwm_end != '\0') {
    return spwm_gain;
  }

  if (spwm_brightness_percent != nullptr) {
    *spwm_brightness_percent = std::max(0L, std::min(100L, spwm_percent));
  }
  return spwm_sm16269s_make_gain_word(spwm_percent);
}

// -------------------------------------------------------------------------------------------------
// DP3265S profile definition (rev 2026-05-19)
// Based on DP3265S Rev2.3 datasheet and DP3264 reference implementation.
// 64x32 P4 panel, 48 driver chips, 8 per data line, 4 per logical row.
// -------------------------------------------------------------------------------------------------

static const SPWM_Panel_Settings SPWM_DP3265S_SETTINGS = []() {
  SPWM_Panel_Settings s = spwm_make_default_panel_settings();
  s.default_rows = 32;
  s.default_columns = 64;
  s.upload_channels_per_chip = 16;
  s.upload_word_bits = 16;
  s.upload_chip_count = 8;
  s.auto_tune_oe_gaps = false;
  s.auto_tune_frames = 0;
  s.auto_tune_max_step_clks = 0;
  s.end_of_frame_extra_row_cycles = 64;
  s.frame_end_sleep_us = 0;
  s.oe_during_upload_clk_count = 128;
  s.oe_after_upload_clk_count = 128;
  s.oe_clk_look_behind = 16;
  s.oe_clk_length = 4;
  s.first_oe_clk_length = 12;
  s.shiftreg_row_select_a_pulse_clk_count = 2;
  s.shiftreg_row_select_a_pulse_start_clk = 0;
  s.shiftreg_row_select_a_pulse_centered = true;
  s.oe_style = SPWM_OE_STYLE_DP3265S;
  return s;
}();

// 13 configuration registers, values from DP3264 reference (board707).
// REG02 adjusted for 32-row panel (LINE_NUM = 31 = 0x1F).
static const uint16_t SPWM_DP3265S_REG01 = 0x1100;
static const uint16_t SPWM_DP3265S_REG02 = 0x0207;  // LINE_SET: 32 scan rows
static const uint16_t SPWM_DP3265S_REG03 = 0x033F;  // GROUP_SET: 64 groups
static const uint16_t SPWM_DP3265S_REG04 = 0x043F;  // PWM_WIDTH: 256 DCLK/row
static const uint16_t SPWM_DP3265S_REG05 = 0x0504;  // DISSHD_TIME: short dead-time
static const uint16_t SPWM_DP3265S_REG06 = 0x0642;  // PLL_DIV
static const uint16_t SPWM_DP3265S_REG07 = 0x0700;
static const uint16_t SPWM_DP3265S_REG08 = 0x08BF;
static const uint16_t SPWM_DP3265S_REG09 = 0x0960;
static const uint16_t SPWM_DP3265S_REG0A = 0x0ABE;
static const uint16_t SPWM_DP3265S_REG0B = 0x0B8B;
static const uint16_t SPWM_DP3265S_REG0C = 0x0C08;
static const uint16_t SPWM_DP3265S_REG0D = 0x0D12;

// All registers use 5-CLK tail-latch (WR_CFG command), matching DP3264 reference.
static const uint8_t SPWM_DP3265S_LAT[] = {5};
static const SPWM_Register_Timing SPWM_DP3265S_REGISTER_TIMINGS[] = {
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
    spwm_make_register_timing(SPWM_DP3265S_LAT),
};

// Init sequence: VSYNC + 16 fill CLKs + PRE_ACT, repeated for each register.
// The 16 space_clocks after VSYNC fill the shift register (matches DP3264
// send_clocks(16) call between send_vsync() and send_latches(14)).
static const SPWM_Init_Step SPWM_DP3265S_INIT_STEPS[] = {
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},  // VSYNC + 16 fill CLKs
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},   // PRE_ACT
    {SPWM_INIT_STEP_REGISTER,   1,  0, 0},   // REG11
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   2,  0, 0},   // REG02
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   3,  0, 0},   // REG03
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   4,  0, 0},   // REG04
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   5,  0, 0},   // REG05
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   6,  0, 0},   // REG06
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   7,  0, 0},   // REG07
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   8,  0, 0},   // REG08
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   9,  0, 0},   // REG09
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   10, 0, 0},   // REG0A
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   11, 0, 0},   // REG0B
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   12, 0, 0},   // REG0C
    {SPWM_INIT_STEP_LAT_PULSES, 3,  0, 16},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER,   13, 0, 0},   // REG0D
};

static const SPWM_Init_Sequence SPWM_DP3265S_INIT_SEQUENCE =
    spwm_make_init_sequence(SPWM_DP3265S_INIT_STEPS);

SPWM_Config spwm_create_dp3265s_config(const SPWM_Panel_Settings &cfg, int cols) {
  SPWM_Config c(13,
                SPWM_DP3265S_REGISTER_TIMINGS[0],
                spwm_resolve_register_repeat_count(cfg, cols));
  uint16_t regs[] = {
      SPWM_DP3265S_REG01, SPWM_DP3265S_REG02, SPWM_DP3265S_REG03,
      SPWM_DP3265S_REG04, SPWM_DP3265S_REG05, SPWM_DP3265S_REG06,
      SPWM_DP3265S_REG07, SPWM_DP3265S_REG08, SPWM_DP3265S_REG09,
      SPWM_DP3265S_REG0A, SPWM_DP3265S_REG0B, SPWM_DP3265S_REG0C,
      SPWM_DP3265S_REG0D};

  long spwm_brightness_percent = -1;
  int spwm_target_igain_tenths = -1;
  regs[7] = spwm_apply_dp3265s_igain_env(
      regs[7], &spwm_brightness_percent, &spwm_target_igain_tenths);
  regs[7] = spwm_apply_u16_env("SPWM_DP3265S_REG08", regs[7]);
  regs[8] = spwm_apply_u16_env("SPWM_DP3265S_REG09", regs[8]);
  regs[9] = spwm_apply_u16_env("SPWM_DP3265S_REG0A", regs[9]);
  regs[10] = spwm_apply_u16_env("SPWM_DP3265S_REG0B", regs[10]);
  regs[11] = spwm_apply_u16_env("SPWM_DP3265S_REG0C", regs[11]);
  regs[12] = spwm_apply_u16_env("SPWM_DP3265S_REG0D", regs[12]);

  if (spwm_env_enabled("SPWM_DP3265S_DEBUG")) {
    fprintf(stderr,
            "[dp3265s] brightness=%ld, target_igain=%.1f%%, "
            "REG08=0x%04x, REG09=0x%04x, REG0A=0x%04x, "
            "REG0B=0x%04x, REG0C=0x%04x, REG0D=0x%04x\n",
            spwm_brightness_percent,
            spwm_target_igain_tenths / 10.0,
            regs[7], regs[8], regs[9], regs[10], regs[11], regs[12]);
  }

  c.spwm_add_register(1,  {regs[0]}, &SPWM_DP3265S_REGISTER_TIMINGS[0]);
  c.spwm_add_register(2,  {regs[1]}, &SPWM_DP3265S_REGISTER_TIMINGS[1]);
  c.spwm_add_register(3,  {regs[2]}, &SPWM_DP3265S_REGISTER_TIMINGS[2]);
  c.spwm_add_register(4,  {regs[3]}, &SPWM_DP3265S_REGISTER_TIMINGS[3]);
  c.spwm_add_register(5,  {regs[4]}, &SPWM_DP3265S_REGISTER_TIMINGS[4]);
  c.spwm_add_register(6,  {regs[5]}, &SPWM_DP3265S_REGISTER_TIMINGS[5]);
  c.spwm_add_register(7,  {regs[6]}, &SPWM_DP3265S_REGISTER_TIMINGS[6]);
  c.spwm_add_register(8,  {regs[7]}, &SPWM_DP3265S_REGISTER_TIMINGS[7]);
  c.spwm_add_register(9,  {regs[8]}, &SPWM_DP3265S_REGISTER_TIMINGS[8]);
  c.spwm_add_register(10, {regs[9]}, &SPWM_DP3265S_REGISTER_TIMINGS[9]);
  c.spwm_add_register(11, {regs[10]}, &SPWM_DP3265S_REGISTER_TIMINGS[10]);
  c.spwm_add_register(12, {regs[11]}, &SPWM_DP3265S_REGISTER_TIMINGS[11]);
  c.spwm_add_register(13, {regs[12]}, &SPWM_DP3265S_REGISTER_TIMINGS[12]);
  return c;
}

// -------------------------------------------------------------------------------------------------
// SM16269S profile definition (initial bring-up profile)
// Based on SM16269 ZIGZZZAV1.0 datasheet. The public datasheet exposes the
// SRAM/PWM behavior and 6-bit current gain word, but does not publish the
// complete command/register table. Keep this profile deliberately small and
// override-friendly so panel bring-up can tune command words from the ini/env.
// 64x32 panel, 48 driver chips, 8 per HUB75 data line, DP3265S-like layout.
// -------------------------------------------------------------------------------------------------

static const SPWM_Panel_Settings SPWM_SM16269S_SETTINGS = []() {
  SPWM_Panel_Settings s = SPWM_DP3265S_SETTINGS;
  s.oe_style = SPWM_OE_STYLE_SM16269S;
  return s;
}();

static const uint16_t SPWM_SM16269S_GAIN = 0x003f;  // G5..G0 current gain, max
static const uint16_t SPWM_SM16269S_CFG1 = 0x1810;  // ghost=3, precharge=1
static const uint16_t SPWM_SM16269S_CFG2 = 0x3ce0;  // 1/8 scan, high-refresh, 16-bit
// SM16269S dual-latch style: shift the 16-bit payload while LE is low, then
// select the destination register with a post-data LE-high DCLK burst.
static const uint8_t SPWM_SM16269S_GAIN_LAT[] = {0, 3};
static const uint8_t SPWM_SM16269S_CFG1_LAT[] = {0, 5};
static const uint8_t SPWM_SM16269S_CFG2_LAT[] = {0, 7};
static const SPWM_Register_Timing SPWM_SM16269S_GAIN_TIMING =
    spwm_make_register_timing(SPWM_SM16269S_GAIN_LAT);
static const SPWM_Register_Timing SPWM_SM16269S_CFG1_TIMING =
    spwm_make_register_timing(SPWM_SM16269S_CFG1_LAT);
static const SPWM_Register_Timing SPWM_SM16269S_CFG2_TIMING =
    spwm_make_register_timing(SPWM_SM16269S_CFG2_LAT);

static const SPWM_Init_Step SPWM_SM16269S_INIT_STEPS[] = {
    {SPWM_INIT_STEP_REGISTER, 1, 0, 0},  // current gain, post-data LE=3
    {SPWM_INIT_STEP_REGISTER, 2, 0, 0},  // optional config1, post-data LE=5
    {SPWM_INIT_STEP_REGISTER, 3, 0, 0},  // optional config2, post-data LE=7
};

static const SPWM_Init_Sequence SPWM_SM16269S_INIT_SEQUENCE =
    spwm_make_init_sequence(SPWM_SM16269S_INIT_STEPS);

SPWM_Config spwm_create_sm16269s_config(const SPWM_Panel_Settings &cfg, int cols) {
  SPWM_Config c(3,
                SPWM_SM16269S_GAIN_TIMING,
                spwm_resolve_register_repeat_count(cfg, cols));
  long spwm_brightness_percent = -1;
  uint16_t gain = spwm_apply_sm16269s_gain_env(
      SPWM_SM16269S_GAIN, &spwm_brightness_percent);
  gain = spwm_apply_u16_env("SPWM_SM16269S_GAIN", gain);
  uint16_t cfg1 = spwm_apply_u16_env("SPWM_SM16269S_CFG1",
                                     SPWM_SM16269S_CFG1);
  uint16_t cfg2 = spwm_apply_u16_env("SPWM_SM16269S_CFG2",
                                     SPWM_SM16269S_CFG2);

  if (spwm_env_enabled("SPWM_SM16269S_DEBUG")) {
    fprintf(stderr,
            "[sm16269s] brightness=%ld, GAIN=0x%04x, "
            "CFG1=0x%04x, CFG2=0x%04x\n",
            spwm_brightness_percent, gain, cfg1, cfg2);
  }

  c.spwm_add_register(1, {gain}, &SPWM_SM16269S_GAIN_TIMING);
  c.spwm_add_register(2, {cfg1}, &SPWM_SM16269S_CFG1_TIMING);
  c.spwm_add_register(3, {cfg2}, &SPWM_SM16269S_CFG2_TIMING);
  return c;
}

static const size_t SPWM_FM6373_REGISTER_COUNT = 5;
static const uint8_t SPWM_FM6373_REGISTER_SEND_LAT[][1] = {
    {5},
    {5},
    {5},
    {5},
    {5},
};
static const SPWM_Register_Timing SPWM_FM6373_REGISTER_TIMINGS[] = {
    spwm_make_register_timing(SPWM_FM6373_REGISTER_SEND_LAT[0]),
    spwm_make_register_timing(SPWM_FM6373_REGISTER_SEND_LAT[1]),
    spwm_make_register_timing(SPWM_FM6373_REGISTER_SEND_LAT[2]),
    spwm_make_register_timing(SPWM_FM6373_REGISTER_SEND_LAT[3]),
    spwm_make_register_timing(SPWM_FM6373_REGISTER_SEND_LAT[4]),
};

// Use named field assignments instead of positional aggregate initialization so
// panel-setting edits stay readable in the C++11 build as this struct evolves.
static const SPWM_Panel_Settings SPWM_FM6373_SETTINGS = []() {
  // Default panel setting based on FM6373
  SPWM_Panel_Settings spwm_settings = spwm_make_default_panel_settings();
  return spwm_settings;
}();
static const uint16_t SPWM_FM6373_REGISTER1_WORD = 0x00AA;
static const uint16_t SPWM_FM6373_REGISTER2_WORD = 0x01AA;
static const uint16_t SPWM_FM6373_REGISTER4_WORD = 0x0055;
static const uint16_t SPWM_FM6373_REGISTER5_WORD = 0x0155;

// Register block 3 carries the per-frame RGB control words. Each new
// frame advances one entry in the R/G/B sequences below.
static const uint16_t SPWM_FM6373_BLOCK3_SEQ_R[] = {
    0x0000, 0x0100, 0x021f, 0x033f, 0x0402, 0x0508, 0x0602, 0x0720,
    0x0820, 0x0900, 0x0a00, 0x0b00, 0x0c01, 0x0d01, 0x0e04, 0x0f01,
    0x10c2, 0x1121, 0x1201, 0x1300, 0x1400, 0x1500, 0x1600, 0x17f0,
    0x181f, 0x1900, 0x1a1f, 0x1b10, 0x1c2a, 0x1d0a, 0x1e42, 0x1f04,
    0x2008, 0x2101, 0x221c, 0x7000, 0x7100, 0x7200, 0x7300, 0x7400,
    0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0x2300,
};

static const uint16_t SPWM_FM6373_BLOCK3_SEQ_G[] = {
    0x0000, 0x0100, 0x021f, 0x033f, 0x0402, 0x0508, 0x0602, 0x0720,
    0x0820, 0x0900, 0x0a00, 0x0b00, 0x0c08, 0x0d01, 0x0e04, 0x0f01,
    0x10c2, 0x1121, 0x1201, 0x1300, 0x1400, 0x1500, 0x1600, 0x17f0,
    0x181f, 0x1950, 0x1a1f, 0x1b10, 0x1c2a, 0x1d0a, 0x1e46, 0x1f20,
    0x2008, 0x2101, 0x221c, 0x7000, 0x7100, 0x7200, 0x7300, 0x7400,
    0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0x2300,
};

static const uint16_t SPWM_FM6373_BLOCK3_SEQ_B[] = {
    0x0000, 0x0100, 0x021f, 0x033f, 0x0402, 0x0508, 0x0602, 0x0720,
    0x0820, 0x0900, 0x0a00, 0x0b00, 0x0c08, 0x0d01, 0x0e04, 0x0f01,
    0x10c2, 0x1121, 0x1201, 0x1300, 0x1400, 0x1500, 0x1600, 0x17f0,
    0x182f, 0x1900, 0x1a1f, 0x1b10, 0x1c2a, 0x1d0a, 0x1e48, 0x1f20,
    0x2010, 0x2101, 0x221c, 0x7000, 0x7100, 0x7200, 0x7300, 0x7400,
    0xf000, 0xf100, 0xf200, 0xf300, 0xf400, 0xf500, 0x2300,
};

// FM6373 frame start: emit LAT bursts of 3, 11, and 14 clocks, each with an
// optional trailing LAT-low spacer count, then stream register blocks 1-5 with
// block 3 coming from the rotating RGB register sequence above.

static const SPWM_Init_Step SPWM_FM6373_INIT_STEPS[] = {
    // LAT pulses | Row lines left at 0 | Spacer CLKs.
    {SPWM_INIT_STEP_LAT_PULSES, 3, 0, 0},   // 3 LAT pulses, row lines left at 0, no spacer clocks.
    {SPWM_INIT_STEP_LAT_PULSES, 11, 0, 0},  // 11 LAT pulses, row lines left at 0, no spacer clocks.
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},  // 14 LAT pulses, row lines left at 0, no spacer clocks.
    {SPWM_INIT_STEP_REGISTER, 1, 0, 0},     // Send fixed register 1, row lines left at 0, no spacer clocks.
    {SPWM_INIT_STEP_REGISTER, 2, 0, 0},     // Send fixed register 2, row lines left at 0, no spacer clocks.
    {SPWM_INIT_STEP_RGB_REGISTER, 3, 0, 0}, // Send RGB register 3, row lines left at 0, no spacer clocks.
    {SPWM_INIT_STEP_REGISTER, 4, 0, 0},     // Send fixed register 4, row lines left at 0, no spacer clocks.
    {SPWM_INIT_STEP_REGISTER, 5, 0, 0},     // Send fixed register 5, row lines left at 0, no spacer clocks.
};

static const SPWM_Init_Sequence SPWM_FM6373_INIT_SEQUENCE =
    spwm_make_init_sequence(SPWM_FM6373_INIT_STEPS);

// Purpose: Build the FM6373 register layout for the active panel width.
// Inputs: Panel timing/settings and the resolved column count.
// Outputs: A runtime register bundle with repeated fixed words and RGB sequence data.
// Side effects: None.
SPWM_Config spwm_create_fm6373_config(const SPWM_Panel_Settings &spwm_settings,
                                      int spwm_columns) {
  SPWM_Config spwm_config(SPWM_FM6373_REGISTER_COUNT,
                          SPWM_FM6373_REGISTER_TIMINGS[0],
                          spwm_resolve_register_repeat_count(spwm_settings,
                                                             spwm_columns));

  // Register blocks 1, 2, 4, and 5 are fixed every frame.
  spwm_config.spwm_add_register(1, {SPWM_FM6373_REGISTER1_WORD},
                                &SPWM_FM6373_REGISTER_TIMINGS[0]);
  spwm_config.spwm_add_register(2, {SPWM_FM6373_REGISTER2_WORD},
                                &SPWM_FM6373_REGISTER_TIMINGS[1]);
  // Register block 3 advances through one RGB triple per frame.
  spwm_config.spwm_add_rgb_register(
      3,
      {spwm_make_words(SPWM_FM6373_BLOCK3_SEQ_R),
       spwm_make_words(SPWM_FM6373_BLOCK3_SEQ_G),
       spwm_make_words(SPWM_FM6373_BLOCK3_SEQ_B)},
      SPWM_FM6373_REGISTER_TIMINGS[2]);
  spwm_config.spwm_add_register(4, {SPWM_FM6373_REGISTER4_WORD},
                                &SPWM_FM6373_REGISTER_TIMINGS[3]);
  spwm_config.spwm_add_register(5, {SPWM_FM6373_REGISTER5_WORD},
                                &SPWM_FM6373_REGISTER_TIMINGS[4]);

  return spwm_config;
}

// -------------------------------------------------------------------------------------------------
// ICND1065L profile definition.
// This follows the FM6373/SPWM upload structure but uses the captured
// ICND1065L register-3 RGB payload.
// -------------------------------------------------------------------------------------------------

static const size_t SPWM_ICND1065L_REGISTER_COUNT = 5;
static const uint8_t SPWM_ICND1065L_REGISTER_SEND_LAT[][1] = {
    {5},
    {5},
    {5},
    {5},
    {5},
};
static const SPWM_Register_Timing SPWM_ICND1065L_REGISTER_TIMINGS[] = {
    spwm_make_register_timing(SPWM_ICND1065L_REGISTER_SEND_LAT[0]),
    spwm_make_register_timing(SPWM_ICND1065L_REGISTER_SEND_LAT[1]),
    spwm_make_register_timing(SPWM_ICND1065L_REGISTER_SEND_LAT[2]),
    spwm_make_register_timing(SPWM_ICND1065L_REGISTER_SEND_LAT[3]),
    spwm_make_register_timing(SPWM_ICND1065L_REGISTER_SEND_LAT[4]),
};

static const SPWM_Panel_Settings SPWM_ICND1065L_SETTINGS = []() {
  SPWM_Panel_Settings spwm_settings = spwm_make_default_panel_settings();
  spwm_settings.auto_tune_oe_gaps = false;
  spwm_settings.auto_tune_frames = 0;
  spwm_settings.auto_tune_max_step_clks = 0;
  spwm_settings.first_oe_clk_length = 12;
  spwm_settings.end_of_frame_extra_row_cycles = 5;
  spwm_settings.frame_end_sleep_us = 300;
  spwm_settings.oe_during_upload_clk_count = 140;
  spwm_settings.oe_after_upload_clk_count = 140;
  spwm_settings.oe_clk_look_behind = 0;
  spwm_settings.oe_clk_length = 4;
  spwm_settings.shiftreg_row_select_a_pulse_clk_count = 6;
  spwm_settings.oe_style = SPWM_OE_STYLE_FM6363;
  return spwm_settings;
}();
static const uint16_t SPWM_ICND1065L_REGISTER1_WORD = 0x00AA;
static const uint16_t SPWM_ICND1065L_REGISTER2_WORD = 0x01AA;
static const uint16_t SPWM_ICND1065L_REGISTER4_WORD = 0x0055;
static const uint16_t SPWM_ICND1065L_REGISTER5_WORD = 0x0155;

// Register block 3 carries the per-frame RGB control words for ICND1065L.
static const uint16_t SPWM_ICND1065L_BLOCK3_SEQ_R[] = {
    0x0000, 0x026a, 0x0322, 0x0412, 0x0500, 0x0601, 0x0712, 0x0c10,
    0x0d02, 0x0e84, 0x0f01, 0x1040, 0x1127, 0x1800, 0x1926, 0x1c60,
    0x1d02, 0x1e71, 0x2040, 0x2101, 0x2380, 0x74a0,
};

static const uint16_t SPWM_ICND1065L_BLOCK3_SEQ_G[] = {
    0x0000, 0x026a, 0x0322, 0x0412, 0x0500, 0x0601, 0x0712, 0x0c10,
    0x0d04, 0x0e84, 0x0f01, 0x1040, 0x1127, 0x1800, 0x1908, 0x1c60,
    0x1d02, 0x1e92, 0x2060, 0x2101, 0x2305, 0x74a0,
};

static const uint16_t SPWM_ICND1065L_BLOCK3_SEQ_B[] = {
    0x0000, 0x026a, 0x0322, 0x0412, 0x0500, 0x0601, 0x0712, 0x0c10,
    0x0d03, 0x0e84, 0x0f11, 0x1040, 0x1127, 0x1800, 0x190a, 0x1c60,
    0x1d02, 0x1eb5, 0x2060, 0x2101, 0x2300, 0x74a0,
};

// ICND1065L frame start mirrors FM6373: emit LAT bursts of 3, 11, and 14
// clocks, each with an optional trailing LAT-low spacer count, then stream
// register blocks 1-5 with block 3 coming from the rotating RGB register
// sequence above.
static const SPWM_Init_Step SPWM_ICND1065L_INIT_STEPS[] = {
    // LAT pulses | Row lines left at 0 | Spacer CLKs.
    {SPWM_INIT_STEP_LAT_PULSES, 3, 0, 12},
    {SPWM_INIT_STEP_LAT_PULSES, 11, 0, 3},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 9},
    {SPWM_INIT_STEP_REGISTER, 1, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 2, 0, 0},
    {SPWM_INIT_STEP_RGB_REGISTER, 3, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 4, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 5, 0, 0},
};

static const SPWM_Init_Sequence SPWM_ICND1065L_INIT_SEQUENCE =
    spwm_make_init_sequence(SPWM_ICND1065L_INIT_STEPS);

// Some 172-column ICND1065L receivers still shift 176 physical RGB slots per
// row. Four internal chain positions are not bonded to LEDs, so the uploader
// has to skip those physical slots instead of padding only at the tail.
SPWM_Panel_Settings spwm_resolve_icnd1065l_settings(int spwm_columns) {
  SPWM_Panel_Settings spwm_settings = SPWM_ICND1065L_SETTINGS;
  const int spwm_resolved_columns =
      spwm_columns > 0 ? spwm_columns : spwm_settings.default_columns;
  spwm_apply_missing_column_layout(&spwm_settings, spwm_resolved_columns, 172,
                                   SPWM_SPARSE_172_COLUMN_LAYOUT);
  return spwm_settings;
}

// Purpose: Build the ICND1065L register layout for the active panel width.
// Inputs: Panel timing/settings and the resolved column count.
// Outputs: A runtime register bundle with repeated fixed words and RGB sequence data.
// Side effects: None.
SPWM_Config spwm_create_icnd1065l_config(
    const SPWM_Panel_Settings &spwm_settings, int spwm_columns) {
  SPWM_Config spwm_config(SPWM_ICND1065L_REGISTER_COUNT,
                          SPWM_ICND1065L_REGISTER_TIMINGS[0],
                          spwm_resolve_register_repeat_count(spwm_settings,
                                                             spwm_columns));

  spwm_config.spwm_add_register(1, {SPWM_ICND1065L_REGISTER1_WORD},
                                &SPWM_ICND1065L_REGISTER_TIMINGS[0]);
  spwm_config.spwm_add_register(2, {SPWM_ICND1065L_REGISTER2_WORD},
                                &SPWM_ICND1065L_REGISTER_TIMINGS[1]);
  spwm_config.spwm_add_rgb_register(
      3,
      {spwm_make_words(SPWM_ICND1065L_BLOCK3_SEQ_R),
       spwm_make_words(SPWM_ICND1065L_BLOCK3_SEQ_G),
       spwm_make_words(SPWM_ICND1065L_BLOCK3_SEQ_B)},
      SPWM_ICND1065L_REGISTER_TIMINGS[2]);
  spwm_config.spwm_add_register(4, {SPWM_ICND1065L_REGISTER4_WORD},
                                &SPWM_ICND1065L_REGISTER_TIMINGS[3]);
  spwm_config.spwm_add_register(5, {SPWM_ICND1065L_REGISTER5_WORD},
                                &SPWM_ICND1065L_REGISTER_TIMINGS[4]);

  return spwm_config;
}

// -------------------------------------------------------------------------------------------------
// SM16380SH profile definition.
// This stays close to the FM6373 upload path but uses a shorter init script,
// one extra fixed register, and a different leading-OE length.
// -------------------------------------------------------------------------------------------------

static const size_t SPWM_SM16380SH_REGISTER_COUNT = 6;
static const uint8_t SPWM_SM16380SH_REGISTER_SEND_LAT[][1] = {
    {5},
    {5},
    {5},
    {5},
    {5},
    {5},
};
static const SPWM_Register_Timing SPWM_SM16380SH_REGISTER_TIMINGS[] = {
    spwm_make_register_timing(SPWM_SM16380SH_REGISTER_SEND_LAT[0]),
    spwm_make_register_timing(SPWM_SM16380SH_REGISTER_SEND_LAT[1]),
    spwm_make_register_timing(SPWM_SM16380SH_REGISTER_SEND_LAT[2]),
    spwm_make_register_timing(SPWM_SM16380SH_REGISTER_SEND_LAT[3]),
    spwm_make_register_timing(SPWM_SM16380SH_REGISTER_SEND_LAT[4]),
    spwm_make_register_timing(SPWM_SM16380SH_REGISTER_SEND_LAT[5]),
};

static const SPWM_Panel_Settings SPWM_SM16380SH_SETTINGS = []() {
  SPWM_Panel_Settings spwm_settings = spwm_make_default_panel_settings();
  spwm_settings.first_oe_clk_length = 10;  
  spwm_settings.oe_clk_look_behind = 16;
  return spwm_settings;
}();
static const uint16_t SPWM_SM16380SH_REGISTER1_WORD = 0x00AA;
static const uint16_t SPWM_SM16380SH_REGISTER2_WORD = 0x01AA;
static const uint16_t SPWM_SM16380SH_REGISTER4_WORD = 0xF003;
static const uint16_t SPWM_SM16380SH_REGISTER5_WORD = 0x0055;
static const uint16_t SPWM_SM16380SH_REGISTER6_WORD = 0x0155;

// Register block 3 carries the per-frame RGB control words captured for the
// SM16380SH panel. Each new frame advances one entry in the R/G/B sequences
// below.
static const uint16_t SPWM_SM16380SH_BLOCK3_SEQ_R[] = {
    0x0000, 0x021f, 0x0300, 0x0400, 0x0500, 0x0600, 0x0750, 0x0800, 0x0900,
    0x0a02, 0x0b0c, 0x0c08, 0x0d00, 0x0e05, 0x0f00, 0x1000, 0x1100, 0x1200,
    0x1300, 0x1414, 0x1500, 0x1630, 0x1700, 0x1801, 0x1904, 0x1a03, 0x1b14,
    0x1c12, 0x1d00, 0x1e00, 0x1f0c, 0x2000, 0x2200,
};

static const uint16_t SPWM_SM16380SH_BLOCK3_SEQ_G[] = {
    0x0000, 0x021f, 0x0300, 0x0400, 0x0500, 0x0600, 0x0751, 0x0800, 0x0900,
    0x0a02, 0x0b0c, 0x0c18, 0x0d00, 0x0e05, 0x0f00, 0x1000, 0x1100, 0x1200,
    0x1300, 0x1422, 0x1500, 0x1630, 0x1700, 0x1801, 0x1903, 0x1a01, 0x1b14,
    0x1c8f, 0x1d00, 0x1e00, 0x1f0c, 0x2000, 0x2200,
};

static const uint16_t SPWM_SM16380SH_BLOCK3_SEQ_B[] = {
    0x0000, 0x021f, 0x0300, 0x0400, 0x0500, 0x0600, 0x0753, 0x0800, 0x0900,
    0x0a02, 0x0b0c, 0x0c30, 0x0d00, 0x0e05, 0x0f00, 0x1000, 0x1100, 0x1200,
    0x1300, 0x1432, 0x1500, 0x1630, 0x1700, 0x1801, 0x1903, 0x1a01, 0x1b14,
    0x1c8f, 0x1d00, 0x1e00, 0x1f0c, 0x2000, 0x2200,
};

// SM16380SH frame start: emit LAT bursts of 3 and 14 clocks, each with an
// optional trailing LAT-low spacer count, then stream register blocks 1-6 with
// block 3 coming from the rotating RGB register sequence above.
static const SPWM_Init_Step SPWM_SM16380SH_INIT_STEPS[] = {
    // LAT pulses | Row lines left at 0 | Spacer CLKs.
    {SPWM_INIT_STEP_LAT_PULSES, 3, 0, 6},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 8},
    {SPWM_INIT_STEP_REGISTER, 1, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 2, 0, 0},
    {SPWM_INIT_STEP_RGB_REGISTER, 3, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 4, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 5, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 6, 0, 0},
};

static const SPWM_Init_Sequence SPWM_SM16380SH_INIT_SEQUENCE =
    spwm_make_init_sequence(SPWM_SM16380SH_INIT_STEPS);

// Purpose: Build the SM16380SH register layout for the active panel width.
// Inputs: Panel timing/settings and the resolved column count.
// Outputs: A runtime register bundle with repeated fixed words and RGB sequence data.
// Side effects: None.
SPWM_Config spwm_create_sm16380sh_config(
    const SPWM_Panel_Settings &spwm_settings, int spwm_columns) {
  SPWM_Config spwm_config(SPWM_SM16380SH_REGISTER_COUNT,
                          SPWM_SM16380SH_REGISTER_TIMINGS[0],
                          spwm_resolve_register_repeat_count(spwm_settings,
                                                             spwm_columns));

  spwm_config.spwm_add_register(1, {SPWM_SM16380SH_REGISTER1_WORD},
                                &SPWM_SM16380SH_REGISTER_TIMINGS[0]);
  spwm_config.spwm_add_register(2, {SPWM_SM16380SH_REGISTER2_WORD},
                                &SPWM_SM16380SH_REGISTER_TIMINGS[1]);
  spwm_config.spwm_add_rgb_register(
      3,
      {spwm_make_words(SPWM_SM16380SH_BLOCK3_SEQ_R),
       spwm_make_words(SPWM_SM16380SH_BLOCK3_SEQ_G),
       spwm_make_words(SPWM_SM16380SH_BLOCK3_SEQ_B)},
      SPWM_SM16380SH_REGISTER_TIMINGS[2]);
  spwm_config.spwm_add_register(4, {SPWM_SM16380SH_REGISTER4_WORD},
                                &SPWM_SM16380SH_REGISTER_TIMINGS[3]);
  spwm_config.spwm_add_register(5, {SPWM_SM16380SH_REGISTER5_WORD},
                                &SPWM_SM16380SH_REGISTER_TIMINGS[4]);
  spwm_config.spwm_add_register(6, {SPWM_SM16380SH_REGISTER6_WORD},
                                &SPWM_SM16380SH_REGISTER_TIMINGS[5]);

  return spwm_config;
}

// -------------------------------------------------------------------------------------------------
// FM6363 profile definition.
// FM6363 uses the DP32020A-style shift-register receiver, so register uploads
// carry longer, per-register LAT timing. Its default OE profile also expects
// the FM6363/DP32020A blank-clock-aligned timing even if row select is
// overridden to direct A-E writes later.
// -------------------------------------------------------------------------------------------------

static const size_t SPWM_FM6363_REGISTER_COUNT = 5;
static const int SPWM_FM6363_REGISTER_SEND_LAT_SPACE = 7;
static const uint8_t SPWM_FM6363_REGISTER1_SEND_LAT[] = {4, 14};
static const uint8_t SPWM_FM6363_REGISTER2_SEND_LAT[] = {6, 14};
static const uint8_t SPWM_FM6363_REGISTER3_SEND_LAT[] = {8, 14};
static const uint8_t SPWM_FM6363_REGISTER4_SEND_LAT[] = {10, 14};
static const uint8_t SPWM_FM6363_REGISTER5_SEND_LAT[] = {2};
static const SPWM_Register_Timing SPWM_FM6363_REGISTER_TIMINGS[] = {
    spwm_make_register_timing(SPWM_FM6363_REGISTER1_SEND_LAT,
                              SPWM_FM6363_REGISTER_SEND_LAT_SPACE),
    spwm_make_register_timing(SPWM_FM6363_REGISTER2_SEND_LAT,
                              SPWM_FM6363_REGISTER_SEND_LAT_SPACE),
    spwm_make_register_timing(SPWM_FM6363_REGISTER3_SEND_LAT,
                              SPWM_FM6363_REGISTER_SEND_LAT_SPACE),
    spwm_make_register_timing(SPWM_FM6363_REGISTER4_SEND_LAT,
                              SPWM_FM6363_REGISTER_SEND_LAT_SPACE),
    spwm_make_register_timing(SPWM_FM6363_REGISTER5_SEND_LAT),
};

static const SPWM_Panel_Settings SPWM_FM6363_SETTINGS = []() {
  SPWM_Panel_Settings spwm_settings = spwm_make_default_panel_settings();
  spwm_settings.auto_tune_oe_gaps = false;
  spwm_settings.auto_tune_frames = 0;
  spwm_settings.auto_tune_max_step_clks = 0;
  spwm_settings.first_oe_clk_length = 78;
  spwm_settings.end_of_frame_extra_row_cycles = 10;
  spwm_settings.frame_end_sleep_us = 100;
  spwm_settings.oe_during_upload_clk_count = 212;
  spwm_settings.oe_after_upload_clk_count = 212;
  spwm_settings.oe_clk_look_behind = 0;
  spwm_settings.oe_clk_length = 74;
  spwm_settings.oe_style = SPWM_OE_STYLE_FM6363;
  return spwm_settings;
}();
static const uint16_t SPWM_FM6363_REGISTER1_WORD = 0x1fb0;
static const uint16_t SPWM_FM6363_REGISTER2_WORD = 0xf39c;
static const uint16_t SPWM_FM6363_REGISTER3_WORD = 0x20b6;
static const uint16_t SPWM_FM6363_REGISTER4_WORD = 0x1a00;
static const uint16_t SPWM_FM6363_REGISTER5_WORD = 0x7e08;

// FM6363 frame start: emit the wake-up LAT bursts, each with an optional
// trailing LAT-low spacer count, then stream the five fixed control registers
// with their per-register LAT postambles.
static const SPWM_Init_Step SPWM_FM6363_INIT_STEPS[] = {
    // LAT pulses | Row lines left at 0 | Spacer CLKs.
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_LAT_PULSES, 12, 0, 0},
    {SPWM_INIT_STEP_LAT_PULSES, 3, 0, 0},
    {SPWM_INIT_STEP_LAT_PULSES, 14, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 1, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 2, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 3, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 4, 0, 0},
    {SPWM_INIT_STEP_REGISTER, 5, 0, 0},
};

static const SPWM_Init_Sequence SPWM_FM6363_INIT_SEQUENCE =
    spwm_make_init_sequence(SPWM_FM6363_INIT_STEPS);

// Purpose: Build the FM6363 register layout for the active panel width.
// Inputs: Panel timing/settings and the resolved column count.
// Outputs: A runtime register bundle with the five fixed FM6363 control words.
// Side effects: None.
SPWM_Config spwm_create_fm6363_config(const SPWM_Panel_Settings &spwm_settings,
                                      int spwm_columns) {
  SPWM_Config spwm_config(SPWM_FM6363_REGISTER_COUNT,
                          SPWM_FM6363_REGISTER_TIMINGS[0],
                          spwm_resolve_register_repeat_count(spwm_settings,
                                                             spwm_columns));

  spwm_config.spwm_add_register(1, {SPWM_FM6363_REGISTER1_WORD},
                                &SPWM_FM6363_REGISTER_TIMINGS[0]);
  spwm_config.spwm_add_register(2, {SPWM_FM6363_REGISTER2_WORD},
                                &SPWM_FM6363_REGISTER_TIMINGS[1]);
  spwm_config.spwm_add_register(3, {SPWM_FM6363_REGISTER3_WORD},
                                &SPWM_FM6363_REGISTER_TIMINGS[2]);
  spwm_config.spwm_add_register(4, {SPWM_FM6363_REGISTER4_WORD},
                                &SPWM_FM6363_REGISTER_TIMINGS[3]);
  spwm_config.spwm_add_register(5, {SPWM_FM6363_REGISTER5_WORD},
                                &SPWM_FM6363_REGISTER_TIMINGS[4]);

  return spwm_config;
}

// This table describes panel-tied behavior only: init sequence, register
// payloads, default OE timing, and panel geometry defaults. The runtime row
// transport still comes from --led-spwm-row-addr-type, so a profile such as
// FM6363 can keep its OE timing while using either direct A-E or blank-clock
// row transport.
static const SPWM_Panel_Profile SPWM_PANEL_PROFILES[] = {
    {"fm6373",
     SPWM_FM6373_SETTINGS,
     spwm_create_fm6373_config,
     SPWM_FM6373_INIT_SEQUENCE},
    {"icnd1065l",
     SPWM_ICND1065L_SETTINGS,
     spwm_create_icnd1065l_config,
     SPWM_ICND1065L_INIT_SEQUENCE},
    {"sm16380sh",
     SPWM_SM16380SH_SETTINGS,
     spwm_create_sm16380sh_config,
     SPWM_SM16380SH_INIT_SEQUENCE},
    {"dp3265s",
     SPWM_DP3265S_SETTINGS,
     spwm_create_dp3265s_config,
     SPWM_DP3265S_INIT_SEQUENCE},
    {"sm16269s",
     SPWM_SM16269S_SETTINGS,
     spwm_create_sm16269s_config,
     SPWM_SM16269S_INIT_SEQUENCE},
    {"fm6363",
     SPWM_FM6363_SETTINGS,
     spwm_create_fm6363_config,
     SPWM_FM6363_INIT_SEQUENCE},
};

}  // namespace

// Return the first profile in the built-in table, which is also the fallback
// when no explicit match is found.
const SPWM_Panel_Profile &spwm_get_default_panel_profile() {
  // Keep the preferred fallback profile first in SPWM_PANEL_PROFILES.
  return SPWM_PANEL_PROFILES[0];
}

// Look up a panel profile by name using the same prefix match used by the
// runtime panel-type option.
const SPWM_Panel_Profile *spwm_find_panel_profile(const char *spwm_panel_type) {
  for (const SPWM_Panel_Profile &spwm_profile : SPWM_PANEL_PROFILES) {
    if (spwm_panel_type_matches(spwm_panel_type, spwm_profile.panel_type)) {
      return &spwm_profile;
    }
  }
  return nullptr;
}

SPWM_Panel_Settings spwm_resolve_profile_settings(
    const SPWM_Panel_Profile &spwm_profile, int spwm_columns) {
  SPWM_Panel_Settings result;
  if (spwm_profile.panel_type != nullptr &&
      strcasecmp(spwm_profile.panel_type, "icnd1065l") == 0) {
    result = spwm_resolve_icnd1065l_settings(spwm_columns);
  } else {
    result = spwm_profile.settings;
  }
  spwm_apply_ini_overrides(&result, spwm_profile.panel_type);
  return result;
}
}  // namespace internal
}  // namespace rgb_matrix
