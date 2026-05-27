// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// spwm-panel-ini.h - INI file override for SPWM_Panel_Settings
// Reads /etc/spwm-panel.ini or ./spwm-panel.ini and overrides
// the named panel profile's settings at runtime.
#ifndef RGBMATRIX_SPWM_PANEL_INI_H
#define RGBMATRIX_SPWM_PANEL_INI_H

#include "spwm-helpers.h"

namespace rgb_matrix {
namespace internal {

// Apply overrides from an INI file to the given settings struct.
// Searches for the file in this order:
//   1. ./spwm-panel.ini  (current working directory)
//   2. /etc/spwm-panel.ini
// If neither file exists, settings are unchanged.
// Only keys present in the file are overridden.
void spwm_apply_ini_overrides(SPWM_Panel_Settings *settings,
                               const char *panel_type);

}  // namespace internal
}  // namespace rgb_matrix

#endif  // RGBMATRIX_SPWM_PANEL_INI_H