# COSC 5302 – Project 1: Multithreaded FTP Server

## Overview

This project implements a simple multithreaded FTP server and client in C on Linux using POSIX sockets and pthreads. The server supports three operations:

- **DOWNLOAD** – Client downloads a file from the server
- **DELETE** – Client deletes a file on the server
- **RENAME** – Client renames a file on the server

The server handles multiple concurrent clients by spawning a new detached thread per accepted connection.

---

## Files

| File | Description |
|------|-------------|
| `server.c` | Multithreaded FTP server |
| `client.c` | Interactive FTP client |
| `Makefile` | Build rules |
| `README.md` | This file |

---

## Requirements

- Linux / Unix system
- GCC compiler
- POSIX pthreads (`-lpthread`)

---

## Compilation

```bash
make
```

This produces two executables: `server` and `client`.

To clean up:
```bash
make clean
```

---

## Running

### Step 1 – Start the Server

The server listens on port **21000** by default. Run it from the directory containing the files you want to serve:

```bash
./server
```

Example output:
```
[Server] FTP server listening on port 21000 ...
```

### Step 2 – Run the Client

```bash
./client [server_ip] [port]
```

Arguments are optional and default to `127.0.0.1` and `21000`.

Example (same machine):
```bash
./client
```

Example (remote server):
```bash
./client 192.168.1.10 21000
```

---

## Client Commands

Once connected, use these commands at the `ftp>` prompt:

| Command | Description |
|---------|-------------|
| `download <filename>` | Download a file from the server to the current local directory |
| `delete <filename>` | Delete a file on the server |
| `rename <oldname> <newname>` | Rename a file on the server |
| `quit` | Disconnect from the server |

### Example Session

```
ftp> download notes.txt
[Client] Downloading 'notes.txt' (1024 bytes)...
[Client] Download complete: saved as 'notes.txt'.

ftp> rename draft.txt final.txt
[Client] Server: 200 OK

ftp> delete temp.log
[Client] Server: 200 OK

ftp> quit
[Client] Disconnected.
```

---

## Server Responses

| Response | Meaning |
|----------|---------|
| `200 OK` | Operation succeeded |
| `200 OK <size>` | Download: file follows with `<size>` bytes |
| `404 FILE_NOT_FOUND` | File does not exist |
| `500 ERROR` | General error (e.g., invalid path) |

---

## Architecture

- **server.c**: Creates a TCP socket, binds to port 21000, and enters an accept loop. Each accepted client connection is handed off to `client_handler()` running in its own detached `pthread`. A `pthread_mutex_t` (`fs_mutex`) serializes file-system operations (open/read, remove, rename) to prevent race conditions when multiple clients operate concurrently.

- **client.c**: Connects to the server and enters an interactive read-eval loop. Commands are sent as text lines; responses are parsed and file data is streamed to disk for DOWNLOAD.

---

## Testing with Multiple Clients

Open multiple terminals and run `./client` in each. All clients can interact with the server concurrently:

**Terminal 1:**
```bash
./client        # connect client 1
```

**Terminal 2:**
```bash
./client        # connect client 2
```

The server will log each connection and handle all operations concurrently.

---

## Security Notes

- Path traversal attacks (e.g., `../etc/passwd`) are rejected by the server.
- The server only serves files from its **working directory** (the directory it was launched from).
