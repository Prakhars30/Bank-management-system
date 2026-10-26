/* server.c - listen, fork one child per client */
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "client_handler.h"
#include "config.h"
#include "record_file.h"
#include "seed_data.h"
#include "tx_manager.h"

static volatile sig_atomic_t stop_requested = 0;

static void sigchld_handler(int sig)
{
    int saved = errno;
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
    errno = saved;
}

static void sigterm_handler(int sig) { (void)sig; stop_requested = 1; }

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    sa.sa_handler = sigterm_handler;
    sa.sa_flags = 0;                           /* no SA_RESTART: accept() must wake up */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);                  /* a client that vanished must not kill us */
}

static int create_listen_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    struct sockaddr_in addr;
    if (fd < 0) { perror("socket"); return -1; }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(fd, 16) < 0) {
        perror("bind/listen");
        close(fd);
        return -1;
    }
    return fd;
}

static void run_child(int listen_fd, int client_fd, struct sockaddr_in *caddr)
{
    close(listen_fd);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    handle_client(client_fd);
    printf("[pid %d] DISCONNECT %s:%d\n", (int)getpid(), inet_ntoa(caddr->sin_addr), ntohs(caddr->sin_port));
    fflush(stdout);
    exit(0);                                   /* the OS releases every lock this process held */
}

int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;

    setup_signals();
    if (rf_open_all() < 0) return 1;
    recover_all();                             /* undo anything a crash left half done, BEFORE listening */
    seed_if_missing();                         /* first run only */

    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) return 1;
    printf("bank server listening on port %d\n", port);
    fflush(stdout);

    while (!stop_requested) {
        struct sockaddr_in caddr;
        socklen_t len = sizeof caddr;
        int client_fd = accept(listen_fd, (struct sockaddr *)&caddr, &len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        printf("CONNECT    %s:%d\n", inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) run_child(listen_fd, client_fd, &caddr);
        close(client_fd);
    }
    close(listen_fd);
    return 0;
}