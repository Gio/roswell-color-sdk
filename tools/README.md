# tools

The tools for the game's art and audio. Each editor works on a project folder that you choose: the folder
holds the project's JSON files (the source), and the editor exports C from them. Python tools need only the
standard library (tkinter for the editors).

| Tool | What it does |
| --- | --- |
| `arted.py` | The art editor, its checks and its export to C |
| `auded.py` | The audio editor (music and sound effects), its checks and its export to C |
| `test_art.py`, `test_audio.py` | Their tests |
| `artx.js`, `soundx.py` | The one-time imports from old-spook into this repo's `assets/` |

## Choosing the folder

Run an editor with no folder and it asks for one:

    py tools/arted.py
    py tools/auded.py

Type the folder, or Browse to it, and press Open. The window says what Open will do: open the project
already there, or make the folder (if it does not exist) with a blank project in it. It starts from the
folder you ran it from. Nothing is saved outside the project folder. In the editor, File > Open folder (Ctrl+O) switches to another folder the same way.

You can also give the folder on the command line; it is made too if it does not exist:

    py tools/arted.py D:\games\mygame\art
    py tools/auded.py D:\games\mygame\audio

The other commands always take the folder:

    py tools/arted.py new <art dir> [C dir]          py tools/auded.py new <audio dir> [C dir]
    py tools/arted.py check <art dir>                py tools/auded.py check <audio dir>
    py tools/arted.py export <art dir> [C dir]       py tools/auded.py export <audio dir> [C dir]
                                                     py tools/auded.py wav <audio dir> <tune> <file>

**Where the C goes.** Each project remembers its export folder in its `art.json` or `sound.json`
(`"export"`, relative to the project, so they can move together). A new project exports to `src/art` or
`src/sound` beside its folder (an audio folder inside an art folder exports beside the art folder), or to the
C dir given to `new`. File > Export folder in the editor changes it. A C dir given to `export` is used
for that run only.

In this repo, the art is in `assets/` (exporting to `src/art`) and the audio in `assets/sound/` (exporting
to `src/sound`).

## Art: `arted.py`

Palettes, fonts, groups of sprite and background tiles, sprites and metatiles, and place pictures.

- The New menu makes a palette, font, group, sprite, metatile, tile or picture; File > Save writes the
  project; File > Export C writes the C. Help > Keys lists the controls.
- `check` lists anything that would stop the export. `check`, `new` and `export` need no display.
- The file layout is described at the top of `arted.py`.
- `py tools/test_art.py` drives the editor through every view and edit on a temp copy of this repo's
  `assets/` (about a minute; it needs a display and never touches the real files).

## Audio: `auded.py`

Songs, sound effects, instruments and waves for the console's 4 channels (CH1 pulse with sweep, CH2 pulse,
CH3 wave, CH4 noise).

- Songs and sound effects are grids of notes, a row at a time, typed on the keyboard like a piano
  (Help > Keys). Space plays from the cursor. What you hear is what the console plays: the preview is
  checked against the console sample for sample.
- `check` lists problems and `wav` writes a song or sound effect to a .wav. None of the commands need a
  display.
- In the game, call `snd_update()` once a frame, `snd_music_play(&SND_SONG_TITLE)` for a song and
  `snd_sfx_play(&SND_SFX_BLIP)` for a sound effect. A sound effect takes the channels it uses from the song
  until it ends (the exported `sound.h` lists it all).
- The file layout is described at the top of `auded.py`.
- `py tools/test_audio.py` tests the editor on a temp copy of this repo's `assets/sound`. When WSL has
  Emscripten (`~/emsdk`), it also builds the export with the console SDK and plays every song and sound
  effect on the console, through its music player and through the driver, against the editor's preview.

## The old-spook imports

This repo's art and audio were imported once from old-spook: `node tools/artx.js` and
`py tools/soundx.py`. Both take the old-spook folder and the folder to write to. Neither will replace a
project that is already there unless given `--force`, which replaces every edit made since. The audio import is
a translation, not a copy: the old game used Web Audio filters and free volumes the console does not have.
