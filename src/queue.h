#ifndef _GQUEUE_H_
#define _GQUEUE_H_

#include <stddef.h>
#include <stdint.h>

#define MSG_SIZE 32
typedef struct {
    uint8_t buf[MSG_SIZE];
    size_t len;
} msg_t;

#define MSGQ_SIZE 16
typedef struct {
    msg_t msgs[MSGQ_SIZE];
    size_t head;
    size_t tail;
    size_t count;
} msg_queue_t;

int enqueue_msg(
        msg_queue_t *msgs,
        const uint8_t *buf,
        size_t len);
int pop_msg(
        msg_queue_t *msgs,
        msg_t *out_msg);

#endif // _GQUEUE_H_
