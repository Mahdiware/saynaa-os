#include "libc/stdio.h"

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    char name[32];
    int a = 0;
    int b = 0;

    printf("Name: ");
    if (scanf("%s", name) != 1) {
        puts("bad input");
        return 1;
    }

    printf("Two numbers: ");
    if (scanf("%d %d", &a, &b) != 2) {
        puts("bad input");
        return 1;
    }

    printf("Hello %s, sum=%d\n", name, a + b);
    return 0;
}
