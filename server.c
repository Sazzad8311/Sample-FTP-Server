/*
 * COSC 5302 - Project 1: Multithreaded FTP Server
 * server.c
 *
 * Supports: DOWNLOAD, DELETE, RENAME
 * Uses POSIX threads for concurrent client handling.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define SERVER_PORT   21000
#define BACKLOG       10
#define BUFFER_SIZE   4096
#define CMD_SIZE      256

#define CMD_DOWNLOAD  "DOWNLOAD"
#define CMD_DELETE    "DELETE"
#define CMD_RENAME    "RENAME"
#define CMD_QUIT      "QUIT"

#define RESP_OK       "200 OK\n"
#define RESP_ERR      "500 ERROR\n"
#define RESP_NOFILE   "404 FILE_NOT_FOUND\n"

pthread_mutex_t fs_mutex = PTHREAD_MUTEX_INITIALIZER;

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

static void handle_download(int client_sock, const char *filename) {
    pthread_mutex_lock(&fs_mutex);
    if (strstr(filename, "..") || strchr(filename, '/')) {
        send_all(client_sock, RESP_ERR, strlen(RESP_ERR));
        pthread_mutex_unlock(&fs_mutex);
        return;
    }
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        send_all(client_sock, RESP_NOFILE, strlen(RESP_NOFILE));
        pthread_mutex_unlock(&fs_mutex);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char header[CMD_SIZE];
    snprintf(header, sizeof(header), "200 OK %ld\n", filesize);
    send_all(client_sock, header, strlen(header));
    char buf[BUFFER_SIZE];
    size_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0)
        if (send_all(client_sock, buf, nread) < 0) break;
    fclose(fp);
    pthread_mutex_unlock(&fs_mutex);
    printf("[Server] DOWNLOAD '%s' (%ld bytes) sent.\n", filename, filesize);
}

static void handle_delete(int client_sock, const char *filename) {
    if (strstr(filename, "..") || strchr(filename, '/')) {
        send_all(client_sock, RESP_ERR, strlen(RESP_ERR));
        return;
    }
    pthread_mutex_lock(&fs_mutex);
    int ret = remove(filename);
    pthread_mutex_unlock(&fs_mutex);
    if (ret == 0) {
        send_all(client_sock, RESP_OK, strlen(RESP_OK));
        printf("[Server] DELETE '%s' successful.\n", filename);
    } else {
        send_all(client_sock, errno == ENOENT ? RESP_NOFILE : RESP_ERR,
                 errno == ENOENT ? strlen(RESP_NOFILE) : strlen(RESP_ERR));
        perror("[Server] DELETE error");
    }
}

static void handle_rename(int client_sock, const char *oldname, const char *newname) {
    if (strstr(oldname,"..") || strchr(oldname,'/') ||
        strstr(newname,"..") || strchr(newname,'/')) {
        send_all(client_sock, RESP_ERR, strlen(RESP_ERR));
        return;
    }
    pthread_mutex_lock(&fs_mutex);
    int ret = rename(oldname, newname);
    pthread_mutex_unlock(&fs_mutex);
    if (ret == 0) {
        send_all(client_sock, RESP_OK, strlen(RESP_OK));
        printf("[Server] RENAME '%s' -> '%s' successful.\n", oldname, newname);
    } else {
        send_all(client_sock, errno == ENOENT ? RESP_NOFILE : RESP_ERR,
                 errno == ENOENT ? strlen(RESP_NOFILE) : strlen(RESP_ERR));
        perror("[Server] RENAME error");
    }
}

static void *client_handler(void *arg) {
    int client_sock = *(int *)arg;
    free(arg);
    char cmd_line[CMD_SIZE];
    char cmd[CMD_SIZE], arg1[CMD_SIZE], arg2[CMD_SIZE];
    printf("[Server] Client connected (fd=%d)\n", client_sock);
    while (1) {
        memset(cmd_line, 0, sizeof(cmd_line));
        if (recv_line(client_sock, cmd_line, sizeof(cmd_line)) < 0) break;
        memset(cmd,0,sizeof(cmd)); memset(arg1,0,sizeof(arg1)); memset(arg2,0,sizeof(arg2));
        int parsed = sscanf(cmd_line, "%255s %255s %255s", cmd, arg1, arg2);
        if (strcmp(cmd, CMD_QUIT) == 0) {
            printf("[Server] Client (fd=%d) quit.\n", client_sock);
            break;
        } else if (strcmp(cmd, CMD_DOWNLOAD) == 0 && parsed >= 2)
            handle_download(client_sock, arg1);
        else if (strcmp(cmd, CMD_DELETE) == 0 && parsed >= 2)
            handle_delete(client_sock, arg1);
        else if (strcmp(cmd, CMD_RENAME) == 0 && parsed >= 3)
            handle_rename(client_sock, arg1, arg2);
        else {
            fprintf(stderr, "[Server] Unknown command: '%s'\n", cmd_line);
            send_all(client_sock, RESP_ERR, strlen(RESP_ERR));
        }
    }
    close(client_sock);
    return NULL;
}

int main(void) {
    int server_sock;
    struct sockaddr_in server_addr;
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) { perror("socket"); exit(EXIT_FAILURE); }
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(SERVER_PORT);
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); close(server_sock); exit(EXIT_FAILURE);
    }
    if (listen(server_sock, BACKLOG) < 0) {
        perror("listen"); close(server_sock); exit(EXIT_FAILURE);
    }
    printf("[Server] FTP server listening on port %d ...\n", SERVER_PORT);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int *client_sock = malloc(sizeof(int));
        if (!client_sock) { perror("malloc"); continue; }
        *client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (*client_sock < 0) { perror("accept"); free(client_sock); continue; }
        printf("[Server] Connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, client_handler, client_sock) != 0) {
            perror("pthread_create"); close(*client_sock); free(client_sock);
        }
        pthread_attr_destroy(&attr);
    }
    close(server_sock);
    return 0;
}
