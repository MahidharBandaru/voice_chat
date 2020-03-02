#include <stdio.h>

int main () {
    char *s = "123 456 hi there";
    // printf("%s",s);

    long long a, b;
    char buf[256];
    sscanf(s, "%lld %lld %[^\n]s", &a, &b, buf);
    printf("%lld %lld %s", a, b, buf);
    return 0;
}