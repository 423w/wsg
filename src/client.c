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

// conversation buffer
// clients will want to message multiple people
// need some way to maintain message persistence (thru buffer)
// client side to make server dumb
typedef struct {
    char contact_name[32];
    char messages[100][300];
    int message_count;
    int unread_count;
} Conversation;

// initialize conversation
Conversation conversations[MAX_CLIENTS];
int total_conversations = 0;
char my_username[32];

// thread args, used to pass fd and username into threads
typedef struct {
    int fd;
    char username[32];
    char *current_recipient;
} ThreadArgs;

// function prototypes
Conversation* get_conversation(const char* contact);
void add_msg_to_buffer(const char* contact, const char* msg);
void display_conversation(const char* contact);
void* recv_thread(void* arg);


// thread function, runs in the background and prints incoming msgs
void *recv_thread (void* arg){
    ThreadArgs *args = (ThreadArgs*)arg;

    Message msg;
    while (1) {
        int bytes = recv(args->fd, &msg, sizeof(Message), 0);
        if (bytes < 1) { // 0 = disconnected, -1 = error
            pthread_mutex_lock(&screen_mutex); // lock b4 printing to screen
            wattron(chat_win, COLOR_PAIR(3) | A_BOLD);
            wprintw(chat_win, "disconnected from server\n"); // print to chat window
            wattroff(chat_win, COLOR_PAIR(3) | A_BOLD);
            wrefresh(chat_win); // refresh after printing
            pthread_mutex_unlock(&screen_mutex);
            break;
        }

        char formatted_msg[300];
        snprintf(formatted_msg, 300, "[%s]: %s", msg.sender, msg.body);
        add_msg_to_buffer(msg.sender, formatted_msg);

        if (strcmp(msg.sender, args->current_recipient) == 0){
            pthread_mutex_lock(&screen_mutex); // lock b4 printing to screen
            wattron(chat_win, COLOR_PAIR(2)); // cyan for others
            wprintw(chat_win, "[%s]: %s\n", msg.sender, msg.body);
            wattroff(chat_win, COLOR_PAIR(2));
            wrefresh(chat_win); // refresh after printing
            pthread_mutex_unlock(&screen_mutex);

            Conversation* conv = get_conversation(msg.sender);
            if (conv && conv->unread_count > 0) {
                conv->unread_count--;
            }
        }        
    }
    return NULL;
}

// get conversation (or make a new one)
Conversation* get_conversation(const char* contact){
    for (int i = 0; i < total_conversations; i++){
        if (strcmp(conversations[i].contact_name, contact) == 0){
            return &conversations[i]; // conversation found
        }
    }
    // if not found then must make
    if (total_conversations >= MAX_CLIENTS){
        return NULL;
    }
    Conversation* new_convo = &conversations[total_conversations];
    strncpy(new_convo->contact_name, contact, 32);
    new_convo->message_count = 0;
    new_convo->unread_count = 0;
    total_conversations++;

    return new_convo;
}

// add msg to convo buffer
void add_msg_to_buffer(const char* contact, const char* msg){
    Conversation* convo = get_conversation(contact);
    if (!convo){
        return;
    }
    // use circular buffer
    int i = convo->message_count % 100;
    strncpy(convo->messages[i], msg, 300);
    convo->messages[i][299] = 0; // null term
    convo->message_count++;
    convo->unread_count++;
}

// display all msgs from a convo
void display_conversation(const char* contact) {
    Conversation* conv = get_conversation(contact);
    if (!conv) return;
    
    pthread_mutex_lock(&screen_mutex);
    werase(chat_win);
    
    // header
    wattron(chat_win, COLOR_PAIR(4) | A_BOLD);
    wprintw(chat_win, "Chat: %s -> %s\n", my_username, contact);
    wattroff(chat_win, COLOR_PAIR(4) | A_BOLD);
    wprintw(chat_win, "----------------------------------------\n");
    
    // display messages (show last 50 or all if less)
    int start = (conv->message_count > 50) ? (conv->message_count - 50) : 0;
    int end = conv->message_count;
    
    for (int i = start; i < end; i++) {
        int idx = i % 100;  // circular buffer index
        wprintw(chat_win, "%s\n", conv->messages[idx]);
    }
    
    wrefresh(chat_win);
    pthread_mutex_unlock(&screen_mutex);
    
    // mark all as read
    conv->unread_count = 0;
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
    strncpy(my_username, username, 32);

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

    // initialize convo tracking
    total_conversations = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        conversations[i].message_count = 0;
        conversations[i].unread_count = 0;
        memset(conversations[i].contact_name, 0, 32);
    }

    // initialize ncurses
    initscr();
    cbreak();   // disable line buff
    noecho();   // dont echo

    // initialize color support
    if (has_colors()) {
        start_color();
        // define color pairs: init_pair(pair_number, foreground, background)
        init_pair(1, COLOR_GREEN, COLOR_BLACK);   // your messages
        init_pair(2, COLOR_CYAN, COLOR_BLACK);    // their messages
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);  // system messages
        init_pair(4, COLOR_MAGENTA, COLOR_BLACK); // chat header
    }

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
    wrefresh(input_win);

    // display initial conversation (will be empty at first)
    display_conversation(current_recipient);


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

        // count total unread messages from all conversations
        int total_unread = 0;
        for (int i = 0; i < total_conversations; i++) {
            // dont count unreads from current conversation
            if (strcmp(conversations[i].contact_name, current_recipient) != 0) {
                total_unread += conversations[i].unread_count;
            }
        }

        // show notification if there are unread messages
        int cursor_col;
        if (total_unread > 0) {
            mvwprintw(input_win, 1, 2, "> (%d new) ", total_unread);
            cursor_col = 2 + 3 + snprintf(NULL, 0, "%d", total_unread) + 6;  // Calculate length
        } else {
            mvwprintw(input_win, 1, 2, "> ");
            cursor_col = 4;
        }

        wrefresh(input_win);
        pthread_mutex_unlock(&screen_mutex);

        echo();
        mvwgetnstr(input_win, 1, cursor_col, input, 255);
        noecho();

        if (strlen(input) == 0) {
            continue;
        }

        if (strncmp(input, "/chat ", 6) == 0){
            strncpy(current_recipient, input + 6, 32);
            current_recipient[31] = 0;

            display_conversation(current_recipient);
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

        char formatted_msg[300];
        snprintf(formatted_msg, 300, "[you]: %s", msg.body);
        add_msg_to_buffer(current_recipient, formatted_msg);

        pthread_mutex_lock(&screen_mutex);
        wattron(chat_win, COLOR_PAIR(1)); // green for you
        wprintw(chat_win, "%s\n", formatted_msg);
        wattroff(chat_win, COLOR_PAIR(1));
        wrefresh(chat_win);
        pthread_mutex_unlock(&screen_mutex);

        Conversation* conv = get_conversation(current_recipient);
        if (conv && conv->unread_count > 0) {
            conv->unread_count--;
        }
    }
    endwin();
    close(fd);

    // if input loop exits, wait for recv thread to finish
    pthread_join(thread, NULL);
    return 0;
}