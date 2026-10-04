/* bfi.c — Brainfuck interpreter for Equinox OS.
 *
 * Shipped as an eggkg package (repo Eggkg-l): mtcc C dialect, one
 * #include-spliced libc, kernel syscall builtins — `eggkg install bfi`
 * builds it in-OS with mtcc and drops bfi.mrp into /bin.
 *
 *   bfi hello.bf     run a Brainfuck program
 *
 * Semantics (classic 8-bit):
 *   tape[30000] cells, all zero; one data pointer p (starts at 0).
 *   >  <  +  -  .  ,  [  ]   are the commands; every other byte of
 *   the source is a comment and is skipped while parsing.
 *
 *   ,  reads one keypress (blocking, polled via getkey + sleep)
 *   .  writes one byte
 *   [  if tape[p] == 0, jump past the matching ]
 *   ]  if tape[p] != 0, jump back after the matching [
 *
 * Bracket matching is precomputed once into match[] so the run loop
 * never has to scan. Loops deeper than 512 nesting levels are rejected
 * up front (Brainfuck never needs more).
 *
 * mtcc constraints respected: no struct/typedef/switch/sizeof/function
 * pointers, all declarations before use, globals first.
 */
#include <stdio.h>
#include <fileio.h>

#define MAXPROG  4096    /* source-size cap (mtcc array limit)      */
#define TAPESZ   4096    /* tape cells — classic hello.bf needs ~10 */
#define MAXLOOP  512     /* [ / ] nesting cap                        */

char src[MAXPROG];       /* raw program text                         */
char cmds[MAXPROG];      /* filtered command stream (no comments)    */
int  jump[MAXPROG];      /* bracket partner in cmds[], -1 otherwise  */
char tape[TAPESZ];       /* data tape, zeroed at boot                */
int  p;                  /* data pointer                             */
int  stack[MAXLOOP];     /* open-[ positions while matching          */
int  nsrc;               /* bytes in src[]                           */
int  ncmd;               /* bytes in cmds[]                          */

/* block until a key is pressed; return its code (>= 0) */
int wait_key(void) {
    int c;
    c = getkey();
    while (c == -1) {
        sleep(20);
        c = getkey();
    }
    return c;
}

/* build cmds[] + jump[] from src[]; return 0 ok / 1 bracket error */
int prep(void) {
    int top;
    int i;
    int j;
    char c;

    ncmd = 0;
    top = 0;
    i = 0;
    while (i < nsrc) {
        c = src[i];
        i++;
        if (c == '>' || c == '<' || c == '+' || c == '-' ||
            c == '.' || c == ',' || c == '[' || c == ']') {
            if (ncmd >= MAXPROG) {
                printf("bfi: program too big\n");
                return 1;
            }
            cmds[ncmd] = c;
            jump[ncmd] = -1;
            if (c == '[') {
                if (top >= MAXLOOP) {
                    printf("bfi: loops nested too deep\n");
                    return 1;
                }
                stack[top] = ncmd;
                top++;
            }
            if (c == ']') {
                if (top <= 0) {
                    printf("bfi: ']' without '['\n");
                    return 1;
                }
                top--;
                j = stack[top];
                jump[j] = ncmd;
                jump[ncmd] = j;
            }
            ncmd++;
        }
    }
    if (top != 0) {
        printf("bfi: '[' without ']'\n");
        return 1;
    }
    return 0;
}

int main() {
    char args[160];
    char path[64];
    int n;
    int i;
    int pn;
    int pc;
    int k;
    char out[2];

    /* first non-"bfi" argument token = program path */
    n = getargs(args, 160);
    path[0] = 0;
    i = 0;
    while (i < n) {
        while (i < n && args[i] == ' ') i++;
        if (i >= n) break;
        pn = 0;
        while (i < n && args[i] != ' ' && pn < 62) { path[pn] = args[i]; pn++; i++; }
        path[pn] = 0;
        if (pn == 3 && path[0] == 'b' && path[1] == 'f' && path[2] == 'i') continue;
        break;
    }

    if (path[0] == 0) {
        printf("usage: bfi FILE.bf\n");
        return 2;
    }

    n = file_size(path);
    if (n <= 0) {
        printf("bfi: cannot read '%s'\n", path);
        return 1;
    }
    if (n >= MAXPROG) {
        printf("bfi: file too big (max %d)\n", MAXPROG - 1);
        return 1;
    }
    n = file_read_all(path, src, MAXPROG - 1);
    if (n <= 0) {
        printf("bfi: cannot read '%s'\n", path);
        return 1;
    }
    nsrc = n;

    if (prep() != 0) return 1;

    p = 0;
    pc = 0;
    out[1] = 0;
    while (pc < ncmd) {
        k = cmds[pc];
        if (k == '>') {
            p++;
            if (p >= TAPESZ) { printf("\nbfi: tape overflow\n"); return 1; }
        } else if (k == '<') {
            p--;
            if (p < 0) { printf("\nbfi: tape underflow\n"); return 1; }
        } else if (k == '+') {
            tape[p] = (tape[p] + 1) & 255;
        } else if (k == '-') {
            tape[p] = (tape[p] - 1) & 255;
        } else if (k == '.') {
            out[0] = tape[p];
            print(out);
        } else if (k == ',') {
            tape[p] = wait_key() & 255;
        } else if (k == '[') {
            if ((tape[p] & 255) == 0) pc = jump[pc];
        } else if (k == ']') {
            if ((tape[p] & 255) != 0) pc = jump[pc];
        }
        pc++;
    }
    print("\n");
    return 0;
}
