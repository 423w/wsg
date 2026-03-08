# C Messaging Service — Project Brief

## End Goal

Build a lightweight, terminal-based encrypted messaging service in C. The long-term vision is a privacy-focused alternative to services like Signal or Telegram, where the server acts as a dumb relay that cannot read message contents, and end-to-end encryption is handled entirely on the client side.

The final system should support:
- Multiple clients connecting to a central relay server
- Real-time direct messaging between named users
- End-to-end encryption (client-side only, server never sees plaintext)
- Remote deployment — the server runs on one machine and friends can connect from other computers over the internet

---

## Current Iteration Goal

Get a working plaintext messaging MVP running as fast as possible. Two or more terminals should be able to connect to a single server, register a username, and send direct messages to each other by name. This will be deployed on a personal machine with friends connecting remotely.

**Encryption is explicitly deferred.** Get the plumbing right first.

---

## Architecture

### Components

**Server (`server.c`)**
- Single process, runs continuously on one machine
- Uses `select()` for non-blocking I/O to handle multiple clients concurrently
- Maintains an in-memory array of connected clients (no database, no persistence)
- On receiving a message, looks up the recipient by username and forwards the message struct to their socket
- No message storage — if recipient is offline, message is dropped (acceptable for now)

**Client (`client.c`)**
- Connects to server via TCP using IP + port passed as command line argument: `./client <server_ip>`
- On startup: prompts for username and initial recipient, sends registration to server
- Uses ncurses for split-pane TUI: scrollable chat window (top) and input line (bottom)
- Receive thread runs in background, prints incoming messages to chat window with thread-safe mutex locks
- Main thread handles input loop with sticky recipient (use `/chat <name>` to switch, `/quit` to exit)
- Color-coded messages: green (you), cyan (others), yellow (system), magenta (headers)
- Compiled with `-lpthread -lncurses`

**Shared Header (`common.h`)**
- Contains the `Message` struct, `Client` struct, and shared constants
- Both `server.c` and `client.c` include this

### Wire Protocol

Every transmission over the socket is exactly one `Message` struct. No parsing, no framing complexity.

```c
// common.h

#define MAX_CLIENTS 32
#define PORT 9090

typedef struct {
    char recipient[32];
    char sender[32];
    char body[256];
} Message;
```

### Server Client Tracking

```c
typedef struct {
    char username[32];
    int fd;
    int active;
} Client;

Client clients[MAX_CLIENTS];
```

### File Structure

```
wsg/
├── CONTEXT.md
├── .gitignore
└── src/
    ├── common.h      ← Message struct, Client struct, constants
    ├── server.c      ← relay server
    └── client.c      ← terminal client
```

### Build

```bash
make          # builds both server and client
make clean    # removes binaries
```

Manual compilation:
```bash
gcc src/server.c -o src/server -Wall -Wextra
gcc src/client.c -o src/client -Wall -Wextra -lpthread -lncurses
```

---

## Server Flow

```
startup
  → socket(), bind(), listen() on PORT
  → initialize clients[] with active = 0

main loop using select()
  → rebuild fd_set each iteration (listening fd + all active client fds)
  → select() blocks until a fd is ready

  → new connection on listen fd?
      accept(), recv() registration Message, store fd + username in clients[]
  → data on existing client fd?
      recv() full Message struct
      find recipient in clients[] by username
      if found → send() Message to recipient fd
  → recv() returns 0 or -1?
      mark slot inactive, close fd (client disconnected)
```

---

## Client Flow

```
startup
  → connect to <server_ip> <port>
  → prompt username and initial recipient (before ncurses)
  → send registration Message to server (only sender filled)
  → initialize ncurses (initscr, cbreak, noecho, color support)
  → create chat_win (scrollable) and input_win (3 lines at bottom)

recv_thread (background)
  → loop:
      blocking recv() for Message struct
      mutex_lock → wprintw to chat_win with color → wrefresh → mutex_unlock
      if recv returns <= 0 → print "disconnected", exit thread

main (input loop)
  → loop:
      draw prompt in input_win with mutex protection
      mvwgetnstr() to read input
      if empty → continue (handles resize events)
      if "/chat <name>" → switch current_recipient, update header
      if "/quit" → break
      else → build Message{sender, current_recipient, body}, send(), echo to chat_win
  → cleanup: endwin(), close(fd), pthread_join()
```

---

## Completed

- [x] `common.h` — `Message`, `Client`, constants defined
- [x] `server.c` — socket setup, select loop, client registration, message forwarding, disconnect handling
- [x] `client.c` — connect, username registration, recv thread, input loop in main
- [x] Local test — two clients successfully exchange messages over localhost
- [x] `Makefile` — `make` builds both, `make clean` removes binaries, `common.h` listed as dependency
- [x] **ncurses TUI** — 2-pane layout (chat window + input line)
- [x] **Sticky recipient** — ask once at startup, use `/chat <name>` to switch
- [x] **Color support** — green for your messages, cyan for others, yellow for system messages, magenta for chat header
- [x] **Thread-safe rendering** — mutex-protected screen updates
- [x] **Terminal resize handling** — ignores empty input from resize events

---

## Current Issues & Next Steps

### 1. Message Delivery Problem (Critical)
**Problem:** If Alice is chatting with Bob, and Connor sends a message to Alice, Connor's message will appear in Alice's chat window even though she's viewing her conversation with Bob. If Connor's messages don't appear immediately, they get dropped entirely because the server doesn't buffer undelivered messages.

**Current behavior:**
- Server forwards messages in real-time only if recipient is connected
- Client displays ALL incoming messages in current chat window, regardless of sender
- No message queuing or buffering for busy/offline users
- Messages from non-active conversations get lost

**Requirements:**
- Server needs minimal message buffering (while staying "dumb" for privacy)
- Client needs to handle multiple concurrent conversations
- Undelivered messages shouldn't be dropped

**Potential solutions to explore:**
1. Server-side: Queue messages per user (encrypted, time-limited buffer)
2. Client-side: Maintain multiple conversation buffers, background notification system
3. Hybrid: Server holds encrypted messages, client pulls on demand

### 2. Disconnect message clarity
When viewing a conversation, disconnect should show `"<username> disconnected"` not generic `"disconnected from server"`

### 3. Message persistence
Local chat history saved to `~/.wsg/history/<contact>.log` for each conversation (append on send/recv, load on `/chat <name>`)

### 4. Contact list UI (deferred)
3-pane layout with contact list on left, arrow key navigation, online status indicators

### 5. Error handling improvements
- Handle `send()` failures gracefully
- Notify sender if recipient not found (server response mechanism needed)
- Handle server full (all MAX_CLIENTS slots taken)

---

## Deferred (Future Iterations)

- End-to-end encryption (study Signal Protocol: X3DH + Double Ratchet)
- Offline message queuing
- Group messaging
- User authentication / passwords
- Persistent message history
- Anonymity / metadata protection
- TLS for transport security
