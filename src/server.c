#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common.h"

/*
server running on my machine for now
users will connect to it and be able to message across it
ideally msg persistence maintained on client end so user privacy is protected
*/

int main() {
    // build sokt and bind
    // socket fd
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("socket connection failure\n");
        return -1;
    }

    // bind socket now
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;          // ipv4
    addr.sin_port = htons(PORT);        // 9090 in common.h
    addr.sin_addr.s_addr = INADDR_ANY;  // any interface

    int ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0){
        printf("bind failure\n");
        return -1;
    }
    // listen to incoming connections
    ret = listen(fd, 10);
    if (ret < 0){
        printf("listen failure\n");
        return -1;
    }

    // build client array and set all to inactive
    Client clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++){
        clients[i].active = 0;
    }

    // build fd_set to tell select which fds to watch
    int max_fd = fd;
    fd_set fds;
    // event loop
    while (1) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        // add every ctive client to the fd set
        for (int i = 0; i < MAX_CLIENTS; i++){
            if (clients[i].active){
                FD_SET(clients[i].fd, &fds);
                // update max fd if needed
                if (clients[i].fd > max_fd){
                    max_fd = clients[i].fd;
                }
            }
        }

        // wait until an fd has data
        select(max_fd + 1, &fds, NULL, NULL, NULL);

        // if client is new
        if (FD_ISSET(fd, &fds)){
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            int client_fd = accept(fd, (struct sockaddr*)&client_addr, &client_addr_len);

            Message registration;
            recv(client_fd, &registration, sizeof(Message), 0); // recv registration (sender name)
            // find an empty spot in clients for new client
            for (int i = 0; i < MAX_CLIENTS; i++){
                if (!clients[i].active){
                    clients[i].active = 1;
                    clients[i].fd = client_fd;
                    // copy username from sender
                    strncpy(clients[i].username, registration.sender, 32);
                    break;
                }
            }
        }

        // if its an existing connection
        for (int i =0; i< MAX_CLIENTS; i++){
            // skip empty clients
            if (!clients[i].active) continue;
            // if client has data tho
            if (FD_ISSET(clients[i].fd, &fds)){
                Message msg;
                int bytes = recv(clients[i].fd, &msg, sizeof(Message), 0);
                if (bytes < 1){ // 0 = disconnected, -1 = error
                    printf("%s disconnected\n", clients[i].username);
                    close(clients[i].fd);
                    clients[i].active = 0;
                    continue;
                }
                // send msg to recipient
                for (int j = 0; j < MAX_CLIENTS; j++){
                    if (clients[j].active && strcmp(clients[j].username, msg.recipient) == 0){
                        send(clients[j].fd, &msg, sizeof(Message), 0);
                        break;
                    }
                }
            }
        }
    }
}

