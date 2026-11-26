# Roswell Color SDK

**Roswell Color** is a fantasy console with similar display, audio, and control limits to the Game Boy Color.
Games are written in C99 and compiled to WASM with Emscripten.

It is not a proper emulator: there are no registers or memory maps to poke. Games call a small, retained-mode C API
(`rc_dev.h`) that enforces the hardware's limits and reproduces its quirks: 10 sprites per scanline, 4-color tiles,
a 4-channel sound chip with sweeps, envelopes and an LFSR noise channel.

The SDK ships with an art editor and an audio editor that export assets straight to C.

## Features

| Area | What you get |
| --- | --- |
| **Display** | 160×144 pixels, 60 fps, integer-scaled to the browser window |
| **Color** | 15-bit RGB, 8 background palettes × 4 colors, 8 sprite palettes × 3 colors (+ transparent), up to 56 colors on screen |
| **Tiles** | 8×8 tiles at 2 bits per pixel, 1,024 tiles in banks you lay out yourself, 2 bindable slots per layer group |
| **Layers** | A scrolling, wrapping 32×32 background and an opaque window, with per-tile palette, flip and priority |
| **Sprites** | 40 sprites, 8×8 or 8×16, 10 per scanline, with hardware-accurate overlap and priority rules |
| **Audio** | 4 channels: pulse with sweep, pulse, 32-sample wave, noise. Stereo panning, master volume, 44.1 kHz output |
| **Music** | Frame-timed event tables, looping, shared with sound effects |
| **Controls** | D-pad + A, B, C, Start. Two keyboard layouts and up to 4 gamepads, merged into one controller |
| **Testing** | Headless mode (browser or Node.js), frame stepping, input feeding, pixel and audio readback |
| **Debug** | `RC_DEBUG` build with per-line limit warnings, an on-screen usage panel and inspection functions |

### Design rules

- **Retained mode.** Set up tiles, palettes, maps and sprites once, then change only what moves. There is no
  per-frame draw call.
- **No error codes.** Like the hardware, calls never fail: out-of-range indexes are ignored, out-of-range values
  are clamped, and blocks that partly fit are partly copied. A debug build tells you each time it happens.

## Quick start

```c
#define RC_DEBUG            // optional: must come before the include
#include "rc_dev.h"

static const uint8_t art[16] = { 0xFF,0x00, 0x81,0x7E, 0x81,0x7E, 0x81,0x7E,
                                 0x81,0x7E, 0x81,0x7E, 0x81,0x7E, 0xFF,0x00 };
static int px = 76;

static void frame(void *user) {
    (void)user;
    if (rc_held(RC_BTN_LEFT))  px--;
    if (rc_held(RC_BTN_RIGHT)) px++;
    rc_sprite_move(0, px, 68);
}

int main(void) {
    static const int bank_sizes[1] = { 16 };
    static const rc_color colors[3] = {
        RC_RGB(31,31,31), RC_RGB(31,0,0), RC_RGB(0,0,0) };

    rc_init("My game");
    rc_banks_setup(bank_sizes, 1);
    rc_bank_load(0, 0, art, 1);
    rc_sprite_bind(0, 0, 0);
    rc_sprite_palette(0, colors);
    rc_sprite_set(0, px, 68, 0, RC_SPRITE_PALETTE(0));
    rc_run(frame, NULL);    // never returns in the browser
    return 0;
}
```

Link against `librc.a` and `libraylib.web.a` with `emcc`, then open the generated page through a local web server.
Programs that use `rc_init_headless()` can also run under Node.js.

## Documentation

[`inc/rc_dev.h`](inc/rc_dev.h) is the complete API reference. You should not need to read the SDK's source to use
it. It covers:

1. Quick start
2. Rules that apply everywhere
3. Limits
4. Console lifecycle and the frame
5. Colors and palettes
6. Tiles and tile banks
7. Background and window
8. Sprites
9. Audio
10. Controls
11. Asset data formats
12. Debug

The editors are documented in [`tools/README.md`](tools/README.md).

## Controls

| Input | Keyboard 1 | Keyboard 2 | Gamepad (by position) |
| --- | --- | --- | --- |
| Up, Down, Left, Right | Arrow keys | W S A D | D-pad or left stick |
| A | Z | , | Left face |
| B | X | . | Bottom face |
| C | C | / | Right face |
| Start | Space | Enter | Right center (Options / Menu) |

## Tools

Two Python editors (standard library only, tkinter for the UI) keep their projects as JSON and export C that
compiles straight into your game.

| Tool | What it does |
| --- | --- |
| [`tools/arted.py`](tools/arted.py) | Art editor: palettes, fonts, tile groups, sprites, metatiles and pictures |
| [`tools/auded.py`](tools/auded.py) | Audio editor: songs, sound effects, instruments and waves, with a sample-accurate preview |

```sh
py tools/arted.py                 # open or create the art project in assets/
py tools/auded.py                 # open or create the audio project in assets/
py tools/arted.py export          # export art as C to data/art/
py tools/auded.py export          # export audio as C to data/music/ and data/sounds/
py tools/auded.py wav assets <tune> out.wav
```

`check`, `new`, `export` and `wav` run without a display, so they fit in a build script.

## Building the SDK

### Requirements

- [Emscripten](https://emscripten.org/) (`emcc`, `emar`) on your `PATH`
- GNU Make, `git`, `tar` and `lzip` (for `make dist`)
- Python 3 with tkinter, for the editors

On Windows, run the build from a POSIX shell such as Git Bash or MSYS2.

### Build

```sh
make            # full build: raylib, librc and the tools
```

The first build looks for `lib/raylib/libraylib.web.a`. If it is missing it builds raylib 6.0 from
`vendor/raylib`, cloning it from GitHub when that folder does not exist either.

| Target | What it does |
| --- | --- |
| `make` / `make release` | Full build into `rcsdk-v0.1.2/` |
| `make rcsdk` | Builds `librc.a` only |
| `make raylib` | Fetches and builds raylib for the web |
| `make tools` | Copies the editors into the SDK folder |
| `make dist` | Packs the SDK as `rcsdk-v0.1.2.tar.lz` |
| `make clean` | Removes intermediate files, keeps raylib |
| `make distclean` | Removes everything built or downloaded |

> **Note:** make reads `.RECIPEPREFIX` as a single byte, so the Makefile must stay in ISO 8859-1 (Windows-1252).
> If `make` fails with "permission denied", check that your editor has not re-saved it as UTF-8. `.gitattributes`
> keeps the encoding in the repository.

### SDK layout

```
rcsdk-v0.1.2/
├── LICENSE
├── inc/
│   ├── rc_dev.h
│   └── raylib/raylib.h
├── lib/
│   ├── librc.a
│   └── raylib/libraylib.web.a
└── tools/
    ├── README.md
    ├── arted.py
    └── auded.py
```

## Repository layout

| Path | Contents |
| --- | --- |
| `inc/rc_dev.h` | Public API and reference |
| `src/` | SDK source: `core.c`, `display.c`, `audio.c`, `input.c`, `platform.c` |
| `tools/` | Art and audio editors |
| `vendor/`, `lib/`, `build/` | raylib source, built libraries and intermediate files (generated, not tracked) |

## License

Roswell Color is released under the [MIT License](LICENSE). raylib, which the SDK builds and bundles, is
distributed under its own [zlib/libpng license](https://github.com/raysan5/raylib/blob/master/LICENSE).

## Acknowledgements

Built on [raylib](https://www.raylib.com/) by Ramon Santamaria.

Designed for the [Mall of the Future](https://malloffuture.com).

Roswell Color is an independent project. It is not affiliated with or endorsed by Nintendo. Game Boy Color is a
trademark of Nintendo.
