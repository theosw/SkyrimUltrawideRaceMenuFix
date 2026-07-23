# Ultrawide Chargen Camera Fix

SKSE plugin that fixes the character creation camera on ultrawide displays.

## The bug

On any display wider than 16:9, the character creation camera
(`showracemenu` / new game chargen) sits too close to the character. At 21:9
it is ~30% too close; at 32:9 the distance halves — the face zoom clips
*inside* the character's head, and the body zoom crops off the head and legs.
This has been broken since 2011 and there is no INI or game setting for it:
the vanilla `RaceSexCamera` computes its zoom distances from the horizontal
extent of the real projection, which grows linearly with aspect ratio, so the
computed distance shrinks by the same factor.

## The fix

The plugin hooks `RaceSexCamera::Update` and pushes the camera straight back
along its view axis so the on-axis distance to the character becomes exactly
what a 16:9 screen gets. Skyrim's vertical FOV is aspect-independent (Hor+),
so restoring the 16:9 distance restores the exact 16:9 framing — the extra
screen width just becomes breathing room. On a 16:9 display the plugin does
nothing.

Details:

- The aspect ratio is read from the live `NiCamera` frustum, so it is correct
  regardless of how the resolution was configured (including setups where
  `iSize W/H` intentionally lies to the engine).
- Corrections are computed in a shadow space: the engine always integrates
  from its own uncorrected position, so the fix cannot feed back into the
  engine's zoom smoothing or fight RaceMenu's camera tab (manual camera moves
  are detected and adopted).
- `fUserScale` in the INI applies an extra multiplier for taste and is
  re-read every time the menu opens, so it can be tuned live.

## Configuration

`Data/SKSE/Plugins/UltrawideChargenCameraFix.ini`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `fDistanceScale` | `0.0` | `0` = auto (real aspect ÷ 16:9); otherwise an explicit multiplier, e.g. `2.0` for 32:9 |
| `fUserScale` | `1.0` | Extra multiplier on top; `1.0` = exact 16:9 framing |
| `bVerboseLog` | `false` | Per-update camera logging for debugging |

## Requirements

- Skyrim SE/AE with [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Building

```
cmake --preset AE
cmake --build build --config Release
```

Uses [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) (MIT);
point `UCCF_COMMONLIBSSE_NG_DIR` at a checkout. Dependencies resolve through
vcpkg (`VCPKG_ROOT` must be set).

## License

[GPL-3.0](LICENSE)
