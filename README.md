# Ebiten, in Aether

A 2D game engine written in
[Aether](https://github.com/aether-lang-dev/aether), rendering through
[aether-ui](https://github.com/aether-lang-dev/aether-ui)'s canvas
(GTK4 / AppKit / Win32). Games are `.ae` programs: a fixed-60-TPS
`on_update`, a per-frame `on_draw` against a software-composited
framebuffer, sprite images with affine transforms, color matrices, blend
modes, paths, bitmap text, and audio. This branch is Aether end to end —
there is no Go on it, and no GPU dependency.

The engine's design and API lineage come from
[Ebitengine](https://ebitengine.org) ("Ebiten"), Hajime Hoshi's Go engine.
License: Apache 2.0, portions copyright Hajime Hoshi and The Ebiten
Authors — see [`NOTICE.md`](NOTICE.md). Architecture, status table, and
what is deliberately not carried over:
[`AE-MIGRATION.md`](AE-MIGRATION.md).

## The legacy Go branch

The complete, unmodified Go implementation lives on
[`legacy_golang`](https://github.com/paul-hammant/ebiten-ae/tree/legacy_golang),
which tracks upstream
[hajimehoshi/ebiten](https://github.com/hajimehoshi/ebiten). It is the
porting oracle: behaviour questions are settled by reading (or running)
the Go, and anything not yet in the status table is found there. Nothing
from it is deleted — `main` and `legacy_golang` are two implementations
of the same engine, one repository apart.

## Layout

- `ebiten/core.ae` — pure drawing core: GeoM, ColorScale, ColorM, blend
  table, Image + DrawImage/DrawTriangles, and the per-pixel loops.
  Headless, no ui dependency — the whole engine is Aether.
- `ebiten/png.ae` — PNG decoder (std.zlib inflate).
- `ebiten/util.ae` — ebitenutil: DebugPrint bitmap font (embedded), scaled text.
- `ebiten/vector.ae` — vector: primitives + scanline path fill.
- `ebiten/audio.ae` — audio players over std.audio.
- `ebiten/module.ae` — the engine (`import ebiten`): game loop, input,
  window/canvas present.
- `examples/<name>/` — ported examples, one aeb node each
  (`examples/resources/` keeps the original assets).
- `tests/` — headless unit tests.

## Build & run

```sh
aeb all.ae                    # build everything
aeb examples/snake            # or one example
./target/build/examples/snake/bin/snake

# tests (headless, from the repo root):
ae run tests/test_core.ae

./ci.sh                       # the whole gate: tests + builds + driver smoke
```

Needs a sibling `../aether-ui` checkout (or `AETHER_UI_ROOT`) at or after
commit `ad960a8` (`canvas_on_key_release` + `canvas_draw_image_scaled_ptr`),
and an aether toolchain with the inlined `std.mem` accessors (the post-0.578
build; aether#1733). The repo's `aether.toml` pins `-O2` — the pixel loops
need the optimizer to exploit the inlined accessors.

## Writing a game

```aether
import ebiten
import ebiten.core

main() {
    e = ebiten.game_new(320, 240)          // logical size
    ebiten.set_scale(e, 2.0)               // optional window upscale
    ebiten.on_update(e) callback {          // 60 TPS fixed step
        if ebiten.is_key_just_pressed(e, ebiten.KEY_SPACE) == 1 { ... }
    }
    ebiten.on_draw(e) callback |screen: ptr| {   // per frame
        core.image_fill(screen, 0, 0, 0, 255)
        core.draw_image(screen, sprite, opts)
        ebiten.debug_print(e, screen, "FPS: ${ebiten.actual_fps(e)}")
    }
    ebiten.run(e, "Title", 660, 520)       // blocks until close
}
```

Driver testing: run with `AETHER_UI_TEST_PORT=<port>` and use the standard
AetherUIDriver routes — `POST /canvas/1/key?name=Left`, `/keyup`, `/click`,
`/move`, `/release`, `GET /screenshot`, `POST /shutdown`. A driver-injected
key press must span an engine tick (sleep ~20ms between key and keyup).
