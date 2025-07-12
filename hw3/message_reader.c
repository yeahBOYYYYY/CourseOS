#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "message_slot.h"

/**
 * Reads a message from a message slot device on a specific channel.
 * @usage:
 *   ./message_reader <device_file> <channel_id>
 * @example:
 *   ./message_reader /dev/slot0 42
 * @param argc Argument count (should be 3).
 * @param argv Argument values.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char **argv) {
    int fd, ret;
    unsigned int channel_id;
    const char *device_path;
    char buffer[MAX_MESSAGE_LEN];

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <device_file> <channel_id>\n", argv[0]);
        exit(1);
    }

    device_path = argv[1];
    channel_id = (unsigned int)atoi(argv[2]);

    if (channel_id == 0) {
        fprintf(stderr, "Invalid channel ID.\n");
        exit(1);
    }

    fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(1);
    }

    ret = ioctl(fd, MSG_SLOT_CHANNEL, &channel_id);
    if (ret < 0) {
        perror("ioctl - set channel");
        close(fd);
        exit(1);
    }

    ret = read(fd, buffer, MAX_MESSAGE_LEN);
    if (ret < 0) {
        perror("read");
        close(fd);
        exit(1);
    }

    // Write only the message content to stdout
    if (write(STDOUT_FILENO, buffer, ret) != ret) {
        perror("write to stdout");
        close(fd);
        exit(1);
    }

    close(fd);
    exit(0);
}
