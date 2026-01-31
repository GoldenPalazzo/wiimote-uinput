#include "libevdev/libevdev-uinput.h"
#include "libevdev/libevdev.h"
#include "wiimote.h"
#include "logger.h"
#include "spoofer.h"
#include <assert.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <memory.h>
#include <unistd.h>
#include <stdio.h>

// ssize_t emit(
//         int fd, short unsigned int type, short unsigned int code, int val) {
//     struct input_event ie = {
//         .type = type,
//         .code = code,
//         .value = val,
//         .time = {
//             .tv_sec = 0,
//             .tv_usec = 0
//         }
//     };
//     ssize_t res = write(fd, &ie, sizeof(ie));
//     if (res < 0) {
//         perror("write fallita");
//     }
//     return res;
// }
//
// int create_uinput_device(const program_config_t *cfg) {
//     assert(cfg != NULL);
//
//     struct uinput_setup usetup;
//     struct uinput_abs_setup abs_setup;
//     int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
//     if (fd < 0) {
//         perror("open /dev/uinput");
//         return fd;
//     }
//
//     ioctl(fd, UI_SET_EVBIT, EV_KEY);
//     ioctl(fd, UI_SET_EVBIT, EV_ABS);
//     // ioctl(fd, UI_SET_EVBIT, EV_FF);
//
//     ioctl(fd, UI_SET_KEYBIT, BTN_SOUTH);
//     ioctl(fd, UI_SET_KEYBIT, BTN_EAST);
//     ioctl(fd, UI_SET_KEYBIT, BTN_WEST);
//     ioctl(fd, UI_SET_KEYBIT, BTN_NORTH);
//
//     ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_UP);
//     ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_DOWN);
//     ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_LEFT);
//     ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_RIGHT);
//
//     ioctl(fd, UI_SET_KEYBIT, BTN_START);
//     ioctl(fd, UI_SET_KEYBIT, BTN_SELECT);
//     ioctl(fd, UI_SET_KEYBIT, BTN_MODE);
//     ioctl(fd, UI_SET_KEYBIT, BTN_THUMBL);
//     ioctl(fd, UI_SET_KEYBIT, BTN_THUMBR);
//
//     ioctl(fd, UI_SET_ABSBIT, ABS_X);
//     ioctl(fd, UI_SET_ABSBIT, ABS_Y);
//     ioctl(fd, UI_SET_ABSBIT, ABS_RX);
//     ioctl(fd, UI_SET_ABSBIT, ABS_RY);
//
//     ioctl(fd, UI_SET_KEYBIT, BTN_TR);
//     ioctl(fd, UI_SET_KEYBIT, BTN_TL);
//     // ioctl(fd, UI_SET_KEYBIT, BTN_TR2);
//     // ioctl(fd, UI_SET_KEYBIT, BTN_TL2);
//     ioctl(fd, UI_SET_ABSBIT, ABS_RZ);
//     ioctl(fd, UI_SET_ABSBIT, ABS_Z);
//
//     // ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
//
//     memset(&usetup, 0, sizeof(usetup));
//     usetup.id.bustype = BUS_USB;
//     usetup.id.vendor = 0x045e;
//     usetup.id.product = 0x028e;
//     // usetup.ff_effects_max = 16;
//     strcpy(usetup.name, "Xbox 360 Wireless Controller");
//
//     memset(&abs_setup, 0, sizeof(abs_setup));
//
//     abs_setup.code = ABS_X;
//     abs_setup.absinfo.minimum = -512;
//     abs_setup.absinfo.maximum = 511;
//     abs_setup.absinfo.fuzz = 0;
//     abs_setup.absinfo.flat = 2;
//     ioctl(fd, UI_ABS_SETUP, &abs_setup);
//
//     abs_setup.code = ABS_Y;
//     ioctl(fd, UI_ABS_SETUP, &abs_setup);
//
//     abs_setup.code = ABS_RX;
//     ioctl(fd, UI_ABS_SETUP, &abs_setup);
//
//     abs_setup.code = ABS_RY;
//     ioctl(fd, UI_ABS_SETUP, &abs_setup);
//
//     abs_setup.code = ABS_Z;
//     abs_setup.absinfo.minimum = 0;
//     abs_setup.absinfo.maximum = 255;
//     abs_setup.absinfo.fuzz = 2;
//     abs_setup.absinfo.flat = 0;
//     abs_setup.absinfo.resolution = 1;
//     ioctl(fd, UI_ABS_SETUP, &abs_setup);
//
//     abs_setup.code = ABS_RZ;
//     ioctl(fd, UI_ABS_SETUP, &abs_setup);
//
//     ioctl(fd, UI_DEV_SETUP, &usetup);
//     ioctl(fd, UI_DEV_CREATE);
//
//     return fd;
// }
//
// int destroy_uinput_device(int fd) {
//     ioctl(fd, UI_DEV_DESTROY);
//     close(fd);
//     return 0;
// }
//
// void wiimote_to_uinput(const wiimote_state_t *wiimote,
//         const program_config_t *cfg, int uinput_fd) {
//     if (!wiimote->initialized) {
//         LOG_ERROR("Wiimote not initialized, cannot map to uinput.");
//         return;
//     }
//     if (wiimote->ext_status != EXT_CLASSIC_CONTROLLER) {
//         emit(uinput_fd, EV_KEY, BTN_SOUTH, wiimote->btn_a);
//         emit(uinput_fd, EV_KEY, BTN_EAST, wiimote->btn_b);
//         emit(uinput_fd, EV_KEY, BTN_WEST, wiimote->btn_1);
//         emit(uinput_fd, EV_KEY, BTN_NORTH, wiimote->btn_2);
//         emit(uinput_fd, EV_KEY, BTN_DPAD_UP, wiimote->btn_up);
//         emit(uinput_fd, EV_KEY, BTN_DPAD_DOWN, wiimote->btn_down);
//         emit(uinput_fd, EV_KEY, BTN_DPAD_LEFT, wiimote->btn_left);
//         emit(uinput_fd, EV_KEY, BTN_DPAD_RIGHT, wiimote->btn_right);
//         emit(uinput_fd, EV_KEY, BTN_START, wiimote->btn_plus);
//         emit(uinput_fd, EV_KEY, BTN_SELECT, wiimote->btn_minus);
//         emit(uinput_fd, EV_KEY, BTN_MODE, wiimote->btn_home);
//     }
//     switch (wiimote->ext_status) {
//         case EXT_NUNCHUCK:
//             emit(uinput_fd, EV_ABS, ABS_X, wiimote->nunchuck.sx-(cfg->upper_limit+1)/2);
//             emit(uinput_fd, EV_ABS, ABS_Y, 512 - wiimote->nunchuck.sy);
//             emit(uinput_fd, EV_KEY, BTN_TL, wiimote->nunchuck.z);
//             emit(uinput_fd, EV_KEY, BTN_TR, wiimote->nunchuck.c);
//             break;
//         case EXT_CLASSIC_CONTROLLER:
//             emit(uinput_fd,
//                     EV_KEY, BTN_EAST, cfg->inverted ?
//                         wiimote->classic_controller.b
//                         : wiimote->classic_controller.a);
//             emit(uinput_fd,
//                     EV_KEY, BTN_SOUTH, cfg->inverted ?
//                         wiimote->classic_controller.a
//                         : wiimote->classic_controller.b);
//             emit(uinput_fd,
//                     EV_KEY, BTN_WEST, cfg->inverted ?
//                         wiimote->classic_controller.y
//                         : wiimote->classic_controller.x);
//             emit(uinput_fd,
//                     EV_KEY, BTN_NORTH, cfg->inverted ?
//                         wiimote->classic_controller.x
//                         : wiimote->classic_controller.y);
//             emit(uinput_fd,
//                     EV_KEY, BTN_START, wiimote->classic_controller.plus);
//             emit(uinput_fd,
//                     EV_KEY, BTN_SELECT, wiimote->classic_controller.minus);
//             emit(uinput_fd,
//                     EV_KEY, BTN_MODE, wiimote->classic_controller.home);
//             emit(uinput_fd,
//                     EV_KEY, BTN_DPAD_UP, wiimote->classic_controller.du);
//             emit(uinput_fd,
//                     EV_KEY, BTN_DPAD_DOWN, wiimote->classic_controller.dd);
//             emit(uinput_fd,
//                     EV_KEY, BTN_DPAD_LEFT, wiimote->classic_controller.dl);
//             emit(uinput_fd,
//                     EV_KEY, BTN_DPAD_RIGHT, wiimote->classic_controller.dr);
//             emit(uinput_fd,
//                     EV_KEY, BTN_TL, wiimote->classic_controller.lz);
//             emit(uinput_fd,
//                     EV_KEY, BTN_TR, wiimote->classic_controller.rz);
//             emit(uinput_fd,
//                     EV_ABS, ABS_Z, wiimote->classic_controller.lt);
//             emit(uinput_fd,
//                     EV_ABS, ABS_RZ, wiimote->classic_controller.rt);
//             // emit(uinput_fd,
//             //         EV_KEY, BTN_TL2, wiimote->classic_controller.lt > 128);
//             // emit(uinput_fd,
//             //         EV_KEY, BTN_TR2, wiimote->classic_controller.rt > 128);
//             int lx = (wiimote->classic_controller.lx-512)*cfg->sens;
//             int ly = (512-wiimote->classic_controller.ly)*cfg->sens;
//             int rx = (wiimote->classic_controller.rx-512)*cfg->sens;
//             int ry = (512-wiimote->classic_controller.ry)*cfg->sens;
//             emit(uinput_fd,
//                     EV_ABS, ABS_X, lx);
//             emit(uinput_fd,
//                     EV_ABS, ABS_Y, ly);
//             emit(uinput_fd,
//                     EV_ABS, ABS_RX, rx);
//             emit(uinput_fd,
//                     EV_ABS, ABS_RY, ry);
//             break;
//         case EXT_NONE:
//         case EXT_UNKNOWN:
//         case EXT_WAITING_DECRYPTION_0:
//         case EXT_WAITING_DECRYPTION_1 :
//         case EXT_DECRYPTED:
//         default:
//             emit(uinput_fd, EV_ABS, ABS_X, 0);
//             emit(uinput_fd, EV_ABS, ABS_Y, 0);
//             emit(uinput_fd, EV_ABS, ABS_RX, 0);
//             emit(uinput_fd, EV_ABS, ABS_RY, 0);
//             emit(uinput_fd, EV_ABS, ABS_Z, 0);
//             emit(uinput_fd, EV_ABS, ABS_RZ, 0);
//             emit(uinput_fd, EV_KEY, BTN_TR, 0);
//             emit(uinput_fd, EV_KEY, BTN_TL, 0);
//             // emit(uinput_fd, EV_KEY, BTN_TR2, 0);
//             // emit(uinput_fd, EV_KEY, BTN_TL2, 0);
//             break;
//     }
//     emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
// }
//

int create_virtual_gamepad(virtual_gamepad_t *gamepad) {
    struct input_absinfo analogs = {
        .minimum = -512,
        .maximum = 511,
        .fuzz = 0,
        .flat = 10,
        .resolution = 1,
        .value = 0
    }, triggers = {
        .minimum = 0,
        .maximum = 255,
        .fuzz = 10,
        .flat = 0,
        .resolution = 1,
        .value = 0
    };

    gamepad->dev = libevdev_new();
    libevdev_set_name(gamepad->dev, "Xbox 360 Golden evdev");
    libevdev_enable_event_type(gamepad->dev, EV_KEY);

    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_SOUTH, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_EAST, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_WEST, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_NORTH, NULL);

    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_DPAD_UP, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_DPAD_DOWN, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_DPAD_LEFT, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_DPAD_RIGHT, NULL);

    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_START, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_SELECT, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_MODE, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_THUMBL, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_THUMBR, NULL);

    libevdev_enable_event_code(gamepad->dev, EV_ABS, ABS_X, &analogs);
    libevdev_enable_event_code(gamepad->dev, EV_ABS, ABS_Y, &analogs);
    libevdev_enable_event_code(gamepad->dev, EV_ABS, ABS_RX, &analogs);
    libevdev_enable_event_code(gamepad->dev, EV_ABS, ABS_RY, &analogs);

    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_TR, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_KEY, BTN_TL, NULL);
    libevdev_enable_event_code(gamepad->dev, EV_ABS, ABS_RZ, &triggers);
    libevdev_enable_event_code(gamepad->dev, EV_ABS, ABS_Z, &triggers);


    int err = libevdev_uinput_create_from_device(gamepad->dev,
            LIBEVDEV_UINPUT_OPEN_MANAGED,
            &gamepad->uidev);
    if (err != 0)
        return err;
    return 0;
}

void update_virtgp_state(const wiimote_state_t *wiimote,
        const program_config_t *cfg, const virtual_gamepad_t *gp) {
    if (!wiimote->initialized) {
        LOG_WARN("Wiimote not initialized, cannot map to uinput.");
        return;
    }
    if (wiimote->ext_status != EXT_CLASSIC_CONTROLLER) {
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_SOUTH, wiimote->btn_a);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_EAST, wiimote->btn_b);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_WEST, wiimote->btn_1);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_NORTH, wiimote->btn_2);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_DPAD_UP, wiimote->btn_up);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_DPAD_DOWN, wiimote->btn_down);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_DPAD_LEFT, wiimote->btn_left);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_DPAD_RIGHT, wiimote->btn_right);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_START, wiimote->btn_plus);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_SELECT, wiimote->btn_minus);
        libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_MODE, wiimote->btn_home);
    }
    switch (wiimote->ext_status) {
        case EXT_NUNCHUCK:
            libevdev_uinput_write_event(gp->uidev, EV_ABS, ABS_X, wiimote->nunchuck.sx-(cfg->upper_limit+1)/2);
            libevdev_uinput_write_event(gp->uidev, EV_ABS, ABS_Y, 512 - wiimote->nunchuck.sy);
            libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_TL, wiimote->nunchuck.z);
            libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_TR, wiimote->nunchuck.c);
            break;
        case EXT_CLASSIC_CONTROLLER:
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_EAST, cfg->inverted ?
                        wiimote->classic_controller.b
                        : wiimote->classic_controller.a);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_SOUTH, cfg->inverted ?
                        wiimote->classic_controller.a
                        : wiimote->classic_controller.b);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_WEST, cfg->inverted ?
                        wiimote->classic_controller.y
                        : wiimote->classic_controller.x);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_NORTH, cfg->inverted ?
                        wiimote->classic_controller.x
                        : wiimote->classic_controller.y);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_START, wiimote->classic_controller.plus);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_SELECT, wiimote->classic_controller.minus);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_MODE, wiimote->classic_controller.home);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_DPAD_UP, wiimote->classic_controller.du);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_DPAD_DOWN, wiimote->classic_controller.dd);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_DPAD_LEFT, wiimote->classic_controller.dl);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_DPAD_RIGHT, wiimote->classic_controller.dr);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_TL, wiimote->classic_controller.lz);
            libevdev_uinput_write_event(gp->uidev,
                    EV_KEY, BTN_TR, wiimote->classic_controller.rz);
            libevdev_uinput_write_event(gp->uidev,
                    EV_ABS, ABS_Z, wiimote->classic_controller.lt);
            libevdev_uinput_write_event(gp->uidev,
                    EV_ABS, ABS_RZ, wiimote->classic_controller.rt);
            // libevdev_uinput_write_event(gp->uidev,
            //         EV_KEY, BTN_TL2, wiimote->classic_controller.lt > 128);
            // libevdev_uinput_write_event(gp->uidev,
            //         EV_KEY, BTN_TR2, wiimote->classic_controller.rt > 128);
            int lx = (wiimote->classic_controller.lx-512)*cfg->sens;
            int ly = (512-wiimote->classic_controller.ly)*cfg->sens;
            int rx = (wiimote->classic_controller.rx-512)*cfg->sens;
            int ry = (512-wiimote->classic_controller.ry)*cfg->sens;
            libevdev_uinput_write_event(gp->uidev,
                    EV_ABS, ABS_X, lx);
            libevdev_uinput_write_event(gp->uidev,
                    EV_ABS, ABS_Y, ly);
            libevdev_uinput_write_event(gp->uidev,
                    EV_ABS, ABS_RX, rx);
            libevdev_uinput_write_event(gp->uidev,
                    EV_ABS, ABS_RY, ry);
            break;
        case EXT_NONE:
        case EXT_UNKNOWN:
        case EXT_WAITING_DECRYPTION_0:
        case EXT_WAITING_DECRYPTION_1 :
        case EXT_DECRYPTED:
        default:
            libevdev_uinput_write_event(gp->uidev, EV_ABS, ABS_X, 0);
            libevdev_uinput_write_event(gp->uidev, EV_ABS, ABS_Y, 0);
            libevdev_uinput_write_event(gp->uidev, EV_ABS, ABS_RX, 0);
            libevdev_uinput_write_event(gp->uidev, EV_ABS, ABS_RY, 0);
            libevdev_uinput_write_event(gp->uidev, EV_ABS, ABS_Z, 0);
            libevdev_uinput_write_event(gp->uidev, EV_ABS, ABS_RZ, 0);
            libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_TR, 0);
            libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_TL, 0);
            // libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_TR2, 0);
            // libevdev_uinput_write_event(gp->uidev, EV_KEY, BTN_TL2, 0);
            break;
    }

    libevdev_uinput_write_event(gp->uidev, EV_SYN, SYN_REPORT, 0);
}

void destroy_virtual_gamepad(virtual_gamepad_t *gamepad) {
    if (gamepad == NULL) return;
    if (gamepad->uidev) {
        libevdev_uinput_destroy(gamepad->uidev);
        gamepad->uidev = NULL;
    }
    if (gamepad->dev) {
        libevdev_free(gamepad->dev);
        gamepad->dev = NULL;
    }
}
