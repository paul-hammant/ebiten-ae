# Ebiten → Aether migration (in-situ)

Converting this engine from Go to [Aether](../aether) with
[aether-ui](../aether-ui) as the display/input layer. The original Go tree
lives on the `legacy_golang` branch as the porting oracle; `main` holds the
Aether engine, laid out idiomatically (Apache 2.0, portions copyright the
original authors — see NOTICE.md). Pattern follows mquickjs-port / avn /
zsync-port: leaf-first, parity-tested, oracle on a legacy branch.

## Architecture decision

Ebiten's stack below the public API — `internal/glfw` (24k lines),
`internal/graphicsdriver` (20k, OpenGL/Metal/DirectX), `internal/ui` (14k),
mobile/js/playstation5 glue — is **not ported**. aether-ui replaces it:

- **Every `aebiten.Image` is a CPU-side RGBA8888 buffer.** `DrawImage` is a
  software blit: inverse-affine sampling (GeoM), ColorScale in premultiplied
  domain, ColorM in straight-alpha domain, full blend-factor table, nearest +
  linear filters. **100% Aether, including the per-pixel inner loops.**
  History: on aether v0.545 the accessor-call overhead forced those loops
  into a temporary C file (141ms → 0.6ms per 300-tile frame); the
  aether#1733 ask got the `std.mem` scalar accessors codegen-inlined, and
  with the repo's `aether.toml` pinning `-O2` the pure-Aether loops now
  measure 0.8ms/frame on that workload and 4.1ms on a rotated 320×240
  bilinear blit — parity with the deleted C, inside the 16ms budget with
  room to spare. (Ask 2 of #1733 — offset bulk copy/fill — would buy the
  last fraction via row memcpy, but per-pixel int32 copies at `-O2`
  already vectorize to parity; it is no longer load-bearing for this
  port.)
- **The screen is one such Image**, pushed to an aether-ui canvas each frame
  (`canvas_draw_image_ptr` / `vg.live` video_region) on a `ui.timer` tick.
- **Game loop**: fixed-TPS `update` accumulator (60Hz, `std.os`
  monotonic clock) + per-frame `draw`, Ebiten semantics.
- **Input**: `canvas_on_key` / `canvas_on_click` / `canvas_on_move` /
  `canvas_on_scroll` feed a key/mouse state table; `inpututil`-style
  just-pressed/just-released derived per tick.
  GAP: aether-ui has no key-release event yet → needs `canvas_on_key_release`
  upstream (GTK4 real, macOS/win32 stubs per the both-servers rule).

## Layout

- `aebiten/core.ae` — pure module (`import aebiten.core`), no ui dependency,
  headless-testable: GeoM, ColorScale, blend table, Image + blitter.
- `aebiten/module.ae` — the engine (`import aebiten`): window/canvas/timer
  loop, input state, screen present. Depends on `ui` + `aebiten.core`.
- `examples/<name>/` — ported examples, one aeb node each
  (`.build.ae` per app, `build_support/aebitenui` supplies the backend link
  block against `../aether-ui`, override with `AETHER_UI_ROOT`).
- `tests/` — headless parity tests for the pure modules.

Build: `aeb examples/<name>` from the repo root →
`target/build/examples/<name>/bin/<name>`.

## Status

| Piece | Go source | Aether | State |
|---|---|---|---|
| Build/link spike vs ../aether-ui | — | `examples/spike` | ✅ builds, runs under xvfb |
| GeoM | `geom.go` | `aebiten/core.ae` | ✅ + tests |
| ColorScale | `colorscale.go` | `aebiten/core.ae` (embedded in Opts) | ✅ + tests |
| Blend (factors + 13 named modes) | `blend.go` | `aebiten/core.ae` | ✅ + tests (custom factor combos: todo) |
| Image + DrawImage blitter | `image.go` (API); software renderer replaces GPU path | `aebiten/core.ae` | ✅ + tests (nearest/linear, sub-image src+dst, fill/set/at, read/write pixels) |
| ColorM 4×5 (`colorm` pkg) | `colorm/`, `internal/affine` | `aebiten/core.ae` `colorm_*` | ✅ + tests (scale/translate/concat/hue/HSV; blitter-integrated) |
| Game loop / RunGame | `run.go`, `gameforui.go`, `internal/clock` | `aebiten/module.ae` | ✅ fixed 60 TPS accumulator + per-frame draw + FPS counter + window scale (`set_scale`) |
| Keys + input state | `keys.go`, `input.go`, `internal/inputstate` | `aebiten/module.ae` | ✅ (key-release added to aether-ui upstream; ~75 keys mapped) |
| inpututil (just pressed/released) | `inpututil/` | `aebiten/module.ae` | ✅ keys + left mouse; wheel per tick |
| ebitenutil DebugPrint | `ebitenutil/`, `text.png` | `aebiten/util.ae` | ✅ font sheet embedded (hex) + scaled/aligned variant |
| vector | `vector/` | `aebiten/vector.ae` | ✅ + tests (fill/stroke rect+circle+line, Path with quad/cubic/arc, nonzero+even-odd scanline fill; no AA yet) |
| PNG decode (assets) | `internal/png` | `aebiten/png.ae` | ✅ + tests (8-bit gray/RGB/palette/GA/RGBA, all filters; no interlace/16-bit) |
| audio | `audio/` | `aebiten/audio.ae` | ✅ + tests over `std.audio` (WAV + whatever miniaudio sniffs; real device backend) |
| text (TTF, text/v2) | `text/` | `aebiten/text.ae` | ✅ + tests — vg.font (aether-ui's pure-Aether TTF parser: glyf, cmap 4, kern) outlines through the scanline rasterizer, into images; flappy titles now render the original PressStart2P |
| DrawTriangles | `image.go` | `aebiten/core.ae` | ✅ + tests (barycentric, per-vertex color+UV, straight-alpha mode; pure Aether — hot-path C port when an example needs it) |
| Shaders (Kage) | `shader.go`, `internal/shader*` | **deferred** | no software-shader story yet |
| gamepad, vibrate, mobile, js, ps5 | `internal/gamepad` etc | **not ported** | out of scope for canvas backend |

## Ported examples

| Example | State |
|---|---|
| `examples/spike` | ✅ pipeline proof (buffer → canvas at 60Hz) |
| `examples/snake` | ✅ full port incl. HUD; driver-verified (keys, movement, screenshot) |
| `examples/rotate` | ✅ (ebiten.png asset in place of gophers.jpg — no JPEG decoder) |
| `examples/flappy` | ✅ full port, driver-verified through game-over (bitmap font for TTF text; ogg→wav fallback; no CRT/touch/gamepad) |
| `examples/life` | ✅ (WritePixels path; runs at 2x window scale via `set_scale`) |
| `examples/paint` | ✅ (ColorM hue-rotating brush; driver-verified drag painting) |
| `examples/polygons` | ✅ (DrawTriangles rainbow n-gon; Up/Down replaces the debugui slider) |
| `examples/doomfire` | ✅ (classic PSX fire, 100x50 at 6x scale) |
| `examples/2048` | ✅ full game (palette, slide+pop animations, scoring; bitmap font for TTF, no touch) |

CI: `./ci.sh` — headless unit tests, full example build (`aeb all.ae`),
snake driver smoke under xvfb.

## Not ported (replaced or dropped)

`internal/{glfw, graphicsdriver, ui, atlas, packing, buffered,
graphicscommand, mipmap, thread, cocoa, microsoftgdk, winver, fbdev, vibrate,
gamepad, gamepaddb}`, `mobile/`, `playstation5/`, JS/WASM support — the
aether-ui backends (GTK4/AppKit/Win32) own windowing, present, and input
delivery.

## aether-ui upstream changes this port drove

- `canvas_on_key_release` (+ driver `POST /canvas/{id}/keyup`) — commit 8732365.
- `canvas_draw_image_scaled_ptr` — ptr-buffer twin of the scaled blit, for the
  engine's window scaling.
