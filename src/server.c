#include <stdio.h>
#include <unistd.h> /* close() */
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include "server.h"
#include "listener.h"
#include "connection.h"
#include "config.h"
#include "util.h"

#define BACKLOG 5

static void sig_handler(int);

static volatile int running; /* For signal handler */
static volatile int sighup_received; /* For signal handler */


int
init_server(const char *address, int port, int tls_flag) {
    struct sockaddr_storage addr;
    int sockfd, addr_len;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP, sig_handler);
    /* ignore SIGPIPE, or it will kill us */
    signal(SIGPIPE, SIG_IGN);

    init_listeners();


    addr_len = parse_address(&addr, address, port);
    if (addr_len == 0) {
        perror("Error parsing address");
        return -1;
    }

    sockfd = add_listener(addr, addr_len, tls_flag);

    /*
    sockfd = socket(addr.ss_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return -1;
    }
    // set SO_REUSEADDR on server socket to facilitate restart
    int reuseval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseval, sizeof(reuseval));
    
    if (bind(sockfd, (struct sockaddr *)&addr, addr_len) < 0) {
        perror("ERROR on binding");
        return -1;
    }
    if (listen(sockfd, BACKLOG) < 0) {
        perror("ERROR listen();");
        return;
    }
    */
    return sockfd;
}

void
run_server() {
    int maxfd;
    fd_set rfds;

    init_connections();
    running = 1;


    while (running) {
        FD_ZERO(&rfds);
        maxfd = fd_set_listeners(&rfds, 0);
        maxfd = fd_set_connections(&rfds, maxfd);

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            /* select() might have failed because we received a signal, so we need to check */
            if (errno != EINTR) {
                perror("select");
                return;
            }
            /* We where inturrupted by a signal */
            if (sighup_received) {
                sighup_received = 0;
                load_config();
            }
            continue; /* our file descriptor sets are undefined, so select again */
        }

        handle_listeners(&rfds);
        handle_connections(&rfds);
    }

    close_listeners();
    free_listeners();
    free_connections();
}

static void
sig_handler(int sig) {
    switch(sig) {
        case(SIGHUP):
            sighup_received = 1;
            break;
        case(SIGINT):
        case(SIGTERM):
            running = 0;
    }
    signal(sig, sig_handler);
    /* Reinstall signal handler */
}
