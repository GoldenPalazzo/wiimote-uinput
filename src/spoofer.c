#include "wiimote.h"
#include "logger.h"
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <memory.h>
#include <unistd.h>
#include <stdio.h>

#define NUNCHUCK_MIN 0
#define NUNCHUCK_MAX 255

void emit(int fd, int type, int code, int val) {
    struct input_event ie = {
        .type = type,
        .code = code,
        .value = val,
        .time = {
            .tv_sec = 0,
            .tv_usec = 0
        }
    };
    int res = write(fd, &ie, sizeof(ie));
    if (res < 0) {
        perror("write fallita");
        exit(res);
    }
}

// void press_btn(int fd, int code) {
//     emit(fd, EV_KEY, code, 1);
//     emit(fd, EV_SYN, SYN_REPORT, 0);
//     sleep(1);
//     emit(fd, EV_KEY, code, 0);
//     emit(fd, EV_SYN, SYN_REPORT, 0);
//     sleep(1);
// }

int create_uinput_device() {
    struct uinput_setup usetup;
    struct uinput_abs_setup abs_setup;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        return fd;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);

    ioctl(fd, UI_SET_KEYBIT, BTN_SOUTH);
    ioctl(fd, UI_SET_KEYBIT, BTN_EAST);
    ioctl(fd, UI_SET_KEYBIT, BTN_WEST);
    ioctl(fd, UI_SET_KEYBIT, BTN_NORTH);

    ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_UP);
    ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_DOWN);
    ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_RIGHT);

    ioctl(fd, UI_SET_KEYBIT, BTN_START);
    ioctl(fd, UI_SET_KEYBIT, BTN_SELECT);

    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_RX);
    ioctl(fd, UI_SET_ABSBIT, ABS_RY);

    ioctl(fd, UI_SET_KEYBIT, BTN_TR);
    ioctl(fd, UI_SET_KEYBIT, BTN_TL);
    ioctl(fd, UI_SET_KEYBIT, BTN_TR2);
    ioctl(fd, UI_SET_KEYBIT, BTN_TL2);

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x045e;
    usetup.id.product = 0x028e;
    strcpy(usetup.name, "Xbox 360 Golden");

    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_X;
    abs_setup.absinfo.minimum = NUNCHUCK_MIN;
    abs_setup.absinfo.maximum = NUNCHUCK_MAX;
    abs_setup.absinfo.fuzz = 0;
    abs_setup.absinfo.flat = 0;
    // abs_setup.absinfo.resolution = 0;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);
    abs_setup.code = ABS_Y;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    // char devname[16];
    // ioctl(fd, UI_GET_SYSNAME(sizeof(devname)), devname);
    // printf("created device %s\n", devname);

    // signal(SIGINT, sigint_handler);
    //
    // while (keep_running) {
    //     press_btn(fd, BTN_A);
    // }

    // printf("received sigint. stopping...\n");
    // ioctl(fd, UI_DEV_DESTROY);
    // close(fd);
    return fd;
}

int destroy_uinput_device(int fd) {
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}

void wiimote_to_uinput(const wiimote_state_t *wiimote, int uinput_fd) {
    LOG_INFO("Wiimote state: buttons=0x%04x", wiimote->buttons);
    emit(uinput_fd, EV_KEY, BTN_SOUTH, WII_BTN_A(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_EAST, WII_BTN_B(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_WEST, !!WII_BTN_1(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_NORTH, WII_BTN_2(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_DPAD_UP, WII_BTN_UP(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_DPAD_DOWN, WII_BTN_DOWN(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_DPAD_LEFT, WII_BTN_LEFT(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_DPAD_RIGHT, WII_BTN_RIGHT(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_START, WII_BTN_PLUS(*wiimote));
    emit(uinput_fd, EV_KEY, BTN_SELECT, WII_BTN_MINUS(*wiimote));
    if (wiimote->ext_connected) {
        LOG_INFO(" Nunchuck: sx=%d sy=%d", wiimote->nunchuck.sx, wiimote->nunchuck.sy);
        emit(uinput_fd, EV_ABS, ABS_X, wiimote->nunchuck.sx);
        emit(uinput_fd, EV_ABS, ABS_Y, NUNCHUCK_MAX-wiimote->nunchuck.sy);
    } else {
        emit(uinput_fd, EV_ABS, ABS_X, 0);
        emit(uinput_fd, EV_ABS, ABS_Y, 0);
    }
    emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
}
