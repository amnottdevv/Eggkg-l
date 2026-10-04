/* wolf.c — WOLF: a ray-cast maze shooter in the spirit of the early
 * 90s FPS (Equinox OS).
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect — no
 * #include, no float, no struct, no switch. Everything is integer:
 * positions are 1/64 map units, view vectors are 1024ths, and the
 * DDA grid walk measures perpendicular distance in world units, so a
 * 1366-column frame is one division per column and nothing else.
 *
 * Held keys come from key_event() (syscall 32: press AND release) —
 * pollkey() only ever reports presses, so "hold W to walk" cannot be
 * built on it. A per-column z-buffer clips the enemy billboards.
 *
 * Controls: W/S or Up/Down = walk, A/D or Left/Right = turn,
 *           Space = fire, Q or Esc = quit.
 */

/* ---- special key codes (kernel key_event / pollkey ABI) ---- */
#define KEY_UP     -1
#define KEY_DOWN   -2
#define KEY_LEFT   -3
#define KEY_RIGHT  -4

/* ---- palette (0x00RRGGBB) ---- */
#define COL_CEIL   0x000a0a16
#define COL_FLOOR  0x00231a30
#define COL_BAR    0x002a1638
#define COL_TEXT   0x00f4f7ff
#define COL_W1     0x009a7a55      /* stone   */
#define COL_W2     0x00b8513f      /* brick   */
#define COL_W3     0x004a7ab0      /* metal   */
#define COL_EN     0x00d8334f      /* enemy   */
#define COL_ENH    0x00ff9aa8      /* enemy head */
#define COL_EYE    0x00101820
#define COL_HP     0x00e03a3a
#define COL_HPOFF  0x003a1418
#define COL_CROSS  0x00ffffff
#define COL_OVER   0x00ff6b6b
#define COL_WIN    0x006bff9c

#define MAPW       16
#define MAPH       16
#define NMAP       256           /* MAPW * MAPH                     */
#define ONE        1024          /* view-vector scale (1.0 = 1024)  */
#define CELL       64            /* world units per map cell        */
#define MAXE       6             /* enemies                         */
#define NZ         1600          /* z-buffer columns (>= screen W)  */
#define PERIOD     33            /* ms per frame                    */
#define HUD        24            /* score bar height                */
#define RCOS       1020          /* cos(4 deg) * 1024               */
#define RSIN       71            /* sin(4 deg) * 1024               */
#define MARGIN     20            /* body radius for wall tests      */

/* ---- state ---- */
int fbi[6];
int SW, SH;
int map[NMAP];
int zbuf[NZ];
int px, py;                      /* position (world units)          */
int ddx, ddy;                    /* view dir, ONE-scaled            */
int plx, ply;                    /* camera plane, ONE-scaled        */
int hp, score, kills, ticks;
int ex[MAXE], ey[MAXE], ehp[MAXE], ealive[MAXE];
int stx[MAXE], sty[MAXE];        /* sprite transforms (draw_sprites)*/
int sdone[MAXE];
int kfwd, kback, kleft, kright;  /* held keys                       */
int want_fire, alive;
int ai_last, hit_last, shot_last;
int spawn_x[6] = { 3, 8, 13, 7, 3, 12 };
int spawn_y[6] = { 3, 3, 7, 13, 13, 14 };
int muzzle;                      /* frames left of the muzzle flash */

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

/* shade a colour by k/256 (k = 0..256) */
int shade_col(int base, int k) {
    int r, g, b;
    if (k < 0) k = 0;
    if (k > 256) k = 256;
    r = (((base >> 16) & 255) * k) >> 8;
    g = (((base >> 8) & 255) * k) >> 8;
    b = ((base & 255) * k) >> 8;
    return (r << 16) | (g << 8) | b;
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

/* fixed-timestep gate: fires at most every `period` ms */
int every(int* last, int period) {
    int now = gettick();
    if (now - *last >= period) {
        *last = now;
        return 1;
    }
    return 0;
}

/* ---- map ---- */

int solid_cell(int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= MAPW || cy >= MAPH) return 1;
    return map[cy * MAPW + cx] != 0;
}

int solid_at(int wx, int wy) {
    return solid_cell(wx >> 6, wy >> 6);
}

/* is a body at (wx,wy) clear of walls? (4 corner samples) */
int walkable(int wx, int wy) {
    if (solid_at(wx - MARGIN, wy - MARGIN)) return 0;
    if (solid_at(wx + MARGIN, wy - MARGIN)) return 0;
    if (solid_at(wx - MARGIN, wy + MARGIN)) return 0;
    if (solid_at(wx + MARGIN, wy + MARGIN)) return 0;
    return 1;
}

void box(int x0, int y0, int w, int h, int t) {
    int x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            if (x >= 0 && y >= 0 && x < MAPW && y < MAPH)
                map[y * MAPW + x] = t;
}

void map_init() {
    int i;
    for (i = 0; i < NMAP; i++) map[i] = 0;
    for (i = 0; i < MAPW; i++) {                 /* border */
        map[i] = 1;
        map[(MAPH - 1) * MAPW + i] = 1;
        map[i * MAPW] = 1;
        map[i * MAPW + MAPW - 1] = 1;
    }
    box(5, 1, 1, 6, 2);                          /* west wall, door y7-8  */
    box(5, 9, 1, 6, 2);
    box(10, 1, 1, 5, 2);                         /* east wall, door y6-7  */
    box(10, 8, 1, 7, 2);
    box(6, 5, 2, 1, 1);                          /* spine, door x=8-9    */
    box(2, 7, 3, 1, 1);                          /* west spur             */
    box(3, 11, 2, 2, 3);                         /* pillars               */
    box(12, 3, 2, 2, 3);
    box(7, 9, 2, 2, 3);
}

int wall_col(int t) {
    if (t == 2) return COL_W2;
    if (t == 3) return COL_W3;
    return COL_W1;
}

/* ---- input: held keys via key_event() (press AND release) ---- */

void input() {
    int ev, press, code;
    ev = key_event();
    while (ev != 0) {
        press = (ev >> 16) & 1;
        code = (ev << 16) >> 16;
        if (code == KEY_UP    || code == 'w') kfwd = press;
        else if (code == KEY_DOWN  || code == 's') kback = press;
        else if (code == KEY_LEFT  || code == 'a') kleft = press;
        else if (code == KEY_RIGHT || code == 'd') kright = press;
        else if (press && (code == ' ' || code == 'f')) want_fire = 1;
        else if (press && (code == 'q' || code == 27)) alive = 0;
        ev = key_event();
    }
}

/* ---- movement ---- */

void turn(int s) {                 /* s = -1 left, +1 right */
    int nx, ny;
    nx = (ddx * RCOS - s * ddy * RSIN) >> 10;
    ny = (s * ddx * RSIN + ddy * RCOS) >> 10;
    ddx = nx;
    ddy = ny;
    nx = (plx * RCOS - s * ply * RSIN) >> 10;
    ny = (s * plx * RSIN + ply * RCOS) >> 10;
    plx = nx;
    ply = ny;
}

void walk(int fwd, int str) {
    int sx, sy, nx, ny;
    sx = -ddy;                     /* strafe axis (perpendicular)  */
    sy = ddx;
    nx = px + (ddx * fwd + sx * str) / ONE;
    ny = py + (ddy * fwd + sy * str) / ONE;
    if (walkable(nx, py)) px = nx;
    if (walkable(px, ny)) py = ny;
}

/* ---- rendering ---- */

void blit_col(int x, int y, int h, int col, int ty) {
    if (x < 0 || x >= SW || x >= NZ) return;
    if (ty >= zbuf[x]) return;                  /* behind a wall */
    if (h < 1) return;
    if (y < 0) { h = h + y; y = 0; }
    if (y + h > SH) h = SH - y;
    if (h > 0) rect(x, y, 1, h, col);
}

void draw_floor() {
    int half, band, y0, y1, k;
    half = SH >> 1;
    rect(0, 0, SW, half, COL_CEIL);
    for (band = 0; band < 12; band++) {
        y0 = half + (band * (SH - half)) / 12;
        y1 = half + ((band + 1) * (SH - half)) / 12;
        k = 70 + band * 16;
        rect(0, y0, SW, y1 - y0, shade_col(COL_FLOOR, k));
    }
}

void render() {
    int x, mapx, mapy, stepx, stepy, side, iter, hit;
    int rdx, rdy, cam, adx, ady, dxs, dys, sdx, sdy;
    int perp, lineh, y0, y1, col, k, dist;

    draw_floor();

    for (x = 0; x < SW; x++) {
        if (x >= NZ) break;
        cam = (2 * x * ONE) / SW - ONE;
        rdx = ddx + (plx * cam) / ONE;
        rdy = ddy + (ply * cam) / ONE;
        if (rdx == 0) rdx = 1;                   /* never divide by 0 */
        if (rdy == 0) rdy = 1;

        mapx = px >> 6;
        mapy = py >> 6;
        if (rdx < 0) stepx = -1; else stepx = 1;
        if (rdy < 0) stepy = -1; else stepy = 1;
        adx = rdx < 0 ? -rdx : rdx;
        ady = rdy < 0 ? -rdy : rdy;

        /* t units = world units; a full cell of travel costs 64 */
        dxs = (CELL * ONE) / adx;
        dys = (CELL * ONE) / ady;
        if (rdx < 0) sdx = ((px - (mapx << 6)) * ONE) / adx;
        else sdx = ((((mapx + 1) << 6) - px) * ONE) / adx;
        if (rdy < 0) sdy = ((py - (mapy << 6)) * ONE) / ady;
        else sdy = ((((mapy + 1) << 6) - py) * ONE) / ady;

        hit = 0;
        side = 0;
        for (iter = 0; iter < 48; iter++) {
            if (sdx < sdy) {
                sdx = sdx + dxs;
                mapx = mapx + stepx;
                side = 0;
            } else {
                sdy = sdy + dys;
                mapy = mapy + stepy;
                side = 1;
            }
            if (mapx < 0 || mapy < 0 || mapx >= MAPW || mapy >= MAPH)
                break;
            if (map[mapy * MAPW + mapx] != 0) {
                hit = 1;
                break;
            }
        }
        if (!hit) {
            zbuf[x] = 999999;
            continue;
        }
        if (side == 0) perp = sdx - dxs;
        else perp = sdy - dys;
        if (perp < 1) perp = 1;
        zbuf[x] = perp;

        lineh = (SH * CELL) / perp;
        y0 = (SH >> 1) - (lineh >> 1);
        y1 = (SH >> 1) + (lineh >> 1);
        if (y0 < 0) y0 = 0;
        if (y1 > SH) y1 = SH;
        if (y1 <= y0) continue;

        col = wall_col(map[mapy * MAPW + mapx]);
        dist = perp >> 6;
        k = 250 - dist * 14;
        if (k < 45) k = 45;
        if (side == 1) k = k * 3 / 4;
        rect(x, y0, 1, y1 - y0, shade_col(col, k));
    }
}

void draw_enemy(int i) {
    int ty, tx, screenx, hgt, y0, y1, x0, x1, x, mid, k, dist;
    int col, hcol, ew, eh, ey0;

    tx = stx[i];
    ty = sty[i];
    if (ty < 8) return;
    screenx = (SW >> 1) + ((SW >> 1) * tx) / ty;
    hgt = ((SH * CELL) / ty) * 3 / 4;
    if (hgt < 5) return;
    if (hgt > SH * 6) hgt = SH * 6;
    y0 = (SH >> 1) - (hgt >> 1);
    y1 = y0 + hgt;
    if (y0 < 0) y0 = 0;
    if (y1 > SH) y1 = SH;
    if (y1 <= y0) return;
    x0 = screenx - (hgt >> 1);
    x1 = screenx + (hgt >> 1);

    dist = ty >> 6;
    k = 250 - dist * 14;
    if (k < 45) k = 45;
    hcol = shade_col(COL_ENH, k);
    col = shade_col(COL_EN, k);
    mid = y0 + (y1 - y0) / 3;

    for (x = x0; x < x1; x++)
        blit_col(x, y0, mid - y0, hcol, ty);
    for (x = x0; x < x1; x++)
        blit_col(x, mid, y1 - mid, col, ty);

    /* two eyes, z-tested exactly like the body */
    ew = (x1 - x0) / 7;
    if (ew < 2) ew = 2;
    eh = (y1 - y0) / 14;
    if (eh < 2) eh = 2;
    ey0 = y0 + (y1 - y0) / 5;
    for (x = screenx - (x1 - x0) / 4 - ew; x < screenx - (x1 - x0) / 4; x++)
        blit_col(x, ey0, eh, COL_EYE, ty);
    for (x = screenx + (x1 - x0) / 4; x < screenx + (x1 - x0) / 4 + ew; x++)
        blit_col(x, ey0, eh, COL_EYE, ty);
}

void draw_sprites() {
    int i, j, den, sxr, syr, best, bi;

    for (i = 0; i < MAXE; i++) {
        stx[i] = 0;
        sty[i] = 0;
        sdone[i] = 0;
        if (!ealive[i]) continue;
        den = (plx * ddy - ddx * ply) / ONE;
        if (den == 0) den = 1;
        sxr = ex[i] - px;
        syr = ey[i] - py;
        stx[i] = (ddy * sxr - ddx * syr) / den;
        sty[i] = ((-ply) * sxr + plx * syr) / den;
    }

    for (j = 0; j < MAXE; j++) {
        best = -1;
        bi = -1;
        for (i = 0; i < MAXE; i++) {
            if (sdone[i]) continue;
            if (sty[i] <= 0 && ealive[i]) { sdone[i] = 1; continue; }
            if (!ealive[i]) { sdone[i] = 1; continue; }
            if (best < 0 || sty[i] > best) {
                best = sty[i];
                bi = i;
            }
        }
        if (bi < 0) break;
        sdone[bi] = 1;
        draw_enemy(bi);
    }
}

int enemies_left() {
    int i, n = 0;
    for (i = 0; i < MAXE; i++) if (ealive[i]) n++;
    return n;
}

void hud() {
    int w;
    rect(0, 0, SW, HUD, COL_BAR);
    rect(0, HUD - 2, SW, 2, COL_TEXT);
    draw_text(10, 8, "HP", 1, COL_TEXT);
    rect(28, 8, 102, 10, COL_HPOFF);
    w = hp;
    if (w > 100) w = 100;
    if (w < 0) w = 0;
    rect(29, 9, w, 8, COL_HP);
    draw_text(144, 8, "SCORE", 1, COL_TEXT);
    text_uint(186, 8, score, 1, COL_TEXT);
    draw_text(SW - 174, 8, "ENEMY", 1, COL_TEXT);
    text_uint(SW - 132, 8, enemies_left(), 1, COL_TEXT);
    /* crosshair */
    rect((SW >> 1) - 8, SH >> 1, 17, 2, COL_CROSS);
    rect(SW >> 1, (SH >> 1) - 8, 2, 17, COL_CROSS);
    if (muzzle > 0)
        rect((SW >> 1) - 3, (SH >> 1) + 12, 6, 6, 0x00ffe066);
}

/* ---- combat ---- */

void shoot() {
    int t, hx, hy, i;
    if (!every(&shot_last, 260)) return;
    want_fire = 0;
    muzzle = 3;
    snd_beep(420, 40);
    for (t = 40; t < 1500; t += 8) {
        hx = px + (ddx * t) / ONE;
        hy = py + (ddy * t) / ONE;
        if (solid_at(hx, hy)) return;            /* hit the wall first */
        for (i = 0; i < MAXE; i++) {
            if (!ealive[i]) continue;
            if (iabs(hx - ex[i]) < 30 && iabs(hy - ey[i]) < 30) {
                ehp[i]--;
                if (ehp[i] <= 0) {
                    ealive[i] = 0;
                    kills++;
                    score += 100;
                    snd_beep(760, 90);
                } else {
                    snd_beep(560, 45);
                }
                return;
            }
        }
    }
}

int try_step(int i, int sx, int sy) {
    int nx, ny;
    if (sx == 0 && sy == 0) return 0;
    nx = ex[i] + sx * 8;
    ny = ey[i] + sy * 8;
    if (!walkable(nx, ny)) return 0;
    ex[i] = nx;
    ey[i] = ny;
    return 1;
}

/* greedy chase with an axis fallback so a corner never traps them */
void enemies_ai() {
    int i, dx, dy;
    if (!every(&ai_last, 180)) return;
    for (i = 0; i < MAXE; i++) {
        if (!ealive[i]) continue;
        dx = px - ex[i];
        dy = py - ey[i];
        if (iabs(dx) + iabs(dy) <= 40) continue;   /* in contact range */
        if (iabs(dx) > iabs(dy)) {
            if (!try_step(i, dx > 0 ? 1 : -1, 0))
                try_step(i, 0, dy > 0 ? 1 : -1);
        } else {
            if (!try_step(i, 0, dy > 0 ? 1 : -1))
                try_step(i, dx > 0 ? 1 : -1, 0);
        }
    }
}

/* contact damage on a slower gate; -1 = player died */
int contact() {
    int i, dx, dy, hurt;
    if (!every(&hit_last, 500)) return 0;
    hurt = 0;
    for (i = 0; i < MAXE; i++) {
        if (!ealive[i]) continue;
        dx = px - ex[i];
        dy = py - ey[i];
        if (iabs(dx) <= 44 && iabs(dy) <= 44) {
            hp -= 12;
            hurt = 1;
        }
    }
    if (hurt) snd_beep(130, 140);
    if (hp <= 0) {
        hp = 0;
        return -1;
    }
    return 0;
}

void world_init() {
    int i;

    map_init();
    px = 2 * CELL + 32;
    py = 2 * CELL + 32;
    ddx = ONE;
    ddy = 0;
    plx = 0;
    ply = 683;                     /* 0.667 * ONE -> ~66 deg FOV */
    hp = 100;
    score = 0;
    kills = 0;
    ticks = 0;
    kfwd = kback = kleft = kright = 0;
    want_fire = 0;
    alive = 1;
    muzzle = 0;
    ai_last = hit_last = shot_last = 0;

    for (i = 0; i < MAXE; i++) {
        ex[i] = spawn_x[i] * CELL + 32;
        ey[i] = spawn_y[i] * CELL + 32;
        ehp[i] = 2;
        ealive[i] = 1;
        if (walkable(ex[i], ey[i]) == 0) {   /* never start inside a wall */
            ex[i] = 12 * CELL + 32;
            ey[i] = 13 * CELL + 32;
        }
    }
}

int main() {
    int last, r;

    if (gfx_init() != 0) {
        print("wolf: needs a 32bpp VESA mode\n");
        exit(1);
    }
    if (SW < 320 || SH < 200) {
        print("wolf: screen too small\n");
        exit(1);
    }

    world_init();

    last = gettick();
    while (alive) {
        input();
        if (!alive) break;

        if (every(&last, PERIOD)) {
            ticks++;
            if (kleft) turn(-1);
            if (kright) turn(1);
            if (kfwd) walk(9, 0);
            if (kback) walk(-9, 0);
            if (want_fire) shoot();
            if (muzzle > 0) muzzle--;

            enemies_ai();
            r = contact();

            render();
            hud();

            if (r < 0) {
                alive = 0;
            } else if (enemies_left() == 0) {
                alive = 0;
                score += 250;
            }
        }
    }

    spk_silence();
    rect(0, 0, SW, SH, 0);
    if (hp <= 0) {
        draw_text((SW >> 1) - 60, (SH >> 1) - 24, "YOU DIED", 2, COL_OVER);
        print("wolf: you died - score ");
    } else {
        draw_text((SW >> 1) - 66, (SH >> 1) - 24, "LEVEL CLEAR", 2,
                  COL_WIN);
        print("wolf: level clear - score ");
    }
    draw_text((SW >> 1) - 60, (SH >> 1) + 6, "SCORE", 2, COL_TEXT);
    text_uint((SW >> 1) + 4, (SH >> 1) + 6, score, 2, COL_TEXT);
    printint(score);
    print(" kills ");
    printint(kills);
    print("\n");
    exit(0);
    return 0;
}
