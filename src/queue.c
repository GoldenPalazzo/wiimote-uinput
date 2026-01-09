#include "queue.h"
#include "logger.h"
#include <memory.h>

#ifdef __clang__
#define NULLABLE _Nullable
#else
#define NULLABLE
#endif

int enqueue_msg(msg_queue_t *msgs, const uint8_t *buf, size_t len) {
    int ret = 0;
    if (msgs->count + 1 > MSGQ_SIZE) {
        LOG_ERROR("Message queue full, cannot enqueue message");
        ret = -1;
        goto enqueue_end;
    }
    memcpy(msgs->msgs[msgs->tail].buf, buf, len);
    msgs->msgs[msgs->tail].len = len;
    msgs->tail = (msgs->tail + 1) % MSGQ_SIZE;
    msgs->count++;
enqueue_end:
    return ret;
}

int pop_msg(msg_queue_t *msgs,  msg_t * NULLABLE out_msg) {
    int ret = 0;
    if (msgs->count == 0) {
        LOG_ERROR("Message queue empty, cannot pop message");
        ret = -1;
        goto pop_end;
    }
    if (out_msg != NULL) {
        memcpy(out_msg->buf, msgs->msgs[msgs->head].buf,
                msgs->msgs[msgs->head].len);
        out_msg->len = msgs->msgs[msgs->head].len;
    }
    msgs->head = (msgs->head + 1) % MSGQ_SIZE;
    msgs->count--;
pop_end:
    return ret;
}

