#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <stdint.h>

/*
 * Ugly hack to work around failing compilation on systems that don't
 * yet populate new version of hidraw.h to userspace.
 */
#ifndef HIDIOCSFEATURE
#warning Please have your distro update the userspace kernel headers
#define HIDIOCSFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x06, len)
#define HIDIOCGFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x07, len)
#endif

/* Unix */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* C */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "wiimote.h"
#include "logger.h"

#define BITMASK_COREBTNS(high, low) ((unsigned char)high << 8) | (unsigned char)low

const char *corebtns_mask = "---pudrlh--mab12";
const char *flags_mask = "iseb";

/* only for debug print purposes */
int send_msg(int fd, const char *buf, size_t buf_size) {
    int res = write(fd, buf, buf_size);
    if (res < 0) {
        perror("write msg to wiimote");
    } else {
        // printf("write() wrote %d bytes\n", res);
    }
    return res;
}

int get_msg(int fd, char *buf, size_t buf_size) {
    int res = read(fd, buf, buf_size);
    if (res < 0) {
        perror("read msg from wiimote");
    } else {
        // printf("read() read %d bytes\n", res);
        // uint8_t type = buf[0];
        // if (type == WII_COREBTNS || type == WII_COREEXT8)
        //     return 0;
        // parse_wiimsg(buf);
    }
    return res;
}

// don't use: i'm using blocking sleep
/*
void send_rumble(int fd, int secs) {
    char buf[2] = {RUMBLE, 1};
    send_msg(fd, buf, 2);
    sleep(secs);
    buf[1] = 0x00;
    send_msg(fd, buf, 2);
}
*/

int wiimote_change_mode(int fd, wiimote_report_type_t intype) {
    if (intype < DATA_REP_COREBTNS || intype > DATA_REP_COREEXT8) {
        LOG_ERROR("Tried to send an invalid input report type %hhx", intype);
        return -1;
    }
    char buf[3] = {REPORTING_MODE, 0x04, intype};
    if (send_msg(fd, buf, 3) < 0) {
        LOG_ERROR("Error changing wiimote input report type to %hhx", intype);
        return -1;
    }
    LOG_INFO("Changed wiimote input report type to %hhx", intype);
    return fd;
}

int decrypt_extension(int fd) {
    int ret = 0;
    char buf[22] = {
        0x16, // write
        0x04, // addr space (in this case control registers)
        0xa4, 0x00, 0xf0, // first encryption address
        0x01, 0x55, // size and data
                    // 7 bytes written
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00
    };
    LOG_INFO("Attempting to decrypt extension (1/2)");
    ret = send_msg(fd, buf, 22); // some kind of ack of type 0x22 is sent but
                          // still has to be investigated, so we're gonna
                          // assume that it worked
    if (ret < 0) {
        LOG_ERROR("Error writing to extension control registers");
        return ret;
    }
    buf[4] = 0xfb;
    buf[6] = 0x00;
    LOG_INFO("Attempting to decrypt extension (2/2)");
    ret = send_msg(fd, buf, 22);
    if (ret < 0) {
        LOG_ERROR("Error writing to extension control registers");
        return ret;
    }
    return ret;
}

void parse_nunchuck(const char *nc_buf, nunchuck_state_t *nc_state) {
    nc_state->sx = nc_buf[0];
    nc_state->sy = nc_buf[1];
    nc_state->c = ((nc_buf[5] >> 1) & 1) ^ 1;
    nc_state->z = ((nc_buf[5] & 1) ^ 1);
    // c and z are inverted
    // printf("nunchuck: sx=%hhu sy=%hhu c=%hhu z=%hhu\n", sx, sy, c, z);
}

void print_wiimote_state(const wiimote_state_t *state) {
    printf("wiimote state:\n");
    printf(" buttons: ");
    for (int i=0; i<16; i++) {
        printf("%c", state->buttons & 1 << (15-i) ? corebtns_mask[i] : '-');
    }
    printf("\n battery: %hhu\n", state->battery);
    printf(" status flags: ");
    for (int i=0; i<4; i++)
        printf("%c", state->status_flags & 1 << (3-i) ? flags_mask[i] : '-');
    printf("\n extension connected: %s\n",
           state->ext_connected ? "yes" : "no");
}

void print_nunchuck_state(const nunchuck_state_t *nc_state) {
    printf("nunchuck state:\n");
    printf(" sx: %hhu\n", nc_state->sx);
    printf(" sy: %hhu\n", nc_state->sy);
    printf(" c: %hhu\n", nc_state->c);
    printf(" z: %hhu\n", nc_state->z);
}

void handle_status_input_reply(int fd, const char *buf, wiimote_state_t *state) {
    state->status_flags = buf[3];
    state->buttons = BITMASK_COREBTNS(buf[1], buf[2]);
    state->battery = buf[7];
    // printf("flag status report: ");
    // for (int i=0; i<4; i++)
    //     printf("%c", status_flags & 1 << (3-i) ? flags_mask[i] : '-');
    // puts("\n");

    if (WII_FLAG_EXT_CONNECTED(*state) && !state->ext_connected) {
        LOG_INFO("Connection to extension detected");
        state->ext_connected = 1;
        decrypt_extension(fd);
    // it is very important to check that the extension was
    // previously connected, because otherwise there will be some
    // strange write errors that make the changemode fail
    } else if (!WII_FLAG_EXT_CONNECTED(*state) && state->ext_connected) {
        printf("Disconnection from extension detected");
        state->ext_connected = 0;
    }
    usleep(50000); // necessary sleep
                   // Following a connection or disconnection event on
                   // the Extension Port, data reporting is disabled
                   // and the Data Reporting Mode must be reset before
                   // new data can arrive.
    wiimote_change_mode(fd, DATA_REP_COREEXT8);
}

int handle_wiimote_event(
        int fd,
        wiimote_state_t *state,
        const char *event_buffer,
        int debug
        ) {
    int ret = 0;
    switch (event_buffer[0]) {
        case DATA_REP_COREBTNS:
        // accel not implemented yet
        case DATA_REP_COREACC:
        case DATA_REP_COREACCIR12:
            state->buttons = BITMASK_COREBTNS(event_buffer[1], event_buffer[2]);
            break;
        case DATA_REP_COREEXT8:
            state->buttons = BITMASK_COREBTNS(event_buffer[1], event_buffer[2]);
            parse_nunchuck(event_buffer+3, &state->nunchuck);
            break;
        case DATA_REP_COREIR10EXT9:
            state->buttons = BITMASK_COREBTNS(event_buffer[1], event_buffer[2]);
            // parse ir data (not implemented here)
            parse_nunchuck(event_buffer+13, &state->nunchuck);
            break;
        case DATA_REP_COREACCIR10EXT6:
            state->buttons = BITMASK_COREBTNS(event_buffer[1], event_buffer[2]);
            // parse accelerometer data (not implemented here)
            parse_nunchuck(event_buffer+16, &state->nunchuck);
            break;
        case DATA_REP_EXT21:
            parse_nunchuck(event_buffer+1, &state->nunchuck);
            break;
        case STATUS_INFO_REPLY:
            handle_status_input_reply(fd, event_buffer, state);
            break;
        case ACK_OUT_RETURN:
            if (event_buffer[4] != 0) {
                LOG_ERROR("Wiimote sent error for command %hhx (%hhx)",
                        event_buffer[3], event_buffer[4]);
            }
            break;
        default:
            LOG_ERROR("Wiimote sent unrecognized report type: %hhx",
                    event_buffer[0]);
            ret = -1;
            break;
    }

    if (debug) {
        print_wiimote_state(state);
        print_nunchuck_state(&state->nunchuck);
    }
    return ret;
}

/*
int connect_wiimote(const char *device_path, wiimote_state_t *initial_state) {
    int fd = open(device_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open wiimote device");
        return fd;
    }
    wiimote_change_mode(fd, DATA_REP_COREEXT8);
    send_msg(fd, (char[]){STATUS_INFO_REQUEST, 0x00}, 2);
    // while ((res = get_msg(fd, buf, sizeof(buf)) > 0) && buf[0] != STATUS_INFO_REPLY);
    // handle_status_input_reply(fd, buf, initial_state);
    return fd;
}
*/

//
// int main(int argc, char *argv[]) {
//     int wiimote_fd;
//     if ((wiimote_fd = open(argv[1], O_RDWR)) < 0) {
//         return wiimote_fd;
//     }
//     wiimote_state_t state;
//     memset(&state, 0, sizeof(state));
//     gwiimote_params_t params;
//     params.fd = wiimote_fd;
//     params.state = state;
//     params.debug_type = argc == 3 ? atoi(argv[2]) : DEBUG_TYPE_NONE;
//     wiimote_change_mode(wiimote_fd, DATA_REP_COREEXT8);
//     wiimote_handler_thread(&params);
//     close(wiimote_fd);
//     return 0;
// }

