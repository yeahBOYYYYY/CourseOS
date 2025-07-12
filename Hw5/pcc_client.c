#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#define BUFFER_SIZE 1048576 // 1MB buffer size

// ========== Function Declarations ==========

void parse_arguments(int argc, char *argv[], struct in_addr *server_ip, uint16_t *server_port, char **file_path);
int open_and_get_file_size(const char *file_path, uint32_t *file_size);
int connect_to_server(struct in_addr server_ip, uint16_t server_port);
void send_file(int sockfd, int fd, uint32_t file_size);
uint32_t receive_printable_count(int sockfd);

// ========== Function Definitions ==========

int main(int argc, char *argv[]) {
    struct in_addr server_ip;
    uint16_t server_port;
    char *file_path;
    uint32_t file_size;
    int fd, sockfd;
    uint32_t printable_chars;

    parse_arguments(argc, argv, &server_ip, &server_port, &file_path);
    fd = open_and_get_file_size(file_path, &file_size);
    sockfd = connect_to_server(server_ip, server_port);
    send_file(sockfd, fd, file_size);
    printable_chars = receive_printable_count(sockfd);

    printf("# of printable characters: %u\n", printable_chars);

    close(fd);
    close(sockfd);
    return 0;
}

/**
 * Parses and validates the command-line arguments.
 *
 * @param argc The number of command-line arguments.
 * @param argv The array of argument strings.
 * @param server_ip Output parameter to hold parsed server IP.
 * @param server_port Output parameter to hold parsed port number.
 * @param file_path Output parameter to hold file path string.
 */
void parse_arguments(int argc, char *argv[], struct in_addr *server_ip, uint16_t *server_port, char **file_path) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <file_path>\n", argv[0]);
        exit(1);
    }

    if (inet_pton(AF_INET, argv[1], server_ip) != 1) {
        perror("Invalid server IP address");
        exit(1);
    }

    *server_port = (uint16_t)atoi(argv[2]);
    *file_path = argv[3];
}

/**
 * Opens a file and retrieves its size.
 *
 * @param file_path Path to the file.
 * @param file_size Output parameter to hold the file size.
 * @return File descriptor of the opened file.
 */
int open_and_get_file_size(const char *file_path, uint32_t *file_size) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        exit(1);
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size == (off_t)-1) {
        perror("Error getting file size");
        exit(1);
    }

    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
        perror("Error resetting file pointer");
        exit(1);
    }

    *file_size = (uint32_t)size;
    return fd;
}

/**
 * Establishes a TCP connection to the server.
 *
 * @param server_ip IP address of the server.
 * @param server_port Port number of the server.
 * @return Socket file descriptor connected to the server.
 */
int connect_to_server(struct in_addr server_ip, uint16_t server_port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating socket");
        exit(1);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr = server_ip
    };

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to server");
        exit(1);
    }

    return sockfd;
}

/**
 * Sends the file size and file contents to the server.
 *
 * @param sockfd The socket file descriptor connected to the server.
 * @param fd The file descriptor of the file to send.
 * @param file_size The size of the file in bytes.
 */
void send_file(int sockfd, int fd, uint32_t file_size) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;

    uint32_t net_file_size = htonl(file_size);
    if (write(sockfd, &net_file_size, sizeof(net_file_size)) != sizeof(net_file_size)) {
        perror("Error sending file size to server");
        exit(1);
    }

    while ((bytes_read = read(fd, buffer, BUFFER_SIZE)) > 0) {
        bytes_written = write(sockfd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("Error sending file contents to server");
            exit(1);
        }
    }

    if (bytes_read == -1) {
        perror("Error reading file");
        exit(1);
    }
}

/**
 * Receives the number of printable characters reported by the server.
 *
 * @param sockfd The socket file descriptor connected to the server.
 * @return The number of printable characters.
 */
uint32_t receive_printable_count(int sockfd) {
    uint32_t net_count;
    if (read(sockfd, &net_count, sizeof(net_count)) != sizeof(net_count)) {
        perror("Error receiving number of printable characters from server");
        exit(1);
    }
    return ntohl(net_count);
}
