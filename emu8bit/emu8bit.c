/* emu8bit.c — emu8bit: 8-bit machine emulator for Equinox OS (mtcc).
 *
 * The successor of emu-ch8: ONE program, THREE 8-bit machines, a ROM
 * menu loader, and a built-in self-test. Runs as a ring-3 .mrp program
 * on top of the int 0x80 syscall ABI (v0.4 Beta).
 *
 *   Machines:
 *     CHIP-8 / SUPER-CHIP   4 KiB RAM, 64x32 (auto-upgrades to 128x64
 *                           when the ROM issues 00FF). Full COSMAC VIP
 *                           set + SCHIP extensions: 00CN/00FB/00FC
 *                           scrolls, 00FE/00FF mode switch, DXY0 16x16
 *                           sprites, FX75/FX85 HP flags.
 *     Intel 8080            32 KiB RAM, 2 MHz, full documented opcode
 *                           set with exact S/Z/AC/P/CY semantics
 *                           (incl. the 8080 ANA aux-carry quirk),
 *                           EI/DI + RST interrupt injection, and the
 *                           classic "Space Invaders" machine: 8-bit
 *                           shifter on ports 2/3/4, IN 1/2 inputs,
 *                           OUT 3/5 sounds, 60 Hz dual IRQs
 *                           (RST 1 mid-frame + RST 2 vblank) and the
 *                           rotated 1bpp 224x256 framebuffer with
 *                           MAME-style color overlay.
 *
 *   Formats: .ch8 .c8 .sc8  -> CHIP-8/SCHIP (00FF switches to hires by
 *                             itself, no sniffing needed)
 *            .bin .rom      -> <= 3584 bytes = CHIP-8, otherwise the
 *                             8080 machine (Space Invaders ROM set)
 *            any other      -> size-based fallback, overridable per
 *                             entry in the menu with T
 *
 *   Menu: scans "." /games /roms and the eggkg package dirs, plus two
 *   built-ins (bounce demo + full self-test). Keys: arrows/wasd or
 *   j/k move, Enter/Space run, T cycles the type override, Esc quits.
 *
 *   Self-test: deterministic opcode-level verification of BOTH cores
 *   (CHIP-8 exercise + keyboard ROMs, SCHIP modes/scrolls, an 8080
 *   diagnostic program + IRQ + shifter test). Needs no filesystem —
 *   every test ROM is embedded — so it also runs under the mtcc host
 *   harness (interp32) in CI. Exit status = failure count.
 *
 * Controls (in game):
 *   CHIP-8 : keypad-mapped 1234/qwer/asdf/zxcv, Space pause,
 *            [ ] CPU speed, Esc back to menu (00FD also exits).
 *   8080/SI: arrows or a/d = move, Space/w = fire, 1 = 1P start,
 *            2 = 2P start, c/5 = coin, r = mirror video, [ ] speed,
 *            Esc back to menu.
 *
 * Written in the mtcc dialect (no struct/typedef/switch/sizeof/unsigned/
 * function pointers, no libc includes except <fileio.h> for the menu),
 * so it builds with a stock in-OS mtcc and with the host harness.
 *
 * Credits: CHIP-8 core descends from the Eggkg-l emu-ch8 package;
 * emu8bit extends it with SUPER-CHIP, the 8080 core, the SI machine,
 * the menu and the self-test.
 */

#include <fileio.h>

#define 0  0
#define 1    1
#define 3  3

/* ---- CHIP-8 / SCHIP ---- */
#define C8_ROM_BASE 0x200
#define C8_MEM_LAST 4095

/* ---- Intel 8080 / Space Invaders ---- */
#define SI_RAM_LAST 32767
#define SI_ROM_MAX  8192
#define SI_VRAM     0x2400
#define 33333  33333
#define 17066   17066
#define 16267   16267

/* ---- menu ---- */
#define MENU_MAX   64
#define MENU_NAME  40
#define MENU_PATH  96

/* ---- screen / shared gfx ---- */
int fbi[6];
int SW, SH;

/* ---- 5x7 HUD font (bit 0 = top row, one byte per column) ---- */
char FCHARS[44] = " !-./:?0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int HFONT[215] = {
    0x00, 0x00, 0x00, 0x00, 0x00,   /* ' ' */
    0x00, 0x00, 0x5F, 0x00, 0x00,   /* '!' */
    0x08, 0x08, 0x08, 0x08, 0x08,   /* '-' */
    0x00, 0x60, 0x60, 0x00, 0x00,   /* '.' */
    0x20, 0x10, 0x08, 0x04, 0x02,   /* '/' */
    0x00, 0x36, 0x36, 0x00, 0x00,   /* ':' */
    0x02, 0x01, 0x51, 0x09, 0x06,   /* '?' */
    0x3E, 0x51, 0x49, 0x45, 0x3E,   /* '0' */
    0x00, 0x42, 0x7F, 0x40, 0x00,   /* '1' */
    0x42, 0x61, 0x51, 0x49, 0x46,   /* '2' */
    0x21, 0x41, 0x45, 0x4B, 0x31,   /* '3' */
    0x18, 0x14, 0x12, 0x7F, 0x10,   /* '4' */
    0x27, 0x45, 0x45, 0x45, 0x39,   /* '5' */
    0x3C, 0x4A, 0x49, 0x49, 0x30,   /* '6' */
    0x01, 0x71, 0x09, 0x05, 0x03,   /* '7' */
    0x36, 0x49, 0x49, 0x49, 0x36,   /* '8' */
    0x06, 0x49, 0x49, 0x29, 0x1E,   /* '9' */
    0x7E, 0x11, 0x11, 0x11, 0x7E,   /* 'A' */
    0x7F, 0x49, 0x49, 0x49, 0x36,   /* 'B' */
    0x3E, 0x41, 0x41, 0x41, 0x22,   /* 'C' */
    0x7F, 0x41, 0x41, 0x22, 0x1C,   /* 'D' */
    0x7F, 0x49, 0x49, 0x49, 0x41,   /* 'E' */
    0x7F, 0x09, 0x09, 0x09, 0x01,   /* 'F' */
    0x3E, 0x41, 0x49, 0x49, 0x7A,   /* 'G' */
    0x7F, 0x08, 0x08, 0x08, 0x7F,   /* 'H' */
    0x00, 0x41, 0x7F, 0x41, 0x00,   /* 'I' */
    0x20, 0x40, 0x41, 0x3F, 0x01,   /* 'J' */
    0x7F, 0x08, 0x14, 0x22, 0x41,   /* 'K' */
    0x7F, 0x40, 0x40, 0x40, 0x40,   /* 'L' */
    0x7F, 0x02, 0x0C, 0x02, 0x7F,   /* 'M' */
    0x7F, 0x04, 0x08, 0x10, 0x7F,   /* 'N' */
    0x3E, 0x41, 0x41, 0x41, 0x3E,   /* 'O' */
    0x7F, 0x09, 0x09, 0x09, 0x06,   /* 'P' */
    0x3E, 0x41, 0x51, 0x21, 0x5E,   /* 'Q' */
    0x7F, 0x09, 0x16, 0x22, 0x41,   /* 'R' */
    0x46, 0x49, 0x49, 0x49, 0x31,   /* 'S' */
    0x01, 0x01, 0x7F, 0x01, 0x01,   /* 'T' */
    0x3F, 0x40, 0x40, 0x40, 0x3F,   /* 'U' */
    0x1F, 0x20, 0x40, 0x20, 0x1F,   /* 'V' */
    0x3F, 0x40, 0x38, 0x40, 0x3F,   /* 'W' */
    0x63, 0x14, 0x08, 0x14, 0x63,   /* 'X' */
    0x07, 0x08, 0x70, 0x08, 0x07,   /* 'Y' */
    0x61, 0x51, 0x49, 0x45, 0x43    /* 'Z' */
};

/* ---- shared gfx helpers ---- */

void rect(int x, int y, int w, int h, int c) {
    fill_rect(x | (w << 16), y | (h << 16), c);
}

int font_idx(char c) {
    int i;
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    i = 0;
    while (FCHARS[i]) {
        if (FCHARS[i] == c) return i;
        i++;
    }
    return 0;
}

void draw_char(int x, int y, char c, int scale, int color) {
    int g = font_idx(c) * 5;
    int col, row, bits;
    if (scale < 1) scale = 1;
    for (col = 0; col < 5; col++) {
        bits = HFONT[g + col];
        for (row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                if (scale == 1) put_pixel(x + col, y + row, color);
                else rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

int draw_text(int x, int y, char* s, int scale, int color) {
    int cx = x;
    while (*s) {
        draw_char(cx, y, *s, scale, color);
        cx += 6 * scale;
        s++;
    }
    return cx;
}

int text_uint(int x, int y, int v, int scale, int color) {
    char buf[12];
    int i = 11;
    if (v < 0) v = 0;
    buf[11] = 0;
    do {
        i--;
        buf[i] = '0' + v % 10;
        v = v / 10;
    } while (v > 0);
    return draw_text(x, y, buf + i, scale, color);
}

/* ---- tiny string helpers ---- */

int str_len(char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void str_copy(char* d, char* s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

int str_eq_ci(char* a, char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = x - 'A' + 'a';
        if (y >= 'A' && y <= 'Z') y = y - 'A' + 'a';
        if (x != y) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

int has_ext(char* name, char* ext) {
    int n = str_len(name);
    int e = str_len(ext);
    if (n <= e || e == 0) return 0;
    if (name[n - e - 1] != '.') return 0;
    return str_eq_ci(name + n - e, ext);
}

void base_name(char* path, char* out, int max) {
    int n = str_len(path);
    int start = n;
    int i = 0;
    while (start > 0 && path[start - 1] != '/') start--;
    while (start + i < n && i < max - 1) { out[i] = path[start + i]; i++; }
    out[i] = 0;
}

/* ---- graphics init (full screen query) ---- */

int gfx_init() {
    if (fb_info(fbi) != 0) return -1;
    if (fbi[5] == 0 || fbi[3] != 32) return -1;
    SW = fbi[1];
    SH = fbi[2];
    return 0;
}

/* ============================================================================
 *  PART 2 — CHIP-8 / SUPER-CHIP core
 * ============================================================================
 *  Descends from emu-ch8 (Eggkg-l). Extensions in emu8bit:
 *    - SUPER-CHIP: 00FE/00FF mode switch (64x32 <-> 128x64), 00CN scroll
 *      down, 00FB/00FC scroll right/left, DXY0 16x16 sprites, FX75/FX85
 *      HP flags. The 10x10 SCHIP font is NOT embedded — FX30 falls back
 *      to the 4x5 font (documented limitation, keeps the .mrp small).
 *    - HARD ROM limit: a ROM larger than 3584 bytes is rejected with a
 *      clear message (emu-ch8 silently truncated).
 *    - The core is callable without any syscall: step8()/pump8() are
 *      pure, which is what the built-in self-test drives.
 * ==========================================================================*/

char mem8[4096];
int V8[16];
int R8HP[8];                  /* SUPER-CHIP HP flags (FX75/FX85) */
int stk8[16];
char keys8[16];
int ips8, insns8, nf8, unk8, romlen8, rnd8, snd_on8, hud_sig8;
int paused8, wait_reg8, quit_esc8;
int si_prev_valid, si_running, si_quit80, si_mirror, si_coin;
int si_ox, si_oy, si_sc, si_pct;
char* screen8;                /* arena-allocated (8192 > mtcc array limit) */
char* drawn8;
char romname8[48];

/* ----------------------------------------------------------------------------
 *  CHIP-8 / SI / menu / selftest scalar state lives in ONE 51-int block;
 *  every function that touches it caches `int* q = c8blk;` locally (the
 *  #defines keep names readable; accesses become ebp-relative, which
 *  spares both the 128-global and the 2048-fixup compiler budgets).
 *  ------------------------------------------------------------------------- */
int c8blk[51];

#define pc8        q[0]
#define I8         q[1]
#define sp8        q[2]
#define dt8        q[3]
#define st8        q[4]
#define run8_on    q[11]
#define wait_key8  q[13]
#define CW8        q[24]
#define CH8        q[25]
#define sc8        q[26]
#define ox8        q[27]
#define oy8        q[28]

/* ---- CHIP-8 4x5 hex font (glyph '0'..'F' at 0x50) — identical to emu-ch8 */
int CFONT[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,   0x20, 0x60, 0x20, 0x20, 0x70,
    0xF0, 0x10, 0xF0, 0x80, 0xF0,   0xF0, 0x10, 0xF0, 0x10, 0xF0,
    0x90, 0x90, 0xF0, 0x10, 0x10,   0xF0, 0x80, 0xF0, 0x10, 0xF0,
    0xF0, 0x80, 0xF0, 0x90, 0xF0,   0xF0, 0x10, 0x20, 0x40, 0x40,
    0xF0, 0x90, 0xF0, 0x90, 0xF0,   0xF0, 0x90, 0xF0, 0x10, 0xF0,
    0xF0, 0x90, 0xF0, 0x90, 0x90,   0xE0, 0x90, 0xE0, 0x90, 0xE0,
    0xF0, 0x80, 0x80, 0x80, 0xF0,   0xE0, 0x90, 0x90, 0x90, 0xE0,
    0xF0, 0x80, 0xF0, 0x80, 0xF0,   0xF0, 0x80, 0xF0, 0x80, 0x80
};

/* physical keypad layout (CHIP-8 key under each HUD mirror cell) */
int KPAD[16] = {
    1, 2, 3, 12,    4, 5, 6, 13,
    7, 8, 9, 14,    10, 0, 11, 15
};

/* built-in demo ROM (hand-assembled, 31 words = 62 bytes) — the same
 * bouncing-smiley ROM that ships with emu-ch8 (bounce.c8). */
int DEMO8[31] = {
    0x00E0, 0x6008, 0x6106, 0x6201, 0x6301, 0x6600, 0x6401, 0xA238,
    0xD015, 0xF415, 0xF507, 0x3500, 0x1214, 0xD015, 0x4000, 0x6201,
    0x4038, 0x62FF, 0x4100, 0x6301, 0x411B, 0x63FF, 0x8024, 0x8134,
    0x7601, 0x36B4, 0x120C, 0x00FD, 0x7E81, 0xA581, 0x7E00
};

/* ---- memory (always masked: a ROM may point I/pc anywhere) ---- */

int r8(int a) { return mem8[a & C8_MEM_LAST]; }
void w8(int a, int v) { mem8[a & C8_MEM_LAST] = v & 255; }

void cls8() {
    int* q = c8blk;
    char* scr = screen8;
    int i;
    for (i = 0; i < 8192; i++) scr[i] = 0;
}

/* ---- canvas: integer scale + origin for the CURRENT mode ---- */

void setup_canvas8() {
    int* q = c8blk;
    int cw, ch;
    int w, h;
    cw = CW8;
    ch = CH8;
    w = cw;
    h = ch;
    sc8 = (SH - 30 - 8) / h;
    if ((SW - 16) / w < sc8) sc8 = (SW - 16) / w;
    if (sc8 > 24) sc8 = 24;
    if (sc8 < 2) sc8 = 2;
    ox8 = (SW - w * sc8) / 2;
    oy8 = 30 + (SH - 30 - h * sc8) / 2;
    rect(0, 0, SW, SH, 0x000b0d16);
    rect(0, 0, SW, 28, 0x0016203a);
    rect(0, 26, SW, 2, 0x0033405e);
    rect(ox8 - 3, oy8 - 3, w * sc8 + 6, h * sc8 + 6, 0x0033405e);
    rect(ox8, oy8, w * sc8, h * sc8, 0x000b0d16);
}

/* ---- SUPER-CHIP mode switch: clears the display, rescales ---- */

void set_mode8(int hires) {
    int* q = c8blk;
    int i;
    if (hires) { CW8 = 128; CH8 = 64; }
    else       { CW8 = 64;  CH8 = 32; }
    cls8();
    for (i = 0; i < 8192; i++) drawn8[i] = 2;   /* force repaint */
    setup_canvas8();
}

/* ---- DXYN / DXY0: XOR sprite draw (wraps, VF = collision) ---- */

void spr8(int x, int y, int n) {
    int* q = c8blk;
    int* v = V8;
    int cw, ch;
    int x0, y0, i, j, b, xx, yy, idx, wbytes;
    cw = CW8;
    ch = CH8;
    x0 = v[x] % cw;
    y0 = v[y] % ch;
    v[15] = 0;
    if (n == 0) {
        /* SUPER-CHIP 16x16 sprite (hires): 32 bytes at I */
        for (j = 0; j < 16; j++) {
            for (wbytes = 0; wbytes < 2; wbytes++) {
                b = r8(I8 + j * 2 + wbytes);
                for (i = 0; i < 8; i++) {
                    if (b & (0x80 >> i)) {
                        xx = x0 + wbytes * 8 + i; if (xx >= cw) xx = xx - cw;
                        yy = y0 + j; if (yy >= ch) yy = yy - ch;
                        idx = yy * cw + xx;
                        if (screen8[idx]) v[15] = 1;
                        screen8[idx] = screen8[idx] ? 0 : 1;
                    }
                }
            }
        }
        return;
    }
    for (j = 0; j < n; j++) {
        b = r8(I8 + j);
        for (i = 0; i < 8; i++) {
            if (b & (0x80 >> i)) {
                xx = x0 + i; if (xx >= cw) xx = xx - cw;
                yy = y0 + j; if (yy >= ch) yy = yy - ch;
                idx = yy * cw + xx;
                if (screen8[idx]) v[15] = 1;
                screen8[idx] = screen8[idx] ? 0 : 1;
            }
        }
    }
}

/* ---- RNG: xorshift32 (never zero) ---- */

void rng_next8() {
    int* q = c8blk;
    int x;
    x = rnd8;
    x = x ^ (x << 13);
    x = x ^ (x >> 17);
    x = x ^ (x << 5);
    rnd8 = x;
}

/* ---- 00CN / 00FB / 00FC: SUPER-CHIP scrolls ---- */

void scroll_down8(int n) {
    int* q = c8blk;
    int x, y;
    if (n < 1) return;
    if (n > CH8) n = CH8;
    for (y = CH8 - 1; y >= n; y--)
        for (x = 0; x < CW8; x++)
            screen8[y * CW8 + x] = screen8[(y - n) * CW8 + x];
    for (y = 0; y < n; y++)
        for (x = 0; x < CW8; x++) screen8[y * CW8 + x] = 0;
}

void scroll_side8(int right) {
    int* q = c8blk;
    int x, y, s;
    for (y = 0; y < CH8; y++) {
        if (right) {
            for (x = CW8 - 1; x >= 4; x--)
                screen8[y * CW8 + x] = screen8[y * CW8 + x - 4];
            for (x = 0; x < 4; x++) screen8[y * CW8 + x] = 0;
        } else {
            for (x = 0; x < CW8 - 4; x++)
                screen8[y * CW8 + x] = screen8[y * CW8 + x + 4];
            for (x = CW8 - 4; x < CW8; x++) screen8[y * CW8 + x] = 0;
        }
    }
}

/* ---- one instruction (no switch in mtcc: a test ladder per opcode) ---- */

void step8() {
    int* q = c8blk;
    int* v = V8;
    int hi, j, s, f;
    int opc, rx, ry, rn, rkk, rnnn;

    if (wait_key8) return;         /* FX0A pending: CPU halted, timers run */

    opc = (r8(pc8) << 8) | r8(pc8 + 1);
    pc8 = pc8 + 2;
    rx = (opc >> 8) & 15;
    ry = (opc >> 4) & 15;
    rn = opc & 15;
    rkk = opc & 255;
    rnnn = opc & 4095;
    insns8++;

    hi = opc >> 12;

    if (hi == 0) {
        if (opc == 0x00E0) cls8();
        else if (opc == 0x00EE) { if (sp8 > 0) { sp8--; pc8 = stk8[sp8]; } }
        else if (opc == 0x00FD) run8_on = 0;      /* exit interpreter */
        else if (opc == 0x00FF) set_mode8(1);     /* SCHIP hires      */
        else if (opc == 0x00FE) set_mode8(0);     /* SCHIP lores      */
        else if ((opc & 0xF0F0) == 0x00C0) scroll_down8(opc & 15);
        else if (opc == 0x00FC) scroll_side8(0);
        else if (opc == 0x00FB) scroll_side8(1);
        else if (opc != 0) unk8++;                /* unknown 0xxx     */
        return;
    }
    if (hi == 1) { pc8 = rnnn; return; }                          /* JP    */
    if (hi == 2) {                                                 /* CALL  */
        if (sp8 >= 16) { print("emu8bit: stack overflow\n"); run8_on = 0; return; }
        stk8[sp8] = pc8; sp8++; pc8 = rnnn; return;
    }
    if (hi == 3) { if (v[rx] == rkk) pc8 += 2; return; }        /* SE    */
    if (hi == 4) { if (v[rx] != rkk) pc8 += 2; return; }        /* SNE   */
    if (hi == 5) { if (rn == 0 && v[rx] == v[ry]) pc8 += 2; return; }
    if (hi == 6) { v[rx] = rkk; return; }                       /* LD    */
    if (hi == 7) { v[rx] = (v[rx] + rkk) & 255; return; }     /* ADD   */
    if (hi == 9) { if (rn == 0 && v[rx] != v[ry]) pc8 += 2; return; }
    if (hi == 10) { I8 = rnnn; return; }                          /* LD I  */
    if (hi == 11) { pc8 = (rnnn + v[0]) & C8_MEM_LAST; return; } /* JP V0 */
    if (hi == 12) { rng_next8(); v[rx] = rnd8 & rkk; return; }  /* RND   */
    if (hi == 13) { spr8(rx, ry, rn); return; }                              /* DRW   */

    if (hi == 8) {
        if (rn == 0) v[rx] = v[ry];
        else if (rn == 1) v[rx] = v[rx] | v[ry];
        else if (rn == 2) v[rx] = v[rx] & v[ry];
        else if (rn == 3) v[rx] = v[rx] ^ v[ry];
        else if (rn == 4) {
            s = v[rx] + v[ry];
            f = (s > 255) ? 1 : 0;
            v[rx] = s & 255;
            v[15] = f;
        }
        else if (rn == 5) {
            f = (v[rx] >= v[ry]) ? 1 : 0;
            v[rx] = (v[rx] - v[ry]) & 255;
            v[15] = f;
        }
        else if (rn == 6) {
            f = v[rx] & 1;
            v[rx] = (v[rx] >> 1) & 255;
            v[15] = f;
        }
        else if (rn == 7) {
            f = (v[ry] >= v[rx]) ? 1 : 0;
            v[rx] = (v[ry] - v[rx]) & 255;
            v[15] = f;
        }
        else if (rn == 14) {
            f = (v[rx] >> 7) & 1;
            v[rx] = (v[rx] << 1) & 255;
            v[15] = f;
        }
        else unk8++;
        return;
    }

    if (hi == 14) {
        if (rn == 14) { if (keys8[v[rx] & 15]) pc8 += 2; return; }
        if (rn == 1)  { if (!keys8[v[rx] & 15]) pc8 += 2; return; }
        unk8++;
        return;
    }

    if (hi == 15) {
        if (rkk == 0x07) v[rx] = dt8;
        else if (rkk == 0x0A) { wait_reg8 = rx; wait_key8 = 1; }
        else if (rkk == 0x15) dt8 = v[rx];
        else if (rkk == 0x18) st8 = v[rx];
        else if (rkk == 0x1E) I8 = (I8 + v[rx]) & C8_MEM_LAST;
        else if (rkk == 0x29) I8 = 0x50 + (v[rx] & 15) * 5;
        else if (rkk == 0x30) I8 = 0x50 + (v[rx] & 15) * 5; /* SCHIP 10x10 font: 4x5 fallback (documented) */
        else if (rkk == 0x33) {
            s = v[rx];
            w8(I8, s / 100);
            w8(I8 + 1, (s / 10) % 10);
            w8(I8 + 2, s % 10);
        }
        else if (rkk == 0x55) { for (j = 0; j <= rx; j++) w8(I8 + j, v[j]); }
        else if (rkk == 0x65) { for (j = 0; j <= rx; j++) v[j] = r8(I8 + j); }
        else if (rkk == 0x75) { for (j = 0; j <= rx && j < 8; j++) R8HP[j] = v[j]; }
        else if (rkk == 0x85) { for (j = 0; j <= rx && j < 8; j++) v[j] = R8HP[j]; }
        else unk8++;
        return;
    }

    unk8++;
}

/* ---- input: key_event() press/release -> 16-key state table ---- */

int key_index8(int code) {
    if (code >= 'A' && code <= 'Z') code = code - 'A' + 'a';
    if (code == '1') return 1;
    if (code == '2') return 2;
    if (code == '3') return 3;
    if (code == '4') return 12;
    if (code == 'q') return 4;
    if (code == 'w') return 5;
    if (code == 'e') return 6;
    if (code == 'r') return 13;
    if (code == 'a') return 7;
    if (code == 's') return 8;
    if (code == 'd') return 9;
    if (code == 'f') return 14;
    if (code == 'z') return 10;
    if (code == 'x') return 0;
    if (code == 'c') return 11;
    if (code == 'v') return 15;
    return -1;
}

/* drain the event queue; returns 1 when Esc was pressed (back to menu) */
int poll_input8() {
    int* q = c8blk;
    int ev, code, press, idx, i;
    for (i = 0; i < 64; i++) {
        ev = key_event();
        if (ev == 0) break;
        press = (ev >> 16) & 1;
        code = (ev << 16) >> 16;
        if (code == 27) { if (press) { run8_on = 0; quit_esc8 = 1; } continue; }
        if (code == ' ') {
            if (press) {
                paused8 = paused8 ? 0 : 1;
                for (idx = 0; idx < 8192; idx++) drawn8[idx] = 2;
            }
            continue;
        }
        if (code == '[') { if (press && ips8 > 200) ips8 -= 100; continue; }
        if (code == ']') { if (press && ips8 < 3000) ips8 += 100; continue; }
        idx = key_index8(code);
        if (idx >= 0) {
            keys8[idx] = press ? 1 : 0;
            if (press && wait_key8) { V8[wait_reg8] = idx; wait_key8 = 0; }
        }
    }
    return quit_esc8;
}

/* ---- render: blit ONLY the cells that changed ---- */

void render8() {
    int* q = c8blk;
    char* scr = screen8;
    char* dr = drawn8;
    int cw = CW8;
    int i, on, row, col;
    for (i = 0; i < cw * CH8; i++) {
        on = scr[i] ? 1 : 0;
        if (dr[i] == on) continue;
        dr[i] = on;
        row = i / cw;
        col = i - row * cw;
        rect(ox8 + col * sc8, oy8 + row * sc8, sc8, sc8, on ? 0x00e8f0ff : 0x000b0d16);
    }
}

void sound_sync8() {
    int* q = c8blk;
    if (st8 > 0) {
        if (!snd_on8) { spk_tone(440); snd_on8 = 1; }
    } else if (snd_on8) {
        spk_silence();
        snd_on8 = 0;
    }
}

/* ---- HUD: title + mode + ROM + speed + a live 4x4 keypad mirror ---- */

int hud_state8() {
    int* q = c8blk;
    int s = 0, i;
    for (i = 0; i < 16; i++) if (keys8[i]) s |= (1 << i);
    s |= ips8 << 16;
    if (paused8) s |= 0x10000000;
    if (CW8 == 128) s |= 0x20000000;
    return s;
}

void draw_pad8() {
    int* q = c8blk;
    int p, k;
    for (p = 0; p < 16; p++) {
        k = KPAD[p];
        rect(SW - 34 + (p % 4) * 6, 3 + (p / 4) * 6, 5, 5,
             keys8[k] ? 0x0033dd66 : 0x0033405e);
    }
}

void draw_hud8() {
    int* q = c8blk;
    char nbuf[44];
    int i, lim, x;
    draw_text(10, 8, "EMU8BIT", 2, 0x00e8f0ff);
    x = draw_text(80, 11, CW8 == 128 ? "SCHIP" : "CHIP8", 1, 0x00ffcc44);
    lim = (SW - 380) / 6;
    if (lim > 42) lim = 42;
    if (lim > 0) {
        i = 0;
        while (romname8[i] && i < lim) { nbuf[i] = romname8[i]; i++; }
        nbuf[i] = 0;
        draw_text(x + 14, 11, nbuf, 1, 0x00e8f0ff);
    }
    x = text_uint(SW - 250, 11, ips8, 1, 0x00e8f0ff);
    draw_text(x + 3, 11, "HZ", 1, 0x0033405e);
    draw_text(SW - 210, 11, "ESC MENU SPACE PAUSE [ ] SPD", 1, 0x0033405e);
    draw_pad8();
}

void draw_pause8() {
    int* q = c8blk;
    int w = 6 * 6 * 3;
    int x = ox8 + (CW8 * sc8 - w) / 2;
    int y = oy8 + (CH8 * sc8 - 21) / 2;
    rect(x - 10, y - 8, w + 20, 37, 0x000b0d16);
    draw_text(x, y, "PAUSED", 3, 0x00ffcc44);
}

/* ---- machine reset + ROM loading ---- */

void reset8() {
    int* q = c8blk;
    char* scr = screen8;
    char* dr = drawn8;
    int i;
    for (i = 0; i < 4096; i++) mem8[i] = 0;
    for (i = 0; i < 16; i++) { V8[i] = 0; stk8[i] = 0; keys8[i] = 0; }
    for (i = 0; i < 8; i++) R8HP[i] = 0;
    for (i = 0; i < 80; i++) mem8[0x50 + i] = CFONT[i];
    CW8 = 64; CH8 = 32;
    cls8();
    for (i = 0; i < 8192; i++) dr[i] = 2;
    pc8 = C8_ROM_BASE; I8 = 0; sp8 = 0; dt8 = 0; st8 = 0;
    wait_key8 = 0; wait_reg8 = 0; paused8 = 0; snd_on8 = 0; quit_esc8 = 0;
    run8_on = 1; insns8 = 0; nf8 = 0; unk8 = 0; hud_sig8 = -1;
}

void load_demo8() {
    int* q = c8blk;
    int i;
    for (i = 0; i < 31; i++) {
        w8(C8_ROM_BASE + i * 2, DEMO8[i] >> 8);
        w8(C8_ROM_BASE + i * 2 + 1, DEMO8[i] & 255);
    }
    romlen8 = 62;
}

/* returns byte count, or a negative error. HARD limit: > 3584 rejected. */
int load_rom8(char* path) {
    int* q = c8blk;
    int sz, n;
    if (file_exists(path) == 0) return -1;
    sz = file_size(path);
    if (sz <= 0) return -2;
    if (sz > 3584) {
        print("emu8bit: ROM too large for CHIP-8 (");
        printint(sz);
        print(" > 3584 byte) — use an 8080 machine ROM or T-override\n");
        return -3;
    }
    n = file_read_all(path, mem8 + C8_ROM_BASE, sz);
    if (n < 0) return -4;
    romlen8 = n;
    return n;
}

void set_rom_name8(char* path) {
    base_name(path, romname8, 48);
}

/* pump n frames of pure core (no I/O) — self-test driver */
void pump8(int n) {
    int* q = c8blk;
    int f, cyc, i;
    for (f = 0; f < n && run8_on; f++) {
        cyc = ips8 / 60;
        for (i = 0; i < cyc && run8_on; i++) step8();
        if (dt8 > 0) dt8--;
        if (st8 > 0) st8--;
        nf8++;
    }
}

/* ---- the CHIP-8/SCHIP run loop (graphics + input path) ---- */

void run8() {
    int* q = c8blk;
    int now, last, acc, fr, cyc, i;

    if (gfx_init() != 0) {
        print("emu8bit: need a VESA 32bpp screen\n");
        return;
    }
    setup_canvas8();
    draw_hud8();
    print("emu8bit: ESC=menu SPACE=pause [ ]=speed  keypad 1234/qwer/asdf/zxcv\n");

    last = gettick();
    acc = 0;
    while (run8_on) {
        if (poll_input8()) break;

        now = gettick();
        acc = acc + (now - last) * 10;
        if (acc < 0) acc = 0;
        last = now;

        if (acc >= 16) {
            fr = acc / 16;
            if (fr > 6) fr = 6;
            acc = acc - fr * 16;
            if (acc > 16 * 6) acc = 0;

            if (!paused8) {
                if (!wait_key8) {
                    cyc = (fr * ips8) / 60;
                    if (cyc > 900) cyc = 900;
                    for (i = 0; i < cyc && run8_on; i++) step8();
                }
                for (i = 0; i < fr; i++) {
                    if (dt8 > 0) dt8--;
                    if (st8 > 0) st8--;
                }
            }
            nf8 = nf8 + fr;

            sound_sync8();
            render8();
            if (hud_state8() != hud_sig8) { hud_sig8 = hud_state8(); draw_hud8(); }
            if (paused8) draw_pause8();
        }
    }

    spk_silence();
    snd_on8 = 0;
    rect(0, 0, SW, SH, 0x000b0d16);
    print("emu8bit: chip8 done - frames ");
    printint(nf8);
    print("  insns ");
    printint(insns8);
    print("  unknown ");
    printint(unk8);
    print("\n");
}

/* ============================================================================
 *  PART 3 — Intel 8080 CPU core
 * ============================================================================
 *  Full documented opcode set (244 opcodes) with exact 8080 flag
 *  semantics: S, Z, AC, P, CY — including the 8080-specific ANA
 *  aux-carry quirk (AC = OR of bit 3 of the operands, unlike the 8085).
 *  Undocumented opcodes (08 10 18 20 28 30 38 CB D9 DD ED FD; D9 behaves
 *  as RET) execute as NOP and bump the unknown counter. T-states come
 *  from CYC80 (2 MHz reference). Interrupts: EI/DI + irq80(vec) injects
 *  an RST-style vector (Space Invaders uses RST 1 / RST 2).
 *  The I/O hooks (si_in80/si_out80) implement the Space Invaders port
 *  hardware: the famous 8-bit shifter (OUT 4 data, OUT 2 offset,
 *  IN 3 result), IN 1/2 inputs and OUT 3/5 sounds. A trace mode records
 *  OUT ports for the built-in self-test.
 *  LADDER ORDER NOTE: low-3-bit groups collide inside 0xC0-0xFF
 *  (0xCD CALL &7=5 vs PUSH, 0xE3/0xEB/0xE9/0xF9/0xF3/0xFB, 0xD3/0xDB vs
 *  0xC3), so every two-byte opcode with a colliding pattern is checked
 *  explicitly before the group tests. Verified against the 8080 map.
 * ==========================================================================*/

char* mem80;                  /* arena-allocated 32 KiB (mtcc array cap 4096) */
char* prev80;                 /* SI previous-frame VRAM copy (7168) */
int gA, gB, gC, gD, gE, gH, gL, gSP, gPC;
int fS80, fZ80, fAC80, fP80, fC80;
int PAR80[256];
int CYC80[256];

/* ---- SI port hardware + trace ---- */


int rb80(int a) { return mem80[a & SI_RAM_LAST] & 255; }
void wb80(int a, int v) { mem80[a & SI_RAM_LAST] = v & 255; }

/* one-time arena allocation for every big buffer (called from main
 * before anything else; the MRP arena is 8 MB by default) */
void emu_alloc() {
    int i;
    screen8 = malloc(8192);
    drawn8 = malloc(8192);
    mem80 = malloc(32768);
    prev80 = malloc(7168);
    if (screen8 == 0 || drawn8 == 0 || mem80 == 0 || prev80 == 0) {
        print("emu8bit: out of arena memory\n");
        exit(1);
    }
    for (i = 0; i < 8192; i++) { screen8[i] = 0; drawn8[i] = 0; }
    for (i = 0; i < 32768; i++) mem80[i] = 0;
    for (i = 0; i < 7168; i++) prev80[i] = 0;
}

int rw80(int a) { return rb80(a) | (rb80(a + 1) << 8); }
void ww80(int a, int v) { wb80(a, v & 255); wb80(a + 1, (v >> 8) & 255); }

/* parity table: 1 = even number of set bits (8080 PF = 1 when even) */
void build_par80() {
    int i, j, b, p;
    for (i = 0; i < 256; i++) {
        b = i;
        p = 1;
        for (j = 0; j < 8; j++) { if (b & 1) p = p ? 0 : 1; b = b >> 1; }
        PAR80[i] = p;
    }
}

/* T-state table (2 MHz; undocumented = 4; conditional RET uses 10).
 * Stored as DATA (a literal array costs zero fixups; one assignment
 * per opcode would burn ~120 of the compiler's 2048-fixup budget). */
int CYC_INIT[256] = {
    4, 10, 7, 5, 5, 5, 7, 4,  4, 10, 7, 5, 5, 5, 7, 4,
    4, 10, 7, 5, 5, 5, 7, 4,  4, 10, 7, 5, 5, 5, 7, 4,
    4, 10, 16, 5, 5, 5, 7, 4,  4, 10, 16, 5, 5, 5, 7, 4,
    4, 10, 13, 5, 10, 10, 10, 4,  4, 10, 13, 5, 5, 5, 7, 4,
    5, 5, 5, 5, 5, 5, 5, 5,  5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,  7, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,  5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,  5, 5, 5, 5, 5, 5, 5, 5,
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,
    10, 10, 10, 10, 17, 11, 7, 11,  10, 10, 10, 4, 17, 17, 7, 11,
    10, 10, 10, 10, 17, 11, 7, 11,  10, 10, 10, 10, 17, 4, 7, 11,
    10, 10, 10, 18, 17, 11, 7, 11,  10, 5, 10, 4, 17, 4, 7, 11,
    10, 10, 10, 4, 17, 11, 7, 11,  10, 5, 10, 4, 17, 4, 7, 11
};

void build_cyc80() {
    int i;
    for (i = 0; i < 256; i++) CYC80[i] = CYC_INIT[i];
}

/* ----------------------------------------------------------------------------
 *  8080 register state lives in ONE 16-int block; every state-touching
 *  function caches `int* r = SB80;` locally. The #defines below keep
 *  the ladder readable (gA, fC80, ...) while ALL accesses become
 *  ebp-relative through the local pointer — a global moffs fixup would
 *  be burned on every access otherwise (mtcc budget: 2048 fixups).
 *  [0]A [1]B [2]C [3]D [4]E [5]H [6]L [7]SP [8]PC
 *  [9]S [10]Z [11]AC [12]P [13]CY [14]INTE [15]HALT
 *  ------------------------------------------------------------------------- */
int SB80[32];
int cpu80_on, cyc80, insns80, unk80;
int shift_sr, shift_off, p1_bits, p2_bits, port_trace80, trace_n;
int si_ufo_on, si_shot_pending, si_flash_pending, si_expl_pending;
int si_hit_pending, si_shield_pending;
int trace_port[32];
int trace_val[32];

#define gA    r[0]
#define gB    r[1]
#define gC    r[2]
#define gD    r[3]
#define gE    r[4]
#define gH    r[5]
#define gL    r[6]
#define gSP   r[7]
#define gPC   r[8]
#define fS80  r[9]
#define fZ80  r[10]
#define fAC80 r[11]
#define fP80  r[12]
#define fC80  r[13]
#define IFF80 r[14]
#define halted80 r[15]

void szp80(int v) {
    int* r = SB80;
    v = v & 255;
    fS80 = (v >> 7) & 1;
    fZ80 = (v == 0) ? 1 : 0;
    fP80 = PAR80[v];
}

/* register-field access (8080 encoding: 0=B 1=C 2=D 3=E 4=H 5=L 6=M 7=A) */

int rg80(int i) {
    int* r = SB80;
    if (i == 0) return gB;
    if (i == 1) return gC;
    if (i == 2) return gD;
    if (i == 3) return gE;
    if (i == 4) return gH;
    if (i == 5) return gL;
    if (i == 6) return rb80((gH << 8) | gL);
    return gA;
}

void wrreg80(int i, int v) {
    int* r = SB80;
    v = v & 255;
    if (i == 0) gB = v;
    else if (i == 1) gC = v;
    else if (i == 2) gD = v;
    else if (i == 3) gE = v;
    else if (i == 4) gH = v;
    else if (i == 5) gL = v;
    else if (i == 6) wb80((gH << 8) | gL, v);
    else gA = v;
}

/* accumulator ops (op: 0 ADD 1 ADC 2 SUB 3 SBB 4 ANA 5 XRA 6 ORA 7 CMP) */

void alu80(int op, int val) {
    int* r = SB80;
    int res, oc;
    val = val & 255;
    if (op == 0) {
        res = gA + val;
        fC80 = (res > 255) ? 1 : 0;
        fAC80 = (((gA & 15) + (val & 15)) > 15) ? 1 : 0;
        gA = res & 255;
        szp80(gA);
    } else if (op == 1) {
        oc = fC80;
        res = gA + val + oc;
        fC80 = (res > 255) ? 1 : 0;
        fAC80 = (((gA & 15) + (val & 15) + oc) > 15) ? 1 : 0;
        gA = res & 255;
        szp80(gA);
    } else if (op == 2) {
        res = gA - val;
        fC80 = (gA < val) ? 1 : 0;
        fAC80 = (((gA & 15) - (val & 15)) < 0) ? 1 : 0;
        gA = res & 255;
        szp80(gA);
    } else if (op == 3) {
        oc = fC80;
        res = gA - val - oc;
        fC80 = (gA < val + oc) ? 1 : 0;
        fAC80 = (((gA & 15) - (val & 15) - oc) < 0) ? 1 : 0;
        gA = res & 255;
        szp80(gA);
    } else if (op == 4) {
        res = gA & val;
        fC80 = 0;
        fAC80 = ((gA | val) & 8) ? 1 : 0;    /* 8080 quirk: AC = OR of bit 3 */
        gA = res;
        szp80(gA);
    } else if (op == 5) {
        gA = gA ^ val;
        fC80 = 0; fAC80 = 0;
        szp80(gA);
    } else if (op == 6) {
        gA = gA | val;
        fC80 = 0; fAC80 = 0;
        szp80(gA);
    } else {
        res = (gA - val) & 255;
        fC80 = (gA < val) ? 1 : 0;
        fAC80 = (((gA & 15) - (val & 15)) < 0) ? 1 : 0;
        szp80(res);                          /* A unchanged */
    }
}

/* condition k: 0 NZ 1 Z 2 NC 3 C 4 PO 5 PE 6 P 7 M */

int cond80(int k) {
    int* r = SB80;
    int res;
    if (k == 0) res = !fZ80;
    else if (k == 1) res = fZ80;
    else if (k == 2) res = !fC80;
    else if (k == 3) res = fC80;
    else if (k == 4) res = !fP80;
    else if (k == 5) res = fP80;
    else if (k == 6) res = !fS80;
    else res = fS80;
    return res ? 1 : 0;
}

void call80(int addr) {
    int* r = SB80;
    wb80(gSP - 1, (gPC >> 8) & 255);
    wb80(gSP - 2, gPC & 255);
    gSP = (gSP - 2) & 0xFFFF;
    gPC = addr & 0xFFFF;
}

void push80(int hi, int lo) {
    int* r = SB80;
    wb80(gSP - 1, hi);
    wb80(gSP - 2, lo);
    gSP = (gSP - 2) & 0xFFFF;
}

int pair80(int rp) {
    int* r = SB80;
    if (rp == 0) return (gB << 8) | gC;
    if (rp == 1) return (gD << 8) | gE;
    if (rp == 2) return (gH << 8) | gL;
    return gSP;
}

void setpair80(int rp, int v) {
    int* r = SB80;
    v = v & 0xFFFF;
    if (rp == 0) { gB = (v >> 8) & 255; gC = v & 255; }
    else if (rp == 1) { gD = (v >> 8) & 255; gE = v & 255; }
    else if (rp == 2) { gH = (v >> 8) & 255; gL = v & 255; }
    else gSP = v;
}

void daa80() {
    int* r = SB80;
    int lo_fix, oldc;
    lo_fix = (fAC80 || (gA & 15) > 9) ? 1 : 0;
    oldc = fC80;
    if (lo_fix) gA = (gA + 6) & 255;
    if (oldc || gA > 0x99) { gA = (gA + 0x60) & 255; fC80 = 1; }
    else fC80 = 0;
    fAC80 = lo_fix;
    szp80(gA);
}

void inr_val80(int i) {
    int* r = SB80;
    int v = rg80(i);
    fAC80 = (((v & 15) + 1) > 15) ? 1 : 0;
    v = (v + 1) & 255;
    wrreg80(i, v);
    szp80(v);
}

void dcr_val80(int i) {
    int* r = SB80;
    int v = rg80(i);
    fAC80 = (((v & 15) - 1) < 0) ? 1 : 0;
    v = (v - 1) & 255;
    wrreg80(i, v);
    szp80(v);
}

/* ---- SI port hardware (used by IN/OUT; trace mode = self-test) ---- */

void si_out80(int port, int v) {
    if (port_trace80 && trace_n < 32) {
        trace_port[trace_n] = port;
        trace_val[trace_n] = v & 255;
        trace_n++;
    }
    if (port == 2) {
        shift_off = v & 7;                     /* shifter offset */
    } else if (port == 3) {
        /* sound bank A: bit0 UFO, bit1 shot, bit2-5 fleet move, bit6 UFO hit */
        if (v & 1) { if (!si_ufo_on) { spk_tone(140); si_ufo_on = 1; } }
        else { if (si_ufo_on) { spk_silence(); si_ufo_on = 0; } }
        if (v & 2) si_shot_pending = 1;
        if (v & 0x0C) si_flash_pending = 1;
        if (v & 0x40) si_expl_pending = 1;
    } else if (port == 4) {
        shift_sr = ((shift_sr >> 8) | ((v & 255) << 8)) & 0xFFFF;
    } else if (port == 5) {
        /* sound bank B: bit0 ship hit, bit1 shield hit, bit2 life lost */
        if (v & 1) si_hit_pending = 1;
        if (v & 2) si_shield_pending = 1;
        if (v & 4) si_shield_pending = 1;
    }
    /* port 6 = watchdog, ignored */
}

int si_in80(int port) {
    if (port == 3) return (shift_sr >> (8 - shift_off)) & 255;
    if (port == 1) return p1_bits;
    if (port == 2) return p2_bits;
    return 0;
}

/* CPU reset (mem80 zeroed here too — cheap and always correct) */

void cpu_reset80() {
    int* r = SB80;
    int i;
    for (i = 0; i < 32768; i++) mem80[i] = 0;
    gA = gB = gC = gD = gE = gH = gL = 0;
    gSP = 0; gPC = 0;
    fS80 = fZ80 = fAC80 = fP80 = fC80 = 0;
    IFF80 = 0; halted80 = 0; cpu80_on = 1;
    cyc80 = 0; insns80 = 0; unk80 = 0;
    shift_sr = 0; shift_off = 0;
    si_ufo_on = 0; si_shot_pending = 0; si_flash_pending = 0;
    si_expl_pending = 0; si_hit_pending = 0; si_shield_pending = 0;
    port_trace80 = 0; trace_n = 0;
    build_par80();
    build_cyc80();
}

/* interrupt injection: RST-style vector; returns 1 when accepted */

int irq80(int vec) {
    int* r = SB80;
    if (!IFF80) return 0;
    call80(vec);
    IFF80 = 0;
    halted80 = 0;
    return 1;
}

/* one instruction — the full ladder */

void step80() {
    int* r = SB80;
    int opc, addr, d16, rr, dst, src, rp, tmp;

    if (halted80 || !cpu80_on) { cyc80 = 4; return; }

    opc = rb80(gPC);
    gPC = (gPC + 1) & 0xFFFF;
    insns80++;
    cyc80 = CYC80[opc];

    /* ---- 0x00-0x3F ---- */
    if (opc < 0x40) {
        if (opc == 0x00 || opc == 0x08 || opc == 0x10 || opc == 0x18 ||
            opc == 0x20 || opc == 0x28 || opc == 0x30 || opc == 0x38) return;
        if ((opc & 0x0F) == 0x01) {                       /* LXI rp,d16 */
            d16 = rw80(gPC); gPC += 2;
            setpair80((opc >> 4) & 3, d16);
            return;
        }
        if ((opc & 0x0F) == 0x09) {                       /* DAD rp */
            rp = (opc >> 4) & 3;
            rr = (gH << 8) | gL;
            rr = rr + pair80(rp);
            fC80 = (rr > 0xFFFF) ? 1 : 0;
            gH = (rr >> 8) & 255;
            gL = rr & 255;
            return;
        }
        if ((opc & 0x0F) == 0x03) {                       /* INX rp     */
            rp = (opc >> 4) & 3;
            setpair80(rp, (pair80(rp) + 1) & 0xFFFF);
            return;
        }
        if ((opc & 0x0F) == 0x0B) {                       /* DCX rp     */
            rp = (opc >> 4) & 3;
            setpair80(rp, (pair80(rp) - 1) & 0xFFFF);
            return;
        }
        if (opc == 0x02 || opc == 0x12) {                 /* STAX B/D   */
            ww80(pair80((opc >> 4) & 1), gA);
            return;
        }
        if (opc == 0x0A || opc == 0x1A) {                 /* LDAX B/D   */
            gA = rb80(pair80((opc >> 4) & 1));
            return;
        }
        if ((opc & 0xC7) == 0x04) { inr_val80((opc >> 3) & 7); return; }
        if ((opc & 0xC7) == 0x05) { dcr_val80((opc >> 3) & 7); return; }
        if ((opc & 0xC7) == 0x06) {
            wrreg80((opc >> 3) & 7, rb80(gPC));
            gPC += 1;                     /* opcode already consumed one byte */
            return;
        }
        if (opc == 0x07) {                                /* RLC */
            rr = (gA >> 7) & 1;
            gA = ((gA << 1) | rr) & 255;
            fC80 = rr; fAC80 = 0;
            return;
        }
        if (opc == 0x0F) {                                /* RRC */
            rr = gA & 1;
            gA = ((gA >> 1) | (rr << 7)) & 255;
            fC80 = rr; fAC80 = 0;
            return;
        }
        if (opc == 0x17) {                                /* RAL */
            rr = fC80;
            fC80 = (gA >> 7) & 1;
            gA = ((gA << 1) | rr) & 255;
            fAC80 = 0;
            return;
        }
        if (opc == 0x1F) {                                /* RAR */
            rr = fC80;
            fC80 = gA & 1;
            gA = ((gA >> 1) | (rr << 7)) & 255;
            fAC80 = 0;
            return;
        }
        if (opc == 0x22) {                                /* SHLD a16  */
            addr = rw80(gPC); gPC += 2;
            wb80(addr, gL);
            wb80(addr + 1, gH);
            return;
        }
        if (opc == 0x2A) {                                /* LHLD a16  */
            addr = rw80(gPC); gPC += 2;
            gL = rb80(addr);
            gH = rb80(addr + 1);
            return;
        }
        if (opc == 0x27) { daa80(); return; }             /* DAA       */
        if (opc == 0x2F) { gA = (~gA) & 255; return; }    /* CMA       */
        if (opc == 0x32) {                                /* STA a16   */
            addr = rw80(gPC); gPC += 2;
            wb80(addr, gA);
            return;
        }
        if (opc == 0x3A) {                                /* LDA a16   */
            addr = rw80(gPC); gPC += 2;
            gA = rb80(addr);
            return;
        }
        if (opc == 0x33) { gSP = (gSP + 1) & 0xFFFF; return; }
        if (opc == 0x3B) { gSP = (gSP - 1) & 0xFFFF; return; }
        if (opc == 0x37) { fC80 = 1; return; }            /* STC */
        if (opc == 0x3F) { fC80 = fC80 ? 0 : 1; return; } /* CMC */
        unk80++;
        return;
    }

    /* ---- 0x40-0xBF: MOV + ALU ---- */
    if (opc < 0xC0) {
        if (opc == 0x76) { halted80 = 1; return; }        /* HLT */
        if (opc < 0x80) {
            dst = (opc >> 3) & 7;
            src = opc & 7;
            wrreg80(dst, rg80(src));
            return;
        }
        alu80((opc >> 3) & 7, rg80(opc & 7));
        return;
    }

    /* ---- 0xC0-0xFF: colliding two-byte ops first, then groups ---- */
    if (opc == 0xC9 || opc == 0xD9) {                     /* RET (D9 undoc) */
        gPC = rb80(gSP) | (rb80(gSP + 1) << 8);
        gSP = (gSP + 2) & 0xFFFF;
        return;
    }
    if (opc == 0xCD) {                                    /* CALL a16 */
        d16 = rw80(gPC); gPC += 2;
        call80(d16);
        return;
    }
    if (opc == 0xE9) { gPC = (gH << 8) | gL; return; }    /* PCHL */
    if (opc == 0xF9) { gSP = (gH << 8) | gL; return; }    /* SPHL */
    if (opc == 0xE3) {                                    /* XTHL */
        tmp = gL;
        gL = rb80(gSP);
        wb80(gSP, tmp);
        tmp = gH;
        gH = rb80(gSP + 1);
        wb80(gSP + 1, tmp);
        return;
    }
    if (opc == 0xEB) {                                    /* XCHG */
        tmp = gD; gD = gH; gH = tmp;
        tmp = gE; gE = gL; gL = tmp;
        return;
    }
    if (opc == 0xF3) { IFF80 = 0; return; }               /* DI */
    if (opc == 0xFB) { IFF80 = 1; return; }               /* EI */

    if ((opc & 0x07) == 0x01) {                           /* POP rp */
        rp = (opc >> 4) & 3;
        if (rp == 0) { gC = rb80(gSP); gB = rb80(gSP + 1); }
        else if (rp == 1) { gE = rb80(gSP); gD = rb80(gSP + 1); }
        else if (rp == 2) { gL = rb80(gSP); gH = rb80(gSP + 1); }
        else {
            gA = rb80(gSP + 1);
            tmp = rb80(gSP);
            fS80 = (tmp >> 7) & 1;
            fZ80 = (tmp >> 6) & 1;
            fAC80 = (tmp >> 4) & 1;
            fP80 = (tmp >> 2) & 1;
            fC80 = tmp & 1;
        }
        gSP = (gSP + 2) & 0xFFFF;
        return;
    }
    if ((opc & 0x07) == 0x05) {                           /* PUSH rp */
        rp = (opc >> 4) & 3;
        if (rp == 0) push80(gB, gC);
        else if (rp == 1) push80(gD, gE);
        else if (rp == 2) push80(gH, gL);
        else push80(gA, (fS80 << 7) | (fZ80 << 6) | (fAC80 << 4) | (fP80 << 2) | 2 | fC80);
        return;
    }
    if ((opc & 0x07) == 0x04) {                           /* C cc a16 */
        d16 = rw80(gPC); gPC += 2;
        if (cond80((opc >> 3) & 7)) call80(d16);
        return;
    }
    if ((opc & 0x07) == 0x02) {                           /* J cc a16 */
        d16 = rw80(gPC); gPC += 2;
        if (cond80((opc >> 3) & 7)) gPC = d16;
        return;
    }
    if (opc == 0xC3) {                                    /* JMP a16 */
        gPC = rw80(gPC);
        return;
    }
    if (opc == 0xD3) {                                    /* OUT p8 */
        addr = rb80(gPC); gPC += 1;
        si_out80(addr, gA);
        return;
    }
    if (opc == 0xDB) {                                    /* IN p8 */
        addr = rb80(gPC); gPC += 1;
        gA = si_in80(addr);
        return;
    }
    if ((opc & 0x07) == 0x00) {                           /* R cc */
        if (cond80((opc >> 3) & 7)) {
            gPC = rb80(gSP) | (rb80(gSP + 1) << 8);
            gSP = (gSP + 2) & 0xFFFF;
        }
        return;
    }
    if ((opc & 0x07) == 0x07) {                           /* RST n */
        call80(((opc >> 3) & 7) * 8);
        return;
    }
    if ((opc & 0x07) == 0x06) {                           /* ALU imm */
        alu80((opc >> 3) & 7, rb80(gPC));
        gPC += 1;                     /* opcode already consumed one byte */
        return;
    }
    unk80++;                                              /* CB DD ED FD */
}

/* ============================================================================
 *  PART 4 — Space Invaders machine (8080 host hardware)
 * ============================================================================
 *  Video: the ROM writes a 224x256 portrait 1bpp framebuffer at 0x2400
 *  (32 bytes per column, 8 vertical pixels per byte). The arcade monitor
 *  was rotated, so the landscape picture is:
 *        screen_x = (byte_index * 8) + bit      (0..255)
 *        screen_y = column_index                (0..223)
 *  We keep a copy of the previous frame; every CHANGED VRAM byte is
 *  repainted with one background rect + one rect per lit pixel. Typical
 *  damage per frame is a few dozen bytes -> a few hundred fill_rect
 *  calls, no blit syscall needed (mtcc exposes none — this renders with
 *  the same primitives the CHIP-8 core uses).
 *  Color overlay (MAME-style): y < 32 red (UFO band), y >= 184 green
 *  (player + shields), middle white. 'r' toggles a horizontal mirror.
 *  Sound: PC-speaker approximations of OUT 3/OUT 5 events + the UFO
 *  drone as a continuous tone while bit 0 of OUT 3 is set.
 *  Timing: 2 MHz / 60 Hz = 33333 T-states per frame, split at 17066
 *  (mid-frame IRQ -> RST 1) + 16267 (vblank IRQ -> RST 2).
 * ==========================================================================*/

/* SI scalar state lives in c8blk (see PART 2 defines); the six input
 * bits are plain globals (cold, touched only on key events) */
int si_kleft, si_kright, si_kfire, si_kstart1, si_kstart2, si_ktilt;

void si_input_press(int code, int press) {
    int* q = c8blk;
    if (code >= 'A' && code <= 'Z') code = code - 'A' + 'a';
    if (code == -3 || code == 'a') si_kleft = press;
    else if (code == -4 || code == 'd') si_kright = press;
    else if (code == ' ' || code == -1 || code == 'w') si_kfire = press;
    else if (code == '1') si_kstart1 = press;
    else if (code == '2') si_kstart2 = press;
    else if (code == 't') si_ktilt = press;
    else if (code == 'c' || code == '5') { if (press) si_coin = 3; }
}

void si_build_ports() {
    int* q = c8blk;
    p1_bits = 0;
    if (si_coin > 0) p1_bits |= 0x01;          /* coin inserted (pulses) */
    if (si_kstart2) p1_bits |= 0x02;
    if (si_kstart1) p1_bits |= 0x04;
    if (si_kfire) p1_bits |= 0x10;
    if (si_kleft) p1_bits |= 0x20;
    if (si_kright) p1_bits |= 0x40;
    p2_bits = 0;
    if (si_ktilt) p2_bits |= 0x04;             /* tilt (DIPs read as 3 ships) */
}

void si_setup_canvas() {
    int* q = c8blk;
    int w, h;
    w = 256;
    h = 224;
    si_sc = (SH - 36) / h;
    if ((SW - 16) / w < si_sc) si_sc = (SW - 16) / w;
    if (si_sc > 4) si_sc = 4;
    if (si_sc < 1) si_sc = 1;
    si_ox = (SW - w * si_sc) / 2;
    si_oy = 28 + (SH - 28 - h * si_sc) / 2;
    rect(0, 0, SW, SH, 0x000b0d16);
    rect(0, 0, SW, 26, 0x0016203a);
    rect(0, 24, SW, 2, 0x0033405e);
    rect(si_ox - 3, si_oy - 3, w * si_sc + 6, h * si_sc + 6, 0x0033405e);
    rect(si_ox, si_oy, w * si_sc, h * si_sc, 0);
}

void si_draw_hud(char* name) {
    int* q = c8blk;
    char nbuf[40];
    int i, lim, x;
    draw_text(10, 7, "EMU8BIT", 2, 0x00e8f0ff);
    x = draw_text(80, 10, "8080/SI", 1, 0x00ffcc44);
    lim = (SW - 340) / 6;
    if (lim > 38) lim = 38;
    if (lim > 0) {
        i = 0;
        while (name[i] && i < lim) { nbuf[i] = name[i]; i++; }
        nbuf[i] = 0;
        draw_text(x + 14, 10, nbuf, 1, 0x00e8f0ff);
    }
    draw_text(SW - 236, 10, "ESC MENU [ ] SPD R MIRROR", 1, 0x0033405e);
}

/* repaint every changed VRAM byte as an 8x1 landscape pixel run */
void si_render() {
    int* q = c8blk;
    int i, b, t, x, y, col, base, v;
    if (!si_prev_valid) {
        rect(si_ox, si_oy, 256 * si_sc, 224 * si_sc, 0);
        for (i = 0; i < 7168; i++) prev80[i] = 0;
        si_prev_valid = 1;
    }
    base = SI_VRAM;
    for (i = 0; i < 7168; i++) {
        v = rb80(base + i);
        if (prev80[i] == v) continue;
        prev80[i] = v;
        y = i / 32;                        /* column -> landscape Y */
        x = (i - y * 32) * 8;              /* byte*8 + bit -> X      */
        col = 0x00e8f0ff;
        if (y < 32) col = 0x00ff5555;         /* UFO band    */
        else if (y >= 184) col = 0x0033dd66;   /* player band */
        rect(si_ox + x * si_sc, si_oy + y * si_sc, 8 * si_sc, si_sc, 0);
        for (t = 0; t < 8; t++) {
            b = x + t;
            if (si_mirror) b = 255 - b;
            if (v & (1 << t))
                rect(si_ox + b * si_sc, si_oy + y * si_sc, si_sc, si_sc, col);
        }
    }
}

void si_sound_sync() {
    int* q = c8blk;
    if (si_shot_pending) { snd_beep(900, 60); si_shot_pending = 0; }
    if (si_flash_pending) { snd_beep(300, 40); si_flash_pending = 0; }
    if (si_expl_pending) { snd_beep(80, 250); si_expl_pending = 0; }
    if (si_hit_pending) { snd_beep(120, 300); si_hit_pending = 0; }
    if (si_shield_pending) { snd_beep(250, 80); si_shield_pending = 0; }
}

/* drain key events; returns 1 = Esc pressed (back to menu) */
int si_poll_input() {
    int* q = c8blk;
    int ev, code, press, i;
    for (i = 0; i < 64; i++) {
        ev = key_event();
        if (ev == 0) break;
        press = (ev >> 16) & 1;
        code = (ev << 16) >> 16;
        if (code == 27) { if (press) { si_running = 0; si_quit80 = 1; } continue; }
        if (code == 'r') { if (press) { si_mirror = si_mirror ? 0 : 1; si_prev_valid = 0; } continue; }
        if (code == '[') { if (press && si_pct > 25) si_pct -= 25; continue; }
        if (code == ']') { if (press && si_pct < 300) si_pct += 25; continue; }
        si_input_press(code, press);
    }
    return si_quit80;
}

/* run the CPU until `budget` T-states are consumed */
void si_run_till(int budget) {
    int* r = SB80;
    int spent;
    spent = 0;
    while (spent < budget) {
        if (halted80 || !cpu80_on) return;
        step80();
        spent += cyc80;
    }
}

/* one 60 Hz frame: half IRQ1 run + half IRQ2 run + video + sound */
void si_frame() {
    int* q = c8blk;
    int* r = SB80;
    si_build_ports();
    si_run_till(17066 * si_pct / 100);
    if (halted80) return;
    irq80(0x08);                            /* mid-frame signal -> RST 1 */
    si_run_till(16267 * si_pct / 100);
    irq80(0x10);                            /* vblank -> RST 2 */
    if (si_coin > 0) si_coin--;
    si_render();
    si_sound_sync();
}

/* the 8080/SI run loop (graphics + input path) */
void run_si(char* path) {
    int* q = c8blk;
    int* r = SB80;
    int n, now, last, acc, fr;
    char nm[48];

    cpu_reset80();
    if (path != 0) {
        if (file_exists(path) == 0) {
            print("emu8bit: 8080 rom not found: ");
            print(path);
            print("\n");
            return;
        }
        n = file_read_all(path, mem80, SI_ROM_MAX);
        if (n <= 0) {
            print("emu8bit: cannot read 8080 rom\n");
            return;
        }
        print("emu8bit: 8080 rom ");
        print(path);
        print(" - ");
        printint(n);
        print(" byte\n");
        base_name(path, nm, 48);
    } else {
        str_copy(nm, "BUILT-IN", 48);
    }

    if (gfx_init() != 0) {
        print("emu8bit: need a VESA 32bpp screen\n");
        return;
    }
    si_prev_valid = 0;
    si_mirror = 0;
    si_quit80 = 0;
    si_running = 1;
    si_coin = 0;
    si_pct = 100;
    si_setup_canvas();
    si_draw_hud(nm);
    print("emu8bit: 8080/SI - arrows|ad move SPACE|w fire 1 1P 2 2P c coin\n");
    print("emu8bit: NOTE: supply the Space Invaders ROM (8 KiB, invaders.rom)\n");

    last = gettick();
    acc = 0;
    while (si_running) {
        if (si_poll_input()) break;
        now = gettick();
        acc = acc + (now - last) * 10;
        if (acc < 0) acc = 0;
        last = now;
        if (acc >= 16) {
            fr = acc / 16;
            if (fr > 4) fr = 4;
            acc = acc - fr * 16;
            for (n = 0; n < fr && si_running && !halted80; n++) si_frame();
        }
    }
    spk_silence();
    si_ufo_on = 0;
    rect(0, 0, SW, SH, 0x000b0d16);
    print("emu8bit: 8080 done - insns ");
    printint(insns80);
    print("  unknown ");
    printint(unk80);
    if (halted80) print("  (CPU halted)");
    print("\n");
}

/* ============================================================================
 *  PART 5 — built-in self-test (data-driven)
 * ============================================================================
 *  Deterministic opcode-level verification of BOTH cores. Every test
 *  ROM is embedded (no filesystem), every driver is pure computation
 *  (no key_event/blit), so the suite runs identically in-OS and under
 *  the mtcc host harness (interp32) — the exit status equals the number
 *  of failed checks.
 *
 *  FIXUP BUDGET NOTE: mtcc allows 2048 fixups per compile; one call +
 *  string pair per st_chk() site would burn ~250 of them, so ALL
 *  state assertions are encoded as DATA rows {kind, index, expected}
 *  consumed by ONE generic checker (st_run). Data literals cost zero
 *  fixups; the checker ladder is compiled once.
 *
 *  Kinds: 0 V8[idx]  1 chip8 mem r8(idx)  2 screen8[idx]  3 8080 rb80(idx)
 *         4 I8  5 dt8  6 unk8  7 1-run8_on  8 CW8  9 CH8  10 gSP
 *         11 gA  12 trace_n  13 trace_port[idx]  14 trace_val[idx]
 *         15 R8HP[idx]  16 st8
 * ==========================================================================*/


int st_total, st_fail;

/* print a 4-digit hex value (printint is decimal-only) */
void phex4(int v) {
    char b[7];
    int i, d;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 4; i++) {
        d = (v >> ((3 - i) * 4)) & 15;
        if (d < 10) b[2 + i] = '0' + d;
        else b[2 + i] = 'A' + d - 10;
    }
    b[6] = 0;
    print(b);
}

void st_head(int kind, int idx, int got, int want) {
    int* q = c8blk;
    print("  FAIL #");
    printint(st_total);
    print(" k");
    printint(kind);
    print("[");
    printint(idx);
    print("] got ");
    phex4(got);
    print(" want ");
    phex4(want);
    print("\n");
}

/* the generic checker: consumes n rows of {kind, index, expected} */
void st_run(int* chk, int n) {
    int* q = c8blk;
    int* r = SB80;
    int* v = V8;
    char* scr = screen8;
    int k, kind, idx, got, want;
    for (k = 0; k < n; k++) {
        kind = chk[k * 3];
        idx = chk[k * 3 + 1];
        want = chk[k * 3 + 2];
        st_total++;
        got = 0;
        if (kind == 0) got = v[idx];
        else if (kind == 1) got = r8(idx);
        else if (kind == 2) got = scr[idx];
        else if (kind == 3) got = rb80(idx);
        else if (kind == 4) got = I8;
        else if (kind == 5) got = dt8;
        else if (kind == 6) got = unk8;
        else if (kind == 7) got = 1 - run8_on;
        else if (kind == 8) got = CW8;
        else if (kind == 9) got = CH8;
        else if (kind == 10) got = gSP;
        else if (kind == 11) got = gA;
        else if (kind == 12) got = trace_n;
        else if (kind == 13) got = trace_port[idx];
        else if (kind == 14) got = trace_val[idx];
        else if (kind == 15) got = R8HP[idx];
        else if (kind == 16) got = st8;
        if (got != want) {
            st_fail++;
            st_head(kind, idx, got, want);
        }
    }
    if (st_fail == 0) {
        print("  ok   ");
        printint(n);
        print(" checks\n");
    }
}

/* one explicit check (used sparingly — each site costs fixups) */
void st_chk(int got, int want) {
    int* q = c8blk;
    st_total++;
    if (got != want) {
        st_fail++;
        print("  FAIL got ");
        phex4(got);
        print(" want ");
        phex4(want);
        print("\n");
    }
}

/* ---- ROM A: CHIP-8 opcode exercise -------------------------------------
 * 0x200 6005 6103 8014 6201 6301 8234 62FF 8234   ADD/carry ladder
 * 0x210 6405 6503 8455 6505 8457 6603 8666 6703   SUB/SUBN/SHL/SHR
 * 0x220 8776 68AA 690F 8892 68AA 6A0F 88A1 68AA   AND/OR/XOR
 * 0x230 6BFF 88B3 6C05 3C05 60FF 4C05 60EE 6D07   SE/SNE skip ladder
 * 0x240 6D05 5D50 6011 A2A0 6208 6304 D235 D235   5XY0 + DRW x3
 * 0x250 D235 6C83 A400 FC33 FC1E A410 F055 F165   BCD/FX1E/FX55/FX65
 * 0x260 6004 B26A 0000 0000 0000 60FF 0000 00FD   BNNN (V0=4 -> 0x26E)
 * 0x2A0 sprite: 7E 81 A5 81 7E (drawn at (8,4), 5 rows)                */
int ROM_A[56] = {
    0x6005, 0x6103, 0x8014, 0x6201, 0x6301, 0x8234, 0x62FF, 0x8234,
    0x6405, 0x6503, 0x8455, 0x6505, 0x8457, 0x6603, 0x866E, 0x6703,
    0x8776, 0x68AA, 0x690F, 0x8892, 0x68AA, 0x6A0F, 0x88A1, 0x68AA,
    0x6BFF, 0x88B3, 0x6C05, 0x3C05, 0x60FF, 0x4C05, 0x60EE, 0x6D07,
    0x6D05, 0x5D50, 0x6011, 0xA2A0, 0x6208, 0x6304, 0xD235, 0xD235,
    0xD235, 0x6C83, 0xA400, 0xFC33, 0xFC1E, 0xA410, 0xF055, 0xF165,
    0x6004, 0xB26A, 0x0000, 0x0000, 0x0000, 0x60FF, 0x0000, 0x00FD
};
int SPR5[5] = { 0x7E, 0x81, 0xA5, 0x81, 0x7E };

/* ---- ROM B: CHIP-8 keyboard (EX9E/EXA1/FX0A) ---------------------------
 * key3 held at start; FX0A resolved by the driver with key2.
 * Expected: V0=2 (not FF/11/22/33).                                    */
int ROM_B[12] = {
    0x6003, 0xE0A1, 0x60FF, 0xE09E, 0x6011, 0x6004,
    0xE09E, 0x6022, 0xE0A1, 0x6033, 0xF00A, 0x00FD
};

/* ---- ROM C: SUPER-CHIP (00FF/00FE/DXY0/00C4/00FC/FX75/FX85) -----------
 * 0x280 sprite16: 16x{FF,00}; 0x2A0 sprite3: F0 0F F0
 * sprite3 lands at rows 12-14: row12 x0..3 (F0), row13 x4..7 (0F),
 * row14 x0..3 (F0) — proves hires ran, CLS, both scrolls.              */
int ROM_C[22] = {
    0x00FF, 0xA280, 0x6000, 0x6100, 0xD010, 0xD010, 0xD010, 0x00E0,
    0x00FE, 0xA2A0, 0x6004, 0x6108, 0xD013, 0x00C4, 0x00FC, 0x6011,
    0x6122, 0xF175, 0x6000, 0x6100, 0xF185, 0x00FD
};
int SPR3[3] = { 0xF0, 0x0F, 0xF0 };

/* ---- ROM G: delay timer (FX15/FX07 + per-frame decrement) ------------- */
int ROM_G[4] = { 0x6D0A, 0xFD15, 0xFD07, 0x00FD };

/* ---- ROM D: 8080 diagnostic (loaded at 0x0000) ------------------------
 * Hand-assembled; expected values computed per instruction:
 *   ADI 9B+39=D4 AC=1; DAA -> 3A CY=1; SUI 42-19=29; 29-30=F9 CY=1
 *   ANI 0F on F0 -> 00 Z=1 P=1 AC=1 (the 8080 ANA quirk)
 *   PUSH PSW/POP B -> flags byte 0x56 (S0 Z1 AC1 P1 C0)
 *   INR/DCR aux-carry; JZ/JC taken; RLC/RAR/RRC; CMC; ADC A self
 *   XCHG/SHLD/PUSH H/POP D; DAA 99+01 -> 00 CY=1; CALL 0390/RET + INR A
 *   EI; HLT. The 0390 sub (MVI A,44 / RET) is written by the driver.  */
int ROM_D[124] = {
    0x31, 0xFE, 0x7F,             /* 0000 LXI SP,7FFE        */
    0x3E, 0x9B,                   /* 0003 MVI A,9B           */
    0xC6, 0x39,                   /* 0005 ADI 39  -> D4      */
    0x32, 0x10, 0x21,             /* 0007 STA 2110           */
    0x27,                         /* 000A DAA    -> 3A CY=1  */
    0x32, 0x11, 0x21,             /* 000B STA 2111           */
    0x3E, 0x42,                   /* 000E MVI A,42           */
    0xD6, 0x19,                   /* 0010 SUI 19  -> 29      */
    0x32, 0x12, 0x21,             /* 0012 STA 2112           */
    0xD6, 0x30,                   /* 0015 SUI 30  -> F9      */
    0x32, 0x13, 0x21,             /* 0017 STA 2113           */
    0x3E, 0xF0,                   /* 001A MVI A,F0           */
    0xE6, 0x0F,                   /* 001C ANI 0F  -> 00      */
    0x32, 0x14, 0x21,             /* 001E STA 2114           */
    0xF5,                         /* 0021 PUSH PSW           */
    0xC1,                         /* 0022 POP B              */
    0x78,                         /* 0023 MOV A,B -> 00      */
    0x32, 0x15, 0x21,             /* 0024 STA 2115           */
    0x41,                         /* 0027 MOV B,C            */
    0x78,                         /* 0028 MOV A,B -> 56      */
    0x32, 0x16, 0x21,             /* 0029 STA 2116           */
    0x3E, 0x0F,                   /* 002C MVI A,0F           */
    0x3C,                         /* 002E INR A   -> 10 AC=1 */
    0x32, 0x17, 0x21,             /* 002F STA 2117           */
    0x3D,                         /* 0032 DCR A   -> 0F      */
    0x32, 0x18, 0x21,             /* 0033 STA 2118           */
    0xFE, 0x0F,                   /* 0036 CPI 0F  -> Z=1     */
    0xCA, 0x3D, 0x00,             /* 0038 JZ 003D (taken)    */
    0x3E, 0xAA,                   /* 003B (skipped)          */
    0x3E, 0x55,                   /* 003D MVI A,55           */
    0xFE, 0x66,                   /* 003F CPI 66  -> CY=1    */
    0x32, 0x19, 0x21,             /* 0041 STA 2119           */
    0xDA, 0x49, 0x00,             /* 0044 JC 0049 (taken)    */
    0x3E, 0xBB,                   /* 0047 (skipped)          */
    0x3E, 0x81,                   /* 0049 MVI A,81           */
    0x07,                         /* 004B RLC -> 03 CY=1     */
    0x1F,                         /* 004C RAR -> 81 CY=1     */
    0x32, 0x1A, 0x21,             /* 004D STA 211A           */
    0x0F,                         /* 0050 RRC -> C0 CY=1     */
    0x32, 0x1B, 0x21,             /* 0051 STA 211B           */
    0x3F,                         /* 0054 CMC -> CY=0        */
    0x3E, 0x80,                   /* 0055 MVI A,80           */
    0x8F,                         /* 0057 ADC A -> 00 CY=1 Z=1 */
    0x32, 0x1C, 0x21,             /* 0058 STA 211C           */
    0x21, 0x34, 0x12,             /* 005B LXI H,1234         */
    0x11, 0x11, 0x11,             /* 005E LXI D,1111         */
    0xEB,                         /* 0061 XCHG               */
    0x22, 0x1D, 0x21,             /* 0062 SHLD 211D          */
    0xE5,                         /* 0065 PUSH H             */
    0xD1,                         /* 0066 POP D              */
    0x7A,                         /* 0067 MOV A,D -> 11      */
    0x32, 0x1F, 0x21,             /* 0068 STA 211F           */
    0x3E, 0x99,                   /* 006B MVI A,99           */
    0xC6, 0x01,                   /* 006D ADI 01 -> 9A       */
    0x27,                         /* 006F DAA -> 00 CY=1     */
    0x32, 0x20, 0x21,             /* 0070 STA 2120           */
    0xCD, 0x90, 0x03,             /* 0073 CALL 0390          */
    0x3C,                         /* 0076 INR A -> 45        */
    0x32, 0x21, 0x21,             /* 0077 STA 2121           */
    0xFB,                         /* 007A EI                 */
    0x76                          /* 007B HLT                */
};

/* ---- ROM F: 8080 shifter through the REAL port hardware ---------------
 *   MVI A,12 / OUT 4; MVI A,34 / OUT 4  -> shifter = 0x3412 (rotate:
 *   first byte written is read back first). offset 1 -> 0x3412>>7 =
 *   0x68; offset 6 -> 0x3412>>2 & FF = 0x04. Port trace = 4,4,2,2.   */
int ROM_F[27] = {
    0x3E, 0x12, 0xD3, 0x04,
    0x3E, 0x34, 0xD3, 0x04,
    0x3E, 0x01, 0xD3, 0x02,
    0xDB, 0x03, 0x32, 0x30, 0x21,
    0x3E, 0x06, 0xD3, 0x02,
    0xDB, 0x03, 0x32, 0x31, 0x21,
    0x76
};

/* ---- ROM E: interrupt acceptance --------------------------------------
 *   EI; MVI A,11; JMP 0003 (tight loop). Handler injected at 0x0008:
 *   MVI B,99 / RET. irq80(8) must be accepted (1), B=99, A preserved,
 *   INTE cleared: a second irq80(8) must be refused (0).              */
int ROM_E[6] = { 0xFB, 0x3E, 0x11, 0xC3, 0x03, 0x00 };
int ROM_E_HDL[3] = { 0x06, 0x99, 0xC9 };

/* ---- check data -------------------------------------------------------- */

/* ROM A end state (kinds: 0=V8 1=mem8 2=px 4=I 5=dt 6=unk 7=exited) */
int CHK_A[99] = {
    7, 0, 1,
    6, 0, 0,
    4, 0, 1040,
    5, 0, 0,
    0, 0, 4,
    0, 1, 0,
    0, 2, 8,
    0, 3, 4,
    0, 4, 3,
    0, 5, 5,
    0, 6, 6,
    0, 7, 1,
    0, 8, 85,
    0, 9, 15,
    0, 10, 15,
    0, 11, 255,
    0, 12, 131,
    0, 13, 5,
    0, 14, 0,
    0, 15, 0,
    1, 1024, 1,
    1, 1025, 3,
    1, 1026, 1,
    1, 1040, 238,
    1, 1041, 0,
    2, 265, 1,
    2, 264, 0,
    2, 328, 1,
    2, 394, 1,
    2, 393, 0,
    2, 526, 1,
    2, 584, 0
};  /* 33 rows: ROM A end state */

/* ROM B end state: V0 = the FX0A-resolved key 2 */
int CHK_B[9] = {
    7, 0, 1,   6, 0, 0,   0, 0, 0x02
};

/* ROM C end state (SCHIP): kinds 8/9 = CW/CH, 15 = HP flags, 2 = pixels */
int CHK_C[51] = {
    7, 0, 1,
    6, 0, 0,
    8, 0, 64,
    9, 0, 32,
    0, 0, 17,
    0, 1, 34,
    0, 15, 0,
    15, 0, 17,
    15, 1, 34,
    15, 2, 0,
    2, 768, 1,
    2, 771, 1,
    2, 772, 0,
    2, 836, 1,
    2, 832, 0,
    2, 899, 1,
    2, 704, 0
};  /* 17 rows: ROM C (SCHIP) end state */

/* ROM G: dt lifecycle (V13 = FX07 readback, dt after pumps) */
int CHK_G1[6] = { 0, 13, 0x0A,  5, 0, 0x09 };
int CHK_G2[3] = { 5, 0, 0x07 };

/* ROM D end state (8080 memory + SP + A) */
int CHK_D[60] = {
    3, 8464, 212,
    3, 8465, 58,
    3, 8466, 41,
    3, 8467, 249,
    3, 8468, 0,
    3, 8469, 0,
    3, 8470, 86,
    3, 8471, 16,
    3, 8472, 15,
    3, 8473, 85,
    3, 8474, 129,
    3, 8475, 192,
    3, 8476, 0,
    3, 8477, 17,
    3, 8478, 17,
    3, 8479, 17,
    3, 8480, 0,
    3, 8481, 69,
    10, 0, 32766,
    11, 0, 69
};  /* 20 rows: ROM D end state */

/* ROM F end state: shifter results + port trace */
int CHK_F[27] = {
    3, 8496, 104,
    3, 8497, 4,
    12, 0, 4,
    13, 0, 4,
    14, 0, 18,
    13, 1, 4,
    14, 1, 52,
    13, 2, 2,
    14, 2, 1
};  /* 9 rows: ROM F shifter + trace */

/* load an int[] CHIP-8 ROM (big-endian words) at 0x200 */
void load_rom_words8(int* words, int n) {
    int i;
    for (i = 0; i < n; i++) {
        w8(C8_ROM_BASE + i * 2, words[i] >> 8);
        w8(C8_ROM_BASE + i * 2 + 1, words[i] & 255);
    }
}

void selftest_chip8() {
    int* q = c8blk;
    int i;
    print("chip8 core:\n");

    reset8();
    ips8 = 6000;
    load_rom_words8(ROM_A, 56);
    for (i = 0; i < 5; i++) w8(0x2A0 + i, SPR5[i]);
    pump8(60);
    st_run(CHK_A, 32);

    reset8();
    ips8 = 6000;
    load_rom_words8(ROM_B, 12);
    keys8[3] = 1;
    pump8(60);
    if (wait_key8) {
        keys8[2] = 1;
        V8[wait_reg8] = 2;
        wait_key8 = 0;
    }
    pump8(60);
    st_run(CHK_B, 3);
}

void selftest_schip() {
    int* q = c8blk;
    int i, keep;
    print("schip core:\n");

    reset8();
    ips8 = 6000;
    load_rom_words8(ROM_C, 22);
    for (i = 0; i < 32; i++) w8(0x280 + i, (i % 2) ? 0x00 : 0xFF);
    for (i = 0; i < 3; i++) w8(0x2A0 + i, SPR3[i]);
    pump8(60);
    st_run(CHK_C, 17);

    reset8();
    ips8 = 6000;
    load_rom_words8(ROM_G, 4);
    pump8(1);
    st_run(CHK_G1, 2);
    run8_on = 1;
    keep = pc8;
    pump8(2);
    st_run(CHK_G2, 1);
    st_chk(pc8 != keep, 1);
}

void selftest_8080() {
    int* r = SB80;
    int i, accepted;
    print("8080 core:\n");

    cpu_reset80();
    for (i = 0; i < 124; i++) wb80(i, ROM_D[i]);
    wb80(0x0390, 0x3E);
    wb80(0x0391, 0x44);
    wb80(0x0392, 0xC9);
    while (!halted80 && insns80 < 5000) step80();
    st_run(CHK_D, 20);

    cpu_reset80();
    for (i = 0; i < 6; i++) wb80(i, ROM_E[i]);
    for (i = 0; i < 3; i++) wb80(0x08 + i, ROM_E_HDL[i]);
    while (insns80 < 300) step80();
    accepted = irq80(0x08);
    st_chk(accepted, 1);
    while (insns80 < 400) step80();
    st_chk(gB, 0x99);
    st_chk(gA, 0x11);
    st_chk(IFF80, 0);
    st_chk(irq80(0x08), 0);

    cpu_reset80();
    port_trace80 = 1;
    for (i = 0; i < 27; i++) wb80(i, ROM_F[i]);
    while (!halted80 && insns80 < 5000) step80();
    st_run(CHK_F, 9);
}

int run_selftest() {
    int* q = c8blk;
    st_total = 0;
    st_fail = 0;
    print("emu8bit selftest\n");
    print("================\n");
    selftest_chip8();
    selftest_schip();
    selftest_8080();
    print("----------------\n");
    print("SELFTEST: ");
    printint(st_total);
    print(" checks, ");
    printint(st_fail);
    print(" fail\n");
    return st_fail;
}

/* ============================================================================
 *  PART 6 — ROM menu + program entry
 * ============================================================================
 *  The menu scans a fixed list of directories through <fileio.h>
 *  (f_open + F_DIR + f_readdir), filters by ROM extension, de-dupes by
 *  name, and always prepends two built-ins that work with no disk:
 *    [1] bounce.c8   — the embedded CHIP-8 demo (from emu-ch8)
 *    [2] selftest    — the full opcode self-test, then exits
 *  Type detection is automatic (extension + size); T cycles a per-entry
 *  override Auto -> CHIP-8 -> 8080 for ambiguous .bin/.rom files.
 * ==========================================================================*/

char* ent_name;               /* flat arena blocks: MENU_MAX rows of
                               * MENU_NAME / MENU_PATH bytes (mtcc has
                               * no 2D arrays and the 64*96 block is
                               * beyond the 4096-element array cap) */
char* ent_path;
int  ent_size[MENU_MAX];
int  ent_force[MENU_MAX];
int  ent_special[MENU_MAX];       /* 1 = bounce demo, 2 = selftest */
int  n_ent, menu_sel, menu_top;   /* windowed-list state */

void menu_alloc() {
    ent_name = malloc(MENU_MAX * MENU_NAME);
    ent_path = malloc(MENU_MAX * MENU_PATH);
    if (ent_name == 0 || ent_path == 0) {
        print("emu8bit: out of arena memory (menu)\n");
        exit(1);
    }
}

/* dirent layout (fileio.h): name[0..63], is_dir int @64, size int @68 */
int de_isdir(char* de) {
    return (de[64] & 255) | ((de[65] & 255) << 8);
}

int de_size(char* de) {
    return (de[68] & 255) | ((de[69] & 255) << 8) |
           ((de[70] & 255) << 16) | ((de[71] & 255) << 24);
}

int rom_ext_ok(char* name) {
    if (has_ext(name, "ch8")) return 1;
    if (has_ext(name, "c8")) return 1;
    if (has_ext(name, "sc8")) return 1;
    if (has_ext(name, "bin")) return 1;
    if (has_ext(name, "rom")) return 1;
    if (has_ext(name, "inv")) return 1;
    if (has_ext(name, "8080")) return 1;
    return 0;
}

int ent_known(char* name) {
    int* q = c8blk;
    int i;
    for (i = 0; i < n_ent; i++)
        if (str_eq_ci(ent_name + i * MENU_NAME, name)) return 1;
    return 0;
}

void ent_add(char* name, char* path, int size, int special) {
    int* q = c8blk;
    if (n_ent >= MENU_MAX) return;
    str_copy(ent_name + n_ent * MENU_NAME, name, MENU_NAME);
    str_copy(ent_path + n_ent * MENU_PATH, path, MENU_PATH);
    ent_size[n_ent] = size;
    ent_force[n_ent] = 0;
    ent_special[n_ent] = special;
    n_ent++;
}

/* scan all ROM directories; silent when a dir is missing */
void menu_scan() {
    int* q = c8blk;
    char de[72];
    char name[65];
    char path[96];
    char dirs[160];               /* 5 rows x 32 bytes, flat */
    int i, k, fd, r;
    str_copy(dirs + 0 * 32, ".", 32);
    str_copy(dirs + 1 * 32, "/games", 32);
    str_copy(dirs + 2 * 32, "/roms", 32);
    str_copy(dirs + 3 * 32, "/equinox/.local/emu8bit/src", 32);
    str_copy(dirs + 4 * 32, "/equinox/.local/emu-ch8/src", 32);

    ent_add("bounce.c8 (built-in demo)", "", 62, 1);
    ent_add("SELFTEST (all cores)", "", 0, 2);

    for (i = 0; i < 5; i++) {
        fd = f_open(dirs + i * 32, F_DIR);
        if (fd < 0) continue;
        while ((r = f_readdir(fd, de)) == 1) {
            k = 0;
            while (k < 63 && de[k]) { name[k] = de[k]; k++; }
            name[k] = 0;
            if (name[0] == 0) continue;
            if (name[0] == '.' && name[1] == 0) continue;
            if (name[0] == '.' && name[1] == '.' && name[2] == 0) continue;
            if (de_isdir(de)) continue;
            if (!rom_ext_ok(name)) continue;
            if (ent_known(name)) continue;
            k = 0;
            while (dirs[i * 32 + k] && k < 90) { path[k] = dirs[i * 32 + k]; k++; }
            if (k > 0 && path[k - 1] != '/') { path[k] = '/'; k++; }
            path[k] = 0;
            str_copy(path + str_len(path), name, MENU_PATH - str_len(path));
            ent_add(name, path, de_size(de), 0);
        }
        close(fd);
    }
}

/* automatic type detection for a menu entry */
int sniff_type(int idx) {
    int* q = c8blk;
    if (ent_force[idx] != 0) return ent_force[idx];
    if (ent_special[idx] == 1) return 1;
    if (ent_special[idx] == 2) return 0;
    if (has_ext(ent_name + idx * MENU_NAME, "ch8")) return 1;
    if (has_ext(ent_name + idx * MENU_NAME, "c8")) return 1;
    if (has_ext(ent_name + idx * MENU_NAME, "sc8")) return 1;
    if (has_ext(ent_name + idx * MENU_NAME, "inv")) return 3;
    if (has_ext(ent_name + idx * MENU_NAME, "8080")) return 3;
    if (ent_size[idx] > 3584) return 3;   /* .bin/.rom big = 8080 */
    return 1;
}

char type_letter(int idx) {
    int t;
    if (ent_special[idx] == 2) return 'T';
    if (ent_force[idx] == 0) return 'a';
    t = sniff_type(idx);
    if (t == 3) return '8';
    return 'C';
}

void draw_menu() {
    int* q = c8blk;
    int i, row, max_rows;
    char nbuf[36];
    char lc[2];
    rect(0, 0, SW, SH, 0x000b0d16);
    rect(0, 0, SW, 24, 0x0016203a);
    rect(0, 22, SW, 2, 0x0033405e);
    draw_text(10, 6, "EMU8BIT", 2, 0x00e8f0ff);
    draw_text(86, 9, "8-BIT MACHINE EMULATOR - CHIP-8/SCHIP + 8080", 1, 0x0033405e);

    max_rows = (SH - 78) / 12;
    if (max_rows > n_ent) max_rows = n_ent;
    if (max_rows < 1) max_rows = 1;
    if (menu_sel < menu_top) menu_top = menu_sel;
    if (menu_sel >= menu_top + max_rows) menu_top = menu_sel - max_rows + 1;

    for (row = 0; row < max_rows; row++) {
        i = menu_top + row;
        if (i >= n_ent) break;
        if (i == menu_sel) rect(6, 32 + row * 12, SW - 12, 12, 0x001d4ed8);
        lc[0] = type_letter(i);
        lc[1] = 0;
        draw_text(12, 34 + row * 12, lc, 1, 0x00ffcc44);
        str_copy(nbuf, ent_name + i * MENU_NAME, 36);
        draw_text(28, 34 + row * 12, nbuf, 1,
                  i == menu_sel ? 0x00e8f0ff : 0x0033405e);
        if (ent_force[i] != 0) draw_text(SW - 60, 34 + row * 12, "FIX", 1, 0x00ffcc44);
    }

    rect(0, SH - 30, SW, 30, 0x0016203a);
    rect(0, SH - 30, SW, 2, 0x0033405e);
    draw_text(10, SH - 24, "UP/DOWN MOVE  ENTER|SPACE RUN  T TYPE  1-9 QUICK  ESC QUIT", 1, 0x0033405e);
    draw_text(10, SH - 12, "a=auto C=chip8 8=8080 T=selftest", 1, 0x0033405e);
}

/* returns the selected entry index, or -1 for quit */
int menu_loop() {
    int* q = c8blk;
    int c, i9;
    menu_sel = 0;
    menu_top = 0;
    draw_menu();
    for (;;) {
        c = getkey();
        if (c == 27 || c == 'q') return -1;
        if (c == -1 || c == -3 || c == 'w' || c == 'k') {
            if (menu_sel > 0) menu_sel--;
            draw_menu();
        } else if (c == -2 || c == -4 || c == 's' || c == 'j') {
            if (menu_sel < n_ent - 1) menu_sel++;
            draw_menu();
        } else if (c == 13 || c == ' ') {
            return menu_sel;
        } else if (c == 't') {
            if (ent_special[menu_sel] == 0) {
                if (ent_force[menu_sel] == 0) ent_force[menu_sel] = 1;
                else if (ent_force[menu_sel] == 1) ent_force[menu_sel] = 3;
                else ent_force[menu_sel] = 0;
            }
            draw_menu();
        } else if (c >= '1' && c <= '9') {
            i9 = c - '1';
            if (i9 < n_ent) { menu_sel = i9; return i9; }
        }
    }
}

/* ---- program entry ---- */

int run_entry(int idx) {
    int* q = c8blk;
    int t, n;
    if (ent_special[idx] == 2) {
        n = run_selftest();
        print("emu8bit: selftest exit ");
        printint(n);
        print("\n");
        exit(n ? 1 : 0);
        return 0;
    }
    if (ent_special[idx] == 1) {
        reset8();
        ips8 = 700;
        rnd8 = gettick() | 1;
        if (rnd8 == 0) rnd8 = 1;
        load_demo8();
        set_rom_name8("bounce.c8");
        run8();
        return 0;
    }
    t = sniff_type(idx);
    if (t == 3) {
        run_si(ent_path + idx * MENU_PATH);
    } else {
        reset8();
        ips8 = 700;
        n = load_rom8(ent_path + idx * MENU_PATH);
        if (n < 0) {
            print("emu8bit: cannot load ");
            print(ent_path + idx * MENU_PATH);
            print(" (err ");
            printint(-n);
            print(")\n");
        } else {
            set_rom_name8(ent_path + idx * MENU_PATH);
            print("emu8bit: chip8 rom ");
            print(ent_path + idx * MENU_PATH);
            print(" - ");
            printint(n);
            print(" byte\n");
            run8();
        }
    }
    return 0;
}

/* run a ROM straight from the command line (no menu) */
int run_direct(char* path) {
    int* q = c8blk;
    int sz;
    sz = file_size(path);
    if (sz > 3584) run_si(path);
    else {
        reset8();
        ips8 = 700;
        if (load_rom8(path) < 0) {
            print("emu8bit: cannot load ");
            print(path);
            print("\n");
            exit(1);
        }
        set_rom_name8(path);
        run8();
    }
    exit(0);
    return 0;
}

int main() {
    int* q = c8blk;
    char args[96];
    int sel, f;

    print("emu8bit - 8-bit machine emulator (CHIP-8/SCHIP + 8080)\n");
    emu_alloc();
    menu_alloc();
    ips8 = 700;
    rnd8 = gettick() | 1;
    if (rnd8 == 0) rnd8 = 1;

    getargs(args, 96);
    if (args[0]) {
        if (str_eq_ci(args, "selftest")) {
            f = run_selftest();
            exit(f ? 1 : 0);
            return f;
        }
        return run_direct(args);
    }

    if (gfx_init() != 0) {
        print("emu8bit: need a VESA 32bpp screen (fbinfo)\n");
        exit(1);
        return 1;
    }

    menu_scan();
    for (;;) {
        sel = menu_loop();
        if (sel < 0) break;
        run_entry(sel);
    }

    rect(0, 0, SW, SH, 0x000b0d16);
    print("emu8bit: bye\n");
    exit(0);
    return 0;
}
