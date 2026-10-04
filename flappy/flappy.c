/* flappy.c — FLAPPY: tap-to-flap bird between the pipes (Equinox OS).
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect, no
 * #include, kernel syscall builtins only — `eggkg install flappy`
 * builds it in-OS with mtcc and drops flappy.mrp into /bin.
 *
 * Everything runs on a fixed 30 Hz timestep (gettick); the whole
 * scene is only ~14 rects, so a full redraw per tick stays flicker-
 * free on the VESA console. Geometry is derived from fb_info, so the
 * same binary plays on 1024x768 and on the 320x240 host test fb.
 *
 * Controls: Space / Up / W = flap, Q or Esc = quit.
 */

/* ---- special key codes (kernel pollkey ABI) ---- */
#define KEY_UP     -1
#define KEY_DOWN   -2
#define KEY_LEFT   -3
#define KEY_RIGHT  -4

/* ---- palette (0x00RRGGBB) ---- */
#define COL_SKY    0x0014324a
#define COL_CLOUD  0x001d4660
#define COL_PIPE   0x002ecc66
#define COL_RIM    0x001f8f47
#define COL_BIRD   0x00ffcc33
#define COL_WING   0x00c8751a
#define COL_EYE    0x00101820
#define COL_GROUND 0x002a5a2a
#define COL_GRASS  0x0039a04a
#define COL_BAR    0x0016203a
#define COL_TEXT   0x00f4f7ff
#define COL_OVER   0x00ff6b6b

#define HUD        24          /* score bar height            */
#define GND        40          /* ground strip height         */
#define BIRD_W     20
#define BIRD_H     15
#define PIPE_W     46
#define NPIPE      4
#define SPACING    260
#define PERIOD     33          /* ms per tick (~30 Hz)        */
#define AUTO_START 45          /* ticks before the drop starts */

/* ---- state (globals: mtcc has no static locals) ---- */
int fbi[6];                    /* fb_info: addr,W,H,bpp,pitch,avail */
int SW, SH;
int bird_x, bird_y, bird_v;
int ppx[NPIPE];                /* pipe left edge (px)          */
int pgap[NPIPE];               /* gap top (px)                 */
int pdone[NPIPE];              /* already scored               */
int gap_h, ground_y, spacing;
int score, best, ticks, started;
int rng_state = 12345;

/* ---- 5x7 HUD font (bit 0 = top row, one byte per column) ---- */
char FCHARS[44] = " !-./:?0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int FONT[215] = {
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

int gfx_init() {
    if (fb_info(fbi) != 0) return -1;
    if (fbi[5] == 0 || fbi[3] != 32) return -1;
    SW = fbi[1];
    SH = fbi[2];
    return 0;
}

void rect(int x, int y, int w, int h, int c) {
    /* fill_rect packs x|w<<16 and y|h<<16 into the 3-slot ABI */
    if (w < 1 || h < 1) return;
    fill_rect(x | (w << 16), y | (h << 16), c);
}

/* ---- RNG: xorshift32 (never zero) ---- */

void rng_seed(int seed) {
    rng_state = seed | 1;
}

int rng_next() {
    rng_state = rng_state ^ (rng_state << 13);
    rng_state = rng_state ^ (rng_state >> 17);
    rng_state = rng_state ^ (rng_state << 5);
    return rng_state;
}

int rng_range(int lo, int hi) {
    int m;
    if (hi <= lo) return lo;
    m = rng_next() & 0x7fffffff;
    return lo + m % (hi - lo + 1);
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
    return 0;                     /* unknown -> space */
}

void draw_char(int x, int y, char c, int scale, int color) {
    int g = font_idx(c) * 5;
    int col, row, bits;
    if (scale < 1) scale = 1;
    for (col = 0; col < 5; col++) {
        bits = FONT[g + col];
        for (row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                rect(x + col * scale, y + row * scale, scale, scale, color);
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

/* ---- fixed-timestep gate: fires at most every `period` ms ---- */
int every(int* last, int period) {
    int now = gettick();
    if (now - *last >= period) {
        *last = now;
        return 1;
    }
    return 0;
}

/* ---- world ---- */

int gap_span() {                 /* room left for a random gap top */
    int v = ground_y - HUD - gap_h - 24;
    if (v < 8) v = 8;
    return v;
}

void pipe_place(int i) {
    int j, m = 0;
    for (j = 0; j < NPIPE; j++)
        if (j != i && ppx[j] > m) m = ppx[j];
    if (m < 0) m = 0;
    ppx[i] = m + spacing;
    pgap[i] = HUD + 20 + rng_range(0, gap_span());
    pdone[i] = 0;
}

void world_init() {
    int i;
    gap_h = SH / 3;
    if (gap_h < 64) gap_h = 64;
    if (gap_h > 200) gap_h = 200;
    ground_y = SH - GND;
    spacing = SPACING;
    if (spacing < SW / 2 + PIPE_W + 40) spacing = SW / 2 + PIPE_W + 40;

    bird_x = SW / 4;
    bird_y = SH / 2 - BIRD_H;
    bird_v = 0;
    score = 0;
    best = 0;
    ticks = 0;
    started = 0;

    for (i = 0; i < NPIPE; i++) {
        ppx[i] = SW + 60 + i * spacing;
        pgap[i] = HUD + 20 + rng_range(0, gap_span());
        pdone[i] = 0;
    }
}

/* one simulation tick */
int tick() {
    int i, speed;

    ticks++;
    if (!started) {
        /* gentle bob until the first flap — or 1.5 s, so an idle
         * machine still reaches game over instead of hanging. */
        if (ticks > AUTO_START) started = 1;
        else bird_y = SH / 2 - BIRD_H + ((ticks / 8) % 2) * 6 - 3;
    } else {
        bird_v += 1;                       /* gravity */
        if (bird_v > 15) bird_v = 15;
        bird_y += bird_v;
    }

    speed = 3 + score / 4;
    if (speed > 7) speed = 7;

    for (i = 0; i < NPIPE; i++) {
        if (started) ppx[i] -= speed;
        if (ppx[i] + PIPE_W < 0) pipe_place(i);

        if (!pdone[i] && bird_x > ppx[i] + PIPE_W) {
            pdone[i] = 1;
            score++;
            snd_beep(1046, 40);             /* pipe passed */
        }

        /* bird vs pipe body */
        if (started &&
            bird_x + BIRD_W > ppx[i] && bird_x < ppx[i] + PIPE_W) {
            if (bird_y < pgap[i] || bird_y + BIRD_H > pgap[i] + gap_h)
                return -1;
        }
    }

    if (bird_y + BIRD_H > ground_y) return -1;   /* ground */
    if (bird_y < HUD - BIRD_H) bird_y = HUD - BIRD_H;
    if (bird_y < 0) return -1;
    return 0;
}

void draw_scene() {
    int i, y0, y1, ex, ey;

    rect(0, 0, SW, SH, COL_SKY);
    rect(SW / 9, HUD + 34, 84, 16, COL_CLOUD);
    rect(SW / 2, HUD + 90, 120, 14, COL_CLOUD);
    rect(SW * 7 / 10, HUD + 46, 70, 12, COL_CLOUD);

    for (i = 0; i < NPIPE; i++) {
        if (ppx[i] + PIPE_W < 0 || ppx[i] > SW) continue;
        y0 = pgap[i];
        y1 = pgap[i] + gap_h;
        rect(ppx[i], HUD, PIPE_W, y0 - HUD, COL_PIPE);         /* upper */
        rect(ppx[i], y0 - 10, PIPE_W, 10, COL_RIM);            /* lip    */
        rect(ppx[i], y1, PIPE_W, ground_y - y1, COL_PIPE);     /* lower */
        rect(ppx[i], y1, PIPE_W, 10, COL_RIM);                 /* lip    */
    }

    rect(0, ground_y, SW, GND, COL_GROUND);
    rect(0, ground_y, SW, 5, COL_GRASS);

    /* bird: body + wing + eye */
    ey = bird_y;
    rect(bird_x, ey, BIRD_W, BIRD_H, COL_BIRD);
    rect(bird_x + 2, ey + BIRD_H - 6, 10, 5, COL_WING);
    ex = bird_x + BIRD_W - 7;
    rect(ex, ey + 3, 4, 4, COL_EYE);
    rect(ex + 4, ey + 6, 5, 3, COL_RIM);                       /* beak  */

    rect(0, 0, SW, HUD, COL_BAR);
    rect(0, HUD - 2, SW, 2, COL_BIRD);
    draw_text(10, 8, "SCORE", 1, COL_TEXT);
    text_uint(52, 8, score, 1, COL_TEXT);
    draw_text(SW - 92, 8, "SPACE FLAP  Q QUIT", 1, COL_TEXT);
}

int main() {
    int k, last, alive, r;

    if (gfx_init() != 0) {
        print("flappy: needs a 32bpp VESA mode\n");
        exit(1);
    }
    if (SW < 200 || SH < 150) {
        print("flappy: screen too small\n");
        exit(1);
    }

    rng_seed(gettick() ^ 0x2f19c3);
    world_init();
    draw_scene();

    last = gettick();
    alive = 1;
    while (alive) {
        for (;;) {
            k = pollkey();
            if (k == 0) break;
            if (k == ' ' || k == KEY_UP || k == 'w' || k == 'W') {
                started = 1;
                bird_v = -9;                       /* flap */
                snd_beep(660, 25);
            } else if (k == 'q' || k == 'Q' || k == 27) {
                alive = 0;
                break;
            }
        }
        if (!alive) break;

        if (every(&last, PERIOD)) {
            r = tick();
            draw_scene();
            if (r < 0) {
                snd_beep(160, 350);                /* dive buzz */
                alive = 0;
            }
        }
    }

    spk_silence();
    if (score > best) best = score;
    rect(0, 0, SW, SH, 0);
    draw_text(SW / 2 - 60, SH / 2 - 20, "GAME OVER", 2, COL_OVER);
    draw_text(SW / 2 - 60, SH / 2 + 6, "SCORE", 2, COL_TEXT);
    text_uint(SW / 2 + 4, SH / 2 + 6, score, 2, COL_TEXT);
    print("flappy: game over - score ");
    printint(score);
    print("\n");
    exit(0);
    return 0;
}
