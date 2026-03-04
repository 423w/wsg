#pragma once

// common data shared across server and client
#define MAX_CLIENTS 32      // only need 32 clients rn
#define PORT 9090           // shared port for rn


// use typedef so structs exist as new data types
// dont have to use struct keyword every declaration
typedef struct{
    char recipient[32];     // who is getting msg
    char sender[32];        // who is sending it
    char body[256];         // msg content
} Message;

typedef struct{
    char username[32];      // registered name
    int fd;                 // socket fd of user connection
    int active;             // online status
} Client;
