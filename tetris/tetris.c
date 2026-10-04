/* tetris.c — TETRIS: falling tetrominoes, line clears (Equinox OS).
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect, no
 * #include, no float, no struct — `eggkg install tetris` builds it
 * in-OS with mtcc and drops tetris.mrp into /bin.
 *
 * The 10x20 field lives in one int array (mtcc has no 2-D arrays),
 * pieces are 4 (x,y) cells in a 4x4 box and rotation is the integer
 * transform (x,y) -> (3-y,x). Only the dirty frame is redrawn (the
 * board changes a few times per second), so the console never flickers.
 *
 * Controls: Left/Right or A/D = slide, Up/W/X = rotate, Down = soft
 * drop, Space = hard drop, P = pause, Q or Esc = quit.
 */

/* ---- special key codes (kernel pollkey ABI) ---- */
#define KEY_UP     -1
#define KEY_DOWN   -2
#define KEY_LEFT   -3
#define KEY_RIGHT  -4

/* ---- palette (0x00RRGGBB) ---- */
#define COL_BG     0x000b0e1a
#define COL_FIELD  0x00141a2e
#define COL_LINE   0x001d2540
#define COL_BAR    0x0016203a
#define COL_TEXT   0x00f4f7ff
#define COL_DIM    0x007f8aa8
#define COL_OVER   0x00ff6b6b

#define COLS       10
#define ROWS       20
#define NCELL      200          /* COLS * ROWS                     */
#define HUD        24
#define PERIOD     33           /* ms per frame                    */

/* ---- state ---- */
int fbi[6];
int SW, SH;
int board[NCELL];               /* 0 = empty, else 1..7 colour      */
int PB[56] = {                 /* 7 pieces x 4 cells x (x,y)       */
    0, 1, 1, 1, 2, 1, 3, 1,     /* I */
    1, 0, 2, 0, 1, 1, 2, 1,     /* O */
    1, 0, 0, 1, 1, 1, 2, 1,     /* T */
    1, 0, 2, 0, 0, 1, 1, 1,     /* S */
    0, 0, 1, 0, 1, 1, 2, 1,     /* Z */
    0, 0, 0, 1, 1, 1, 2, 1,     /* J */
    2, 0, 0, 1, 1, 1, 2, 1      /* L */
};
int PCOL[7] = { 0x0033ccff, 0x00ffcc33, 0x00aa55ff, 0x0044dd66,
                0x00ff5566, 0x004488ff, 0x00ff9933 };
int cur, npos, cx, cy, crot;    /* live / next piece state          */
int score, lines, level, paused;
int drop_ms, drop_last;
int cs, ox, oy;                 /* cell size + field origin         */
int rx, ry;                     /* out: rotated cell (rot_cell)     */
int rng_state = 424242;

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
    if (w < 1 || h < 1) return;
    fill_rect(x | (w << 16), y | (h << 16), c);
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
    return 0;
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

int every(int* last, int period) {
    int now = gettick();
    if (now - *last >= period) {
        *last = now;
        return 1;
    }
    return 0;
}

/* ---- pieces ---- */

void rot_cell(int p, int x, int y, int r) {
    int t, i;
    if (p == 1) {                 /* the O piece never turns */
        rx = x;
        ry = y;
        return;
    }
    i = 0;
    while (i < r) {
        t = 3 - y;                /* CW inside the 4x4 box */
        y = x;
        x = t;
        i++;
    }
    rx = x;
    ry = y;
}

int fits(int px, int py, int p, int r) {
    int i, x, y;
    for (i = 0; i < 4; i++) {
        rot_cell(p, PB[(p * 4 + i) * 2], PB[(p * 4 + i) * 2 + 1], r);
        x = px + rx;
        y = py + ry;
        if (x < 0 || x >= COLS || y >= ROWS) return 0;
        if (y >= 0 && board[y * COLS + x] != 0) return 0;
    }
    return 1;
}

void layout() {
    cs = (SH - HUD - 24) / ROWS;
    if (cs > 34) cs = 34;
    if (cs < 6) cs = 6;
    ox = SW / 2 - 5 * cs - 60;
    if (ox < 8) ox = 8;
    oy = HUD + (SH - HUD - ROWS * cs) / 2;
    if (oy < HUD) oy = HUD;
}

void clear_lines() {
    int y, x, k, full;
    for (y = ROWS - 1; y >= 0; y--) {
        full = 1;
        for (x = 0; x < COLS; x++)
            if (board[y * COLS + x] == 0) full = 0;
        if (full) {
            lines++;
            score += 100;
            for (k = y; k > 0; k--)
                for (x = 0; x < COLS; x++)
                    board[k * COLS + x] = board[(k - 1) * COLS + x];
            for (x = 0; x < COLS; x++) board[x] = 0;
            y++;                    /* recheck the row that dropped in */
        }
    }
    level = 1 + lines / 8;
    drop_ms = 700 - (level - 1) * 55;
    if (drop_ms < 110) drop_ms = 110;
}

void spawn() {
    cur = npos;
    npos = rng_range(0, 6);
    cx = COLS / 2 - 2;
    cy = 0;
    crot = 0;
}

/* lock the live piece: 0 = ok, -1 = topped out */
int lock_piece() {
    int i, x, y, over;
    over = 0;
    for (i = 0; i < 4; i++) {
        rot_cell(cur, PB[(cur * 4 + i) * 2],
                 PB[(cur * 4 + i) * 2 + 1], crot);
        x = cx + rx;
        y = cy + ry;
        if (y < 0) over = 1;
        else board[y * COLS + x] = cur + 1;
    }
    clear_lines();
    if (over) return -1;
    spawn();
    if (!fits(cx, cy, cur, crot)) return -1;
    return 0;
}

void world_init() {
    int i;
    for (i = 0; i < NCELL; i++) board[i] = 0;
    layout();
    score = 0;
    lines = 0;
    level = 1;
    paused = 0;
    drop_ms = 700;
    drop_last = 0;
    npos = rng_range(0, 6);
    spawn();
}

/* ---- rendering ---- */

void draw_next() {
    int i, px, py, px0, py0;
    px0 = ox + COLS * cs + 18;
    py0 = oy + 46;
    draw_text(px0, py0 - 24, "NEXT", 1, COL_DIM);
    for (i = 0; i < 4; i++) {
        rot_cell(npos, PB[(npos * 4 + i) * 2],
                 PB[(npos * 4 + i) * 2 + 1], 0);
        px = px0 + (rx - 1) * 14;
        py = py0 + ry * 14;
        rect(px, py, 12, 12, PCOL[npos]);
    }
}

void draw() {
    int i, x, y, col;

    rect(0, 0, SW, SH, COL_BG);

    rect(ox - 3, oy - 3, COLS * cs + 6, ROWS * cs + 6, COL_LINE);
    rect(ox, oy, COLS * cs, ROWS * cs, COL_FIELD);

    for (y = 0; y < ROWS; y++) {
        for (x = 0; x < COLS; x++) {
            col = board[y * COLS + x];
            if (col > 0)
                rect(ox + x * cs + 1, oy + y * cs + 1,
                     cs - 2, cs - 2, PCOL[col - 1]);
        }
    }

    if (!paused) {
        for (i = 0; i < 4; i++) {
            rot_cell(cur, PB[(cur * 4 + i) * 2],
                     PB[(cur * 4 + i) * 2 + 1], crot);
            y = cy + ry;
            if (y >= 0)
                rect(ox + (cx + rx) * cs + 1, oy + y * cs + 1,
                     cs - 2, cs - 2, PCOL[cur]);
        }
    }

    rect(0, 0, SW, HUD, COL_BAR);
    rect(0, HUD - 2, SW, 2, PCOL[0]);
    draw_text(10, 8, "SCORE", 1, COL_TEXT);
    text_uint(52, 8, score, 1, COL_TEXT);
    draw_text(SW / 2 - 46, 8, "LINES", 1, COL_TEXT);
    text_uint(SW / 2 - 4, 8, lines, 1, COL_TEXT);
    draw_text(SW - 76, 8, "LEVEL", 1, COL_TEXT);
    text_uint(SW - 34, 8, level, 1, COL_TEXT);

    draw_next();

    if (paused) draw_text(ox + 26, oy + ROWS * cs / 2, "PAUSED", 2,
                          COL_TEXT);
}

/* ---- one logic frame: 0 = ok, -1 = dead ---- */
int step() {
    int r = 0;
    if (paused) return 0;
    if (every(&drop_last, drop_ms)) {
        if (fits(cx, cy + 1, cur, crot)) {
            cy++;
        } else {
            r = lock_piece();
            if (r == 0) snd_beep(300, 30);
        }
    }
    return r;
}

int main() {
    int k, last, alive, r;

    if (gfx_init() != 0) {
        print("tetris: needs a 32bpp VESA mode\n");
        exit(1);
    }
    if (SW < 240 || SH < 200) {
        print("tetris: screen too small\n");
        exit(1);
    }

    rng_state = (gettick() ^ 0x7e7715) | 1;
    world_init();
    draw();

    last = gettick();
    alive = 1;
    while (alive) {
        for (;;) {
            k = pollkey();
            if (k == 0) break;
            if (k == KEY_LEFT || k == 'a' || k == 'A') {
                if (fits(cx - 1, cy, cur, crot)) cx--;
            } else if (k == KEY_RIGHT || k == 'd' || k == 'D') {
                if (fits(cx + 1, cy, cur, crot)) cx++;
            } else if (k == KEY_UP || k == 'w' || k == 'W' ||
                       k == 'x' || k == 'X') {
                if (fits(cx, cy, cur, crot + 1)) crot++;
                else if (fits(cx - 1, cy, cur, crot + 1)) {
                    cx--;
                    crot++;
                } else if (fits(cx + 1, cy, cur, crot + 1)) {
                    cx++;
                    crot++;
                } else if (fits(cx + 2, cy, cur, crot + 1)) {
                    cx = cx + 2;
                    crot++;
                }
                if (crot > 3) crot = crot - 4;
                snd_beep(520, 20);
            } else if (k == KEY_DOWN || k == 's' || k == 'S') {
                if (fits(cx, cy + 1, cur, crot)) cy++;
            } else if (k == ' ') {
                while (fits(cx, cy + 1, cur, crot)) cy++;
                r = lock_piece();
                if (r < 0) alive = 0;
                score += 2;
            } else if (k == 'p' || k == 'P') {
                paused = !paused;
            } else if (k == 'q' || k == 'Q' || k == 27) {
                alive = 0;
                break;
            }
        }
        if (!alive) break;

        if (every(&last, PERIOD)) {
            r = step();
            draw();
            if (r < 0) alive = 0;
        }
    }

    spk_silence();
    rect(0, 0, SW, SH, 0);
    draw_text(SW / 2 - 60, SH / 2 - 20, "GAME OVER", 2, COL_OVER);
    draw_text(SW / 2 - 60, SH / 2 + 6, "SCORE", 2, COL_TEXT);
    text_uint(SW / 2 + 4, SH / 2 + 6, score, 2, COL_TEXT);
    print("tetris: game over - score ");
    printint(score);
    print(" lines ");
    printint(lines);
    print("\n");
    exit(0);
    return 0;
}
