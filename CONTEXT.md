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
- Uses ncurses for 2-pane TUI: scrollable chat window (top) and input line with inline notifications (bottom)
- **Conversation buffers**: Maintains separate message history for each contact (100 messages per contact, circular buffer)
- **Message routing**: recv_thread filters incoming messages
  - If sender matches current_recipient: display immediately in chat_win
  - If sender is different: buffer silently, increment unread count
- **Notification system**: Inline notifications in input prompt show total unreads: `"> (2 new) "`
- Main thread handles input loop with sticky recipient (use `/chat <name>` to switch, `/quit` to exit)
- `/chat <name>` command: switches recipient, loads last 50 messages from buffer, marks as read
- Color-coded: green (you), cyan (others), yellow (system), magenta (headers)
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
- [x] **Phase 1: Client-side conversation buffers** — multiple concurrent conversations with message routing
  - Separate conversation buffer per contact (100 messages, circular buffer)
  - Messages filtered by sender in recv_thread
  - Only current conversation displays in chat window
  - Non-current messages stored silently with unread count
  - `/chat <name>` loads conversation history (last 50 messages)
  - Inline notifications show total unreads: `"> (2 new) "`

---

## Current Issues & Next Steps

### 1. ~~Message Delivery Problem~~ ✅ SOLVED (Phase 1)
**Solution implemented:** Client-side conversation buffers with message routing

**What works:**
- Each contact has dedicated message buffer (100 messages, circular)
- Messages filtered by sender in `recv_thread`
- Only current conversation displays in chat window
- Non-current messages stored silently with unread count
- Inline notifications show total unreads: `"> (2 new) "`
- `/chat <name>` loads conversation history (last 50 messages)
- All messages preserved during client session

**Current limitation:**
- Messages only buffered while client is running (restart = lost history)
- Inline notifications show total count, not per-sender breakdown

**Next step:** Phase 2 - Add disk persistence (see #2 below)

### 2. Message Persistence (Phase 2 - Next Priority)
**Goal:** Save conversation buffers to disk for persistence across restarts

**Current behavior:**
- Conversation buffers stored in RAM only
- Client restart = all conversation history lost
- No way to review old messages from previous sessions

**Implementation plan:**
- Create `~/.wsg/chats/` directory on first run
- On send/receive: append to `~/.wsg/chats/<contact>.log`
  - Format: `[timestamp] [sender]: body`
- On `/chat <name>`: load last N messages from file into buffer
- On startup: optionally load last active conversation

**Benefits:**
- Conversation history survives restarts
- Can review messages from days/weeks ago
- Foundation for search/export features later

**Estimated effort:** 2-3 hours

### 3. Enhanced Notification Bar (Optional UX Improvement)
**Current:** Inline notifications in input prompt: `"> (2 new) "`
**Limitation:** Doesn't show WHO messages are from

**Proposed:** Dedicated 1-line notification window
```
┌────────────────────────────────┐
│ Chat: alice -> bob             │
│ [bob]: hey                     │
├────────────────────────────────┤
│ connor(2) alice(1)             │  ← NEW: shows per-sender unreads
├────────────────────────────────┤
│ > _                            │
└────────────────────────────────┘
```

**Changes needed:**
- Add `WINDOW *notif_win` (3-window layout)
- Add `show_notifications()` function
- Update window sizing (chat: max_y-4, notif: 1 line, input: 3 lines)

**Priority:** Lower (current inline notifications work fine)

### 4. Disconnect Message Clarity
**Current:** Shows generic `"disconnected from server"` in all cases
**Desired:** Show `"<username> disconnected"` when specific user goes offline

**Challenge:** Server doesn't currently notify which user disconnected
**Options:**
- Track recv errors per conversation
- Add server-side disconnect notifications (requires protocol change)

### 5. Error Handling Improvements
- Handle `send()` failures gracefully (currently silent)
- Notify sender if recipient not found (server currently silently drops)
- Handle server full (all MAX_CLIENTS slots taken)
- Better disconnect detection (distinguish server crash vs. user disconnect)

### 6. Contact List UI (Deferred - Lower Priority)
**Concept:** 4-pane layout with contact list sidebar
- Shows all known contacts
- Online/offline status indicators
- Arrow key navigation to switch conversations
- Visual indication of unreads

**Why deferred:**
- Requires server-side "who's online" broadcast (protocol change)
- Current inline notifications + `/chat` command work well enough
- Lower priority than persistence and encryption

---

## Roadmap - Future Phases

### Phase 2: Message Persistence (Next - 2-3 hours)
**Priority: High** - Needed for practical daily use
- Save conversations to `~/.wsg/chats/<contact>.log`
- Load history on `/chat <name>` and on startup
- Timestamp messages with proper formatting
- Handle file I/O errors gracefully
- Optional: `/search <query>` command to search history

### Phase 3: Server-Side Message Queue (Offline Delivery)
**Priority: Medium** - Solves offline user problem
**Current limitation:** If recipient is offline when message sent, message is dropped

**Two approaches:**
- **Option A:** Server queues plaintext messages (simple, but breaks "dumb server" philosophy)
- **Option B:** Defer until encryption - server queues encrypted blobs it can't read (privacy-preserving)

**Decision:** Defer until Phase 4 (encryption), implement as encrypted queue

### Phase 4: End-to-End Encryption (Complex - Major Project)
**Priority: High for production use, but requires significant learning**
- Study Signal Protocol: X3DH (key exchange) + Double Ratchet (forward secrecy)
- Implement using libsodium for crypto primitives
- Client-side encryption before sending (server sees only encrypted blobs)
- Server queues encrypted messages for offline users (can't read them)
- Key exchange protocol and key management
- Perfect forward secrecy (past messages safe even if keys compromised)

**Estimated effort:** 10-15 hours (learning + implementation)

### Phase 5: UX & Polish (Ongoing)
- Better disconnect messages (show username)
- Enhanced notification bar (per-sender breakdown)
- `/help` command listing available commands
- Typing indicators (optional - requires protocol change)
- Better error messages (recipient not found, connection lost, etc.)
- Command history (up/down arrow keys)

### Phase 6: Advanced Features (Long-term)
- Contact list pane (4-window layout with online status)
- Group messaging (requires protocol redesign)
- File transfer (encrypted)
- User authentication / password protection
- TLS for transport security (in addition to E2E encryption)
- Anonymity / metadata protection (Tor integration?)
- Mobile client (different UI, same protocol)
- Multi-device sync
