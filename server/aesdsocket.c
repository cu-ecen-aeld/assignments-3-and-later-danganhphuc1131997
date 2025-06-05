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
#include <pthread.h>
#include <time.h>

#define PORT 9000
#define BACKLOG 5
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define INITIAL_PACKET_SIZE 4096
#define TIMESTAMP_INTERVAL 10 // 10 seconds

static volatile int running = 1;
static int server_fd = -1;
static pthread_mutex_t mutex_file = PTHREAD_MUTEX_INITIALIZER;

// Linked list node for thread management
struct thread_node {
    pthread_t thread;
    struct thread_node *next;
};

static struct thread_node *thread_list = NULL;

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

void add_thread(pthread_t thread) {
    struct thread_node *node = malloc(sizeof(struct thread_node));
    if (node) {
        node->thread = thread;
        node->next = thread_list;
        thread_list = node;
    }
}

// Function to free the thread list
void free_thread_list() {
    struct thread_node *current = thread_list;
    while (current) {
        struct thread_node *temp = current;
        current = current->next;
        free(temp);
    }
    thread_list = NULL;
}

void append_timestamp() {
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z", tm_info);

    pthread_mutex_lock(&mutex_file);
    FILE *fp = fopen(FILE_PATH, "a");
    if (fp) {
        fprintf(fp, "%s\n", timestamp);
        fflush(fp);
        fclose(fp);
    } else {
        syslog(LOG_ERR, "Failed to open file %s for timestamp: %s", FILE_PATH, strerror(errno));
    }
    pthread_mutex_unlock(&mutex_file);
}

void *handle_connection(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char client_ip[INET_ADDRSTRLEN] = "unknown";
    
    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &client_len) == 0) {
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    } else {
        syslog(LOG_ERR, "Failed to get peer name: %s", strerror(errno));
    }
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    FILE *fp = fopen(FILE_PATH, "a+");
    if (!fp) {
        syslog(LOG_ERR, "Failed to open file %s: %s", FILE_PATH, strerror(errno));
        close(client_fd);
        return NULL;
    }

    char buffer[BUFFER_SIZE];
    size_t packet_capacity = INITIAL_PACKET_SIZE;
    char *packet_buffer = (char *)malloc(packet_capacity);
    if (!packet_buffer) {
        syslog(LOG_ERR, "Failed to allocate memory for packet_buffer");
        fclose(fp);
        close(client_fd);
        return NULL;
    }
    size_t packet_len = 0;

    while (running) {
        ssize_t bytes_read = read(client_fd, buffer, BUFFER_SIZE);
        if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(10000);
            continue;
        } else if (bytes_read <= 0) {
            if (bytes_read == 0) {
                syslog(LOG_INFO, "Closed connection from %s", client_ip);
            } else {
                syslog(LOG_ERR, "Read error from %s: %s", client_ip, strerror(errno));
            }
            break;
        }

        if (packet_len + bytes_read > packet_capacity) {
            packet_capacity *= 2;
            char *new_buffer = realloc(packet_buffer, packet_capacity);
            if (!new_buffer) {
                syslog(LOG_ERR, "Failed to reallocate memory for packet_buffer");
                free(packet_buffer);
                fclose(fp);
                close(client_fd);
                return NULL;
            }
            packet_buffer = new_buffer;
        }

        memcpy(packet_buffer + packet_len, buffer, bytes_read);
        packet_len += bytes_read;

        for (size_t i = packet_len - bytes_read; i < packet_len; i++) {
            if (packet_buffer[i] == '\n') {
                size_t chunk_len = i + 1;
                pthread_mutex_lock(&mutex_file);
                fwrite(packet_buffer, 1, chunk_len, fp);
                fflush(fp);
                pthread_mutex_unlock(&mutex_file);
                memmove(packet_buffer, packet_buffer + chunk_len, packet_len - chunk_len);
                packet_len -= chunk_len;

                pthread_mutex_lock(&mutex_file);
                fseek(fp, 0, SEEK_SET);
                char file_buf[BUFFER_SIZE];
                size_t read_bytes;
                while ((read_bytes = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
                    ssize_t sent = 0;
                    while (sent < read_bytes) {
                        ssize_t s = write(client_fd, file_buf + sent, read_bytes - sent);
                        if (s < 0) {
                            syslog(LOG_ERR, "Failed to send data to client %s: %s", client_ip, strerror(errno));
                            break;
                        }
                        sent += s;
                    }
                }
                fseek(fp, 0, SEEK_END);
                pthread_mutex_unlock(&mutex_file);
                break;
            }
        }
    }

    free(packet_buffer);
    fclose(fp);
    close(client_fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    time_t last_timestamp = 0;

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
        // Check and append timestamp every 10 seconds
        time_t now = time(NULL);
        if (now - last_timestamp >= TIMESTAMP_INTERVAL) {
            append_timestamp();
            last_timestamp = now;
        }

        client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            syslog(LOG_ERR, "Failed to allocate memory for client_fd");
            continue;
        }
        *client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (*client_fd < 0) {
            free(client_fd);
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);
                continue;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_connection, client_fd) != 0) {
            syslog(LOG_ERR, "Failed to create thread: %s", strerror(errno));
            free(client_fd);
            close(*client_fd);
            continue;
        }
        add_thread(thread);
    }

    // Join all threads and free thread list
    struct thread_node *current = thread_list;
    while (current) {
        pthread_join(current->thread, NULL);
        struct thread_node *temp = current;
        current = current->next;
        free(temp);
    }
    thread_list = NULL; // Ensure list is cleared

    printf("Closing program ... \r\n");

    pthread_mutex_destroy(&mutex_file);
    if (server_fd >= 0) {
        close(server_fd);
    }

    if (remove(FILE_PATH) != 0) {
        syslog(LOG_ERR, "Failed to delete file %s: %s", FILE_PATH, strerror(errno));
    }

    closelog();
    return 0;
}