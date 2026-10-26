/* client.c - the simplest possible client: send each typed line, print the reply.
 * A menu client, web page or GUI would do the same thing: turn a choice into a
 * command line such as "DEPOSIT 12.50" and show the reply. */
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* print reply lines until the one that starts with OK or ERR; returns 0 if the server hung up */
static int print_reply(FILE *in)
{
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, in) > 0) {
        fputs(line, stdout);
        if (!strncmp(line, "OK", 2) || !strncmp(line, "ERR", 3)) { free(line); return 1; }
    }
    free(line);
    return 0;
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "5050";
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) { fprintf(stderr, "cannot resolve %s\n", host); return 1; }
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) < 0) { perror("connect"); return 1; }
    freeaddrinfo(res);

    FILE *in = fdopen(fd, "r");
    char *cmd = NULL;
    size_t cap = 0;
    if (!print_reply(in)) return 0;
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (getline(&cmd, &cap, stdin) <= 0) break;
        if (write(fd, cmd, strlen(cmd)) < 0) break;
        if (!print_reply(in)) { printf("(server closed the connection)\n"); break; }
    }
    free(cmd);
    return 0;
}
