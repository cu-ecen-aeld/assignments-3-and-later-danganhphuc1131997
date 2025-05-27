#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#define PORT 9000
#define BACKLOG 5
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define INITIAL_PACKET_SIZE 4096 // Kích thước ban đầu của buffer động

static volatile int running = 1;
static int server_fd = -1;

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("Caught signal, exiting \r\n");
        syslog(LOG_INFO, "Caught signal, exiting");
        running = 0;
        if (server_fd >= 0) {
            shutdown(server_fd, SHUT_RDWR);
            close(server_fd);
        }
    }
}

int daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid > 0) {
        exit(0);
    }

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        return -1;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);

    return 0;
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
        }
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (daemon_mode) {
        if (daemonize() < 0) {
            close(server_fd);
            return -1;
        }
        syslog(LOG_INFO, "Running in daemon mode");
    }

    if (listen(server_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    while (running) {
        client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);
                continue;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        FILE *fp = fopen(FILE_PATH, "a+");
        if (!fp) {
            syslog(LOG_ERR, "Failed to open file %s: %s", FILE_PATH, strerror(errno));
            close(client_fd);
            continue;
        }

        flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        // Cấp phát bộ nhớ động cho packet_buffer
        size_t packet_capacity = INITIAL_PACKET_SIZE;
        char *packet_buffer = (char *)malloc(packet_capacity);
        if (!packet_buffer) {
            syslog(LOG_ERR, "Failed to allocate memory for packet_buffer");
            fclose(fp);
            close(client_fd);
            continue;
        }
        size_t packet_len = 0;

        while (running) {
            bytes_read = read(client_fd, buffer, BUFFER_SIZE);
            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                usleep(10000);
                continue;
            } else if (bytes_read <= 0) {
                // Client đóng kết nối hoặc lỗi, ghi dữ liệu còn lại nếu có
                if (packet_len > 0) {
                    bytes_written = fwrite(packet_buffer, 1, packet_len, fp);
                    if (bytes_written != packet_len) {
                        syslog(LOG_ERR, "Failed to write to file");
                    }
                    fflush(fp);
                }
                break;
            }

            // Nếu buffer không đủ, mở rộng bằng realloc
            if (packet_len + bytes_read > packet_capacity) {
                packet_capacity *= 2; // Tăng gấp đôi dung lượng
                char *new_buffer = (char *)realloc(packet_buffer, packet_capacity);
                if (!new_buffer) {
                    syslog(LOG_ERR, "Failed to reallocate memory for packet_buffer");
                    free(packet_buffer);
                    fclose(fp);
                    close(client_fd);
                    continue;
                }
                packet_buffer = new_buffer;
            }

            // Copy dữ liệu vào packet_buffer
            memcpy(packet_buffer + packet_len, buffer, bytes_read);
            packet_len += bytes_read;

            // Tìm ký tự xuống dòng
            int found_newline = 0;
            for (size_t i = packet_len - bytes_read; i < packet_len; i++) {
                if (packet_buffer[i] == '\n') {
                    found_newline = 1;
                    size_t chunk_len = i + 1; // Bao gồm cả \n
                    bytes_written = fwrite(packet_buffer, 1, chunk_len, fp);
                    if (bytes_written != chunk_len) {
                        syslog(LOG_ERR, "Failed to write to file");
                    }
                    fflush(fp);

                    // Dịch dữ liệu còn lại (nếu có) về đầu buffer
                    memmove(packet_buffer, packet_buffer + chunk_len, packet_len - chunk_len);
                    packet_len -= chunk_len;

                    // Gửi nội dung file về client
                    fseek(fp, 0, SEEK_SET);
                    char file_buf[BUFFER_SIZE];
                    size_t read_bytes;
                    while ((read_bytes = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
                        ssize_t sent = 0;
                        while (sent < read_bytes) {
                            ssize_t s = write(client_fd, file_buf + sent, read_bytes - sent);
                            if (s < 0) {
                                syslog(LOG_ERR, "Failed to send data to client: %s", strerror(errno));
                                break;
                            }
                            sent += s;
                        }
                    }
                    fseek(fp, 0, SEEK_END);
                    break; // Thoát vòng lặp tìm \n để xử lý dữ liệu tiếp theo
                }
            }

            // Nếu không tìm thấy \n, tiếp tục nhận dữ liệu
            if (!found_newline) {
                continue;
            }
        }

        if (bytes_read == 0) {
            syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
        } else if (bytes_read < 0) {
            syslog(LOG_ERR, "Read error: %s", strerror(errno));
        }

        free(packet_buffer); // Giải phóng bộ nhớ
        fclose(fp);
        close(client_fd);
    }

    printf("Closing program ... \r\n");

    if (server_fd >= 0) {
        close(server_fd);
    }

    if (remove(FILE_PATH) != 0) {
        syslog(LOG_ERR, "Failed to delete file %s: %s", FILE_PATH, strerror(errno));
    }

    closelog();
    return 0;
}