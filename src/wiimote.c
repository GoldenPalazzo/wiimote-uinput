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

const char *bus_str(int bus) {
	switch (bus) {
	case BUS_USB:
		return "USB";
		break;
	case BUS_HIL:
		return "HIL";
		break;
	case BUS_BLUETOOTH:
		return "Bluetooth";
		break;
	case BUS_VIRTUAL:
		return "Virtual";
		break;
	default:
		return "Other";
		break;
	}
}

void parse_wiimsg(const char *buf) {
    uint16_t btns;
    uint8_t battery, led_state, status_flags;
    switch(buf[0]) {
        case WII_STATUSREPORT:
            btns = BITMASK_COREBTNS(buf[1], buf[2]);
            battery = buf[7];
            led_state = (unsigned char)buf[3] >> 8;
            status_flags = buf[3] & 0xf;
            printf("flag status report: ");
            for (int i=0; i<4; i++)
                printf("%c", status_flags & 1 << (3-i) ? flags_mask[i] : '-');
            puts("\n");
            break;
        case COREBTNS:
            /*
              ---p udrl h--m ab12
              where
               - p is the plus
               - h is home
               - m is minus
            */
            btns = BITMASK_COREBTNS(buf[1], buf[2]);
            printf("corebtns: ");
            for (int i=0; i<16; i++) {
                printf("%c", btns & 1 << (15-i) ? corebtns_mask[i] : '-');
            }
            puts("\n");
            break;
        case COREEXT8:
            btns = BITMASK_COREBTNS(buf[1], buf[2]);
            printf("corebtns: ");
            for (int i=0; i<16; i++) {
                printf("%c", btns & 1 << (15-i) ? corebtns_mask[i] : '-');
            }
            for (size_t i=3; i<11; i++)
                printf("%hhx ", buf[i]);
            puts("\n");
            break;
        default:
            break;
    }
}

/* only for debug print purposes */
int send_msg(int fd, const char *buf, size_t buf_size) {
    int res = write(fd, buf, buf_size);
    if (res < 0) {
        printf("error: %d\n", errno);
        perror("write");
    } else {
        printf("write() wrote %d bytes\n", res);
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

void send_rumble(int fd, int secs) {
    char buf[2] = {WII_RUMBLE, 1};
    send_msg(fd, buf, 2);
    sleep(secs);
    buf[1] = 0x00;
    send_msg(fd, buf, 2);
}

int wiimote_connect(const char *hidraw_file, WiimoteInputReportType intype) {
    if (hidraw_file == NULL){
        printf("error: no hidraw input specified");
        return -1;
    }
    int fd = open(hidraw_file, O_RDWR);
    if (fd < 0) {
        perror("open failed");
        return fd;
    }

    /*
    res = ioctl(fd, HIDIOCGRAWINFO, &info);
	if (res < 0) {
		perror("HIDIOCGRAWINFO");
	} else {
		printf("Raw Info:\n");
		printf("\tbustype: %d (%s)\n",
			info.bustype, bus_str(info.bustype));
		printf("\tvendor: 0x%04hx\n", info.vendor);
		printf("\tproduct: 0x%04hx\n", info.product);
	}
    */

    char buf[3] = {WII_REPORTMODE, WII_REPORTONCHANGE, intype};
    send_msg(fd, buf, 3);
    return fd;
}

int main(int argc, char *argv[]) {
    int wiimote_fd, res;
    char buf[128];
    if ((wiimote_fd = wiimote_connect(argv[1], COREEXT8)) < 0) {
        return wiimote_fd;
    }

    while(1) {
        res = get_msg(wiimote_fd, buf, sizeof(buf));
        // for (size_t i=0; i<(size_t)res; i++)
        //     printf("%hhx ", buf[i]);
        // puts("\n");
        parse_wiimsg(buf);
    }

    close(wiimote_fd);
    return 0;
}
