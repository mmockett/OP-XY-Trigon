# Machine development

This repository holds the Trigon Suite: three synthesis drum kits, one per
folder (`felt`, `flicker`, `dust`). Read README.md and each kit's own README
first.

All three build against the [Open-XY SDK](https://github.com/etcetc000/open-xy)
`staging` branch, which has the per-key mixer channels the kits use
(`padMix`, SDK 1.10,
[etcetc000/open-xy#399](https://github.com/etcetc000/open-xy/pull/399)). Do
not edit the SDK to make a kit work; propose an SDK change separately.

Follow the SDK's [machine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md),
[UX conventions](https://github.com/etcetc000/open-xy/blob/staging/docs/ux.md)
and [display rules](https://github.com/etcetc000/open-xy/blob/staging/docs/display.md).
For every kit you change, run

```sh
python ../open-xy/sdk.py test --machine <kit>
python ../open-xy/sdk.py test --machine <kit> --firmware
sh <kit>/tools/ub.sh ../open-xy
```

The last builds the kit's host tests and a sweep of its engine and screen
with clang++ and g++, with and without `-fwrapv`, and under UBSan and ASan,
and fails unless every build renders the sweep bit for bit alike. Keep
generated builds, firmware, stock OS files and the screen font outside the
repository. CI (`.github/workflows/checks.yml`) runs the SDK's tests for all
three kits and compiles them together into one firmware module.

## Rules

- **One kit, one folder.** A change to one kit touches only its folder.
  Shared tools live in `tools/`.
- **Written from first principles.** No code, tables, constants or samples
  from another instrument, drum machine, ROM or emulator. Measure, then write
  what you learn as formulas or fitted values.
- **Fixed point.** The OP-XY's Blackfin has no floating point, and 64-bit
  multiplies, divides and `__builtin_clz` call runtime helpers the firmware
  lacks. `-Wall -Werror`: no unused functions.
- **No undefined behaviour.** The OP-XY's compiler wraps a signed overflow or
  a shift of a negative value its own way, so a host render that relies on
  one is not what the device plays. `tools/ub.sh` must pass.
- **Generated files stay generated.** `tables.h`, `levels.h`, `art.h`,
  `panel.svg.fragment` and the like are written by the kit's `tools/`; change
  the generator and run it, then commit both.
- **Presets.** A command-line build installs no presets; each kit's
  `tools/presets.py` writes them. Keep the presets and the README's preset
  table in step.

## Sound

- Every key works in a groove at its defaults. Controls change character, not
  loudness, and every control is audible across its whole travel.
- Kicks and toms carry low end a small speaker still plays, not only under
  60 Hz.
- Levels are set by loudness (LUFS-M, 400 ms, K-weighted): kicks and toms
  -16.5, everything else -19, true peak at most -1 dB under the half-scale
  ceiling, every key at 0 dB. Each kit's `tools/calibrate.py` writes the
  trims.
- Colouring and degrading controls default to 0 on the default kit.

## CPU

Aim for at most 3.5% of the 580,352-cycle block for one voice at its worst
block. Count it in the compiled Blackfin code (each kit's CPU tool does), not
by timing on a computer, and measure it with `sdk.py profile` on an OP-XY
before stating it as a figure. No per-sample divides, envelopes at block
rate, no one-off work in a block, silent voices stop.

## Screens: check them the way the OP-XY draws them

The browser preview does not show everything the device does.

- **Judge at 1:1.** The screen is 480 × 222 px. No detail under about 3–4 px,
  no lines closer than about 3 px, no text under about 8 px.
- **Measure text in the device's font.** The OP-XY draws text in Univers Next
  Pro Light, which is stock content and never committed here. Header values
  are 20 px and must fit in 53 px; titles are 10 px and must fit in 56 px.
  Measure captions with `tools/caption_width.py` (set `XY_FONT` to your copy
  of the font).
- **Follow the device's rules for outlines.** An outline with no area, a
  turn (nearly) straight back on itself, points closer than 1/8 px or a
  closing edge under 1/4 px draws a streak on the device: it strokes outlines
  with mitred joins and no mitre limit. Draw lines as thin closed ribbons.
  Each kit's `tests/screen.cpp` checks its moving outlines over a sweep of
  its controls; keep every turn in the static artwork under 150° too
  (FLICKER's `tools/make_panel.py` refuses sharper ones).
- **Keep joined parts joined.** Build parts that must meet as one outline, or
  as nodes sharing one pivot, so the device cannot place them apart.
- **Keep the device fast.** About 20 changed nodes a frame on average, at
  every control setting; nothing that moves is filled; an idle screen changes
  nothing. The device fills only nodes whose artwork has a fill, and takes
  one subpath of at most 64 points per path.
- **Move physically.** Parts swing through their poses and settle; nothing
  jumps.
