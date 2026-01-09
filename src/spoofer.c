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

ssize_t emit(
        int fd, short unsigned int type, short unsigned int code, int val) {
    struct input_event ie = {
        .type = type,
        .code = code,
        .value = val,
        .time = {
            .tv_sec = 0,
            .tv_usec = 0
        }
    };
    ssize_t res = write(fd, &ie, sizeof(ie));
    if (res < 0) {
        perror("write fallita");
    }
    return res;
}

int create_uinput_device(void) {
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
    ioctl(fd, UI_SET_KEYBIT, BTN_MODE);
    ioctl(fd, UI_SET_KEYBIT, BTN_THUMBL);
    ioctl(fd, UI_SET_KEYBIT, BTN_THUMBR);

    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_RX);
    ioctl(fd, UI_SET_ABSBIT, ABS_RY);

    ioctl(fd, UI_SET_KEYBIT, BTN_TR);
    ioctl(fd, UI_SET_KEYBIT, BTN_TL);
    ioctl(fd, UI_SET_KEYBIT, BTN_TR2);
    ioctl(fd, UI_SET_KEYBIT, BTN_TL2);
    ioctl(fd, UI_SET_ABSBIT, ABS_RZ);
    ioctl(fd, UI_SET_ABSBIT, ABS_Z);

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x045e;
    usetup.id.product = 0x028e;
    strcpy(usetup.name, "Xbox 360 Wireless Controller");

    memset(&abs_setup, 0, sizeof(abs_setup));

    abs_setup.code = ABS_X;
    abs_setup.absinfo.minimum = -512;
    abs_setup.absinfo.maximum = 511;
    abs_setup.absinfo.fuzz = 0;
    abs_setup.absinfo.flat = 2;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_Y;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_RX;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_RY;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_Z;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = 255;
    abs_setup.absinfo.fuzz = 2;
    abs_setup.absinfo.flat = 0;
    abs_setup.absinfo.resolution = 1;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    abs_setup.code = ABS_RZ;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    return fd;
}

int destroy_uinput_device(int fd) {
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}

void wiimote_to_uinput(const wiimote_state_t *wiimote, int uinput_fd) {
    if (!wiimote->initialized) {
        LOG_ERROR("Wiimote not initialized, cannot map to uinput.");
        return;
    }
    if (wiimote->ext_status != EXT_CLASSIC_CONTROLLER) {
        emit(uinput_fd, EV_KEY, BTN_SOUTH, wiimote->btn_a);
        emit(uinput_fd, EV_KEY, BTN_EAST, wiimote->btn_b);
        emit(uinput_fd, EV_KEY, BTN_WEST, wiimote->btn_1);
        emit(uinput_fd, EV_KEY, BTN_NORTH, wiimote->btn_2);
        emit(uinput_fd, EV_KEY, BTN_DPAD_UP, wiimote->btn_up);
        emit(uinput_fd, EV_KEY, BTN_DPAD_DOWN, wiimote->btn_down);
        emit(uinput_fd, EV_KEY, BTN_DPAD_LEFT, wiimote->btn_left);
        emit(uinput_fd, EV_KEY, BTN_DPAD_RIGHT, wiimote->btn_right);
        emit(uinput_fd, EV_KEY, BTN_START, wiimote->btn_plus);
        emit(uinput_fd, EV_KEY, BTN_SELECT, wiimote->btn_minus);
        emit(uinput_fd, EV_KEY, BTN_MODE, wiimote->btn_home);
    }
    switch (wiimote->ext_status) {
        case EXT_NUNCHUCK:
            emit(uinput_fd, EV_ABS, ABS_X, wiimote->nunchuck.sx - 512);
            emit(uinput_fd, EV_ABS, ABS_Y, 512 - wiimote->nunchuck.sy);
            emit(uinput_fd, EV_KEY, BTN_TL, wiimote->nunchuck.z);
            emit(uinput_fd, EV_KEY, BTN_TR, wiimote->nunchuck.c);
            break;
        case EXT_CLASSIC_CONTROLLER:
            emit(uinput_fd,
                    EV_KEY, BTN_EAST, wiimote->classic_controller.a);
            emit(uinput_fd,
                    EV_KEY, BTN_SOUTH, wiimote->classic_controller.b);
            emit(uinput_fd,
                    EV_KEY, BTN_NORTH, wiimote->classic_controller.x);
            emit(uinput_fd,
                    EV_KEY, BTN_WEST, wiimote->classic_controller.y);
            emit(uinput_fd,
                    EV_KEY, BTN_START, wiimote->classic_controller.plus);
            emit(uinput_fd,
                    EV_KEY, BTN_SELECT, wiimote->classic_controller.minus);
            emit(uinput_fd,
                    EV_KEY, BTN_MODE, wiimote->classic_controller.home);
            emit(uinput_fd,
                    EV_KEY, BTN_DPAD_UP, wiimote->classic_controller.du);
            emit(uinput_fd,
                    EV_KEY, BTN_DPAD_DOWN, wiimote->classic_controller.dd);
            emit(uinput_fd,
                    EV_KEY, BTN_DPAD_LEFT, wiimote->classic_controller.dl);
            emit(uinput_fd,
                    EV_KEY, BTN_DPAD_RIGHT, wiimote->classic_controller.dr);
            emit(uinput_fd,
                    EV_KEY, BTN_TL, wiimote->classic_controller.lz);
            emit(uinput_fd,
                    EV_KEY, BTN_TR, wiimote->classic_controller.rz);
            emit(uinput_fd,
                    EV_ABS, ABS_Z, wiimote->classic_controller.lt);
            emit(uinput_fd,
                    EV_ABS, ABS_RZ, wiimote->classic_controller.rt);
            emit(uinput_fd,
                    EV_KEY, BTN_TL2, wiimote->classic_controller.lt > 128);
            emit(uinput_fd,
                    EV_KEY, BTN_TR2, wiimote->classic_controller.rt > 128);
            emit(uinput_fd,
                    EV_ABS, ABS_X, wiimote->classic_controller.lx - 512);
            emit(uinput_fd,
                    EV_ABS, ABS_Y, 512 - wiimote->classic_controller.ly);
            emit(uinput_fd,
                    EV_ABS, ABS_RX, wiimote->classic_controller.rx - 512);
            emit(uinput_fd,
                    EV_ABS, ABS_RY, 512 - wiimote->classic_controller.ry);
            break;
        case EXT_NONE:
        case EXT_UNKNOWN:
        case EXT_WAITING_DECRYPTION_0:
        case EXT_WAITING_DECRYPTION_1 :
        case EXT_DECRYPTED:
        default:
            emit(uinput_fd, EV_ABS, ABS_X, 0);
            emit(uinput_fd, EV_ABS, ABS_Y, 0);
            emit(uinput_fd, EV_ABS, ABS_RX, 0);
            emit(uinput_fd, EV_ABS, ABS_RY, 0);
            emit(uinput_fd, EV_ABS, ABS_Z, 0);
            emit(uinput_fd, EV_ABS, ABS_RZ, 0);
            emit(uinput_fd, EV_KEY, BTN_TR, 0);
            emit(uinput_fd, EV_KEY, BTN_TL, 0);
            emit(uinput_fd, EV_KEY, BTN_TR2, 0);
            emit(uinput_fd, EV_KEY, BTN_TL2, 0);
            break;
    }
    emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

