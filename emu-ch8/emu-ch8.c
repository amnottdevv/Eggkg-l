/* emu-ch8.c — CHIP-8 emulator (the `emu-ch8` tool) for Equinox OS.
 *
 * Run a CHIP-8 ROM (.c8, 4 KiB address space, 64x32 mono display):
 *
 *     emu-ch8                       -> built-in demo ROM
 *     emu-ch8 games/bounce.c8       -> ROM relative to the shell cwd
 *     emu-ch8 /equinox/.local/emu-ch8/roms/bounce.c8
 *
 * Written in the mtcc dialect (kernel syscall builtins only, no
 * includes): no struct/typedef/switch/sizeof/function pointers, so the
 * .mrp stays small and the package builds with a stock in-OS mtcc.
 *
 *   CPU     : full COSMAC VIP CHIP-8 set (0NNN is a NOP, 00FD exits —
 *             the SUPER-CHIP "exit interpreter" opcode, so a ROM can
 *             return to the shell by itself).
 *   Display : 64x32 cell framebuffer, integer-scaled onto the VESA
 *             console; only CHANGED cells are blitted (XOR sprites
 *             touch a handful of cells per frame).
 *   Input   : key_event() (press + release) drives a real 16-key state
 *             table — EX9E/EXA1/FX0A need held-key state, which the
 *             press-only pollkey() cannot express.
 *   Timing  : 60 Hz delay/sound timers + a cycles-per-frame CPU budget
 *             (default 700 Hz, [ and ] adjust it live).
 *
 * Controls: Esc quit · Space pause · [ ] slower/faster ·
 *           keypad 1 2 3 4 / q w e r / a s d f / z x c v
 */

/* ---- palette (0x00RRGGBB) ---- */
#define COL_BG    0x000b0d16
#define COL_FG    0x00e8f0ff
#define COL_DIM   0x0033405e
#define COL_BAR   0x0016203a
#define COL_KEYON 0x0033dd66
#define COL_WARN  0x00ffcc44

#define HUD       30     /* top bar height in pixels      */
#define ROM_BASE  0x200  /* CHIP-8 programs load here     */
#define MEM_LAST  4095
#define MAX_ROM   3584   /* 0x1000 - 0x200                */
#define FB_N      2048   /* 64 * 32                       */
#define FRAME_MS  16     /* ~60 frames per second         */
#define TICK_MS   10     /* gettick() runs at 100 Hz      */

/* ---- screen / framebuffer ---- */
int fbi[6];
int SW, SH;
int sc, ox, oy;              /* cell scale + canvas origin */

/* ---- machine state ---- */
char mem[4096];              /* 4 KiB RAM (loads are zero-extended) */
int V[16];                   /* V0..VF (VF = carry/collision flag)  */
int stk[16];
int pc, I, sp;
int dt, st;                  /* delay / sound timers (60 Hz)         */
int opc, rx, ry, rn, rkk, rnnn;   /* decoded instruction fields     */
int running, paused, wait_key, wait_reg;
int quit_esc;                /* 1 = stopped by the Esc key */
int ips;                     /* CPU speed: instructions per second  */
int insns, nf, unk;          /* statistics                           */
int romlen;                  /* ROM size in bytes                    */
int rnd;
int snd_on;
int hud_sig;                 /* last drawn HUD state (-1 = dirty)    */

/* ---- I/O state ---- */
char keys[16];               /* held-key table, index = CHIP-8 key   */
char screen[FB_N];           /* 0/1 cell framebuffer                 */
char drawn[FB_N];            /* last blitted state (2 = repaint me)  */
char romname[48];

/* ---- CHIP-8 4x5 hex font (glyph '0'..'F' at 0x50) ---- */
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

/* ---- built-in demo ROM (hand-assembled, 31 words = 62 bytes) ----
 *
 *   0x200 00E0        CLS                       0x20C loop:
 *   0x202 6008        LD V0, 8     (x)          0x20E A238 LD I, sprite
 *   0x204 6106        LD V1, 6     (y)          0x210 D015 DRW V0,V1,5
 *   0x206 6201        LD V2, 1     (dx)         0x212 F415 LD DT, V4
 *   0x208 6301        LD V3, 1     (dy)         0x214 F507 LD V5, DT  <- wait
 *   0x20A 6600        LD V6, 0     (frame cnt)  0x216 3500 SE V5, 0
 *   0x20C 6401        LD V4, 1                  0x218 1214 JP 0x214
 *   ... x/y bounces off the 4 edges, V6 counts to 180, then 00FD exits.
 *   0x238 sprite: 8x5 smiley (7E 81 A5 81 7E)
 */
int DEMO[31] = {
    0x00E0, 0x6008, 0x6106, 0x6201, 0x6301, 0x6600, 0x6401, 0xA238,
    0xD015, 0xF415, 0xF507, 0x3500, 0x1214, 0xD015, 0x4000, 0x6201,
    0x4038, 0x62FF, 0x4100, 0x6301, 0x411B, 0x63FF, 0x8024, 0x8134,
    0x7601, 0x36B4, 0x120C, 0x00FD, 0x7E81, 0xA581, 0x7E00
};

/* ---- physical keypad layout (CHIP-8 key under each drawn cell) ---- */
int KPAD[16] = {
    1, 2, 3, 12,    4, 5, 6, 13,
    7, 8, 9, 14,    10, 0, 11, 15
};

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

/* ---- graphics helpers ---- */

void rect(int x, int y, int w, int h, int c) {
    /* fill_rect packs x|w<<16 and y|h<<16 into the 3-slot ABI */
    fill_rect(x | (w << 16), y | (h << 16), c);
}

int gfx_init() {
    int i;
    if (fb_info(fbi) != 0) return -1;
    if (fbi[5] == 0 || fbi[3] != 32) return -1;
    SW = fbi[1];
    SH = fbi[2];
    sc = (SH - HUD - 8) / 32;
    if ((SW - 16) / 64 < sc) sc = (SW - 16) / 64;
    if (sc > 24) sc = 24;
    if (sc < 2) sc = 2;
    ox = (SW - 64 * sc) / 2;
    oy = HUD + (SH - HUD - 32 * sc) / 2;
    rect(0, 0, SW, SH, COL_BG);
    rect(ox - 3, oy - 3, 64 * sc + 6, 32 * sc + 6, COL_DIM);   /* frame */
    rect(ox, oy, 64 * sc, 32 * sc, COL_BG);
    for (i = 0; i < FB_N; i++) drawn[i] = 2;                   /* repaint all */
    return 0;
}

/* ---- memory (always masked: a ROM may point I/pc anywhere) ---- */

int r8(int a) { return mem[a & MEM_LAST]; }
void w8(int a, int v) { mem[a & MEM_LAST] = v & 255; }

void cls() {
    int i;
    for (i = 0; i < FB_N; i++) screen[i] = 0;
}

/* ---- HUD text (5x7, integer scale) ---- */

int font_idx(char c) {
    int i;
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    i = 0;
    while (FCHARS[i]) {
        if (FCHARS[i] == c) return i;
        i++;
    }
    return 0;                       /* unknown -> space */
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

/* ---- RNG: xorshift32 (never zero) ---- */

int rng_next() {
    rnd = rnd ^ (rnd << 13);
    rnd = rnd ^ (rnd >> 17);
    rnd = rnd ^ (rnd << 5);
    return rnd;
}

/* ---- DXYN: XOR sprite draw (wraps at the edges, VF = collision) ---- */

void draw_sprite() {
    int x0, y0, i, j, b, xx, yy, idx;
    x0 = V[rx] % 64;
    y0 = V[ry] % 32;
    V[15] = 0;
    for (j = 0; j < rn; j++) {
        b = r8(I + j);
        for (i = 0; i < 8; i++) {
            if (b & (0x80 >> i)) {
                xx = x0 + i; if (xx > 63) xx = xx - 64;
                yy = y0 + j; if (yy > 31) yy = yy - 32;
                idx = yy * 64 + xx;
                if (screen[idx]) V[15] = 1;
                screen[idx] = screen[idx] ? 0 : 1;
            }
        }
    }
}

/* ---- one instruction (no switch in mtcc: a test ladder per opcode) ---- */

void step() {
    int hi, j, s, f;

    if (wait_key) return;          /* FX0A pending: CPU halted, timers run */

    opc = (r8(pc) << 8) | r8(pc + 1);
    pc = pc + 2;
    rx = (opc >> 8) & 15;
    ry = (opc >> 4) & 15;
    rn = opc & 15;
    rkk = opc & 255;
    rnnn = opc & 4095;
    insns++;

    hi = opc >> 12;

    if (hi == 0) {
        if (opc == 0x00E0) cls();
        else if (opc == 0x00EE) { if (sp > 0) { sp--; pc = stk[sp]; } }
        else if (opc == 0x00FD) running = 0;     /* exit interpreter */
        else if (opc != 0) unk++;                /* 0000 = ROM padding: NOP */
        return;
    }
    if (hi == 1) { pc = rnnn; return; }                          /* JP    */
    if (hi == 2) {                                               /* CALL  */
        if (sp >= 16) { print("emu-ch8: stack overflow\n"); running = 0; return; }
        stk[sp] = pc; sp++; pc = rnnn; return;
    }
    if (hi == 3) { if (V[rx] == rkk) pc += 2; return; }          /* SE    */
    if (hi == 4) { if (V[rx] != rkk) pc += 2; return; }          /* SNE   */
    if (hi == 5) { if (rn == 0 && V[rx] == V[ry]) pc += 2; return; }
    if (hi == 6) { V[rx] = rkk; return; }                        /* LD    */
    if (hi == 7) { V[rx] = (V[rx] + rkk) & 255; return; }        /* ADD   */
    if (hi == 9) { if (rn == 0 && V[rx] != V[ry]) pc += 2; return; }
    if (hi == 10) { I = rnnn; return; }                          /* LD I  */
    if (hi == 11) { pc = rnnn + V[0]; return; }                  /* JP V0 */
    if (hi == 12) { V[rx] = rng_next() & rkk; return; }          /* RND   */
    if (hi == 13) { draw_sprite(); return; }                     /* DRW   */

    if (hi == 8) {
        if (rn == 0) V[rx] = V[ry];
        else if (rn == 1) V[rx] = V[rx] | V[ry];
        else if (rn == 2) V[rx] = V[rx] & V[ry];
        else if (rn == 3) V[rx] = V[rx] ^ V[ry];
        else if (rn == 4) {                              /* ADD  (VF carry) */
            s = V[rx] + V[ry];
            f = (s > 255) ? 1 : 0;
            V[rx] = s & 255;
            V[15] = f;
        }
        else if (rn == 5) {                              /* SUB  (VF !borrow) */
            f = (V[rx] >= V[ry]) ? 1 : 0;
            s = (V[rx] - V[ry]) & 255;
            V[rx] = s;
            V[15] = f;
        }
        else if (rn == 6) {                              /* SHR  (VF = bit0) */
            f = V[rx] & 1;
            V[rx] = (V[rx] >> 1) & 255;
            V[15] = f;
        }
        else if (rn == 7) {                              /* SUBN (VF !borrow) */
            f = (V[ry] >= V[rx]) ? 1 : 0;
            s = (V[ry] - V[rx]) & 255;
            V[rx] = s;
            V[15] = f;
        }
        else if (rn == 14) {                             /* SHL  (VF = bit7) */
            f = (V[rx] >> 7) & 1;
            V[rx] = (V[rx] << 1) & 255;
            V[15] = f;
        }
        else unk++;
        return;
    }

    if (hi == 14) {
        if (rn == 14) { if (keys[V[rx] & 15]) pc += 2; return; }
        if (rn == 1)  { if (!keys[V[rx] & 15]) pc += 2; return; }
        unk++;
        return;
    }

    if (hi == 15) {
        if (rkk == 0x07) V[rx] = dt;
        else if (rkk == 0x0A) { wait_reg = rx; wait_key = 1; }
        else if (rkk == 0x15) dt = V[rx];
        else if (rkk == 0x18) st = V[rx];
        else if (rkk == 0x1E) I = (I + V[rx]) & MEM_LAST;
        else if (rkk == 0x29) I = 0x50 + (V[rx] & 15) * 5;
        else if (rkk == 0x33) {                       /* BCD */
            s = V[rx];
            w8(I, s / 100);
            w8(I + 1, (s / 10) % 10);
            w8(I + 2, s % 10);
        }
        else if (rkk == 0x55) { for (j = 0; j <= rx; j++) w8(I + j, V[j]); }
        else if (rkk == 0x65) { for (j = 0; j <= rx; j++) V[j] = r8(I + j); }
        else unk++;
        return;
    }

    unk++;                                   /* opcode outside the CHIP-8 set */
}

/* ---- input: key_event() press/release -> a true 16-key state table ----
 * pollkey() reports presses only, so a held key can never be seen as
 * "down" on the NEXT frame — and EX9E/EXA1/FX0A need exactly that.
 * key_event() (syscall 32, the DOOM input path) reports releases too.
 * The drain is bounded so a broken queue can never spin forever.
 */

int key_index(int code) {
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

void poll_input() {
    int ev, code, press, idx, i, j;
    for (i = 0; i < 64; i++) {
        ev = key_event();
        if (ev == 0) break;
        press = (ev >> 16) & 1;
        code = (ev << 16) >> 16;          /* sign-extend the raw code */
        if (code == 27) { if (press) { running = 0; quit_esc = 1; } continue; }   /* Esc */
        if (code == ' ') {                                          /* Space */
            if (press) {
                paused = paused ? 0 : 1;
                for (j = 0; j < FB_N; j++) drawn[j] = 2;   /* repaint (drop PAUSED) */
            }
            continue;
        }
        if (code == '[') { if (press && ips > 200) ips -= 100; continue; }
        if (code == ']') { if (press && ips < 3000) ips += 100; continue; }
        idx = key_index(code);
        if (idx >= 0) {
            keys[idx] = press ? 1 : 0;
            if (press && wait_key) { V[wait_reg] = idx; wait_key = 0; }
        }
    }
}

/* ---- blit ONLY the cells that changed (XOR sprites touch a few) ---- */

void render() {
    int i, on, row, col;
    for (i = 0; i < FB_N; i++) {
        on = screen[i] ? 1 : 0;
        if (drawn[i] == on) continue;
        drawn[i] = on;
        row = i / 64;
        col = i - row * 64;
        rect(ox + col * sc, oy + row * sc, sc, sc, on ? COL_FG : COL_BG);
    }
}

void sound_sync() {
    if (st > 0) {
        if (!snd_on) { spk_tone(440); snd_on = 1; }
    } else if (snd_on) {
        spk_silence();
        snd_on = 0;
    }
}

/* ---- HUD: title + ROM + speed + hints + a live 4x4 keypad mirror ---- */

int hud_state() {
    int s = 0, i;
    for (i = 0; i < 16; i++) if (keys[i]) s |= (1 << i);
    s |= ips << 16;
    if (paused) s |= 0x10000000;
    return s;
}

void draw_pad() {
    int p, k;
    for (p = 0; p < 16; p++) {
        k = KPAD[p];
        rect(SW - 34 + (p % 4) * 6, 3 + (p / 4) * 6, 5, 5,
             keys[k] ? COL_KEYON : COL_DIM);
    }
}

void draw_hud() {
    char nbuf[48];
    int i, lim, x;
    rect(0, 0, SW, HUD, COL_BAR);
    rect(0, HUD - 2, SW, 2, COL_DIM);
    draw_text(10, 8, "CH8", 2, COL_FG);
    lim = (SW - 340) / 6;
    if (lim > 46) lim = 46;
    if (lim > 0) {
        i = 0;
        while (romname[i] && i < lim) { nbuf[i] = romname[i]; i++; }
        nbuf[i] = 0;
        draw_text(54, 11, nbuf, 1, COL_FG);
    }
    x = text_uint(SW - 250, 11, ips, 1, COL_FG);
    draw_text(x + 3, 11, "HZ", 1, COL_DIM);
    draw_text(SW - 204, 11, "ESC QUIT SPACE PAUSE [ ] SPD", 1, COL_DIM);
    draw_pad();
}

void draw_pause() {
    int w = 6 * 6 * 3;                    /* 6 glyphs * 6 px * scale 3 */
    int x = ox + (64 * sc - w) / 2;
    int y = oy + (32 * sc - 21) / 2;
    rect(x - 10, y - 8, w + 20, 37, COL_BG);
    draw_text(x, y, "PAUSED", 3, COL_WARN);
}

/* ---- machine + ROM loading ---- */

void machine_reset() {
    int i;
    for (i = 0; i < 4096; i++) mem[i] = 0;
    for (i = 0; i < 16; i++) { V[i] = 0; stk[i] = 0; keys[i] = 0; }
    for (i = 0; i < 80; i++) mem[0x50 + i] = CFONT[i];
    for (i = 0; i < FB_N; i++) { screen[i] = 0; drawn[i] = 2; }
    pc = ROM_BASE; I = 0; sp = 0; dt = 0; st = 0;
    opc = 0; rx = 0; ry = 0; rn = 0; rkk = 0; rnnn = 0;
    wait_key = 0; wait_reg = 0; paused = 0; snd_on = 0; quit_esc = 0;
    insns = 0; nf = 0; unk = 0;
}

void load_demo() {
    int i;
    for (i = 0; i < 31; i++) {
        w8(ROM_BASE + i * 2, DEMO[i] >> 8);
        w8(ROM_BASE + i * 2 + 1, DEMO[i] & 255);
    }
    romlen = 62;
}

int load_rom(char* path) {
    int sz, n;
    if (file_exists(path) == 0) return -1;
    sz = file_size(path);
    if (sz <= 0) return -2;
    if (sz > MAX_ROM) {
        print("emu-ch8: rom lebih besar dari 3584 byte - dipotong\n");
        sz = MAX_ROM;
    }
    n = file_read_all(path, mem + ROM_BASE, sz);
    if (n < 0) return -3;
    romlen = n;
    return n;
}

/* display name = the part after the last '/' */
void set_rom_name(char* path) {
    int n, start, i;
    n = 0;
    while (path[n]) n++;
    start = n;
    while (start > 0 && path[start - 1] != '/') start--;
    i = 0;
    while (start + i < n && i < 47) { romname[i] = path[start + i]; i++; }
    romname[i] = 0;
}

int main() {
    char path[96];
    int i, j, n, now, last, acc, fr, cyc;

    print("emu-ch8 - CHIP-8 emulator (mtcc)\n");
    ips = 700;
    rnd = gettick() | 1;
    hud_sig = -1;
    machine_reset();

    path[0] = 0;
    getargs(path, 90);
    i = 0;
    while (path[i] == ' ' || path[i] == '\t') i++;   /* trim leading blanks */
    if (i > 0) {
        j = 0;
        while (path[i]) { path[j] = path[i]; i++; j++; }
        path[j] = 0;
    }

    if (path[0]) {
        n = load_rom(path);
        if (n <= 0) {
            print("emu-ch8: gagal membaca ");
            print(path);
            print("\n");
            exit(1);
        }
        set_rom_name(path);
        print("emu-ch8: rom ");
        print(romname);
        print(" - ");
        printint(romlen);
        print(" byte\n");
    } else {
        load_demo();
        set_rom_name("DEMO.C8");
        print("emu-ch8: tanpa argumen - demo bawaan (62 byte)\n");
    }

    if (gfx_init() != 0) {
        print("emu-ch8: butuh layar VESA 32bpp\n");
        exit(1);
    }
    print("emu-ch8: ESC=quit SPACE=pause [ ]=speed  keypad 1234/qwer/asdf/zxcv\n");

    running = 1;
    last = gettick();
    acc = 0;
    while (running) {
        poll_input();
        if (!running) break;

        now = gettick();
        acc = acc + (now - last) * TICK_MS;
        if (acc < 0) acc = 0;
        last = now;

        if (acc >= FRAME_MS) {
            fr = acc / FRAME_MS;
            if (fr > 6) fr = 6;
            acc = acc - fr * FRAME_MS;
            if (acc > FRAME_MS * 6) acc = 0;      /* drop a stale backlog */

            if (!paused) {
                if (!wait_key) {
                    cyc = (fr * ips) / 60;
                    if (cyc > 900) cyc = 900;
                    for (i = 0; i < cyc && running; i++) step();
                }
                for (i = 0; i < fr; i++) {        /* 60 Hz timers */
                    if (dt > 0) dt--;
                    if (st > 0) st--;
                }
            }
            nf = nf + fr;

            sound_sync();
            render();
            if (hud_state() != hud_sig) { hud_sig = hud_state(); draw_hud(); }
            if (paused) draw_pause();
        }
    }

    spk_silence();
    snd_on = 0;
    rect(0, 0, SW, SH, 0);                        /* clean canvas for the shell */
    print("emu-ch8: selesai - frame ");
    printint(nf);
    print("  instruksi ");
    printint(insns);
    print("  opcode aneh ");
    printint(unk);
    print("\n");
    if (quit_esc) print("emu-ch8: berhenti karena ESC\n");
    else          print("emu-ch8: ROM selesai sendiri\n");
    exit(0);
    return 0;
}
