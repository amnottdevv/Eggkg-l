/* sysmon.c — interactive system monitor TUI for Equinox OS.
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect, only
 * <multitasking.h> + <fileio.h> spliced from libc (memmove-free,
 * 13-function budget) — `eggkg install sysmon` builds it in-OS and
 * drops sysmon.mrp into /bin.
 *
 *   sysmon          live system view, refreshes every 300 ms
 *                   [q] quit   [space] pause/resume   [r] force refresh
 *
 * Rendering uses the kernel console's ANSI subset (ESC[H to re-home,
 * ESC[K to clear to end-of-line, SGR for colours) so the view updates
 * in place without flicker; no framebuffer drawing needed.
 *
 * Data comes from the v0.4 accounting syscalls:
 *   meminfo(w[6])  -> {pool total KB, pool free KB, faulted user KB,
 *                      live tasks, zombies, reserved}
 *   task_list(out) -> out[0]=count, then per-task {pid,state,kind,console}
 */
#include <multitasking.h>

#define MAX_TASKS 16

char esc[3];           /* "ESC [" prefix for every sequence */
int info[65];              /* 1 + 16 tasks * 4 cols (literal: mtcc arrays */
int w[6];                  /* need literal sizes)                        */
int paused;
int tick_prev;

/* print an ANSI CSI sequence, e.g. ant("36m"), ant("H"), ant("0m") */
void ant(char* code) {
    esc[0] = 27; esc[1] = '['; esc[2] = 0;
    print(esc);
    print(code);
}

void sep(int width) {
    int i;
    for (i = 0; i < width; i++) print("-");
    print("\n");
}

/* draw a horizontal gauge: [####----------] used/total */
void gauge(int used, int total) {
    int i;
    int filled;
    if (total <= 0) total = 1;
    filled = used * 20 / total;
    if (filled < 0) filled = 0;
    if (filled > 20) filled = 20;
    print("[");
    for (i = 0; i < 20; i++) {
        if (i < filled) print("#");
        else print("-");
    }
    print("] ");
    printint(used * 100 / total);
    print("%");
}

char* state_name(int s) {
    if (s == 0) return "READY";
    if (s == 1) return "RUN  ";
    if (s == 2) return "BLOCK";
    return "DEAD ";
}

void state_color(int s) {
    if (s == 0) ant("93m");        /* bright yellow  */
    else if (s == 1) ant("92m");   /* bright green   */
    else if (s == 2) ant("94m");   /* bright blue    */
    else ant("91m");               /* bright red     */
}

char* kind_name(int k) {
    if (k == 0) return "shell ";
    if (k == 1) return "user  ";
    return "kernel";
}

int main() {
    int n;
    int i;
    int k;
    int up;
    int used_kb;

    paused = 0;
    tick_prev = gettick();

    ant("2J");            /* full clear once, then re-home each frame */
    ant("?25l");          /* hide cursor if the console honours it    */

    while (1) {
        ant("H");         /* home cursor (no scroll) */

        /* ---- header ---- */
        ant("44;97m");
        print("  SYSMON — Equinox OS  ");
        ant("0m");
        print("\n");
        sep(26);

        /* ---- system block ---- */
        ant("96m");
        print("SYSTEM\n");
        ant("0m");

        up = gettick() / 1000;
        print("  uptime   ");
        printint(up / 60);
        print("m ");
        printint(up % 60);
        print("s\n");

        if (mem_info(w) == 0) {
            used_kb = w[0] - w[1];
            print("  pool     ");
            printint(used_kb);
            print(" / ");
            printint(w[0]);
            print(" KB used\n");
            print("  heap     ");
            gauge(used_kb, w[0]);
            print("\n");
            print("  faulted  ");
            printint(w[2]);
            print(" KB touched\n");
            print("  tasks    ");
            printint(w[3]);
            print(" live, ");
            printint(w[4]);
            print(" dead\n");
        } else {
            print("  meminfo unavailable\n");
        }
        print("\n");

        /* ---- task table ---- */
        ant("96m");
        print("TASKS\n");
        ant("0m");
        ant("90m");
        print("  PID   STATE  KIND    CON\n");
        ant("0m");

        n = task_list(info);
        if (n < 0) n = 0;
        if (n > MAX_TASKS) n = MAX_TASKS;
        for (i = 0; i < n; i++) {
            int pid, st, kd, co;
            pid = info[1 + i * 4 + 0];
            st  = info[1 + i * 4 + 1];
            kd  = info[1 + i * 4 + 2];
            co  = info[1 + i * 4 + 3];
            print("  ");
            if (pid < 10) print(" ");
            printint(pid);
            print("   ");
            state_color(st);
            print(state_name(st));
            ant("0m");
            print(" ");
            print(kind_name(kd));
            print(" ");
            printint(co);
            print("\n");
        }
        if (n == 0) {
            ant("90m");
            print("  (no tasks)\n");
            ant("0m");
        }
        print("\n");

        /* ---- footer ---- */
        ant("90m");
        print("  [q] quit   [space] pause   [r] refresh\n");
        if (paused) print("  == PAUSED ==\n");
        ant("0m");
        ant("K");

        /* ---- input ---- */
        k = getkey();
        while (k == -1) {
            if (!paused && gettick() - tick_prev >= 300) break;
            sleep(20);
            k = getkey();
            if (k != -1) break;
        }
        if (k == 'q' || k == 'Q') {
            ant("2J");
            ant("H");
            ant("?25h");
            print("sysmon: bye\n");
            return 0;
        }
        if (k == ' ') paused = !paused;
        if (k == 'r' || k == 'R') tick_prev = 0;
        if (k == -1 || k == 'r' || k == 'R') tick_prev = gettick();
    }
}
