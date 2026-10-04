/* bfc.c — Brainfuck -> C transpiler for Equinox OS.
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect, no
 * struct/switch/sizeof/function pointers — `eggkg install bfc`
 * builds it in-OS with mtcc and drops bfc.mrp into /bin.
 *
 *   bfc [-o NAME] [-opt] FILE.bf  translate -> NAME.c -> NAME.mrp
 *
 * The generated C is plain mtcc dialect: a zeroed tape global, one
 * data pointer, and a wait_key() poller so ',' blocks. Brainfuck
 * loops become while-loops on the current cell. Every non-command
 * byte in the source is a comment and is dropped from the output.
 * With -opt (or -O), runs of + - > < collapse into single statements
 * (tape[ip] = (tape[ip] + 8) & 255; / ip += 2;) and the clear-loops
 * [-] / [+] become tape[ip] = 0; — same semantics, smaller code.
 */
#include <multitasking.h>

#define MAXPROG  4096    /* BF source cap (mtcc array limit)         */
#define MAXGEN   4096    /* generated C size cap                    */

char src[MAXPROG];       /* raw Brainfuck program                   */
char gen[MAXGEN];        /* generated C source                      */
char bf[MAXPROG];        /* filtered command stream (comments out)  */
char tmp[MAXPROG];       /* clear-loop pass output                  */
char ob[MAXPROG];        /* grouped commands (after -opt)           */
int  on[MAXPROG];        /* run lengths for ob[]                    */
int  nsrc;
int  ngen;
int  nbf;
int  non;
int  opt;               /* 1 = run -opt optimizations              */

/* small unsigned itoa (no libc splice in bfc itself) */
int nstr(int v, char* b) {
    char rev[12];
    int i;
    int k;
    if (v == 0) { b[0] = '0'; b[1] = 0; return 1; }
    i = 0;
    while (v > 0) { rev[i] = '0' + (v % 10); v = v / 10; i++; }
    k = 0;
    while (i > 0) { i--; b[k] = rev[i]; k++; }
    b[k] = 0;
    return k;
}

/* append one char to gen[]; 1 ok / 0 overflow */
int putc_gen(char c) {
    if (ngen >= MAXGEN - 1) return 0;
    gen[ngen] = c;
    ngen++;
    return 1;
}

/* append a string literal to gen[] */
int puts_gen(char* s) {
    int i;
    for (i = 0; s[i]; i++) {
        if (!putc_gen(s[i])) return 0;
    }
    return 1;
}

int main() {
    char args[160];
    char path[64];
    char out[64];
    int n;
    int i;
    int pn;
    char c;

    /* args: bfc [-h] [-o OUTBASE] FILE.bf  (first token "bfc" skipped) */
    n = getargs(args, 160);
    path[0] = 0;
    out[0] = 0;
    i = 0;
    while (i < n) {
        char tok[64];
        int want_out;
        int ti;

        while (i < n && args[i] == ' ') i++;
        if (i >= n) break;
        ti = 0;
        while (i < n && args[i] != ' ' && ti < 62) { tok[ti] = args[i]; ti++; i++; }
        tok[ti] = 0;

        if (ti == 3 && tok[0] == 'b' && tok[1] == 'f' && tok[2] == 'c') continue;

        if (ti == 2 && tok[0] == '-' && tok[1] == 'h') {
            print("bfc - Brainfuck to C transpiler (Equinox OS)\n");
            print("usage: bfc [-h] [-o OUTBASE] FILE.bf\n");
            print("  FILE.bf   Brainfuck source\n");
            print("  -o NAME   output base name (default: input basename);\n");
            print("            writes NAME.c, then NAME.mrp via mtcc\n");
            print("  -opt      optimize: collapse runs of + - > < and [-]/[+]\n");
            print("  -h        this help\n");
            return 0;
        }
        if (ti == 4 && tok[0] == '-' && tok[1] == 'o' && tok[2] == 'p' && tok[3] == 't') {
            opt = 1;
            continue;
        }
        if (ti == 2 && tok[0] == '-' && tok[1] == 'O') {
            opt = 1;
            continue;
        }
        if (ti == 2 && tok[0] == '-' && tok[1] == 'o') {
            /* next token is the output base name */
            want_out = 1;
            while (i < n && args[i] == ' ') i++;
            ti = 0;
            while (i < n && args[i] != ' ' && ti < 62) { out[ti] = args[i]; ti++; i++; }
            out[ti] = 0;
            if (ti == 0) {
                print("bfc: -o needs a name\n");
                return 2;
            }
            continue;
        }
        if (ti > 0 && tok[0] == '-') {
            print("bfc: unknown option '"); print(tok); print("' (try -h)\n");
            return 2;
        }
        if (path[0] == 0) {
            pn = 0;
            i = 0;
            while (tok[i] && pn < 62) { path[pn] = tok[i]; pn++; i++; }
            path[pn] = 0;
        } else {
            print("bfc: too many arguments (try -h)\n");
            return 2;
        }
    }

    if (path[0] == 0) {
        print("usage: bfc [-h] [-o OUTBASE] FILE.bf\n");
        return 2;
    }

    n = file_size(path);
    if (n <= 0 || n >= MAXPROG) {
        print("bfc: cannot read '"); print(path); print("' (or too big)\n");
        return 1;
    }
    n = file_read_all(path, src, MAXPROG - 1);
    if (n <= 0) {
        print("bfc: cannot read '"); print(path); print("'\n");
        return 1;
    }
    nsrc = n;

    /* ---- syntax check: balanced [ ], at least one command ---- */
    {
        int depth;
        int first_open;   /* position of the oldest unclosed '[' */
        int cmds;
        depth = 0;
        first_open = -1;
        cmds = 0;
        i = 0;
        while (i < nsrc) {
            c = src[i];
            i++;
            if (c == '>' || c == '<' || c == '+' || c == '-' ||
                c == '.' || c == ',') cmds++;
            else if (c == '[') {
                if (depth == 0) first_open = i - 1;
                depth++;
            } else if (c == ']') {
                depth--;
                if (depth < 0) {
                    print("bfc: syntax error: ']' at byte ");
                    printint(i - 1);
                    print(" has no matching '['\n");
                    return 1;
                }
                if (depth == 0) first_open = -1;
            }
        }
        if (depth > 0) {
            print("bfc: syntax error: unclosed '[' at byte ");
            printint(first_open);
            print(" (missing ']')\n");
            return 1;
        }
        if (cmds == 0) {
            print("bfc: syntax error: no Brainfuck commands found\n");
            return 1;
        }
    }

    /* output path: -o OUTBASE if given, else input basename */
    if (out[0]) {
        pn = 0;
        while (out[pn] && pn < 60) pn++;
        /* append ".c" unless already there */
        if (!(pn >= 2 && out[pn-2] == '.' && out[pn-1] == 'c')) {
            out[pn] = '.'; out[pn+1] = 'c'; out[pn+2] = 0;
        }
    } else {
        pn = 0;
        i = 0;
        while (path[i] && pn < 62) { out[pn] = path[i]; pn++; i++; }
        /* walk back to the last dot, if any */
        i = pn - 1;
        while (i >= 0 && out[i] != '.') i--;
        if (i > 0) pn = i;
        out[pn] = '.'; out[pn+1] = 'c'; out[pn+2] = 0;
    }

    /* ---- build the filtered command stream (comments dropped) ---- */
    nbf = 0;
    i = 0;
    while (i < nsrc) {
        c = src[i];
        i++;
        if (c == '>' || c == '<' || c == '+' || c == '-' ||
            c == '.' || c == ',' || c == '[' || c == ']') {
            bf[nbf] = c;
            nbf++;
        }
    }

    /* ---- optimize (-opt): [-] / [+] -> 'z', then run-length pack */
    if (opt) {
        int k;
        k = 0;
        i = 0;
        while (i < nbf) {
            if (i + 2 < nbf && bf[i] == '[' &&
                (bf[i+1] == '-' || bf[i+1] == '+') && bf[i+2] == ']') {
                tmp[k] = 'z';
                k++;
                i += 3;
            } else {
                tmp[k] = bf[i];
                k++;
                i++;
            }
        }
        non = 0;
        i = 0;
        while (i < k) {
            int j;
            c = tmp[i];
            j = i;
            if (c == '+' || c == '-' || c == '>' || c == '<') {
                while (j < k && tmp[j] == c) j++;
            } else {
                j = i + 1;
            }
            ob[non] = c;
            on[non] = j - i;
            non++;
            i = j;
        }
    } else {
        non = 0;
        i = 0;
        while (i < nbf) {
            ob[non] = bf[i];
            on[non] = 1;
            non++;
            i++;
        }
    }

    /* ---- generated C prologue ---- */
    ngen = 0;
    puts_gen("/* generated by bfc from ");
    puts_gen(path);
    puts_gen(" — do not edit by hand */\n");
    puts_gen("#include <stdio.h>\n");
    puts_gen("char tape[4096];\n");
    puts_gen("int  ip;\n");
    puts_gen("int wait_key(void) { int c; c=getkey(); while (c==-1) { sleep(20); c=getkey(); } return c; }\n");
    puts_gen("int main() {\n");
    puts_gen("    ip = 0;\n");

    i = 0;
    while (i < non) {
        char nb[12];
        int cnt;
        c = ob[i];
        cnt = on[i];
        i++;
        nstr(cnt, nb);
        if (c == '>') {
            if (cnt == 1) puts_gen("    ip++;\n");
            else { puts_gen("    ip += "); puts_gen(nb); puts_gen(";\n"); }
        } else if (c == '<') {
            if (cnt == 1) puts_gen("    ip--;\n");
            else { puts_gen("    ip -= "); puts_gen(nb); puts_gen(";\n"); }
        } else if (c == '+') {
            puts_gen("    tape[ip] = (tape[ip] + "); puts_gen(nb); puts_gen(") & 255;\n");
        } else if (c == '-') {
            puts_gen("    tape[ip] = (tape[ip] - "); puts_gen(nb); puts_gen(") & 255;\n");
        } else if (c == 'z') {
            puts_gen("    tape[ip] = 0;\n");
        } else if (c == '.') {
            cnt = on[i-1];
            while (cnt > 0) { puts_gen("    printf(\"%c\", tape[ip] & 255);\n"); cnt--; }
        } else if (c == ',') {
            cnt = on[i-1];
            while (cnt > 0) { puts_gen("    tape[ip] = wait_key() & 255;\n"); cnt--; }
        } else if (c == '[') {
            puts_gen("    while ((tape[ip] & 255) != 0) {\n");
        } else if (c == ']') {
            puts_gen("    }\n");
        }
        if (ngen >= MAXGEN - 64) {
            print("bfc: program too big for the generator buffer\n");
            return 1;
        }
    }

    puts_gen("    printf(\"\\n\");\n");
    puts_gen("    return 0;\n");
    puts_gen("}\n");
    if (!putc_gen(0)) {
        print("bfc: program too big\n");
        return 1;
    }

    n = file_write(out, gen, ngen - 1);
    if (n < 0) {
        print("bfc: cannot write '"); print(out); print("'\n");
        return 1;
    }
    print("bfc: "); print(path); print(" -> "); print(out); print(" ("); printint(ngen - 1); print(" bytes)\n");

    /* compile the generated C in a child task (same pattern as the
     * equinoxinstall pool) -> hello.mrp lands next to the .c */
    {
        char cmd[80];
        int pid;
        int st;
        int k;
        cmd[0] = '-'; cmd[1] = 'c'; cmd[2] = ' ';
        k = 3;
        i = 0;
        while (out[i] && k < 78 - 2) { cmd[k] = out[i]; k++; i++; }
        cmd[k] = 0;
        pid = task_spawn_args("mtcc.mrp", 8388608, cmd);
        if (pid <= 0) {
            print("bfc: cannot spawn mtcc\n");
            return 1;
        }
        st = -1;
        task_wait(pid, &st);
        if (st != 0) {
            print("bfc: mtcc failed\n");
            return 1;
        }
    }
    /* hello.c -> hello.mrp name for the banner */
    {
        char mrp[64];
        int pn2;
        pn2 = 0;
        i = 0;
        while (out[i] && out[i] != '.' && pn2 < 62) { mrp[pn2] = out[i]; pn2++; i++; }
        mrp[pn2] = '.'; mrp[pn2+1] = 'm'; mrp[pn2+2] = 'r'; mrp[pn2+3] = 'p'; mrp[pn2+4] = 0;
        print("bfc: done -> "); print(mrp); print(" (run it: run "); print(mrp); print(")\n");
    }
    return 0;
}
