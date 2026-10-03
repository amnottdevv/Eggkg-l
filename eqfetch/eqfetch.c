

/* hex 1 byte -> "xx" */
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

void art(int i) {
    if (i == 0) pa("               ,,ggddY888Ybbgg,,                ");
    else if (i == 1) pa("          ,agd8##'   .d8888888888bga,           ");
    else if (i == 2) pa("       ,gdP##'     .d88888888888888888g,        ");
    else if (i == 3) pa("     ,dP#        ,d888888888888888888888b,      ");
    else if (i == 4) pa("   ,dP#         ,8888888888888888888888888b,    ");
    else if (i == 5) pa("  ,8#          ,8888888P###88888888888888888,   ");
    else if (i == 6) pa(" ,8'           I888888I    )88888888888888888,  ");
    else if (i == 7) pa(",8'            `8888888booo8888888888888888888, ");
    else if (i == 8) pa("d'              `88888888888888888888888888888b ");
    else if (i == 9) pa("8                `#8888888888888888888888888888 ");
    else if (i == 10) pa("8                  `#88888888888888888888888888 ");
    else if (i == 11) pa("8                      `#8888888888888888888888 ");
    else if (i == 12) pa("Y,                        `8888888888888888888P ");
    else if (i == 13) pa("`8,                         `88888888888888888' ");
    else if (i == 14) pa(" `8,              .oo.       `888888888888888'  ");
    else if (i == 15) pa("  `8a             8888        88888888888888'   ");
    else if (i == 16) pa("   `Yba           `##'       ,888888888888P'    ");
    else if (i == 17) pa("     #Yba                   ,88888888888'       ");
    else if (i == 18) pa("       `#Yba,             ,8888888888P#'        ");
    else if (i == 19) pa("          `#Y8baa,      ,d88888888P#'           ");
    else if (i == 20) pa("               ``##YYba8888P888#'               ");
    else pa("                                                ");
}

void info(int j, int* w, int* fb, int up) {
    int i;
    i = j - 5;   
    if (i == 0) print("root@equinox");
    else if (i == 1) print("------------");
    else if (i == 2) print("OS:      Equinox OS");
    else if (i == 3) print("Kernel:  Equinox-kernel-S0.4");
    else if (i == 4) {
        print("Uptime:  ");
        printint(up / 3600); print("h ");
        printint((up / 60) % 60); print("m ");
        printint(up % 60); print("s");
    }
    else if (i == 5) print("Shell:   equinox shell");
    else if (i == 6) {
        print("Display: ");
        if (fb[5]) {
            printint(fb[1]); print("x"); printint(fb[2]);
            print(" @ "); printint(fb[3]); print("bpp");
        } else print("VGA text");
    }
    else if (i == 7) print("RAM:     256 MB (simulated)");
    else if (i == 8) {
        print("Net:     ");
        if (w[0]) {
            pr_ip(w[2]);
            if (w[1]) print(" (dhcp)"); else print(" (static)");
        } else print("down");
    }
    else if (i == 9) {
        print("MAC:     ");
        prhex(w[5] & 255);         print(":");
        prhex((w[5] >> 8) & 255);  print(":");
        prhex((w[5] >> 16) & 255); print(":");
        prhex((w[5] >> 24) & 255); print(":");
        prhex(w[6] & 255);         print(":");
        prhex((w[6] >> 8) & 255);
    }
    else if (i == 10) {
        print("Packets: rx ");
        printint(w[7]); print(" / tx "); printint(w[8]);
    }
}

int main() {
    int w[10];
    int fb[6];
    int i;
    int up;

    for (i = 0; i < 10; i++) w[i] = 0;
    for (i = 0; i < 6; i++) fb[i] = 0;
    net_info(w);
    fb_info(fb);
    up = gettick() / 100;      /* timer_init(100) => 100 tick */

    print("\n");
    for (i = 0; i < 21; i++) {
        print("  ");
        art(i);
        print("   ");
        info(i, w, fb, up);
        print("\n");
    }
    print("\n");
    return 0;
}
