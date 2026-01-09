#ifndef _GWIIMOTE_H_
#define _GWIIMOTE_H_
#include <stddef.h>
#include <stdint.h>
#include "queue.h"

enum extension_status {
    EXT_NONE,
    EXT_WAITING_DECRYPTION_0,
    EXT_WAITING_DECRYPTION_1,
    EXT_DECRYPTED,
    EXT_UNKNOWN,
    EXT_NUNCHUCK,
    EXT_CLASSIC_CONTROLLER,
};

#define NUNCHUCK_SIGNATURE 0xA4200000
typedef struct {
    uint16_t sx, sy;
    uint8_t c, z;
} nunchuck_state_t;

#define CC_SIGNATURE       0xA4200101
typedef struct {
    uint8_t data_format;
    uint16_t lx, ly, rx, ry;
    uint8_t lt, rt;
    uint8_t lz, rz;
    uint8_t du, dd, dl, dr;
    uint8_t a, b, x, y;
    uint8_t home, plus, minus;
} classic_controller_state_t;


#define WII_LED_ONEHOT(b) ((b).status_flags >> 0x08)
#define WII_FLAG_EXT_CONNECTED(b) ((b).status_flags & 0x02)
typedef struct {
    uint8_t btn_a, btn_b, btn_1, btn_2;
    uint8_t btn_plus, btn_minus, btn_home;
    uint8_t btn_up, btn_down, btn_left, btn_right;

    enum extension_status ext_status;
    nunchuck_state_t nunchuck;
    classic_controller_state_t classic_controller;

    uint8_t battery;
    uint8_t status_flags;

    uint8_t initialized;
} wiimote_state_t;

int connect_wiimote(const char *device_path, wiimote_state_t *initial_state);
int handle_wiimote_event(
        msg_queue_t *msgs,
        wiimote_state_t *state,
        const uint8_t *event_buffer);

#endif // _GWIIMOTE_H_

