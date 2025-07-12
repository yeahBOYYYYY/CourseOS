#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "message_slot.h"

/**
 * Sends a message to a message slot device on a specific channel.
 * @usage:
 *   ./message_sender <device_file> <channel_id> <censorship_mode> <message>
 * @example:
 *   ./message_sender /dev/slot0 42 1 "hello world"
 * @param argc Argument count (should be 5).
 * @param argv Argument values.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char **argv) {
    int fd, ret;
    unsigned int channel_id, censorship;
    const char *device_path, *message;

    if (argc != 5) {
        fprintf(stderr, "Usage: %s <device_file> <channel_id> <censorship_mode> <message>\n", argv[0]);
        exit(1);
    }

    device_path = argv[1];
    channel_id = (unsigned int)atoi(argv[2]);
    censorship = (unsigned int)atoi(argv[3]);
    message = argv[4];

    if (channel_id == 0 || (censorship != 0 && censorship != 1)) {
        fprintf(stderr, "Invalid channel ID or censorship mode.\n");
        exit(1);
    }

    fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("open");
        exit(1);
    }

    ret = ioctl(fd, MSG_SLOT_SET_CEN, &censorship);
    if (ret < 0) {
        perror("ioctl - set censorship");
        close(fd);
        exit(1);
    }

    ret = ioctl(fd, MSG_SLOT_CHANNEL, &channel_id);
    if (ret < 0) {
        perror("ioctl - set channel");
        close(fd);
        exit(1);
    }

    ret = write(fd, message, strlen(message));
    if (ret < 0) {
        perror("write");
        close(fd);
        exit(1);
    }

    close(fd);
    exit(0);
}
