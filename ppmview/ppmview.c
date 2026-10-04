/* ppmview.c — PPM image viewer (P6/P3) for Equinox OS.
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect, no
 * #include, kernel syscall builtins only — `eggkg install ppmview`
 * builds it in-OS with mtcc and drops ppmview.mrp into /bin.
 *
 *   ppmview [file.ppm]   view a Netpbm image: P6 binary or P3 ASCII
 *   ppmview sample       view the built-in 16x12 P3 sample
 *   (nothing else)       view <pkg>/src/demo.ppm, else the sample
 *
 * Controls: Q / Esc quit, + - zoom, arrows pan, I info, 0 fit.
 *
 * Rendering (route A — zero kernel changes): the file is STREAMED
 * through SYS_OPEN/SYS_READ in 3*W-byte chunks, never loaded whole
 * (the default MRP arena is only 2 MB), and painted with SYS_FILLRECT:
 * consecutive pixels sharing one colour collapse into a SINGLE
 * rectangle, so a flat image costs a few thousand syscalls instead of
 * one per pixel. A pixel budget bounds the worst case (photos):
 * FIT_PX for the automatic fit, MAX_PX once zoomed in. The HUD shows
 * progress while drawing and Q aborts a long pass.
 *
 * Nearest-neighbour mapping, division only per pixel/row:
 *   source pixel sx -> dest columns [sx*dw/w .. (sx+1)*dw/w - 1]
 *   source row   sy -> dest rows    [sy*dh/h .. (sy+1)*dh/h - 1]
 * so an upscale draws ONE rect per source run with the full vertical
 * span, and a downscale draws each destination row exactly once.
 */

/* ---- special key codes (kernel pollkey ABI) ---- */
#define KEY_UP     -1
#define KEY_DOWN   -2
#define KEY_LEFT   -3
#define KEY_RIGHT  -4

/* ---- palette (0x00RRGGBB) ---- */
#define COL_BG     0x000000
#define COL_BAR    0x0016203a      /* same HUD bar as the games      */
#define COL_ACC    0x00ffcc33
#define COL_TEXT   0x00f4f7ff
#define COL_DIM    0x008fa3c8
#define COL_PGRS   0x002ecc66

/* ---- limits ---- */
#define HUD        26              /* status bar height             */
#define CHUNK      1020            /* row chunk (3 * 340 px)        */
#define PX_CHUNK   340             /* pixels per row_fill() call    */
#define MAX_DIM    8192            /* max width/height we accept    */
#define FIT_PX     350000          /* pixel budget of the auto fit  */
#define MAX_PX     1000000         /* pixel cap once zoomed in      */
#define ZMAX       3
#define ZMIN       -2
/* sample auto-exit: wait_keys() takes MILLISECONDS, but gettick() ticks
   at 100 Hz (1 tick = 10 ms), so 15000 ms = 15 s. Keep this #define
   comment on ONE line: mtcc's preprocessor is line-oriented and does
   not strip a block comment that runs past the end of the directive. */
#define SAMPLE_MS  15000

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
    0x7F, 0x41, 0x41, 0x41, 0x1C,   /* 'D' */
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

/* ---- framebuffer ---- */
int fbi[6];
int SW, SH, AW, AH;

/* ---- byte source: fd (file) or memory (built-in sample) ---- */
int fd = -1;
int mem_src, mem_len, mem_pos;
char *mem_buf;
char rbuf[CHUNK];
int rpos, rlen, rback, rd_end, rd_pos;

/* ---- active image ---- */
char cur_path[64];
char demo_path[48] = "/equinox/.local/ppmview/src/demo.ppm";
char disp_name[48];
char typstr[4];
int p_w, p_h, p_max, p_type;
int pp_msg;                         /* 1 = galat sudah dicetak      */
int mode;                           /* 0 = file, 1 = contoh bawaan  */

/* ---- row chunk + view ---- */
char chunk[CHUNK];
int zoom, pan_x, pan_y, pan_lx, pan_ly;
int show_info;
int dw, dh, x0, y0;
int nrect;

/* ---- built-in sample (P3, generated at start-up) ---- */
char smp[4000];
int smp_len;
int SBAR[24] = { 255,0,0, 0,255,0, 0,0,255, 255,255,0,
                 255,0,255, 0,255,255, 255,255,255, 64,64,64 };

/* ---- graphics helpers ---- */

int gfx_init() {
    if (fb_info(fbi) != 0) return -1;
    if (fbi[5] == 0 || fbi[3] != 32) return -1;
    SW = fbi[1];
    SH = fbi[2];
    AW = SW;
    AH = SH - HUD;
    if (AH < 32) return -1;
    return 0;
}

void rect(int x, int y, int w, int h, int c) {
    /* fill_rect packs x|w<<16 and y|h<<16 into the 3-slot ABI */
    if (w < 1 || h < 1) return;
    if (x < 0) { w += x; x = 0; }            /* defensive: never pass  */
    if (y < 0) { h += y; y = 0; }            /* a negative coord — the */
    if (x + w > SW) w = SW - x;              /* kernel reads them as   */
    if (y + h > SH) h = SH - y;              /* uint16                 */
    if (w < 1 || h < 1) return;
    fill_rect(x | (w << 16), y | (h << 16), c);
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

int text_sint(int x, int y, int v, int scale, int color) {
    if (v < 0) {
        x = draw_text(x, y, "-", scale, color);
        v = -v;
    }
    return text_uint(x, y, v, scale, color);
}

/* ---- tiny string helpers (no libc) ---- */

void str_set(char *d, char *s) {
    int i = 0;
    while (s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

void name_of(char *d, char *s) {        /* basename */
    int i, j, st = 0;
    for (i = 0; s[i]; i++)
        if (s[i] == '/') st = i + 1;
    j = 0;
    while (s[st + j]) { d[j] = s[st + j]; j++; }
    d[j] = 0;
}

void trim(char *s) {
    int i, j, n;
    i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) {
        j = 0;
        while (s[i]) { s[j] = s[i]; i++; j++; }
        s[j] = 0;
    }
    n = 0;
    while (s[n]) n++;
    while (n > 0) {
        if (s[n - 1] != ' ' && s[n - 1] != '\t' &&
            s[n - 1] != '\n' && s[n - 1] != '\r') break;
        s[n - 1] = 0;
        n--;
    }
}

int is_word(char *s, char *w) {
    int i = 0;
    while (s[i] && w[i] && s[i] == w[i]) i++;
    if (s[i] || w[i]) return 0;
    return 1;
}

/* ---- buffered byte reader (header tokens + pixel stream) ---- */

void rd_reset() {
    rpos = 0;
    rlen = 0;
    rback = -1;
    rd_end = 0;
    rd_pos = 0;
}

int rd_getc() {
    int c, n;
    if (rback >= 0) { c = rback; rback = -1; return c; }
    if (mem_src) {
        if (mem_pos >= mem_len) return -1;
        c = mem_buf[mem_pos] & 255;
        mem_pos++;
        rd_pos++;
        return c;
    }
    if (rd_end) return -1;
    if (rpos >= rlen) {
        n = read(fd, rbuf, CHUNK);
        if (n <= 0) { rd_end = 1; return -1; }
        rlen = n;
        rpos = 0;
    }
    c = rbuf[rpos] & 255;
    rpos++;
    rd_pos++;
    return c;
}

void rd_unget(int c) {
    if (c >= 0) rback = c;
}

int rd_readn(char *dst, int n) {
    int i, take, j, k;
    i = 0;
    while (i < n) {
        if (rback >= 0) {
            dst[i] = rback;
            rback = -1;
            i++;
            rd_pos++;
            continue;
        }
        if (mem_src) {
            if (mem_pos >= mem_len) return i;
            take = mem_len - mem_pos;
            if (take > n - i) take = n - i;
            j = 0;
            while (j < take) {
                dst[i] = mem_buf[mem_pos];
                i++;
                mem_pos++;
                j++;
            }
            rd_pos += take;
            continue;
        }
        if (rd_end) return i;
        if (rpos >= rlen) {
            k = read(fd, rbuf, CHUNK);
            if (k <= 0) { rd_end = 1; return i; }
            rlen = k;
            rpos = 0;
        }
        take = rlen - rpos;
        if (take > n - i) take = n - i;
        j = 0;
        while (j < take) {
            dst[i] = rbuf[rpos];
            i++;
            rpos++;
            j++;
        }
        rd_pos += take;
    }
    return n;
}

/* next '#' runs to end of line; returns 0 and sets *out, or -1 */
int rd_int(int *out) {
    int c, v, got, neg;
    v = 0;
    got = 0;
    neg = 0;
    for (;;) {
        c = rd_getc();
        if (c < 0) { *out = 0; return -1; }
        if (c == '#') {
            while (c >= 0 && c != '\n') c = rd_getc();
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 12)
            continue;
        break;
    }
    if (c == '-') { neg = 1; c = rd_getc(); }
    else if (c == '+') c = rd_getc();
    while (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        got = 1;
        c = rd_getc();
    }
    if (!got) { *out = 0; return -1; }
    if (c >= 0) rd_unget(c);
    if (neg) v = -v;
    *out = v;
    return 0;
}

/* ---- PPM header ---- */

int pp_bad(char *m) {
    pp_msg = 1;
    print("ppmview: ");
    print(m);
    print("\n");
    return -1;
}

int pp_header() {
    int c1, c2, v, c;

    pp_msg = 0;
    p_type = 0;
    p_w = 0;
    p_h = 0;
    p_max = 0;
    typstr[0] = 'P';
    typstr[2] = 0;

    c1 = rd_getc();
    c2 = rd_getc();
    if (c1 == 'P' && (c2 == '6' || c2 == '3')) {
        p_type = c2 - '0';
    } else {
        if (c1 == 'P' && c2 >= '1' && c2 <= '6') {
            print("ppmview: format P");
            printint(c2 - '0');
            print(" tidak didukung (pakai P6/P3)\n");
            pp_msg = 1;
        } else {
            pp_bad("file ini bukan gambar PPM");
        }
        return -1;
    }
    typstr[1] = '0' + p_type;

    if (rd_int(&v) != 0 || v < 1) return pp_bad("lebar tidak valid");
    p_w = v;
    if (rd_int(&v) != 0 || v < 1) return pp_bad("tinggi tidak valid");
    p_h = v;
    if (rd_int(&v) != 0 || v < 1) return pp_bad("maxval tidak valid");
    p_max = v;

    if (p_w > MAX_DIM || p_h > MAX_DIM)
        return pp_bad("ukuran lebih dari 8192 piksel");
    if (p_type == 6 && p_max > 255)
        return pp_bad("P6 dengan maxval > 255 belum didukung");

    /* P6: exactly one whitespace ends the header (netpbm rule), so the
     * raster may legally start with a byte like 0x20 or 0x0A. */
    if (p_type == 6) {
        c = rd_getc();
        if (c == '\r') {
            c = rd_getc();
            if (c != '\n') rd_unget(c);
        } else if (c != ' ' && c != '\t' && c != '\n' && c != 12) {
            return pp_bad("header P6 rusak");
        }
    }
    return 0;
}

int pp_open(char *path) {
    int r;
    mem_src = 0;
    str_set(cur_path, path);
    if (fd >= 0) { close(fd); fd = -1; }
    fd = open(path);
    if (fd < 0) return fd;
    rd_reset();
    r = pp_header();
    if (r != 0) { close(fd); fd = -1; return r; }
    return 0;
}

int pp_sample() {
    mem_src = 1;
    mem_buf = smp;
    mem_len = smp_len;
    mem_pos = 0;
    if (fd >= 0) { close(fd); fd = -1; }
    cur_path[0] = 0;
    rd_reset();
    return pp_header();
}

int pp_reopen() {
    if (mem_src) {
        mem_pos = 0;
        rd_reset();
        return pp_header();
    }
    return pp_open(cur_path);
}

/* ---- pixel rows: one chunk at a time (never the whole file) ---- */

int row_fill(int npix) {
    int i, v, n;
    if (p_type == 6) {
        n = rd_readn(chunk, npix * 3);
        if (n < npix * 3) return -1;
        if (p_max != 255) {
            for (i = 0; i < npix * 3; i++) {
                v = (chunk[i] & 255) * 255 / p_max;
                chunk[i] = v;
            }
        }
    } else {
        for (i = 0; i < npix * 3; i++) {
            if (rd_int(&v) != 0) return -1;
            if (p_max != 255) v = v * 255 / p_max;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            chunk[i] = v;
        }
    }
    return 0;
}

int row_skip(int npix) {
    int done, take;
    done = 0;
    while (done < npix) {
        take = npix - done;
        if (take > PX_CHUNK) take = PX_CHUNK;
        if (row_fill(take) != 0) return -1;
        done += take;
    }
    return 0;
}

int render_err() {
    print("ppmview: data piksel tidak lengkap atau rusak\n");
    return -1;
}

void flush_run(int xa, int xb, int col, int y, int h) {
    if (xb - xa < 0) return;
    rect(x0 + xa, y, xb - xa + 1, h, col);
    nrect++;
}

void progress(int pct) {
    int w;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    w = SW * pct / 100;
    rect(0, HUD - 6, SW, 6, COL_BG);
    rect(0, HUD - 6, w, 6, COL_PGRS);
}

/* 0 = selesai, 1 = dibatalkan (Q), -1 = rusak */
int render() {
    int sy, sx, i, dy0, dy1, prev, yt, yb, h, k, take, vis;
    int cx0, cx1, rc0, rc1, rc, col, r, g, b, vx0, vx1, n;

    nrect = 0;
    prev = -1000000;
    for (sy = 0; sy < p_h; sy++) {
        if ((sy & 15) == 0) {
            progress(sy * 100 / p_h);
            k = pollkey();
            if (k == 'q' || k == 'Q' || k == 27) return 1;
        }

        dy0 = sy * dh / p_h;
        dy1 = (sy + 1) * dh / p_h - 1;
        vis = 0;
        if (dy1 >= dy0 && dy0 != prev) {
            prev = dy0;                     /* this dest row is taken */
            yt = y0 + dy0;
            yb = y0 + dy1;
            if (yb >= HUD && yt <= SH - 1) {
                if (yt < HUD) yt = HUD;
                if (yb > SH - 1) yb = SH - 1;
                h = yb - yt + 1;
                vis = 1;
            }
        }
        if (!vis) {
            if (row_skip(p_w) != 0) return render_err();
            continue;
        }

        vx0 = -x0;
        vx1 = SW - 1 - x0;
        rc = -1;
        rc0 = 0;
        rc1 = 0;
        sx = 0;
        while (sx < p_w) {
            take = p_w - sx;
            if (take > PX_CHUNK) take = PX_CHUNK;
            if (row_fill(take) != 0) return render_err();
            for (i = 0; i < take; i++) {
                n = i * 3;
                r = chunk[n] & 255;
                g = chunk[n + 1] & 255;
                b = chunk[n + 2] & 255;
                col = (r << 16) | (g << 8) | b;
                cx0 = (sx + i) * dw / p_w;
                cx1 = (sx + i + 1) * dw / p_w - 1;
                if (cx1 < cx0) continue;
                if (cx1 < vx0 || cx0 > vx1) continue;
                if (cx0 < vx0) cx0 = vx0;
                if (cx1 > vx1) cx1 = vx1;
                if (rc >= 0 && col == rc && cx0 == rc1 + 1) {
                    rc1 = cx1;              /* extend the run */
                    continue;
                }
                if (rc >= 0) flush_run(rc0, rc1, rc, yt, h);
                rc = col;
                rc0 = cx0;
                rc1 = cx1;
            }
            sx += take;
        }
        if (rc >= 0) flush_run(rc0, rc1, rc, yt, h);
    }
    progress(100);
    return 0;
}

/* ---- view geometry: fit -> zoom budget -> centre + pan ---- */

void compute_view() {
    int num, numw, numh, cap;

    numw = AW * 64 / p_w;
    numh = AH * 64 / p_h;
    num = numw;
    if (numh < num) num = numh;
    if (num < 1) num = 1;
    if (zoom > 0) num = num << zoom;
    if (zoom < 0) num = num >> (-zoom);
    if (num < 1) num = 1;

    cap = FIT_PX;
    if (zoom > 0) cap = MAX_PX;
    for (;;) {
        dw = p_w * num / 64;
        dh = p_h * num / 64;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        if (dw * dh <= cap) break;
        if (num <= 2) break;
        if (num > 4) num = num * 3 / 4;
        else num = num - 1;
    }

    pan_lx = (dw - SW) / 2;
    if (pan_lx < 0) pan_lx = 0;
    pan_ly = (dh - AH) / 2;
    if (pan_ly < 0) pan_ly = 0;
    if (pan_lx > 0) {
        if (pan_x > pan_lx) pan_x = pan_lx;
        if (pan_x < -pan_lx) pan_x = -pan_lx;
    } else pan_x = 0;
    if (pan_ly > 0) {
        if (pan_y > pan_ly) pan_y = pan_ly;
        if (pan_y < -pan_ly) pan_y = -pan_ly;
    } else pan_y = 0;

    x0 = (SW - dw) / 2 + pan_x;
    y0 = HUD + (AH - dh) / 2 + pan_y;
}

/* ---- HUD (drawn last, so a screendump only sees it once done) ---- */

void hud() {
    int x;
    rect(0, 0, SW, HUD, COL_BAR);
    rect(0, HUD - 2, SW, 2, COL_ACC);
    x = draw_text(8, 6, "PPMVIEW", 1, COL_ACC);
    x = draw_text(x + 6, 6, disp_name, 1, COL_TEXT);
    x = text_uint(x + 8, 6, p_w, 1, COL_DIM);
    x = draw_text(x, 6, "x", 1, COL_DIM);
    x = text_uint(x, 6, p_h, 1, COL_DIM);
    x = draw_text(x + 4, 6, typstr, 1, COL_DIM);
    x = draw_text(x + 8, 6, "to", 1, COL_DIM);
    x = text_uint(x + 6, 6, dw, 1, COL_TEXT);
    x = draw_text(x, 6, "x", 1, COL_TEXT);
    x = text_uint(x, 6, dh, 1, COL_TEXT);
    x = draw_text(x + 8, 6, "Z", 1, COL_DIM);
    x = text_sint(x + 6, 6, zoom, 1, COL_ACC);
    if (pan_x != 0 || pan_y != 0) {
        x = draw_text(x + 8, 6, "PAN", 1, COL_ACC);
    }
    if (show_info) {
        draw_text(8, 15,
                  "Q QUIT   +- ZOOM   ARROWS PAN   I INFO   0 FIT",
                  1, COL_DIM);
    }
}

/* ---- input: 1 = quit, 0 = timeout, 2 = redraw, 3 = HUD only ---- */

int wait_keys(int timeout) {
    int t0, k, st;
    t0 = gettick();
    for (;;) {
        k = pollkey();
        if (k != 0) {
            if (k == 'q' || k == 'Q' || k == 27) return 1;
            if (k == '+' || k == '=') {
                if (zoom < ZMAX) {
                    zoom++;
                    pan_x = 0;
                    pan_y = 0;
                    return 2;
                }
            } else if (k == '-' || k == '_') {
                if (zoom > ZMIN) {
                    zoom--;
                    pan_x = 0;
                    pan_y = 0;
                    return 2;
                }
            } else if (k == '0') {
                zoom = 0;
                pan_x = 0;
                pan_y = 0;
                return 2;
            } else if (k == 'i' || k == 'I') {
                show_info = 1 - show_info;
                return 3;
            } else if (k == KEY_LEFT || k == KEY_RIGHT) {
                if (pan_lx > 0) {
                    st = pan_lx / 4;
                    if (st < 16) st = 16;
                    if (k == KEY_LEFT) pan_x = pan_x - st;
                    else pan_x = pan_x + st;
                    if (pan_x > pan_lx) pan_x = pan_lx;
                    if (pan_x < -pan_lx) pan_x = -pan_lx;
                    return 2;
                }
            } else if (k == KEY_UP || k == KEY_DOWN) {
                if (pan_ly > 0) {
                    st = pan_ly / 4;
                    if (st < 16) st = 16;
                    if (k == KEY_UP) pan_y = pan_y - st;
                    else pan_y = pan_y + st;
                    if (pan_y > pan_ly) pan_y = pan_ly;
                    if (pan_y < -pan_ly) pan_y = -pan_ly;
                    return 2;
                }
            }
        }
        /* timeout is in MILLISECONDS, but gettick() counts ticks at
         * 100 Hz (timer_init(100) => 1 tick = 10 ms), so compare in
         * ticks: without the /10 a 15000 ms timeout waited 150 s. */
        if (timeout > 0 && gettick() - t0 > timeout / 10) return 0;
        sleep(20);
    }
}

/* ---- built-in 16x12 P3 sample (exercises the tokenizer) ---- */

void smp_put(int c) {
    if (smp_len < 3980) {
        smp[smp_len] = c;
        smp_len++;
    }
}

void smp_puts(char *s) {
    int i = 0;
    while (s[i]) {
        smp_put(s[i]);
        i++;
    }
}

void smp_num(int v) {
    char t[8];
    int i = 7;
    t[7] = 0;
    do {
        i--;
        t[i] = '0' + v % 10;
        v = v / 10;
    } while (v > 0 && i > 0);
    smp_puts(t + i);
    smp_put(' ');
}

void sample_build() {
    int x, y, i, v, r, g, b;
    smp_len = 0;
    smp_puts("P3\n16 12\n255\n");
    for (y = 0; y < 12; y++) {
        for (x = 0; x < 16; x++) {
            if (y < 8) {
                i = (x / 2) * 3;
                r = SBAR[i];
                g = SBAR[i + 1];
                b = SBAR[i + 2];
            } else {
                v = (x / 4) * 64;
                r = v;
                g = v;
                b = 255 - v;
            }
            smp_num(r);
            smp_num(g);
            smp_num(b);
        }
    }
}

void fail_open(char *path, int r) {
    if (pp_msg == 0) {
        print("ppmview: tidak bisa membuka ");
        print(path);
        print(" (err ");
        printint(-r);
        print(")\n");
    }
    exit(1);
}

int main() {
    char path[128];
    int r, k, rr, redo, timeout;

    if (gfx_init() != 0) {
        print("ppmview: butuh layar VESA 32bpp\n");
        exit(1);
    }

    fd = -1;
    mem_src = 0;
    mode = 0;
    zoom = 0;
    pan_x = 0;
    pan_y = 0;
    show_info = 1;
    nrect = 0;

    path[0] = 0;
    getargs(path, 120);
    trim(path);

    sample_build();                      /* siapkan contoh P3 bawaan */

    if (path[0] && !is_word(path, "sample")) {
        /* explicit path: a missing/broken file is an error, no fallback */
        mode = 0;
        r = pp_open(path);
        if (r != 0) fail_open(path, r);
    } else if (path[0]) {
        mode = 1;                          /* "ppmview sample" */
        r = pp_sample();
        if (r != 0) exit(1);
    } else {
        /* no argument: the demo shipped with the package first */
        r = pp_open(demo_path);
        if (r == -1) exit(1);              /* present but broken: say so */
        if (r == 0) {
            mode = 0;
        } else {
            mode = 1;
            r = pp_sample();
            if (r != 0) exit(1);
        }
    }

    if (mode == 1) str_set(disp_name, "SAMPLE");
    else name_of(disp_name, cur_path);

    timeout = 0;
    if (mode == 1) timeout = SAMPLE_MS;    /* host test has no keyboard */

    redo = 1;
    for (;;) {
        if (redo) {
            compute_view();
            rect(0, 0, SW, SH, COL_BG);
            rr = render();
            if (rr < 0) exit(1);
            hud();
            if (rr > 0) {
                print("ppmview: render dibatalkan\n");
                exit(0);
            }
            redo = 0;
        }
        k = wait_keys(timeout);
        if (k == 1 || k == 0) break;
        if (k == 3) { hud(); continue; }
        if (k == 2) {
            if (pp_reopen() != 0) {
                print("ppmview: gagal membuka ulang sumber\n");
                exit(1);
            }
            redo = 1;
        }
    }

    print("ppmview: gambar ");
    printint(p_w);
    print("x");
    printint(p_h);
    print(" ");
    print(typstr);
    print(" -> ");
    printint(dw);
    print("x");
    printint(dh);
    print(", ");
    printint(nrect);
    print(" rect\n");
    print("ppmview: selesai\n");
    exit(0);
    return 0;
}
