#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <threads.h>
#include <stdbool.h>
#include "queue.h"

// ==================== Structures ====================

/**
 * Node structure for the queue.
 *
 * @param data Pointer to the item stored in the node.
 * @param next Pointer to the next node in the queue.
 */
typedef struct Node {
    void *data;
    struct Node *next;
} Node;

/**
 * Thread-safe FIFO queue structure.
 *
 * @param head Pointer to the first node in the queue.
 * @param tail Pointer to the last node in the queue.
 * @param size Current number of elements in the queue.
 * @param visited_items Total number of dequeue completions.
 */
typedef struct Queue {
    Node *head;
    Node *tail;
    size_t size;
    size_t visited_items;
} Queue;

/**
 * Node for the condition variable queue.
 *
 * @param cond Condition variable used for blocking/waking a thread.
 * @param id ID of the thread associated with this node.
 * @param next Pointer to the next condition node in the wait queue.
 */
typedef struct CondNode {
    cnd_t cond;
    thrd_t id;
    struct CondNode *next;
} CondNode;

/**
 * Queue for managing condition variable nodes.
 *
 * @param head Pointer to the first CondNode in the queue.
 * @param tail Pointer to the last CondNode in the queue.
 */
typedef struct {
    CondNode *head;
    CondNode *tail;
} CondQueue;

// ==================== Global Variables ====================

CondQueue condQueue;
Queue queue;
mtx_t lock;

// ==================== Queue Functions ====================

/**
 * Initializes the global queue and condition queue.
 */
void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    queue.size = 0;
    queue.visited_items = 0;
    mtx_init(&lock, mtx_plain);
}

/**
 * Destroys the queue and condition queue, freeing all memory.
 */
void destroyQueue(void) {
    mtx_lock(&lock);
    while (queue.head != NULL) {
        Node *temp = queue.head;
        queue.head = queue.head->next;
        free(temp);
    }
    queue.head = NULL;
    queue.tail = NULL;
    queue.size = 0;
    queue.visited_items = 0;

    while (condQueue.head != NULL) {
        CondNode *temp = condQueue.head;
        condQueue.head = condQueue.head->next;
        cnd_destroy(&temp->cond);
        free(temp);
    }
    condQueue.head = NULL;
    condQueue.tail = NULL;

    mtx_unlock(&lock);
    mtx_destroy(&lock);
}

/**
 * Adds an item to the queue. Wakes up a waiting thread if any exist.
 * 
 * @param item Pointer to the data to enqueue.
 */
void enqueue(void *item) {
    mtx_lock(&lock);
    Node *new_node = malloc(sizeof(Node));
    new_node->data = item;
    new_node->next = NULL;

    if (queue.size == 0) {
        queue.head = new_node;
        queue.tail = new_node;
    } else {
        queue.tail->next = new_node;
        queue.tail = new_node;
    }

    queue.size++;

    if (condQueue.head != NULL) {
        cnd_signal(&(condQueue.head->cond));
    }

    mtx_unlock(&lock);
}

/**
 * Removes and returns the next item from the queue. If the queue is empty,
 * the calling thread blocks until an item is available and it's the thread's turn.
 * 
 * @return The data of the dequeued item.
 */
void *dequeue(void) {
    mtx_lock(&lock);

    CondNode *new_node = malloc(sizeof(CondNode));
    new_node->id = thrd_current();
    new_node->next = NULL;
    cnd_init(&new_node->cond);

    if (condQueue.head == NULL) {
        condQueue.head = new_node;
        condQueue.tail = new_node;
    } else {
        condQueue.tail->next = new_node;
        condQueue.tail = new_node;
    }

    queue.visited_items++;

    while (queue.size == 0 || new_node->id != condQueue.head->id) {
        cnd_wait(&new_node->cond, &lock);
    }

    Node *temp = queue.head;
    queue.head = queue.head->next;
    if (queue.head == NULL) {
        queue.tail = NULL;
    }

    void *data = temp->data;
    free(temp);
    queue.size--;

    condQueue.head = condQueue.head->next;
    if (condQueue.head == NULL) {
        condQueue.tail = NULL;
    } else {
        cnd_signal(&(condQueue.head->cond));
    }

    mtx_unlock(&lock);

    return data;
}

/**
 * Returns the total number of items that have been dequeued or attempted to be dequeued.
 * 
 * @return The number of visited items.
 */
size_t visited(void) {
    return queue.visited_items;
}
