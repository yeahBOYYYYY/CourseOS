#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string.h>
#include "message_slot.h"

MODULE_LICENSE("GPL");

#define MAX_CHANNELS 1048576 // 2**20
#define MAX_SLOTS 256

/**
 * Represents a stored message in a channel.
 * @param length Length of the message in bytes.
 * @param content Buffer containing the message content.
 */
typedef struct message {
    size_t length;
    char content[MAX_MESSAGE_LEN];
} message;

/**
 * Represents a single channel in a message slot.
 * @param id Unique identifier for the channel.
 * @param msg Pointer to the message stored in this channel.
 * @param next Pointer to the next channel in theslot.
 */
typedef struct channel {
    unsigned int id;
    message *msg;
    struct channel *next;
} channel;

/**
 * Represents a message slot device (identified by a minor number).
 * @param minor Minor device number for the slot.
 * @param channels Linked list of channels in this slot.
 * @param next Pointer to the next slot in the global list.
 */
typedef struct slot {
    int minor;
    channel *channels;
    struct slot *next;
} slot;

/**
 * Stores context for an open file descriptor.
 * @param channel_id ID of the currently selected channel (0 means not set).
 * @param censorship Censorship flag: 0 for off, 1 for on.
 */
typedef struct {
    unsigned int channel_id;
    int censorship;
} file_context;

static slot *slots = NULL;

// ========== Helper Functions ========================================

/**
 * Retrieve or create a slot by its minor number.
 * @param minor The minor number of the device file.
 * @return Pointer to the slot if found or successfully created, NULL on failure.
 */
static slot *get_slot(int minor) {
    slot *cur, *new_slot;

    cur = slots;
    while (cur) {
        if (cur->minor == minor) return cur;
        cur = cur->next;
    }

    // Create new slot
    new_slot = kmalloc(sizeof(slot), GFP_KERNEL);
    if (!new_slot) return NULL;

    new_slot->minor = minor;
    new_slot->channels = NULL;
    new_slot->next = slots;
    slots = new_slot;
    return new_slot;
}

/**
 * Retrieve or create a channel by ID within a given slot.
 * @param s Pointer to the parent slot.
 * @param id Channel ID to look for or create.
 * @return Pointer to the channel if found or successfully created, NULL on failure.
 */
static channel *get_channel(slot *s, unsigned int id) {
    channel *new_channel;

    channel *cur = s->channels;
    while (cur) {
        if (cur->id == id) return cur;
        cur = cur->next;
    }

    // Create new channel
    new_channel = kmalloc(sizeof(channel), GFP_KERNEL);
    if (!new_channel) return NULL;

    new_channel->id = id;
    new_channel->msg = NULL;
    new_channel->next = s->channels;
    s->channels = new_channel;
    return new_channel;
}

/**
 * Apply censorship to a message, replaces every third character with '#'.
 * @param dst Destination buffer for the censored message.
 * @param src Source buffer containing the original message.
 * @param len Length of the message in bytes.
 */
static void censor_message(char *dst, const char *src, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        dst[i] = ((i + 1) % 3 == 0) ? '#' : src[i];
    }
}

//========== File Operations ========================================

/**
 * Called when a process opens the device file.
 * @param inode Pointer to the inode structure.
 * @param file Pointer to the file structure.
 * @return 0 on success, -ENOMEM on allocation failure.
 */
static int device_open(struct inode *inode, struct file *file) {
    file_context *ctx = kmalloc(sizeof(file_context), GFP_KERNEL);
    if (!ctx) return -ENOMEM;
    ctx->channel_id = 0;
    ctx->censorship = 0;
    file->private_data = ctx;
    return 0;
}

/**
 * Called when a process closes the device file.
 * @param inode Pointer to the inode structure.
 * @param file Pointer to the file structure.
 * @return 0 always.
 */
static int device_release(struct inode *inode, struct file *file) {
    kfree(file->private_data);
    return 0;
}

/**
 * Handles ioctl operations (channel selection and censorship).
 * @param file Pointer to the file structure.
 * @param cmd ioctl command (MSG_SLOT_CHANNEL or MSG_SLOT_SET_CEN).
 * @param arg Pointer to the unsigned int parameter from user space.
 * @return 0 on success, or -EINVAL/-EFAULT on error.
 */
static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    file_context *ctx = (file_context *)file->private_data;
    unsigned int val;

    if (cmd != MSG_SLOT_CHANNEL && cmd != MSG_SLOT_SET_CEN)
        return -EINVAL;

    if (copy_from_user(&val, (unsigned int __user *)arg, sizeof(unsigned int)))
        return -EFAULT;

    if (cmd == MSG_SLOT_CHANNEL) {
        if (val == 0)
            return -EINVAL;
        ctx->channel_id = val;
    } else if (cmd == MSG_SLOT_SET_CEN) {
        if (val != 0 && val != 1)
            return -EINVAL;
        ctx->censorship = val;
    }

    return 0;
}

/**
 * Writes a message to the selected channel.
 * @param file Pointer to the file structure.
 * @param buffer Pointer to user-provided message buffer.
 * @param length Length of the message in bytes.
 * @param offset Not used.
 * @return Number of bytes written on success, negative error code on failure.
 */
static ssize_t device_write(struct file *file, const char __user *buffer, size_t length, loff_t *offset) {
    char *msg_buf;
    int minor;
    channel *ch;
    slot *s;

    file_context *ctx = (file_context *)file->private_data;
    if (ctx->channel_id == 0) return -EINVAL;
    if (length == 0 || length > MAX_MESSAGE_LEN) return -EMSGSIZE;

    msg_buf = kmalloc(length, GFP_KERNEL);
    if (!msg_buf) return -ENOMEM;

    if (copy_from_user(msg_buf, buffer, length)) {
        kfree(msg_buf);
        return -EFAULT;
    }

    minor = iminor(file_inode(file));
    s = get_slot(minor);
    if (!s) {
        kfree(msg_buf);
        return -ENOMEM;
    }

    ch = get_channel(s, ctx->channel_id);
    if (!ch) {
        kfree(msg_buf);
        return -ENOMEM;
    }

    if (!ch->msg)
        ch->msg = kmalloc(sizeof(message), GFP_KERNEL);
    if (!ch->msg) {
        kfree(msg_buf);
        return -ENOMEM;
    }

    ch->msg->length = length;
    if (ctx->censorship)
        censor_message(ch->msg->content, msg_buf, length);
    else
        memcpy(ch->msg->content, msg_buf, length);

    kfree(msg_buf);
    return length;
}

/**
 * Reads the last written message from the selected channel.
 * @param file Pointer to the file structure.
 * @param buffer Destination user-space buffer.
 * @param length Maximum number of bytes to copy.
 * @param offset Not used.
 * @return Number of bytes read on success, negative error code on failure.
 */
static ssize_t device_read(struct file *file, char __user *buffer, size_t length, loff_t *offset) {
    int minor;
    channel *ch;
    slot *s;

    file_context *ctx = (file_context *)file->private_data;
    if (ctx->channel_id == 0) return -EINVAL;

    minor = iminor(file_inode(file));
    s = get_slot(minor);
    if (!s) return -EWOULDBLOCK;

    ch = get_channel(s, ctx->channel_id);
    if (!ch || !ch->msg) return -EWOULDBLOCK;

    if (length < ch->msg->length)
        return -ENOSPC;

    if (copy_to_user(buffer, ch->msg->content, ch->msg->length))
        return -EFAULT;

    return ch->msg->length;
}

// ========== Module setup ========================================

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .read = device_read,
    .write = device_write,
    .unlocked_ioctl = device_ioctl,
    .release = device_release
};

/**
 * Module initialization function, registers the character device with major number MAJOR_NUM.
 * @return 0 on success, or a negative error code on failure.
 */
static int __init msgslot_init(void) {
    int ret = register_chrdev(MAJOR_NUM, "message_slot", &fops);
    if (ret < 0) {
        printk(KERN_ERR "message_slot: failed to register device.\n");
        return ret;
    }
    return 0;
}

/**
 * Module cleanup function, frees all allocated memory and unregisters the device driver.
 */
static void __exit msgslot_cleanup(void) {
    slot *s, *tmp_s;
    channel *ch, *tmp_ch;

    s = slots;
    while (s) {
        ch = s->channels;
        while (ch) {
            if (ch->msg) kfree(ch->msg);
            tmp_ch = ch;
            ch = ch->next;
            kfree(tmp_ch);
        }
        tmp_s = s;
        s = s->next;
        kfree(tmp_s);
    }
    unregister_chrdev(MAJOR_NUM, "message_slot");
}

module_init(msgslot_init);
module_exit(msgslot_cleanup);
