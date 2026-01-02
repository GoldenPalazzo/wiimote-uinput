#ifndef _GWIIMOTE_H_
#define _GWIIMOTE_H_
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RUMBLE = 0x10,
    LEDS,
    REPORTING_MODE,
    IR_CAMERA_ENABLE,
    SPEAKER_ENABLE,
    STATUS_INFO_REQUEST,
    WRITE_MEMREG_REQUEST,
    READ_MEMREG_REQUEST,
    SPEAKER_DATA,
    SPEAKER_MUTE,
    IR_CAMERA_ENABLE_2,

    STATUS_INFO_REPLY = 0x20,
    READ_MEMREG_REPLY,
    ACK_OUT_RETURN,

    DATA_REP_COREBTNS = 0x30,
    DATA_REP_COREACC,
    DATA_REP_COREEXT8,
    DATA_REP_COREACCIR12,
    DATA_REP_COREEXT19,
    DATA_REP_COREACC16,
    DATA_REP_COREIR10EXT9,
    DATA_REP_COREACCIR10EXT6,

    DATA_REP_EXT21 = 0x3D,
    DATA_REP_INTERLEAVED1,
    DATA_REP_INTERLEAVED2,
} wiimote_report_type_t;

typedef struct {
    uint8_t sx, sy, c, z;
} nunchuck_state_t;

#define WII_BTN_LEFT(b) ((b).buttons & 0x0100)
#define WII_BTN_RIGHT(b) ((b).buttons & 0x0200)
#define WII_BTN_DOWN(b) ((b).buttons & 0x0400)
#define WII_BTN_UP(b) ((b).buttons & 0x0800)
#define WII_BTN_PLUS(b) ((b).buttons & 0x1000)
#define WII_BTN_MINUS(b) ((b).buttons & 0x0010)
#define WII_BTN_A(b) ((b).buttons & 0x0008)
#define WII_BTN_B(b) ((b).buttons & 0x0004)
#define WII_BTN_HOME(b) ((b).buttons & 0x0080)
#define WII_BTN_1(b) ((b).buttons & 0x0002)
#define WII_BTN_2(b) ((b).buttons & 0x0001)
#define WII_LED_ONEHOT(b) ((b).status_flags >> 0x08)
#define WII_FLAG_EXT_CONNECTED(b) ((b).status_flags & 0x02)

typedef struct {
    uint16_t buttons;
    uint8_t ext_connected;
    nunchuck_state_t nunchuck;

    uint8_t battery;
    uint8_t status_flags;

    uint8_t initialized;
} wiimote_state_t;

typedef enum {
    DEBUG_TYPE_NONE,
    DEBUG_TYPE_HID,
    DEBUG_TYPE_WIIMOTE,
} debug_type_t;

int connect_wiimote(const char *device_path, wiimote_state_t *initial_state);
int handle_wiimote_event(
        int fd,
        wiimote_state_t *state,
        const char *event_buffer,
        int debug
        );
#endif


