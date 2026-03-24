/*
 * COSC 5302 - Project 1: FTP Client
 * client.c
 *
 * Interactive client that connects to the FTP server and supports:
 *   download <filename>
 *   delete   <filename>
 *   rename   <oldname> <newname>
 *   quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT  21000
#define BUFFER_SIZE   4096
#define CMD_SIZE      512

static int send_all(int sock, const void *buf, size_t len) {
    size_t sent = 0;
    const char *ptr = (const char *)buf;
    while (sent < len) {
        ssize_t n = send(sock, ptr + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}


/* Receive one line (newline-terminated) */
static int recv_line(int sock, char *buf, int maxlen) {
    int total = 0;
    char c;
    while (total < maxlen - 1) {
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') break;
        buf[total++] = c;
    }
    buf[total] = '\0';
    if (total > 0 && buf[total-1] == '\r') buf[--total] = '\0';
    return total;
}

/* ---- DOWNLOAD ---- */
static void do_download(int sock, const char *filename) {
    /* Send command */
    char cmd[CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", filename);
    if (send_all(sock, cmd, strlen(cmd)) < 0) {
        fprintf(stderr, "[Client] Send error.\n"); return;
    }

    /* Read response header */
    char response[CMD_SIZE];
    if (recv_line(sock, response, sizeof(response)) < 0) {
        fprintf(stderr, "[Client] No response.\n"); return;
    }

    if (strncmp(response, "200 OK", 6) != 0) {
        printf("[Client] Server: %s\n", response); return;
    }

    /* Parse file size from "200 OK <size>" */
    long filesize = 0;
    sscanf(response, "200 OK %ld", &filesize);
    printf("[Client] Downloading '%s' (%ld bytes)...\n", filename, filesize);

    /* Receive file and save locally */
    FILE *fp = fopen(filename, "wb");
    if (!fp) { perror("fopen"); return; }

    long remaining = filesize;
    char buf[BUFFER_SIZE];
    while (remaining > 0) {
        size_t want = (remaining < BUFFER_SIZE) ? (size_t)remaining : BUFFER_SIZE;
        ssize_t n = recv(sock, buf, want, 0);
        if (n <= 0) { fprintf(stderr, "[Client] Connection lost during download.\n"); break; }
        fwrite(buf, 1, n, fp);
        remaining -= n;
    }
    fclose(fp);
    if (remaining == 0)
        printf("[Client] Download complete: saved as '%s'.\n", filename);
}

/* ---- DELETE ---- */
static void do_delete(int sock, const char *filename) {
    char cmd[CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "DELETE %s\n", filename);
    send_all(sock, cmd, strlen(cmd));
    char response[CMD_SIZE];
    recv_line(sock, response, sizeof(response));
    printf("[Client] Server: %s\n", response);
}

/* ---- RENAME ---- */
static void do_rename(int sock, const char *oldname, const char *newname) {
    char cmd[CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "RENAME %s %s\n", oldname, newname);
    send_all(sock, cmd, strlen(cmd));
    char response[CMD_SIZE];
    recv_line(sock, response, sizeof(response));
    printf("[Client] Server: %s\n", response);
}

/* ---- main ---- */
int main(int argc, char *argv[]) {
    const char *server_ip = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc >= 2) server_ip = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", server_ip); exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect"); exit(EXIT_FAILURE);
    }
    printf("[Client] Connected to %s:%d\n", server_ip, port);
    printf("[Client] Commands: download <file> | delete <file> | rename <old> <new> | quit\n\n");

    char input[CMD_SIZE];
    char op[64], a1[256], a2[256];

    while (1) {
        printf("ftp> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        /* strip newline */
        input[strcspn(input, "\n")] = '\0';
        if (strlen(input) == 0) continue;

        memset(op,0,sizeof(op)); memset(a1,0,sizeof(a1)); memset(a2,0,sizeof(a2));
        int parsed = sscanf(input, "%63s %255s %255s", op, a1, a2);

        if (strcasecmp(op, "quit") == 0 || strcasecmp(op, "exit") == 0) {
            send_all(sock, "QUIT\n", 5);
            break;
        } else if (strcasecmp(op, "download") == 0 && parsed >= 2) {
            do_download(sock, a1);
        } else if (strcasecmp(op, "delete") == 0 && parsed >= 2) {
            do_delete(sock, a1);
        } else if (strcasecmp(op, "rename") == 0 && parsed >= 3) {
            do_rename(sock, a1, a2);
        } else {
            printf("Usage:\n"
                   "  download <filename>\n"
                   "  delete   <filename>\n"
                   "  rename   <oldname> <newname>\n"
                   "  quit\n");
        }
    }

    close(sock);
    printf("[Client] Disconnected.\n");
    return 0;
}
