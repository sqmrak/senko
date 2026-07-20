#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(80);
    inet_pton(AF_INET, "93.184.216.34", &a.sin_addr);

    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) {
        perror("connect example.com:80");
        close(s);
        return 1;
    }

    const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    if (write(s, req, strlen(req)) < 0) { perror("write"); close(s); return 1; }

    char buf[256];
    ssize_t n = read(s, buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = '\0';
        fputs(buf, stderr);
        close(s);
        return 0;
    }
    perror("read");
    close(s);
    return 1;
}