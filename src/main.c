#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

int proxy_run(int listen_port, const char *up_host, int up_port);

int main(int argc, char **argv) {

    signal(SIGPIPE, SIG_IGN);


    int listen_port = 7000;
    const char *up_host = "127.0.0.1";
    int up_port = 6379;


    if (argc >= 2) listen_port = atoi(argv[1]);
    if (argc >= 3) up_host = argv[2];
    if (argc >= 4) up_port = atoi(argv[3]);

    printf("[Proxy] Listening on %d, routing to %s:%d\n", listen_port, up_host, up_port);


    return proxy_run(listen_port, up_host, up_port);
}