#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include "resp.h"
#include "inspect.h"

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
        char up_buffer[8192];
        ssize_t bytes_read;

        size_t client_to_upstream = 0;
        size_t upstream_to_client = 0;
        
        size_t total_bytes = 0;
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
                if (errno == EINTR) continue; // <-- F2 FIX: Recoverable signal interruption
                break;
            }

            if(fds[0].revents & POLLIN){
                bytes_read = read(client_fd, buffer + total_bytes, sizeof(buffer) - total_bytes);
                if(bytes_read <= 0) break; // client closed connection
                total_bytes += (size_t)bytes_read;
                client_to_upstream += (size_t)bytes_read;

                size_t parsed_offset = 0;
                int disable_parser = 0;
                while(parsed_offset < total_bytes) {
                    resp_value *cmd = NULL;
                    size_t consumed = 0;
                    if (!disable_parser) {
                        resp_status status = resp_parse(buffer + parsed_offset, total_bytes - parsed_offset,&consumed,&cmd);
                        if (status == RESP_OK) {
                            // Successfully parsed a command
                            if (cmd->type == RESP_ARRAY && cmd->as.array.n > 0){                                
                                resp_value *first_item = cmd -> as.array.items[0];
                                if (first_item->type == RESP_BULK){
                                    char* cmd_name = first_item -> as.str.data;
                                    size_t cmd_len = first_item -> as.str.len;
        
                                    printf("[Proxy] Seen command: %.*s\n", (int)cmd_len, cmd_name);
                                    if(is_ft_search(cmd) == 1){
                                        printf("🚨 INTERCEPTED FT.SEARCH!\n");
                                    }
                                }
                            }
                            
                            write_all(up_fd, buffer + parsed_offset, consumed); // Forward the command to upstream
                            resp_free(cmd); // Free the parsed command after inspection
                        } else if (status == RESP_NEED_MORE) {
                            // Not enough data to parse a complete command
                            break;
                        } else {
                            // Parsing error occurred
                            disable_parser = 1; // Disable further parsing on error
                            break;
                        }
                    } else {
                        consumed = total_bytes - parsed_offset; // If parser is disabled, consume all
                    }

                    if (consumed == 0) {
                        disable_parser = 1; // Disable further parsing if no progress is made
                        break;
                    }

                    parsed_offset += consumed;
                }
                if(disable_parser) {
                    // If parser is disabled, forward all remaining data to upstream
                    write_all(up_fd, buffer + parsed_offset, total_bytes - parsed_offset);
                    parsed_offset = total_bytes; // All data has been forwarded
                }

                if(parsed_offset > 0 && parsed_offset < total_bytes) {
                    // Move unprocessed data to the beginning of the buffer
                    memmove(buffer, buffer + parsed_offset, total_bytes - parsed_offset);
                }
                total_bytes -= parsed_offset; // Update total_bytes to reflect unprocessed data


            }

            if(fds[1].revents & POLLIN){
                bytes_read = read(up_fd, up_buffer + total_bytes, sizeof(up_buffer) - total_bytes);
                if(bytes_read <= 0) break; // upstream closed connection
                upstream_to_client += (size_t)bytes_read;
                if(write_all(client_fd, up_buffer, bytes_read) == -1) break;
            }

            // --- Check for broken connections (POLLERR or POLLHUP)
            if ((fds[0].revents & (POLLERR | POLLHUP)) || (fds[1].revents & (POLLERR | POLLHUP))) {
            break; // Someone forcefully closed the connection
            }
        }

        printf("conn closed: client→upstream %zu bytes, upstream→client %zu bytes.\n", client_to_upstream, upstream_to_client);
        fflush(stdout); // Force the block-buffered text to print
        close(up_fd);
        close(client_fd);
    }
    return 0;
}