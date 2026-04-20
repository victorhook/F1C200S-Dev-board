#include <stdio.h>
#include <sys/utsname.h>

int main(void) {
    struct utsname u;
    uname(&u);
    printf("Hello from %s on %s!\n", u.machine, u.sysname);
    return 0;
}
