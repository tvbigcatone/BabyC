#include <unistd.h>
#include <sys/select.h>
#include <poll.h>
#include <stdio.h>

int main(void)
{
    int p[2];
    fd_set r;
    struct pollfd pf;
    char c;
    int n;
    if (pipe(p) != 0) { puts("pipe"); return 1; }
    FD_ZERO(&r); FD_SET(p[0], &r);
    {
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
        n = select(p[0] + 1, &r, (fd_set *)0, (fd_set *)0, &tv);
        if (n != 0) { puts("select empty"); return 2; }
    }
    if (write(p[1], "a", 1) != 1) { puts("write"); return 3; }
    FD_ZERO(&r); FD_SET(p[0], &r);
    {
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
        n = select(p[0] + 1, &r, (fd_set *)0, (fd_set *)0, &tv);
        if (n != 1 || FD_ISSET(p[0], &r) == 0) { puts("select ready"); return 4; }
    }
    pf.fd = p[0]; pf.events = POLLIN; pf.revents = 0;
    if (poll(&pf, 1, 0) != 1 || (pf.revents & POLLIN) == 0) { puts("poll"); return 5; }
    pf.fd = -1; pf.events = POLLIN; pf.revents = 99;
    if (poll(&pf, 1, 0) != 0 || pf.revents != 0) { puts("poll fd=-1"); return 7; }
    pf.fd = 12345; pf.events = POLLIN; pf.revents = 0;
    if (poll(&pf, 1, 0) != 1 || (pf.revents & POLLNVAL) == 0) { puts("poll POLLNVAL"); return 8; }
    if (read(p[0], &c, 1) != 1 || c != 'a') { puts("read"); return 6; }
    close(p[0]); close(p[1]); puts("BabyPOSIX select/poll: ok"); return 0;
}
