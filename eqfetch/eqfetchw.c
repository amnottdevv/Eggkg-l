/* ============================================================
 *  Equinox OS — colored "neofetch"-style screen
 *  ANSI escape sequences for VT100/ANSI terminals
 *  compile: cc -std=c99 -o neofetch neofetch.c
 * ============================================================ */

/* ---------- ANSI color helpers ---------- */
#define ESC          "\033["
#define RESET        ESC "0m"
#define BOLD         ESC "1m"
#define DIM          ESC "2m"

/* Foreground 30-37, Bright 90-97 */
#define F_BLACK      ESC "30m"
#define F_RED        ESC "31m"
#define F_GREEN      ESC "32m"
#define F_YELLOW     ESC "33m"
#define F_BLUE       ESC "34m"
#define F_MAGENTA    ESC "35m"
#define F_CYAN       ESC "36m"
#define F_WHITE      ESC "37m"
#define F_GRAY       ESC "90m"
#define F_BRED       ESC "91m"
#define F_BGREEN     ESC "92m"
#define F_BYELLOW    ESC "93m"
#define F_BBLUE      ESC "94m"
#define F_BMAGENTA   ESC "95m"
#define F_BCYAN      ESC "96m"
#define F_BWHITE     ESC "97m"

/* Background (optional) */
#define B_BLUE       ESC "44m"
#define B_BBLUE      ESC "104m"

/* 256-color gradient (kalau terminal support) */
#define C256(n)      ESC "38;5;" #n "m"

/* ------------------------------------------------------------
 * prhex — 1 byte -> "xx"
 * ------------------------------------------------------------ */
void prhex(int v) {
    char b[3];
    int d;
    d = (v >> 4) & 15;
    if (d < 10) b[0] = 48 + d; else b[0] = 87 + d;
    d = v & 15;
    if (d < 10) b[1] = 48 + d; else b[1] = 87 + d;
    b[2] = 0;
    print(b);
}

void pr2(int v) {
    if (v < 10) print("0");
    printint(v);
}

void pr_ip(int w) {
    printint(w & 255);         print(".");
    printint((w >> 8) & 255);  print(".");
    printint((w >> 16) & 255); print(".");
    printint((w >> 24) & 255);
}

/* ------------------------------------------------------------
 * pa — print dengan translasi '#' -> '"' (biar tidak bentrok)
 * ------------------------------------------------------------ */
void pa(char* s) {
    char b[80];
    int k;
    k = 0;
    while (s[k] && k < 70) {
        b[k] = s[k];
        if (b[k] == 35) b[k] = 34;
        k++;
    }
    b[k] = 0;
    print(b);
}

/* ------------------------------------------------------------
 * art — ASCII art dengan warna gradasi
 *   baris 0-4   : merah -> kuning   (atas)
 *   baris 5-10  : kuning -> hijau   (tengah atas)
 *   baris 11-16 : hijau -> cyan     (tengah bawah)
 *   baris 17-20 : cyan -> biru      (bawah)
 * ------------------------------------------------------------ */
void art(int i) {
    char* line;
    char* color;

    if      (i == 0)  { color = F_BRED;     line = "               ,,ggddY888Ybbgg,,                "; }
    else if (i == 1)  { color = F_BRED;     line = "          ,agd8##'   .d8888888888bga,           "; }
    else if (i == 2)  { color = ESC "38;5;202m"; line = "       ,gdP##'     .d88888888888888888g,        "; }
    else if (i == 3)  { color = F_BYELLOW;  line = "     ,dP#        ,d888888888888888888888b,      "; }
    else if (i == 4)  { color = F_BYELLOW;  line = "   ,dP#         ,8888888888888888888888888b,    "; }
    else if (i == 5)  { color = ESC "38;5;220m"; line = "  ,8#          ,8888888P###88888888888888888,   "; }
    else if (i == 6)  { color = F_BGREEN;   line = " ,8'           I888888I    )88888888888888888,  "; }
    else if (i == 7)  { color = F_BGREEN;   line = ",8'            `8888888booo8888888888888888888, "; }
    else if (i == 8)  { color = ESC "38;5;46m";  line = "d'              `88888888888888888888888888888b "; }
    else if (i == 9)  { color = F_BCYAN;    line = "8                `#8888888888888888888888888888 "; }
    else if (i == 10) { color = F_BCYAN;    line = "8                  `#88888888888888888888888888 "; }
    else if (i == 11) { color = ESC "38;5;51m";  line = "8                      `#8888888888888888888888 "; }
    else if (i == 12) { color = ESC "38;5;45m";  line = "Y,                        `8888888888888888888P "; }
    else if (i == 13) { color = F_BBLUE;    line = "`8,                         `88888888888888888' "; }
    else if (i == 14) { color = F_BBLUE;    line = " `8,              .oo.       `888888888888888'  "; }
    else if (i == 15) { color = ESC "38;5;33m";  line = "  `8a             8888        88888888888888'   "; }
    else if (i == 16) { color = F_BMAGENTA; line = "   `Yba           `##'       ,888888888888P'    "; }
    else if (i == 17) { color = F_BMAGENTA; line = "     #Yba                   ,88888888888'       "; }
    else if (i == 18) { color = ESC "38;5;201m"; line = "       `#Yba,             ,8888888888P#'        "; }
    else if (i == 19) { color = F_BMAGENTA; line = "          `#Y8baa,      ,d88888888P#'           "; }
    else if (i == 20) { color = F_BMAGENTA; line = "               ``##YYba8888P888#'               "; }
    else              { color = RESET;      line = "                                                "; }

    print(color);
    pa(line);
    print(RESET);
}

/* ------------------------------------------------------------
 * info — panel kanan dengan label berwarna
 * ------------------------------------------------------------ */
void info(int j, int* w, int* fb, int up) {
    int i;
    i = j - 5;

    if (i == 0) {
        print(BOLD F_BCYAN "root" F_WHITE "@" F_BGREEN "equinox" RESET);
    }
    else if (i == 1) {
        print(F_GRAY "------------" RESET);
    }
    else if (i == 2) {
        print(F_BYELLOW "OS:      " F_WHITE "Equinox OS" RESET);
    }
    else if (i == 3) {
        print(F_BYELLOW "Kernel:  " F_WHITE "Equinox-kernel-S0.4" RESET);
    }
    else if (i == 4) {
        print(F_BYELLOW "Uptime:  " F_WHITE);
        printint(up / 3600); print("h ");
        printint((up / 60) % 60); print("m ");
        printint(up % 60); print("s");
        print(RESET);
    }
    else if (i == 5) {
        print(F_BYELLOW "Shell:   " F_WHITE "equinox shell" RESET);
    }
    else if (i == 6) {
        print(F_BYELLOW "Display: " F_WHITE);
        if (fb[5]) {
            printint(fb[1]); print("x"); printint(fb[2]);
            print(F_GRAY " @ " RESET F_BGREEN);
            printint(fb[3]);
            print(F_WHITE "bpp");
        } else {
            print("VGA text");
        }
        print(RESET);
    }
    else if (i == 7) {
        print(F_BYELLOW "RAM:     " F_WHITE "256 MB " F_GRAY "(simulated)" RESET);
    }
    else if (i == 8) {
        print(F_BYELLOW "Net:     ");
        if (w[0]) {
            print(F_BGREEN);
            pr_ip(w[2]);
            print(F_GRAY);
            if (w[1]) print(" (dhcp)"); else print(" (static)");
        } else {
            print(F_BRED "down");
        }
        print(RESET);
    }
    else if (i == 9) {
        print(F_BYELLOW "MAC:     " F_BCYAN);
        prhex(w[5] & 255);         print(":");
        prhex((w[5] >> 8) & 255);  print(":");
        prhex((w[5] >> 16) & 255); print(":");
        prhex((w[5] >> 24) & 255); print(":");
        prhex(w[6] & 255);         print(":");
        prhex((w[6] >> 8) & 255);
        print(RESET);
    }
    else if (i == 10) {
        print(F_BYELLOW "Packets: " F_BGREEN "rx " F_WHITE);
        printint(w[7]);
        print(F_BRED " / tx " F_WHITE);
        printint(w[8]);
        print(RESET);
    }
    else {
        print(RESET);
    }
}

/* ------------------------------------------------------------
 * main
 * ------------------------------------------------------------ */
int main() {
    int w[10];
    int fb[6];
    int i;
    int up;

    for (i = 0; i < 10; i++) w[i] = 0;
    for (i = 0; i < 6; i++)  fb[i] = 0;

    net_info(w);
    fb_info(fb);
    up = gettick() / 100;      /* timer_init(100) => 100 tick */

    /* header kecil di atas */
    print("\n");
    print(BOLD F_BCYAN "  ╭─ " F_BGREEN "Equinox OS" F_BCYAN " ─ " F_GRAY "neofetch" F_BCYAN " ─╮" RESET "\n\n");

    for (i = 0; i < 21; i++) {
        print("  ");
        art(i);
        print("   ");
        info(i, w, fb, up);
        print("\n");
    }

    /* color palette bar (opsional, ala neofetch) */
    print("\n  ");
    print(F_BLACK  "███");
    print(F_RED    "███");
    print(F_GREEN  "███");
    print(F_YELLOW "███");
    print(F_BLUE   "███");
    print(F_MAGENTA"███");
    print(F_CYAN   "███");
    print(F_WHITE  "███");
    print(RESET  "  ");
    print(F_BBLACK ? "" : "");
    print(F_GRAY   "██████");
    print(RESET "\n\n");

    return 0;
}
