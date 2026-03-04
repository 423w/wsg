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
- On startup: prompts for username, sends it to server as a registration Message
- Receive thread runs in background blocking on `recv()`, prints incoming messages
- Input loop runs in main: prompts "to:" and "message:", builds and sends a Message struct
- Compiled with `-lpthread`

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
gcc server.c -o server
gcc client.c -o client -lpthread
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
  → prompt username → send to server as a registration Message (only sender filled)

recv_thread (background)
  → loop:
      blocking recv() for Message struct
      print "[sender]: body"
      if recv returns <= 0 → print disconnected, exit thread

main (input loop)
  → loop:
      prompt "to: "
      prompt "message: "
      build Message{sender, recipient, body}
      send() to server
```

---

## Completed

- [x] `common.h` — `Message`, `Client`, constants defined
- [x] `server.c` — socket setup, select loop, client registration, message forwarding, disconnect handling
- [x] `client.c` — connect, username registration, recv thread, input loop in main
- [x] Local test — two clients successfully exchange messages over localhost
- [x] `Makefile` — `make` builds both, `make clean` removes binaries, `common.h` listed as dependency

---

## Next Iteration

### TUI with ncurses
Decided to use `ncurses` for the client UI. Raw `printf` causes incoming messages to interleave with input prompts, breaking the UX. `ncurses` lets us split the terminal into independent windows so chat output and user input never collide.

**Target layout:**
```
┌─────────────┬────────────────────────────┐
│  contacts   │   [alice]                  │
│             │                            │
│ > alice     │   [10:32] bob: hey         │
│   bob       │   [10:33] alice: what up   │
│             │                            │
│             ├────────────────────────────┤
│             │ > _                        │
└─────────────┴────────────────────────────┘
```

**Build order:**
1. Basic split pane — chat window on top, input line at bottom (replaces current printf flow)
2. Sticky recipient — once in a chat, stay there until explicitly switched
3. Local message history — append to `~/.wsg/history/<contact>.log` on send/recv, load on open
4. Contact list pane — left panel showing known contacts, arrow keys to select

**ncurses key concepts:**
- `initscr()` / `endwin()` — init and teardown
- `newwin(h, w, y, x)` — create a window at a position
- `wprintw(win, ...)` — print into a window
- `wrefresh(win)` — flush changes to screen
- `wgetch(win)` — read input from a window
- Link with `-lncurses`

### 2. UX — Fix interleaved output
**Problem:** incoming messages print in the middle of the "to:" / "message:" prompts, breaking the input flow visually. The terminal has no concept of separating the input line from printed output.

**Goal:** messages should appear clearly above the prompt, and the prompt should stay at the bottom — so typing is never visually interrupted.

**Options to explore:**
- Reprint the prompt after receiving a message (simple, low effort)
- Use `ncurses` to split the terminal into a chat pane and input pane (clean, more complex)

### 3. Error handling improvements
- Handle `send()` failures gracefully
- Notify sender if recipient not found (server currently silently drops the message)
- Handle server full (all MAX_CLIENTS slots taken)

### 4. Messaging structure improvements
- Consider keeping recipient sticky — once you set "to:", don't re-prompt every message
- Or move to a chat-style input: `@bob hello` syntax to switch recipient inline

---

## Deferred (Future Iterations)

- End-to-end encryption (study Signal Protocol: X3DH + Double Ratchet)
- Offline message queuing
- Group messaging
- User authentication / passwords
- Persistent message history
- Anonymity / metadata protection
- TLS for transport security
