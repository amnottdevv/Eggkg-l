# Eggkg-l

Package repository for [Equinox OS](https://github.com/amnottdevv) —
consumed by the in-OS `eggkg` command through
`package.list` (format v0, blob URLs converted to raw automatically).

## Packages

| Package | What it is | Sources |
| --- | --- | --- |
| `bash` | shell + coreutils class (19 programs) | 20 `.c` |
| `eqfetch` | mini neofetch | 1 `.c` |
| `emu-ch8` | CHIP-8 emulator (+ `bounce.c8` ROM) | 1 `.c` + 1 data |
| `wolf` | ray-cast maze shooter | 1 `.c` |
| `space` | space invader | 1 `.c` |
| `tetris` | falling blocks | 1 `.c` |
| `flappy` | flappy-bird style | 1 `.c` |
| `ppmview` | PPM (P6/P3) viewer (+ `demo.ppm`) | 1 `.c` + 1 data |

Index: 8 packages / 35 URLs / 2347 bytes.

## Requirement: Equinox v0.9.3 or newer

Every `build.ruf` here is written in the **`.ruf` v3** dialect, which
adds Makefile-style variables to the recipe language:

```ruf
pkg    := "ppmview"
source := "/equinox/.local/$pkg/src"
Target := "/equinox/.local/$pkg"
echo   "eggkg: Build $pkg (PPM P6/P3 viewer)"
src    $source
out    $Target
```

The v0.9 `mtcc` has no `:=` support and answers
`mtcc: make: baris N: direktif tidak dikenal: pkg`, so installing these
packages into a v0.9 image fails at the build step. Use the v0.9.3
kernel (or rewrite the recipe as plain `src`/`out` lines — both dialects
are accepted by the v0.9.3 parser).

Two details worth remembering when editing a recipe:

* a variable name must **not** be a directive word (`echo`, `src`,
  `exclude`, `out`, `lib`, `copy`, `move`) — directives are matched
  before the `:=` test, so `src := ...` registers a source directory
  instead of storing a variable;
* `copy A ? B` is intentionally unused: eggkg already moves the built
  `.mrp` from `out` into `/bin`.

## Installing

```console
eggkg update https://raw.githubusercontent.com/amnottdevv/Eggkg-l/main/package.list
eggkg install ppmview -y
```

Data files (`bounce.c8`, `demo.ppm`) are downloaded next to the
sources in `/equinox/.local/<pkg>/src/` and are ignored by the build
walker, which only matches the exact `.c` extension.
