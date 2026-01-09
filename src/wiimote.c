#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>

/*
 * Ugly hack to work around failing compilation on systems that don't
 * yet populate new version of hidraw.h to userspace.
 */
#ifndef HIDIOCSFEATURE
#warning Please have your distro update the userspace kernel headers
#define HIDIOCSFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x06, len)
#define HIDIOCGFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x07, len)
#endif

#ifdef __clang__
#define NULLABLE _Nullable
#else
#define NULLABLE
#endif

/* Unix */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* C */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "wiimote.h"
#include "logger.h"

#define BITMASK_COREBTNS(high, low) ((uint8_t)high << 8) | (uint8_t)low

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

// Messages

uint8_t EXT_DECRYPT_PHASE1[22] = {
    WRITE_MEMREG_REQUEST, // write
    0x04, // addr space (in this case control registers)
    0xa4, 0x00, 0xf0, // first encryption address
    0x01, 0x55, // size and data
                // 7 bytes written
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00 // padding
};

uint8_t EXT_DECRYPT_PHASE2[22] = {
    WRITE_MEMREG_REQUEST, // write
    0x04, // addr space (in this case control registers)
    0xa4, 0x00, 0xfb, // first encryption address
    0x01, 0x00, // size and data
                // 7 bytes written
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00 // padding
};

const uint8_t CC_DATAMODE_REQ[] = {
    READ_MEMREG_REQUEST,
    0x04,
    0xa4, 0x00, 0xfe,
    0x00, 0x01,
};

// Enqueue requests

// int enqueue_decrypt_req(msg_queue_t *msgs) {
//     int ret = 0;
//     if (msgs->count + 2 > MSGQ_SIZE) {
//         LOG_ERROR("Message queue full, trying to decrypt extension later");
//         ret = -1;
//         goto decrypt_end;
//     }
//     uint8_t buf[22] = {
//         WRITE_MEMREG_REQUEST, // write
//         0x04, // addr space (in this case control registers)
//         0xa4, 0x00, 0xf0, // first encryption address
//         0x01, 0x55, // size and data
//                     // 7 bytes written
//         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
//         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
//         0x00 // padding
//     };
//     enqueue_msg(msgs, buf, 22);
//     LOG_DEBUG("Enqueued first decryption write");
//     buf[4] = 0xfb;
//     buf[6] = 0x00;
//     enqueue_msg(msgs, buf, 22);
//     LOG_DEBUG("Enqueued second decryption write");
// decrypt_end:
//     return ret;
// }

int enqueue_ext_detect(msg_queue_t *msgs) {
    int ret = 0;
    if (msgs->count + 1 > MSGQ_SIZE) {
        LOG_ERROR("Message queue full, trying to detect extension later");
        ret = -1;
        goto detect_end;
    }
    uint8_t buf[] = {
        READ_MEMREG_REQUEST, // read
        0x04, // addr space (in this case control registers)
        0xa4, 0x00, 0xfa, // ext type address
        0x00, 0x06, // size
    };
    enqueue_msg(msgs, buf, sizeof(buf));
    LOG_DEBUG("Enqueued extension detection read");
detect_end:
    return ret;
}

// Parsers

void parse_wiimote(
        const uint8_t * NULLABLE btns_buf,
        const uint8_t * NULLABLE acc_buf,
        const uint8_t * NULLABLE ir_buf,
        wiimote_state_t *wm_state) {
    if (btns_buf != NULL) {
        wm_state->btn_up = (btns_buf[0] & 0x08) >> 3;
        wm_state->btn_down = (btns_buf[0] & 0x04) >> 2;
        wm_state->btn_right = (btns_buf[0] & 0x02) >> 1;
        wm_state->btn_left = (btns_buf[0] & 0x01);
        wm_state->btn_minus = (btns_buf[1] & 0x10) >> 4;
        wm_state->btn_plus = (btns_buf[0] & 0x10) >> 4;
        wm_state->btn_home = (btns_buf[1] & 0x80) >> 7;
        wm_state->btn_a = (btns_buf[1] & 0x08) >> 3;
        wm_state->btn_b = (btns_buf[1] & 0x04) >> 2;
        wm_state->btn_1 = (btns_buf[1] & 0x02) >> 1;
        wm_state->btn_2 = (btns_buf[1] & 0x01);
    }
}

void parse_nunchuck(const uint8_t *nc_buf, nunchuck_state_t *nc_state) {
    nc_state->sx = nc_buf[0] * 1023/255;
    nc_state->sy = nc_buf[1] * 1023/255;
    nc_state->c = ((nc_buf[5] >> 1) & 1) ^ 1;
    nc_state->z = ((nc_buf[5] & 1) ^ 1);
    // c and z are inverted
}

void parse_cc(const uint8_t *cc_buf, classic_controller_state_t *cc_state) {
    // maximum analog range is [0;1023], so we scale accordingly
    // maximum trigger range is [0;255], only case 1 uses [0;31]
    switch (cc_state->data_format) {
        case 1:
            // lx [0;63] ly [0;63] rx [0;31] ry [0;31]
            cc_state->lx = (cc_buf[0] & 0x3f) * 1023/63;
            cc_state->ly = (cc_buf[1] & 0x3f) * 1023/63;
            cc_state->rx = ((cc_buf[2] & 0x80) >> 7
                            | (cc_buf[1] & 0xc0) >> 5
                            | (cc_buf[0] & 0xc0) >> 3) * 1023/31;
            cc_state->ry = (cc_buf[2] & 0x1f) * 1023/31;
            cc_state->lt = ((cc_buf[3] & 0xe0) >> 5
                            | (cc_buf[2] & 0x60) >> 2) * 255/31;
            cc_state->rt = (cc_buf[3] & 0x1f) * 255/31;
            cc_state->dr = !((cc_buf[4] >> 7) & 1);
            cc_state->dd = !((cc_buf[4] >> 6) & 1);
            cc_state->dl = !((cc_buf[5] >> 1) & 1);
            cc_state->du = !(cc_buf[5] & 1);
            cc_state->b = !((cc_buf[5] >> 6) & 1);
            cc_state->y = !((cc_buf[5] >> 5) & 1);
            cc_state->a = !((cc_buf[5] >> 4) & 1);
            cc_state->x = !((cc_buf[5] >> 3) & 1);
            cc_state->minus = !((cc_buf[4] >> 4) & 1);
            cc_state->home = !((cc_buf[4] >> 3) & 1);
            cc_state->plus = !((cc_buf[4] >> 2) & 1);
            cc_state->lz = !((cc_buf[5] >> 7) & 1);
            cc_state->rz = !((cc_buf[5] >> 2) & 1);
            break;
        case 2:
        case 3:
        default:
            LOG_ERROR("Classic Controller data format %hhx not supported",
                    cc_state->data_format);
            break;
    }
}

void parse_generic(const uint8_t *cc_buf, wiimote_state_t *state) {
    switch (state->ext_status) {
        case EXT_CLASSIC_CONTROLLER:
            parse_cc(cc_buf, &state->classic_controller);
            break;
        case EXT_NUNCHUCK:
            parse_nunchuck(cc_buf, &state->nunchuck);
            break;
        case EXT_NONE:
        case EXT_WAITING_DECRYPTION_0:
        case EXT_WAITING_DECRYPTION_1:
        case EXT_DECRYPTED:
        case EXT_UNKNOWN:
        default:
            break;
    }
}

// Handlers

void handle_status_input_reply(
        msg_queue_t *msgs,
        wiimote_state_t *state,
        const uint8_t *buf
        ) {
    state->initialized = 1;
    state->status_flags = buf[3];
    state->battery = buf[7];
    parse_wiimote(buf+1, NULL, NULL, state);

    if (WII_FLAG_EXT_CONNECTED(*state)
        && state->ext_status == EXT_NONE) {
        LOG_INFO("Connection to extension detected");
        state->ext_status = EXT_WAITING_DECRYPTION_0;
        if (enqueue_msg(
                msgs,
                EXT_DECRYPT_PHASE1,
                sizeof(EXT_DECRYPT_PHASE1)) == 0) {
            LOG_INFO("Started extension decryption process");
        } else {
            LOG_ERROR("Failed to enqueue extension decryption request");
        }
    } else if (!WII_FLAG_EXT_CONNECTED(*state)
                && state->ext_status != EXT_NONE) {
        LOG_INFO("Disconnection from extension detected");
        state->ext_status = EXT_NONE;
    }
}

int handle_wiimote_event(
        msg_queue_t *msgs,
        wiimote_state_t *state,
        const uint8_t *event_buffer
        ) {
    int ret = 0;
    // LOG_DEBUG("Wiimote event report type: %hhx", event_buffer[0]);
    switch (event_buffer[0]) {
        case DATA_REP_COREBTNS:
        // accel not implemented yet
        case DATA_REP_COREACC:
        case DATA_REP_COREACCIR12:
            parse_wiimote(event_buffer+1, NULL, NULL, state);
            break;
        case DATA_REP_COREEXT8:
            parse_wiimote(event_buffer+1, NULL, NULL, state);
            parse_generic(event_buffer+3, state);
            break;
        case DATA_REP_COREIR10EXT9:
            parse_wiimote(event_buffer+1, NULL, NULL, state);
            // parse ir data (not implemented here)
            parse_generic(event_buffer+13, state);
            break;
        case DATA_REP_COREACCIR10EXT6:
            parse_wiimote(event_buffer+1, NULL, NULL, state);
            // parse accelerometer data (not implemented here)
            parse_generic(event_buffer+16, state);
            break;
        case DATA_REP_EXT21:
            parse_generic(event_buffer+1, state);
            break;
        case STATUS_INFO_REPLY:
            handle_status_input_reply(
                msgs, state, event_buffer);
            break;
        case ACK_OUT_RETURN:
            parse_wiimote(event_buffer+1, NULL, NULL, state);
            if (event_buffer[4] == 0x03) {
                LOG_ERROR("Wiimote sent error for command %hhx (%hhx)",
                    event_buffer[3], event_buffer[4]);
            } else if (event_buffer[3] == WRITE_MEMREG_REQUEST) {
                if (state->ext_status == EXT_WAITING_DECRYPTION_0) {
                    LOG_INFO("Extension decryption phase 1 write acknowledged");
                    state->ext_status = EXT_WAITING_DECRYPTION_1;
                    if (enqueue_msg(msgs, EXT_DECRYPT_PHASE2,
                            sizeof(EXT_DECRYPT_PHASE2)) == 0) {
                        LOG_INFO("Sent extension decryption phase 2 write");
                    } else {
                        LOG_ERROR("Failed to enqueue extension decryption "
                            "phase 2 write");
                    }
                } else if (state->ext_status == EXT_WAITING_DECRYPTION_1) {
                    LOG_INFO("Extension decryption phase 2 write acknowledged");
                    state->ext_status = EXT_DECRYPTED;
                    enqueue_ext_detect(msgs);
                }
            }
            break;
        case READ_MEMREG_REPLY:
            parse_wiimote(event_buffer+1, NULL, NULL, state);
            uint8_t se = event_buffer[3];
            uint8_t errors = se & 0xf;
            uint8_t size = ((se >> 4) & 0xf) + 1;
            uint16_t abs_offset = (uint16_t)
                (((uint16_t)event_buffer[4]) << 8
                | event_buffer[5]);
            LOG_DEBUG("    Read reply: size=%hhx offset=%04x errors=%hhx",
                    size, abs_offset, errors);
            if (errors == 7) {
                LOG_ERROR("    Attempted reading write-only register %04x",
                        abs_offset);
                ret = -1;
                break;
            } else if (errors == 8) {
                LOG_ERROR("    Attempted reading nonexistent register %04x",
                        abs_offset);
                ret = -1;
                break;
            }
            uint8_t data[16];
            memcpy(data, event_buffer+6, size);
            if (abs_offset == 0x00fa
                && size == 6
                && state->ext_status == EXT_DECRYPTED) {
                uint64_t ext_signature =
                    ((uint64_t)data[0] << 40) |
                    ((uint64_t)data[1] << 32) |
                    ((uint64_t)data[2] << 24) |
                    ((uint64_t)data[3] << 16) |
                    ((uint64_t)data[4] << 8)  |
                    ((uint64_t)data[5] << 0);
                LOG_INFO("Extension signature: %012llx", ext_signature);
                switch (ext_signature) {
                    case NUNCHUCK_SIGNATURE:
                        LOG_INFO("Nunchuck extension detected");
                        state->ext_status = EXT_NUNCHUCK;
                        break;
                    case CC_SIGNATURE:
                        LOG_INFO("Classic Controller extension detected");
                        state->ext_status = EXT_CLASSIC_CONTROLLER;
                        enqueue_msg(
                                msgs,
                                CC_DATAMODE_REQ,
                                sizeof(CC_DATAMODE_REQ));
                        break;
                    default:
                        LOG_WARN("Unknown extension detected. Signature: "
                                "%012llx",
                                ext_signature);
                        state->ext_status = EXT_UNKNOWN;
                        break;
                }
            } else if (abs_offset == 0x00fe
                       && size == 1
                       && state->ext_status == EXT_CLASSIC_CONTROLLER) {
                state->classic_controller.data_format = data[0];
                LOG_INFO("Classic Controller data mode set to %hhx", data[0]);
            }
            break;
        default:
            LOG_ERROR("Wiimote sent unrecognized report type: %hhx",
                    event_buffer[0]);
            ret = -1;
            break;
    }

    return ret;
}

