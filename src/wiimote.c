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

#define BITMASK_COREBTNS(high, low) ((unsigned char)high << 8) | (unsigned char)low

/* only for debug print purposes */
int send_msg(int fd, const char *buf, size_t buf_size) {
    int res = write(fd, buf, buf_size);
    if (res < 0) {
        printf("error: %d\n", errno);
        perror("write");
    } else {
        // printf("write() wrote %d bytes\n", res);
    }
    return res;
}

int get_msg(int fd, char *buf, size_t buf_size) {
    int res = read(fd, buf, buf_size);
    if (res < 0) {
        printf("error: %d\n", errno);
        perror("read");
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
void send_rumble(int fd, int secs) {
    char buf[2] = {RUMBLE, 1};
    send_msg(fd, buf, 2);
    sleep(secs);
    buf[1] = 0x00;
    send_msg(fd, buf, 2);
}

int wiimote_change_mode(int fd, wiimote_report_type_t intype) {
    if (intype < DATA_REP_COREBTNS || intype > DATA_REP_COREEXT8) {
        printf("error: invalid input report type %hhx\n", intype);
        return -1;
    }
    char buf[3] = {REPORTING_MODE, 0x04, intype};
    send_msg(fd, buf, 3);
    return fd;
}

void decrypt_extension(int fd) {
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
    send_msg(fd, buf, 22); // some kind of ack of type 0x22 is sent but
                          // still has to be investigated, so we're gonna
                          // assume that it worked
    buf[4] = 0xfb;
    buf[6] = 0x00;
    send_msg(fd, buf, 22);
    printf("extension decrypted!\n");
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

int wiimote_handler_thread(void *args) {
    gwiimote_params_t *params = (gwiimote_params_t *)args;
    wiimote_state_t *state = &params->state;
    int fd = params->fd;
    char buf[128];
    int res;
    while (1) {
        res = get_msg(fd, buf, sizeof(buf));
        if (res <= 0) {
            break;
        }

        switch (buf[0]) {
            case DATA_REP_COREBTNS:
            case DATA_REP_COREEXT8:
                // parse input report and update state
                // (not implemented here)
                state->buttons = BITMASK_COREBTNS(buf[1], buf[2]);
                if (buf[0] == DATA_REP_COREEXT8) {
                    parse_nunchuck(buf+3, &state->nunchuck);
                }
                break;
            case STATUS_INFO_REPLY:
                state->status_flags = buf[3];
                state->buttons = BITMASK_COREBTNS(buf[1], buf[2]);
                state->battery = buf[7];
                // printf("flag status report: ");
                // for (int i=0; i<4; i++)
                //     printf("%c", status_flags & 1 << (3-i) ? flags_mask[i] : '-');
                // puts("\n");

                if (WII_FLAG_EXT_CONNECTED(*state) && !state->ext_connected) {
                    printf("connection to extension detected\n");
                    state->ext_connected = 1;
                    decrypt_extension(fd);
                // it is very important to check that the extension was
                // previously connected, because otherwise there will be some
                // strange write errors that make the changemode fail
                } else if (!WII_FLAG_EXT_CONNECTED(*state) && state->ext_connected) {
                    printf("disconnection from extension detected\n");
                    state->ext_connected = 0;
                }
                usleep(50000); // necessary sleep
                               // Following a connection or disconnection event on
                               // the Extension Port, data reporting is disabled
                               // and the Data Reporting Mode must be reset before
                               // new data can arrive.
                wiimote_change_mode(fd, DATA_REP_COREEXT8);
                break;
            case ACK_OUT_RETURN:
                if (buf[4] != 0) {
                    printf("error from command %hhx (%hhx)\n", buf[3], buf[4]);
                }
                break;
            default:
                break;
        }

        switch (params->debug_type) {
            case DEBUG_TYPE_HID:
                for (int i=0; i<res; i++)
                    printf("%02hhx ", buf[i]);
                puts("\n");
                break;
            case DEBUG_TYPE_WIIMOTE:
                // handled below
                print_wiimote_state(state);
                print_nunchuck_state(&state->nunchuck);
                break;
            case DEBUG_TYPE_NONE:
            default:
                break;
        }
    }
    return res;
}

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

