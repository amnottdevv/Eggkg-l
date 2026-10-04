/* sysmon.c — interactive system monitor TUI for Equinox OS.
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect, only
 * <multitasking.h> spliced from libc — `eggkg install sysmon`
 * builds it in-OS and drops sysmon.mrp into /bin.
 *
 *   sysmon                 live view; UP/DOWN select a row, k kills
 *                          that task, space pauses, r refreshes, q quits
 *   sysmon -list-task      print the task table once and exit
 *   sysmon -kill PID       kill task by pid and exit
 *   sysmon -spawn PATH -args "ARGS"   spawn a new task and exit
 *   sysmon -h              help
 *
 * Rendering uses the kernel console's ANSI subset (ESC[H to re-home,
 * ESC[K to clear to end-of-line, SGR for colours) so the view updates
 * in place without flicker; no framebuffer drawing needed.
 *
 * Data comes from the v0.4 accounting syscalls:
 *   mem_info(w[6]) -> {pool total KB, pool free KB, faulted user KB,
 *                      live tasks, zombies, reserved}
 *   task_list(out) -> out[0]=count, then per-task {pid,state,kind,console}
 *   task_kill(pid), task_spawn_args(path, hint, args)
 */
#include <multitasking.h>

#define MAX_TASKS 16

/* special key codes (kernel pollkey ABI) */
#define KEY_UP     -1
#define KEY_DOWN   -2
#define KEY_LEFT   -3
#define KEY_RIGHT  -4

char esc[3];           /* "ESC [" prefix for every sequence */
int info[65];          /* 1 + 16 tasks * 4 cols (literal: mtcc arrays
                          need literal sizes)                          */
int w[6];
int paused;
int tick_prev;
int sel;               /* highlighted row in the TUI */

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

/* print the task table rows; rows[sel_row] is highlighted (sel_row<0 = none) */
void print_table(int n, int sel_row) {
    int i;
    ant("90m");
    print("  PID   STATE  KIND    CON\n");
    ant("0m");
    for (i = 0; i < n; i++) {
        int pid, st, kd, co;
        pid = info[1 + i * 4 + 0];
        st  = info[1 + i * 4 + 1];
        kd  = info[1 + i * 4 + 2];
        co  = info[1 + i * 4 + 3];
        if (i == sel_row) {
            ant("96;1m");
            print("> ");
        } else {
            print("  ");
        }
        if (pid < 10) print(" ");
        printint(pid);
        print("   ");
        state_color(st);
        print(state_name(st));
        ant("0m");
        if (i == sel_row) ant("96;1m");
        print(" ");
        print(kind_name(kd));
        print(" ");
        printint(co);
        ant("0m");
        print("\n");
    }
}

/* tiny atoi for -kill; returns 0 if the string is not a positive integer */
int to_int(char* s) {
    int v;
    int i;
    v = 0;
    i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        i++;
    }
    return v;
}

/* split args into tokens in tokbuf (8 slots * 64 bytes, row-major) */
int split(char* args, char* tokbuf) {
    int n;
    int i;
    int ti;
    n = 0;
    i = 0;
    while (args[i] && n < 8) {
        while (args[i] == ' ') i++;
        if (!args[i]) break;
        ti = 0;
        while (args[i] && args[i] != ' ' && ti < 62) {
            tokbuf[n * 64 + ti] = args[i];
            ti++;
            i++;
        }
        tokbuf[n * 64 + ti] = 0;
        n++;
    }
    return n;
}

/* return a pointer to token k inside tokbuf */
char* tok(char* tokbuf, int k) {
    return tokbuf + k * 64;
}

int str_eq(char* a, char* b) {
    int i;
    i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

/* ---------------- subcommand handlers ---------------- */

void do_help(void) {
    print("sysmon - Equinox OS system monitor\n");
    print("  sysmon                 live TUI (arrows+k+q)\n");
    print("  sysmon -list-task      one-shot task table\n");
    print("  sysmon -kill PID       kill task\n");
    print("  sysmon -spawn P -args \"A\"  spawn task\n");
}

int do_list_task(void) {
    int n;
    n = task_list(info);
    if (n < 0) n = 0;
    if (n > MAX_TASKS) n = MAX_TASKS;
    print_table(n, -1);
    return 0;
}

int do_kill(int pid) {
    int r;
    r = task_kill(pid);
    if (r == 0) {
        print("sysmon: killed pid ");
        printint(pid);
        print("\n");
        return 0;
    }
    print("sysmon: kill failed for pid ");
    printint(pid);
    print("\n");
    return 1;
}

/* search tokens i+1 .. n_tok-1 for "-args VALUE"; returns VALUE or "" */
char* find_args(char* tokbuf, int n_tok, int start) {
    int j;
    for (j = start; j + 1 < n_tok; j++) {
        if (str_eq(tok(tokbuf, j), "-args")) {
            return tok(tokbuf, j + 1);
        }
    }
    return "";
}

int do_spawn(char* tokbuf, int n_tok, int i) {
    char* path;
    char* a;
    int pid;

    if (i + 1 >= n_tok || tok(tokbuf, i + 1)[0] == '-') {
        print("sysmon: -spawn needs a path\n");
        return 2;
    }
    path = tok(tokbuf, i + 1);
    a = find_args(tokbuf, n_tok, i + 2);

    pid = task_spawn_args(path, 0, a);
    if (pid > 0) {
        print("sysmon: spawned pid ");
        printint(pid);
        print("\n");
        return 0;
    }
    print("sysmon: spawn failed\n");
    return 1;
}

/* ---------------- TUI ---------------- */

void tui_loop(void) {
    int k;
    int up;
    int n;
    int used_kb;
    int first_frame;

    paused = 0;
    sel = 0;
    tick_prev = gettick();
    first_frame = 1;

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

        n = task_list(info);
        if (n < 0) n = 0;
        if (n > MAX_TASKS) n = MAX_TASKS;
        if (sel >= n) sel = n - 1;
        if (sel < 0) sel = 0;
        print_table(n, sel);
        if (n == 0) {
            ant("90m");
            print("  (no tasks)\n");
            ant("0m");
        }
        print("\n");

        /* ---- footer ---- */
        ant("90m");
        print("  [up/dn] select  [k] kill  [spc] pause  [r] refresh  [q] quit\n");
        if (paused) print("  == PAUSED ==\n");
        ant("0m");
        ant("K");

        /* ---- input ---- */
        k = pollkey();
        while (k == 0) {
            if (!paused && gettick() - tick_prev >= 300) break;
            sleep(20);
            k = pollkey();
        }

        if (k == 'q' || k == 'Q') {
            ant("2J");
            ant("H");
            print("sysmon: bye\n");
            return;
        }
        if (k == KEY_UP) {
            if (sel > 0) sel--;
        } else if (k == KEY_DOWN) {
            if (sel < n - 1) sel++;
        } else if (k == 'k' || k == 'K') {
            if (n > 0) {
                int pid;
                pid = info[1 + sel * 4 + 0];
                task_kill(pid);
            }
        } else if (k == ' ') {
            paused = !paused;
        } else if (k == 'r' || k == 'R') {
            tick_prev = 0;
        }
        tick_prev = gettick();
        first_frame = 0;
    }
}

int main() {
    char args[160];
    char tokbuf[512];
    int n_tok;
    int i;

    args[0] = 0;
    i = getargs(args, 160);
    n_tok = split(args, tokbuf);

    /* Skip leading tokens that do not look like an option.  getargs()
       may or may not include argv[0]; this handles both cases and also
       tolerates a leading path such as "/bin/sysmon". */
    i = 0;
    while (i < n_tok && tok(tokbuf, i)[0] != '-') i++;

    /* ---- non-TUI modes ---- */
    if (i < n_tok) {
        char* opt;
        opt = tok(tokbuf, i);

        if (str_eq(opt, "-h")) {
            do_help();
            return 0;
        }
        if (str_eq(opt, "-list-task")) {
            return do_list_task();
        }
        if (str_eq(opt, "-kill")) {
            if (i + 1 >= n_tok) {
                print("sysmon: -kill needs a PID\n");
                return 2;
            }
            return do_kill(to_int(tok(tokbuf, i + 1)));
        }
        if (str_eq(opt, "-spawn")) {
            return do_spawn(tokbuf, n_tok, i);
        }
        print("sysmon: unknown option '");
        print(opt);
        print("' (try -h)\n");
        return 2;
    }

    /* ---- TUI mode ---- */
    ant("2J");            /* full clear once, then re-home each frame */
    tui_loop();
    return 0;
}
