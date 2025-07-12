#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>

#define BUFFER_SIZE 1048576 // 1MB buffer size

// ========== Function Declarations ==========

void handle_SIGINT(int signum);
void display_statistics();
void setup_signal_handler();
void setup_server(int *server_fd, const char *port);
void server_loop(int server_fd);
void handle_client_connection(int client_fd);
void receive_data(int client_fd, uint32_t *file_size, uint32_t *printable_count, uint32_t temp_pcc_total[95]);
void send_printable_count(int client_fd, uint32_t printable_count);

// ========== Global Variables ==========

uint32_t pcc_total[95];
int client_socket_fd = -1;
int is_server_running = 1;

// ========== Function Definitions ==========

int main(int argc, char *argv[]) {
    if (argc != 2) {
        perror("Usage: <server port>\n");
        exit(EXIT_FAILURE);
    }

    int server_socket_fd;

    setup_server(&server_socket_fd, argv[1]);
    setup_signal_handler();
    server_loop(server_socket_fd);
    display_statistics();
    return 0;
}

/**
 * Sets up signal handling for SIGINT to trigger stats display.
 */
void setup_signal_handler() {
    struct sigaction sigint_action = {
        .sa_handler = handle_SIGINT,
        .sa_flags = SA_RESTART
    };
    if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
        perror("Signal handler registration failed");
        exit(EXIT_FAILURE);
    }
}

/**
 * Initializes the server socket, binds it, listens for connections,
 * and resets the printable character statistics.
 *
 * @param server_fd Pointer to the socket file descriptor to initialize.
 * @param port String representation of the port number to bind to.
 */
void setup_server(int *server_fd, const char *port) {
    int enable = 1;
    struct sockaddr_in server_address;

    *server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(atoi(port));

    if (bind(*server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) != 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(*server_fd, 10) != 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    memset(pcc_total, 0, sizeof(pcc_total));
}

/**
 * Signal handler for SIGINT. If no client is connected,
 * displays the printable character statistics and exits.
 *
 * @param signum The signal number received (expected: SIGINT).
 */
void handle_SIGINT(int signum) {
    if (client_socket_fd == -1) {
        display_statistics();
    }
    is_server_running = 0;
}

/**
 * Main server loop that accepts and handles client connections
 * until a termination signal is received.
 *
 * @param server_fd The listening socket file descriptor.
 */
void server_loop(int server_fd) {
    while (is_server_running) {
        client_socket_fd = accept(server_fd, NULL, NULL);
        if (client_socket_fd < 0) {
            perror("Accept failed");
            continue;
        }

        handle_client_connection(client_socket_fd);

        close(client_socket_fd);
        client_socket_fd = -1;
    }
}

/**
 * Processes a single client connection:
 * receives file data, calculates printable characters,
 * sends the result, and updates global statistics.
 *
 * @param client_fd The socket file descriptor for the connected client.
 */
void handle_client_connection(int client_fd) {
    uint32_t temp_pcc_total[95] = {0};
    uint32_t file_size = 0, printable_count = 0;

    receive_data(client_fd, &file_size, &printable_count, temp_pcc_total);
    send_printable_count(client_fd, printable_count);

    for (int i = 0; i < 95; i++) {
        pcc_total[i] += temp_pcc_total[i];
    }
}

/**
 * Receives the file size and contents from the client,
 * counts printable characters, and populates a temporary character count table.
 *
 * @param client_fd The socket file descriptor connected to the client.
 * @param file_size Output: pointer to where the received file size will be stored (host byte order).
 * @param printable_count Output: pointer to store the count of printable characters.
 * @param temp_pcc_total Output: array storing per-character printable counts (for ASCII 32-126).
 */
void receive_data(int client_fd, uint32_t *file_size, uint32_t *printable_count, uint32_t temp_pcc_total[95]) {
    int total_received = 0, remaining = 4, cur_received;
    char buffer[BUFFER_SIZE];

    // Receive 4-byte file size
    while (remaining > 0) {
        cur_received = read(client_fd, (char *)file_size + total_received, remaining);
        if (cur_received <= 0) {
            perror("Error receiving file size");
            close(client_fd);
            exit(EXIT_FAILURE);
        }
        total_received += cur_received;
        remaining -= cur_received;
    }
    *file_size = ntohl(*file_size);

    // Receive file content
    remaining = *file_size;
    while (remaining > 0) {
        int chunk_size = BUFFER_SIZE < remaining ? BUFFER_SIZE : remaining;
        cur_received = read(client_fd, buffer, chunk_size);
        if (cur_received <= 0) {
            perror("Error receiving file content");
            close(client_fd);
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < cur_received; i++) {
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                temp_pcc_total[(int)(buffer[i]) - 32]++;
                (*printable_count)++;
            }
        }

        remaining -= cur_received;
    }

    *printable_count = htonl(*printable_count);
}

/**
 * Sends the number of printable characters back to the client.
 *
 * @param client_fd The socket file descriptor connected to the client.
 * @param printable_count The printable character count to send (in network byte order).
 */
void send_printable_count(int client_fd, uint32_t printable_count) {
    int total_sent = 0, remaining = 4, cur_sent;

    while (remaining > 0) {
        cur_sent = write(client_fd, (char *)&printable_count + total_sent, remaining);
        if (cur_sent <= 0) {
            perror("Error sending printable count");
            close(client_fd);
            exit(EXIT_FAILURE);
        }
        total_sent += cur_sent;
        remaining -= cur_sent;
    }
}

/**
 * Prints and exits with the full printable character histogram.
 */
void display_statistics() {
    for (int i = 0; i < 95; i++) {
        printf("char '%c' : %u times\n", (char)(i + 32), pcc_total[i]);
    }
    exit(0);
}
