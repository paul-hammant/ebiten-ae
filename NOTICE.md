# Notice

This repository is an [Aether](https://github.com/aether-lang-dev/aether)-language
port of [Ebitengine](https://ebitengine.org) ("Ebiten"), rendering through
[aether-ui](https://github.com/aether-lang-dev/aether-ui) instead of the
original GLFW/OpenGL/Metal/DirectX stack.

The port is licensed under the Apache License, Version 2.0 (see `LICENSE`).

Portions Copyright 2013 Hajime Hoshi and The Ebiten Authors, from the
original Go implementation (Apache License 2.0), which is preserved intact —
including its own `NOTICE.md` covering the Go stack's bundled third-party
libraries — on the `legacy_golang` branch of this repository. Ports of Go
sources and embedded original assets (e.g. the `ebitenutil/text.png` debug
font sheet) carry a "Portions Copyright" header naming them.

`examples/resources/` retains the original example assets and their license
files; in particular the Go Gopher by Renee French is licensed under
CC BY 3.0.
