# emu8bit

**8-bit machine emulator for Equinox OS** — one program, three 8-bit machines, a ROM
menu loader and a built-in self-test. Successor of the `emu-ch8` package.

Runs as a ring-3 `.mrp` program on top of the `int 0x80` syscall ABI
(Equinox OS 0.4 Beta). Written in the `mtcc` dialect (no struct / switch /
function pointers / unsigned), so it builds with the stock in-OS compiler.

## Machines

| Machine | Details |
|---|---|
| **CHIP-8 / SUPER-CHIP** | 4 KiB RAM, 64x32 mono display that auto-upgrades to 128x64 when the ROM issues `00FF`. Full COSMAC VIP opcode set plus SCHIP extensions: `00CN`/`00FB`/`00FC` scrolls, `00FE`/`00FF` mode switch, `DXY0` 16x16 sprites, `FX75`/`FX85` HP flags. |
| **Intel 8080** | 32 KiB RAM, 2 MHz, full documented opcode set with exact S/Z/AC/P/CY semantics (including the 8080 `ANA` aux-carry quirk), EI/DI + RST interrupt injection. Includes the classic **Space Invaders** machine: 8-bit shifter on ports 2/3/4, IN 1/2 inputs, OUT 3/5 sounds, 60 Hz dual IRQs (RST 1 mid-frame + RST 2 vblank) and the rotated 1bpp 224x256 framebuffer with a MAME-style color overlay. |

## ROM formats

| Extension | Machine selected |
|---|---|
| `.ch8` `.c8` `.sc8` | CHIP-8 / SUPER-CHIP (a SCHIP ROM switches to hires by itself, no sniffing needed) |
| `.bin` `.rom` | <= 3584 bytes -> CHIP-8, otherwise the 8080 machine (Space Invaders ROM set) |
| any other | size-based fallback, overridable per entry in the menu with **T** |

The ROM menu scans `.` , `/games` , `/roms` and the eggkg package dirs, plus two
built-ins: the **bounce** demo and the full **self-test**.

## Install

Via eggkg (recommended):

```
eggkg update
eggkg install emu8bit
```

Manual build inside Equinox OS:

```
mtcc -make /equinox/.local/emu8bit/build.ruf
```

`bounce.c8` is downloaded into the package `src` dir, which the menu scans, so
the demo is playable right after install.

## Controls

**Menu:** arrows / `wasd` or `j`/`k` move, `Enter`/`Space` run, `T` cycles the
type override, `Esc` quits.

**CHIP-8:** keypad-mapped `1234` / `qwer` / `asdf` / `zxcv`, `Space` pause,
`[` `]` CPU speed, `Esc` back to menu (`00FD` also exits).

**8080 / Space Invaders:** arrows or `a`/`d` = move, `Space`/`w` = fire,
`1` = 1P start, `2` = 2P start, `c`/`5` = coin, `r` = mirror video,
`[` `]` speed, `Esc` back to menu.

## Self-test

The built-in self-test deterministically verifies **both** CPU cores at
opcode level: CHIP-8 exercise + keyboard ROMs, SCHIP modes/scrolls, and an
8080 diagnostic program + IRQ + shifter test. Every test ROM is embedded, so
no filesystem is needed — the same suite also runs under the `mtcc` host
harness (interp32) in CI. Exit status = failure count.

Run it from the menu, or directly:

```
emu8bit selftest
```

Any ROM path can also be passed directly to skip the menu:

```
emu8bit /games/invaders.bin
```

## Files

| File | Purpose |
|---|---|
| `emu8bit.c` | the entire emulator (single file, mtcc dialect) |
| `build.ruf` | eggkg build recipe (.ruf format v3) |
| `bounce.c8` | CHIP-8 demo ROM (built-in menu entry) |

## Requirements

- Equinox OS **0.4 Beta** (kernel v0.9.3): ring-3 syscalls 30-46, `key_event`,
  8 MB MRP arena.
- `mtcc` (ships with the OS) — no additional libc entries are required;
  `#include <fileio.h>` splices the fileio prelude automatically for the menu.

## Credits

CHIP-8 core descends from the Eggkg-l `emu-ch8` package; `emu8bit` extends it
with SUPER-CHIP, the 8080 core, the Space Invaders machine, the ROM menu and
the self-test.
