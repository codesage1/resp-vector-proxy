#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

// acting as a TCP proxy between a client and an upstream server
int tcp_connect(const char *host, int port){
    int sockfd;
    struct addrinfo hints, *res;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        perror("getaddrinfo failed");
        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1) {
        perror("socket failed");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("connect failed");
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return sockfd;
}

//acting as a TCP server listening for incoming client connections
int tcp_listen(int port){
    int server_fd;
    int yes = 1;
    struct addrinfo hints;
    struct addrinfo *res;

    char port_str[16];
    snprintf(port_str , sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // Bind to my local IP

    if(getaddrinfo(NULL, port_str, &hints, &res) != 0){
        perror("getaddrinfo failed");
        exit(1);
    }

    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(server_fd == -1){
        perror("socket failed");
        exit(1);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt failed");
        exit(1);
    }

    if(bind(server_fd, res->ai_addr, res->ai_addrlen) == -1){
        perror("bind failed");
        exit(1);
    }

    if(listen(server_fd,10) == -1){
        perror("listen failed");
        exit(1);
    }

    // int new_fd = accept(server_fd, )
    freeaddrinfo(res);

    return server_fd;
}

int write_all(int fd, const char *buf,size_t size){
    size_t total_written = 0;
    while(size > total_written){
        ssize_t temp = write(fd , buf + total_written, size - total_written);

        if(temp <= 0) return -1;

        total_written += (size_t)temp;
    }
    return 0;
}


int proxy_run(int listen_port, const char *up_host, int up_port) {

    int listen_fd = tcp_listen(listen_port);
    printf("Server listening on port %d...\n", listen_port);

    // 2. The Infinite Server Loop
    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) continue;

        int up_fd = tcp_connect(up_host, up_port);
        if (up_fd < 0) {
            perror("Upstream is down");
            close(client_fd);
            continue; // Drop the client, wait for the next one
        }

        char buffer[8192];
        ssize_t bytes_read;

        size_t client_to_upstream = 0;
        size_t upstream_to_client = 0;
        
        struct pollfd fds[2];

        //watching client_fd and up_fd for incoming data
        fds[0].fd = client_fd;
        fds[0].events = POLLIN;

        fds[1].fd = up_fd;
        fds[1].events = POLLIN;

        while(1){
            //asking OS to wait
            int ready = poll(fds, 2, -1);
            if(ready == -1){
                perror("poll failed");
                break;
            }

            if(fds[0].revents & POLLIN){
                bytes_read = read(client_fd, buffer, sizeof(buffer));
                if(bytes_read <= 0) break; // client closed connection
                client_to_upstream += (size_t)bytes_read;
                if(write_all(up_fd, buffer, bytes_read) == -1) break;
            }

            if(fds[1].revents & POLLIN){
                bytes_read = read(up_fd, buffer, sizeof(buffer));
                if(bytes_read <= 0) break; // upstream closed connection
                upstream_to_client += (size_t)bytes_read;
                if(write_all(client_fd, buffer, bytes_read) == -1) break;
            }

            // --- Check for broken connections (POLLERR or POLLHUP)
            if ((fds[0].revents & (POLLERR | POLLHUP)) || (fds[1].revents & (POLLERR | POLLHUP))) {
            break; // Someone forcefully closed the connection
            }
        }
        printf("conn closed: client→upstream %zu bytes, upstream→client %zu bytes.\n", client_to_upstream, upstream_to_client);
        close(up_fd);
        close(client_fd);
    }
    return 0;
}