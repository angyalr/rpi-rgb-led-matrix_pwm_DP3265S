// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// spwm-panel-ini.cc - Runtime INI file override for SPWM_Panel_Settings
#include "spwm-panel-ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace rgb_matrix {
namespace internal {

namespace {

char *trim(char *s) {
  while (*s && isspace((unsigned char)*s)) ++s;
  char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)*(end - 1))) --end;
  *end = '\0';
  return s;
}

void apply_line(SPWM_Panel_Settings *s, const char *key, const char *val) {
  int ival = atoi(val);
  bool bval = (ival != 0) || (strcasecmp(val, "true") == 0)
                           || (strcasecmp(val, "yes") == 0);

#define MATCH_INT(name)  if (strcasecmp(key, #name) == 0) { s->name = ival; return; }
#define MATCH_BOOL(name) if (strcasecmp(key, #name) == 0) { s->name = bval; return; }

  MATCH_INT(default_rows)
  MATCH_INT(default_columns)
  MATCH_INT(upload_channels_per_chip)
  MATCH_INT(upload_word_bits)
  MATCH_INT(upload_chip_count)
  MATCH_INT(end_of_frame_extra_row_cycles)
  MATCH_INT(frame_end_sleep_us)
  MATCH_INT(first_oe_clk_length)
  MATCH_INT(oe_clk_length)
  MATCH_INT(oe_clk_look_behind)
  MATCH_INT(oe_during_upload_clk_count)
  MATCH_INT(oe_after_upload_clk_count)
  MATCH_INT(auto_tune_frames)
  MATCH_INT(auto_tune_max_step_clks)
  MATCH_INT(shiftreg_row_select_a_pulse_clk_count)
  MATCH_INT(shiftreg_row_select_a_pulse_start_clk)
  MATCH_BOOL(auto_tune_oe_gaps)
  MATCH_BOOL(shiftreg_row_select_a_pulse_centered)

  if (strcasecmp(key, "oe_style") == 0) {
    if (strcasecmp(val, "fm6373") == 0)   s->oe_style = SPWM_OE_STYLE_FM6373;
    if (strcasecmp(val, "fm6363") == 0)   s->oe_style = SPWM_OE_STYLE_FM6363;
    if (strcasecmp(val, "dp3265s") == 0)  s->oe_style = SPWM_OE_STYLE_DP3265S;
    return;
  }

#undef MATCH_INT
#undef MATCH_BOOL
}

bool try_load(const char *path, SPWM_Panel_Settings *settings,
              const char *panel_type, bool *debug) {
  FILE *f = fopen(path, "r");
  if (!f) return false;

  char line[256];
  bool in_section = false;

  while (fgets(line, sizeof(line), f)) {
    char *p = trim(line);
    if (*p == '\0' || *p == '#' || *p == ';') continue;

    if (*p == '[') {
      char *end = strchr(p, ']');
      if (end) *end = '\0';
      const char *section = trim(p + 1);
      in_section = (strcasecmp(section, panel_type) == 0 ||
                    strcasecmp(section, "global") == 0);
      continue;
    }

    if (!in_section) continue;

    char *eq = strchr(p, '=');
    if (!eq) continue;
    *eq = '\0';
    char *key = trim(p);
    char *val = trim(eq + 1);
    if (strcasecmp(key, "debug") == 0) {
      const int ival = atoi(val);
      *debug = (ival != 0) || (strcasecmp(val, "true") == 0) ||
               (strcasecmp(val, "yes") == 0);
      continue;
    }
    apply_line(settings, key, val);
  }

  fclose(f);
  return true;
}

void print_settings(const SPWM_Panel_Settings *s) {
  const char *oe_style_name = "unknown";
  switch (s->oe_style) {
    case SPWM_OE_STYLE_FM6373:  oe_style_name = "fm6373";  break;
    case SPWM_OE_STYLE_FM6363:  oe_style_name = "fm6363";  break;
    case SPWM_OE_STYLE_DP3265S: oe_style_name = "dp3265s"; break;
  }
  fprintf(stderr, "[spwm-ini] Active settings:\n");
  fprintf(stderr, "  default_rows                       = %d\n", s->default_rows);
  fprintf(stderr, "  default_columns                    = %d\n", s->default_columns);
  fprintf(stderr, "  upload_channels_per_chip           = %d\n", s->upload_channels_per_chip);
  fprintf(stderr, "  upload_word_bits                   = %d\n", s->upload_word_bits);
  fprintf(stderr, "  upload_chip_count                  = %d\n", s->upload_chip_count);
  fprintf(stderr, "  end_of_frame_extra_row_cycles      = %d\n", s->end_of_frame_extra_row_cycles);
  fprintf(stderr, "  frame_end_sleep_us                 = %d\n", s->frame_end_sleep_us);
  fprintf(stderr, "  first_oe_clk_length                = %d\n", s->first_oe_clk_length);
  fprintf(stderr, "  oe_clk_length                      = %d\n", s->oe_clk_length);
  fprintf(stderr, "  oe_clk_look_behind                 = %d\n", s->oe_clk_look_behind);
  fprintf(stderr, "  oe_during_upload_clk_count         = %d\n", s->oe_during_upload_clk_count);
  fprintf(stderr, "  oe_after_upload_clk_count          = %d\n", s->oe_after_upload_clk_count);
  fprintf(stderr, "  auto_tune_oe_gaps                  = %s\n", s->auto_tune_oe_gaps ? "true" : "false");
  fprintf(stderr, "  auto_tune_frames                   = %d\n", s->auto_tune_frames);
  fprintf(stderr, "  auto_tune_max_step_clks            = %d\n", s->auto_tune_max_step_clks);
  fprintf(stderr, "  shiftreg_row_select_a_pulse_clk_count = %d\n", s->shiftreg_row_select_a_pulse_clk_count);
  fprintf(stderr, "  shiftreg_row_select_a_pulse_start_clk = %d\n", s->shiftreg_row_select_a_pulse_start_clk);
  fprintf(stderr, "  shiftreg_row_select_a_pulse_centered  = %s\n", s->shiftreg_row_select_a_pulse_centered ? "true" : "false");
  fprintf(stderr, "  oe_style                           = %s\n", oe_style_name);
}
 
}  // namespace

void spwm_apply_ini_overrides(SPWM_Panel_Settings *settings,
                               const char *panel_type) {
  if (!settings || !panel_type) return;

  const SPWM_Panel_Settings before = *settings;
  bool debug = false;
  const char *loaded_path = nullptr;
  if (try_load("spwm-panel.ini", settings, panel_type, &debug)) {
    loaded_path = "spwm-panel.ini";
  } else if (try_load("/etc/spwm-panel.ini", settings, panel_type, &debug)) {
    loaded_path = "/etc/spwm-panel.ini";
  }

  if (debug) {
    if (loaded_path != nullptr) {
      fprintf(stderr, "[spwm-ini] Loading overrides from %s for panel [%s]\n",
              loaded_path, panel_type);
    }
    fprintf(stderr, "[spwm-ini] Settings BEFORE overrides:\n");
    print_settings(&before);
    fprintf(stderr, "[spwm-ini] Settings AFTER overrides:\n");
    print_settings(settings);
  }
}

}  // namespace internal
}  // namespace rgb_matrix
