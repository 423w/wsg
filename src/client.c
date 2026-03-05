#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ncurses.h> // for tui
#include "common.h"

/*
client meant to run in user terminal and connect to my machine for rn
tell users to connect to my current ip
maintain msg persistence locally 
*/

// global ncurses state
// global so all threads can access
// mutexing to avoid race condition btwn threads
WINDOW *chat_win;       // chat window
WINDOW *input_win;      // input window
pthread_mutex_t screen_mutex = PTHREAD_MUTEX_INITIALIZER;

// thread args, used to pass fd and username into threads
typedef struct {
    int fd;
    char username[32];
    char *current_recipient;
} ThreadArgs;

// thread function, runs in the background and prints incoming msgs
void *recv_thread (void* arg){
    ThreadArgs *args = (ThreadArgs*)arg;

    Message msg;
    while (1) {
        int bytes = recv(args->fd, &msg, sizeof(Message), 0);
        if (bytes < 1) { // 0 = disconnected, -1 = error
            pthread_mutex_lock(&screen_mutex); // lock b4 printing to screen
            wprintw(chat_win, "disconnected from server\n"); // print to chat window
            wrefresh(chat_win); // refresh after printing
            pthread_mutex_unlock(&screen_mutex);
            break;
        }
        pthread_mutex_lock(&screen_mutex); // lock b4 printing to screen
        wprintw(chat_win, "[%s]: %s\n", msg.sender, msg.body);
        wrefresh(chat_win); // refresh after printing
        pthread_mutex_unlock(&screen_mutex);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2){
        printf(" usage: ./client <server ip address>\n");
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

    // choose chat partner
    char current_recipient[32];
    printf("chat with: ");  // ← NEW: ask once at startup
    fgets(current_recipient, 32, stdin);
    current_recipient[strcspn(current_recipient, "\n")] = 0;

    // build msg to send w just username
    Message registration;
    memset(&registration, 0, sizeof(registration));
    strncpy(registration.sender, username, 32);
    send(fd, &registration, sizeof(Message), 0);


    // initialize ncurses
    initscr();
    cbreak();   // disable line buff
    noecho();   // dont echo

    // build window
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x); // term dimensions
    // separate windows
    // chat on top
    chat_win = newwin(max_y - 3, max_x, 0, 0); // height, width, y, x
    // input on bottom
    input_win = newwin(3, max_x, max_y - 3, 0);
    // enable scrolling in chat
    scrollok(chat_win, TRUE);
    // border
    box(input_win, 0, 0);
    wprintw(chat_win, "Chat: %s -> %s\n", username, current_recipient);
    wprintw(chat_win, "----------------------------------------\n");
    wrefresh(chat_win);
    wrefresh(input_win);


    // pack threadargs
    ThreadArgs args;
    args.fd = fd;
    strncpy(args.username, username, 32);
    args.current_recipient = current_recipient;

    // spin up threads, one to recv, one to send
    pthread_t thread;
    pthread_create(&thread, NULL, recv_thread, &args);

    char input[256];
    while (1) {
        pthread_mutex_lock(&screen_mutex);
        werase(input_win);
        box(input_win, 0, 0);
        mvwprintw(input_win, 1, 2, "> ");
        wrefresh(input_win);
        pthread_mutex_unlock(&screen_mutex);

        echo();
        mvwgetnstr(input_win, 1, 4, input, 255);
        noecho();

        if (strlen(input) == 0) {
            continue;
        }

        if (strncmp(input, "/chat ", 6) == 0){
            strncpy(current_recipient, input + 6, 32);
            current_recipient[31] = 0;

            pthread_mutex_lock(&screen_mutex);
            werase(chat_win);
            wprintw(chat_win, "Chat: %s -> %s\n", username, current_recipient);
            wprintw(chat_win, "----------------------------------------\n");
            wrefresh(chat_win);
            pthread_mutex_unlock(&screen_mutex);
            continue;
        }
        if (strcmp(input, "/quit") == 0) {
            break;
        }
        Message msg;
        memset(&msg, 0, sizeof(msg));
        strncpy(msg.sender, username, 32);
        strncpy(msg.recipient, current_recipient, 32);
        strncpy(msg.body, input, 256);

        send(fd, &msg, sizeof(Message), 0);

        pthread_mutex_lock(&screen_mutex);
        wprintw(chat_win, "[you]: %s\n", msg.body);
        wrefresh(chat_win);
        pthread_mutex_unlock(&screen_mutex);
    }
    endwin();
    close(fd);

    // if input loop exits, wait for recv thread to finish
    pthread_join(thread, NULL);
    return 0;
}