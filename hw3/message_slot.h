#ifndef MESSAGE_SLOT_H
#define MESSAGE_SLOT_H

#include <linux/ioctl.h>

#define MAJOR_NUM 235

// Maximum message size
#define MAX_MESSAGE_LEN 128

// ioctl command to set channel
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned int)

// ioctl command to set censorship
#define MSG_SLOT_SET_CEN _IOW(MAJOR_NUM, 1, unsigned int)

#endif
