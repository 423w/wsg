#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "common.h"

/*
client meant to run in user terminal and connect to my machine for rn
tell users to connect to my current ip
maintain msg persistence locally 
*/

// thread args, used to pass fd and username into threads
typedef struct {
    int fd;
    char username[32];
} ThreadArgs;

// thread function, runs in the background and prints incoming msgs
void *recv_thread (void* arg){
    ThreadArgs *args = (ThreadArgs*)arg;

    Message msg;
    while (1) {
        int bytes = recv(args->fd, &msg, sizeof(Message), 0);
        if (bytes < 1) { // 0 = disconnected, -1 = error
            printf("disconnected\n");
            break;
        }
        printf("[%s]: %s\n", msg.sender, msg.body);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2){
        printf(" usage: ./client <server ip address>");
        return -1;
    }
    // connect to server
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, argv[1], &server.sin_addr); // use my ip for rn, will give to client

    int ret = connect(fd, (struct sockaddr*)&server, sizeof(server));
    if (ret < 0){
        printf("connect failure\n");
        return -1;
    }
    // get client username
    char username[32];
    printf("username: ");
    fgets(username, 32, stdin);
    username[strcspn(username, "\n")] = 0;

    // build msg to send w just username
    Message registration;
    memset(&registration, 0, sizeof(registration));
    strncpy(registration.sender, username, 32);
    send(fd, &registration, sizeof(Message), 0);

    // pack threadargs
    ThreadArgs args;
    args.fd = fd;
    strncpy(args.username, username, 32);

    // spin up threads, one to recv, one to send
    pthread_t thread;
    pthread_create(&thread, NULL, recv_thread, &args);

    Message msg;
    while (1) {
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.sender, username, 32);

        printf("to: ");
        fgets(msg.recipient, 32, stdin);
        msg.recipient[strcspn(msg.recipient, "\n")] = 0;

        printf("message: ");
        fgets(msg.body, 256, stdin);
        msg.body[strcspn(msg.body, "\n")] = 0;

        send(fd, &msg, sizeof(Message), 0);
    }

    // if input loop exits, wait for recv thread to finish
    pthread_join(thread, NULL);
    return 0;
}