# tools

The tools for the game's art and audio. Each editor works on a project folder: the folder holds the project's
JSON files (the source), and the editor exports C from them. Python tools need only the standard library
(tkinter for the editors).

| Tool | What it does |
| --- | --- |
| `arted.py` | The art editor, its checks and its export to C |
| `auded.py` | The audio editor (music and sound effects), its checks and its export to C |

## Where things go

By default both projects live in `assets/` at the project root (the folder above `tools/`). Their files have
different names, so they share the folder. The C goes to `data/`:

| Folder | What is in it |
| --- | --- |
| `assets/` | The source: `art.json`, `palettes.json`, `fonts/`, `groups/`, `pictures/` (art) and `sound.json`, `waves.json`, `instruments.json`, `songs/`, `sfx/` (audio) |
| `data/art/` | `art.h` and the `art_*.c` files |
| `data/music/` | `sound_songs.c`: the songs |
| `data/sounds/` | `sound.h`, `sound_sfx.c`, `sound_waves.c` and `sound_driver.c`: the sound effects, the waves and the driver that plays them with the music |

`data/music/sound_songs.c` includes `../sounds/sound.h`, so the two folders must stay side by side.

## Running the editors

    py tools/arted.py
    py tools/auded.py

With no folder, each opens `assets/`. If it has no project yet, they make the folder if needed and put a blank
project in it. To work on another folder, give it on the command line (it is made too if it does not exist),
or use File > Open folder (Ctrl+O) in the editor:

    py tools/arted.py D:\games\mygame\art
    py tools/auded.py D:\games\mygame\audio

The other commands take the folder too, and default to `assets/` without it:

    py tools/arted.py new [art dir] [C dir]          py tools/auded.py new [audio dir] [C dir]
    py tools/arted.py check [art dir]                py tools/auded.py check [audio dir]
    py tools/arted.py export [art dir] [C dir]       py tools/auded.py export [audio dir] [C dir]
                                                     py tools/auded.py wav [audio dir] <tune> <file>

**Where the C goes.** Each project remembers its export folder in its `art.json` or `sound.json`
(`"export"`, relative to the project, so they can move together). A new art project exports to `data/art`
beside its folder. A new audio project exports to `data` beside its folder, and writes `music/` and `sounds/`
in it. A C dir given to `new` is used instead. File > Export folder in the editor changes it. A C dir given
to `export` is used for that run only.

## Art: `arted.py`

Palettes, fonts, groups of sprite and background tiles, sprites and metatiles, and place pictures.

- The New menu makes a palette, font, group, sprite, metatile, tile or picture; File > Save writes the
  project; File > Export C writes the C. Help > Keys lists the controls.
- `check` lists anything that would stop the export. `check`, `new` and `export` need no display.
- The file layout is described at the top of `arted.py`.

## Audio: `auded.py`

Songs, sound effects, instruments and waves for the console's 4 channels (CH1 pulse with sweep, CH2 pulse,
CH3 wave, CH4 noise).

- Songs and sound effects are grids of notes, a row at a time, typed on the keyboard like a piano
  (Help > Keys). Space plays from the cursor. What you hear is what the console plays: the preview is
  checked against the console sample for sample.
- `check` lists problems and `wav` writes a song or sound effect to a .wav. None of the commands need a
  display.
- In the game, `#include "sound.h"` (from `data/sounds/`), call `snd_update()` once a frame,
  `snd_music_play(&SND_SONG_TITLE)` for a song and `snd_sfx_play(&SND_SFX_BLIP)` for a sound effect. A sound
  effect takes the channels it uses from the song until it ends (the exported `sound.h` lists it all).
- The file layout is described at the top of `auded.py`.
