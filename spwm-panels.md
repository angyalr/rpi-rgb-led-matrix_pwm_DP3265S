# S-PWM Panel Notes

This fork contains experimental support for HUB75 panels that use S-PWM LED
driver ICs with internal grayscale memory. The work was tested on 64x32 panels
where the pixel SRAM upload is separate from the row display scan.

The code is based on the original `hzeller/rpi-rgb-led-matrix` library and the
PWM/S-PWM experiment branch. It is not an official upstream feature.

## Tested Panels

### DP3265S, 64x32

Observed hardware:

- HUB75 input.
- 64 columns x 32 rows.
- 48 x DP3265S LED driver ICs.
- 4 x SM5166PF 3-to-8 row decoder/driver ICs.
- ABC row address inputs.
- The HUB75 OE pin is wired to the DP3265S ROW input on this panel, not to a
  conventional display blanking input.
- One SDI/SDO chain contains eight driver ICs per RGB half-channel.

Example:

```sh
sudo examples-api-use/demo \
  -D0 \
  --led-rows=32 \
  --led-cols=64 \
  --led-chain=1 \
  --led-parallel=1 \
  --led-panel-type=dp3265s \
  --led-spwm-row-addr-type=0 \
  --led-slowdown-gpio=2
```

### SM16269S, 64x32

Observed hardware:

- Same PCB geometry as the DP3265S panel above.
- 48 x SM16269S LED driver ICs.
- SM5166PF row decoder/driver ICs.
- ABC row address inputs.
- The HUB75 OE pin is used as the panel ROW/RCLK-style input for this S-PWM
  scan path.

The public profile name is:

```text
sm16269s
```

Internally this is kept compatible with the final bring-up profile
`sm16269s_16380_fm_g2_lb107`.

Example:

```sh
sudo examples-api-use/demo \
  -D0 \
  --led-rows=32 \
  --led-cols=64 \
  --led-chain=1 \
  --led-parallel=1 \
  --led-panel-type=sm16269s \
  --led-spwm-row-addr-type=0 \
  --led-slowdown-gpio=6
```

`--led-slowdown-gpio=6` was needed on the tested Raspberry Pi 4 setup for a
stable SM16269S image.

## Notes For Reviewers

- S-PWM panel support is implemented in `lib/spwm-helpers.*` and
  `lib/spwm-panel-config.*`.
- Runtime panel selection is exposed with `--led-panel-type`.
- Runtime row transport is still selected with `--led-spwm-row-addr-type`.
- DP3265S and SM16269S panels use RGB SRAM uploads plus a continuously timed
  display scan. The upload path and row scan are intentionally treated as
  separate concerns.
- The SM16269S support was brought up empirically from a similar SM16380SH
  register/init path and panel observations. It may need more review against a
  complete SM16269S datasheet.

Useful references kept in this tree:

- `datasheets/DP3265S_Rev2.3_CN.pdf`
- `datasheets/DP3264S_EN_translated.pdf`
- `datasheets/DMD_STM32-dev-V2.zip`

## Publication Hygiene

Local application code, customer-specific display scripts, database import
helpers, logs, and large kernel tarballs were removed before publication. The
remaining tree should focus on the reusable LED matrix library changes.
