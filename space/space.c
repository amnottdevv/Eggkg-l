/* space.c — SPACE: the classic invader march (Equinox OS).
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect, no
 * #include, kernel syscall builtins only — `eggkg install space`
 * builds it in-OS with mtcc and drops space.mrp into /bin.
 *
 * A grid of aliens marches left/right and drops a row at every wall;
 * a row of bombs answers back. Geometry comes from fb_info, so the
 * field adapts to 1024x768 (8x4 aliens) and to the 320x240 host test
 * framebuffer (narrower grid) from one source.
 *
 * Controls: Left/Right or A/D = move, Space = fire, Q or Esc = quit.
 */

/* ---- special key codes (kernel pollkey ABI) ---- */
#define KEY_UP     -1
#define KEY_DOWN   -2
#define KEY_LEFT   -3
#define KEY_RIGHT  -4

/* ---- palette (0x00RRGGBB) ---- */
#define COL_BG     0x00050510
#define COL_STAR   0x004a5a72
#define COL_BAR    0x00162a3a
#define COL_TEXT   0x00f4f7ff
#define COL_SHIP   0x0033ff88
#define COL_COCK   0x00bfffff
#define COL_BULLET 0x00ffe066
#define COL_BOMB   0x00ff6644
#define COL_EYE    0x00101820
#define COL_OVER   0x00ff6b6b
#define COL_WIN    0x006bff9c

#define HUD        24
#define MAXAL      32          /* alien grid (cols*rows, <= 32)      */
#define MAXBUL     3           /* player bullets in flight           */
#define MAXBOMB    6           /* bombs in flight                    */
#define MAXSTAR    36
#define AW         26          /* alien box                          */
#define AH         18
#define PW         40          /* player ship                        */
#define PH         16

/* ---- state ---- */
int fbi[6];
int SW, SH;
int ncols, nrows;               /* live grid size (fits the screen)  */
int gx0, gy0;                   /* grid origin (top-left alien)      */
int gdir;                       /* -1 left / +1 right                */
int ax[MAXAL], ay[MAXAL], aalive[MAXAL];
int bx[MAXBUL], by[MAXBUL], balive[MAXBUL];
int bxo[MAXBOMB], byo[MAXBOMB], bmalive[MAXBOMB];
int sx[MAXSTAR], sy[MAXSTAR];
int player_x, score, lives, level, ticks, invuln, banner;
int ACOL[4] = { 0x0066ff88, 0x00ff9944, 0x00ff5577, 0x00cc77ff };
int rng_state = 987654;

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

int iabs(int v) {
    return v < 0 ? -v : v;
}

/* ---- RNG: xorshift32 (never zero) ---- */
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

/* ---- field ---- */

void grid_spawn() {
    int i, r, c;
    gx0 = 24;
    gy0 = HUD + 26;
    gdir = 1;
    for (r = 0; r < nrows; r++) {
        for (c = 0; c < ncols; c++) {
            i = r * ncols + c;
            ax[i] = gx0 + c * 36;
            ay[i] = gy0 + r * 26;
            aalive[i] = 1;
        }
    }
}

void world_init() {
    int i;

    ncols = (SW - 48) / 36;
    if (ncols > 8) ncols = 8;
    if (ncols < 4) ncols = 4;
    nrows = 4;
    if (SH < 300) nrows = 3;
    if (nrows * ncols > MAXAL) nrows = MAXAL / ncols;

    player_x = SW / 2;
    score = 0;
    lives = 3;
    level = 1;
    ticks = 0;
    invuln = 0;
    banner = 0;

    for (i = 0; i < MAXBUL; i++) balive[i] = 0;
    for (i = 0; i < MAXBOMB; i++) bmalive[i] = 0;
    for (i = 0; i < MAXSTAR; i++) {
        sx[i] = rng_range(2, SW - 3);
        sy[i] = rng_range(HUD + 2, SH - 3);
    }
    grid_spawn();
}

int march_period() {
    int p = 70 - level * 6;
    if (p < 16) p = 16;
    return p;
}

int bomb_period() {
    int p = 34 - level * 3;
    if (p < 10) p = 10;
    return p;
}

int aliens_left() {
    int i, n = 0;
    for (i = 0; i < ncols * nrows; i++)
        if (aalive[i]) n++;
    return n;
}

/* one simulation tick: 0 = running, 1 = wave cleared, -1 = dead */
int tick() {
    int i, j, minx, maxx, landed, live, period;

    ticks++;

    /* --- march --- */
    if (ticks % march_period() == 0) {
        minx = 99999;
        maxx = -99999;
        landed = 0;
        for (i = 0; i < ncols * nrows; i++) {
            if (!aalive[i]) continue;
            if (ax[i] < minx) minx = ax[i];
            if (ax[i] > maxx) maxx = ax[i];
            if (ay[i] + AH >= SH - 46) landed = 1;
        }
        if (landed) return -1;
        if ((gdir > 0 && maxx + 34 > SW - 6) ||
            (gdir < 0 && minx < 6)) {
            gy0 += 14;
            gdir = -gdir;
            for (i = 0; i < ncols * nrows; i++)
                if (aalive[i]) ay[i] += 14;
        } else {
            gx0 += gdir * 8;
            for (i = 0; i < ncols * nrows; i++)
                if (aalive[i]) ax[i] += gdir * 8;
        }
    }

    /* --- bombs away: a live alien fires at random --- */
    if (ticks % bomb_period() == 0) {
        live = 0;
        for (i = 0; i < ncols * nrows; i++) if (aalive[i]) live++;
        if (live > 0) {
            j = rng_range(0, live - 1);
            for (i = 0; i < ncols * nrows; i++) {
                if (!aalive[i]) continue;
                if (j == 0) {
                    for (live = 0; live < MAXBOMB; live++)
                        if (!bmalive[live]) break;
                    if (live < MAXBOMB) {
                        bmalive[live] = 1;
                        bxo[live] = ax[i] + AW / 2;
                        byo[live] = ay[i] + AH;
                    }
                    break;
                }
                j--;
            }
        }
    }

    /* --- player bullets --- */
    for (i = 0; i < MAXBUL; i++) {
        if (!balive[i]) continue;
        by[i] -= 14;
        if (by[i] < HUD) { balive[i] = 0; continue; }
        for (j = 0; j < ncols * nrows; j++) {
            if (!aalive[j]) continue;
            if (iabs(bx[i] - (ax[j] + AW / 2)) <= 15 &&
                iabs(by[i] - (ay[j] + AH / 2)) <= 13) {
                aalive[j] = 0;
                balive[i] = 0;
                score += 10;
                snd_beep(520, 45);
                break;
            }
        }
    }

    /* --- bombs down --- */
    if (invuln > 0) invuln--;
    for (i = 0; i < MAXBOMB; i++) {
        if (!bmalive[i]) continue;
        byo[i] += 9;
        if (byo[i] > SH) { bmalive[i] = 0; continue; }
        if (iabs(bxo[i] - player_x) <= PW / 2 + 3 &&
            byo[i] >= SH - 46 && invuln == 0) {
            bmalive[i] = 0;
            lives--;
            invuln = 70;
            snd_beep(150, 300);
            if (lives <= 0) return -1;
        }
    }

    if (aliens_left() == 0) return 1;             /* wave cleared */
    return 0;
}

void fire() {
    int i;
    for (i = 0; i < MAXBUL; i++) {
        if (!balive[i]) {
            balive[i] = 1;
            bx[i] = player_x;
            by[i] = SH - 50;
            snd_beep(880, 25);
            return;
        }
    }
}

void draw_scene() {
    int i, c, col;

    rect(0, 0, SW, SH, COL_BG);
    for (i = 0; i < MAXSTAR; i++) {
        c = ((ticks + i * 7) / 40) % 3;
        if (c == 0) rect(sx[i], sy[i], 2, 2, COL_STAR);
        else rect(sx[i], sy[i], 1, 1, COL_STAR);
    }

    /* aliens */
    for (i = 0; i < ncols * nrows; i++) {
        if (!aalive[i]) continue;
        c = i / ncols;
        if (c > 3) c = 3;
        col = ACOL[c];
        rect(ax[i], ay[i], AW, AH, col);
        rect(ax[i] + 4, ay[i] + 5, 5, 5, COL_EYE);
        rect(ax[i] + AW - 9, ay[i] + 5, 5, 5, COL_EYE);
        rect(ax[i] + 6, ay[i] + AH - 4, AW - 12, 3, COL_EYE);
    }

    /* bullets + bombs */
    for (i = 0; i < MAXBUL; i++)
        if (balive[i]) rect(bx[i] - 1, by[i] - 6, 3, 8, COL_BULLET);
    for (i = 0; i < MAXBOMB; i++)
        if (bmalive[i]) rect(bxo[i] - 2, byo[i], 4, 9, COL_BOMB);

    /* player ship (blink while invulnerable) */
    if (invuln == 0 || (ticks / 4) % 2 == 0) {
        c = SH - 40;
        rect(player_x - PW / 2, c + 6, PW, PH - 6, COL_SHIP);
        rect(player_x - 5, c - 4, 10, 10, COL_SHIP);
        rect(player_x - 2, c - 8, 4, 6, COL_COCK);
        rect(player_x - PW / 2 - 4, c + 6, 6, 6, COL_SHIP);
        rect(player_x + PW / 2 - 2, c + 6, 6, 6, COL_SHIP);
    }

    /* HUD */
    rect(0, 0, SW, HUD, COL_BAR);
    rect(0, HUD - 2, SW, 2, COL_SHIP);
    draw_text(8, 8, "SCORE", 1, COL_TEXT);
    text_uint(50, 8, score, 1, COL_TEXT);
    draw_text(SW / 2 - 30, 8, "LIVES", 1, COL_TEXT);
    text_uint(SW / 2 + 8, 8, lives, 1, COL_TEXT);
    draw_text(SW - 84, 8, "WAVE", 1, COL_TEXT);
    text_uint(SW - 48, 8, level, 1, COL_TEXT);

    if (banner > 0)
        draw_text(SW / 2 - 60, SH / 2 - 12, "WAVE CLEAR", 2, COL_WIN);
}

int main() {
    int k, last, alive, r;

    if (gfx_init() != 0) {
        print("space: needs a 32bpp VESA mode\n");
        exit(1);
    }
    if (SW < 220 || SH < 180) {
        print("space: screen too small\n");
        exit(1);
    }

    rng_state = (gettick() ^ 0x51ace) | 1;
    world_init();
    draw_scene();

    last = gettick();
    alive = 1;
    while (alive) {
        for (;;) {
            k = pollkey();
            if (k == 0) break;
            if (k == KEY_LEFT || k == 'a' || k == 'A') {
                player_x -= 14;
                if (player_x < PW / 2 + 4) player_x = PW / 2 + 4;
            } else if (k == KEY_RIGHT || k == 'd' || k == 'D') {
                player_x += 14;
                if (player_x > SW - PW / 2 - 4) player_x = SW - PW / 2 - 4;
            } else if (k == ' ') {
                fire();
            } else if (k == 'q' || k == 'Q' || k == 27) {
                alive = 0;
                break;
            }
        }
        if (!alive) break;

        if (every(&last, 33)) {
            r = tick();
            if (r < 0) {
                alive = 0;
            } else if (r > 0) {
                level++;
                score += 50;
                snd_beep(784, 120);
                grid_spawn();                  /* next wave */
                for (k = 0; k < MAXBOMB; k++) bmalive[k] = 0;
                banner = 60;
            } else if (banner > 0) {
                banner--;
            }
            draw_scene();
        }
    }

    spk_silence();
    rect(0, 0, SW, SH, 0);
    draw_text(SW / 2 - 60, SH / 2 - 20, "GAME OVER", 2, COL_OVER);
    draw_text(SW / 2 - 60, SH / 2 + 6, "SCORE", 2, COL_TEXT);
    text_uint(SW / 2 + 4, SH / 2 + 6, score, 2, COL_TEXT);
    print("space: game over - score ");
    printint(score);
    print(" wave ");
    printint(level);
    print("\n");
    exit(0);
    return 0;
}
