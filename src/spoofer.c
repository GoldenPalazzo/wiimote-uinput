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

inline bool is_horizontal(const wiimote_state_t *wm) {
    return wm->acc_x >= 0x1a0
           && wm->acc_x <= 0x1ef
           && wm->acc_y <= 0x24f
           && wm->acc_y >= 0x1d0;
}

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
        bool is_vert = !is_horizontal(wiimote);
        libevdev_uinput_write_event(gp->uidev, EV_KEY,
                is_vert ? BTN_SOUTH : BTN_NORTH, wiimote->btn_a);
        libevdev_uinput_write_event(gp->uidev, EV_KEY,
                is_vert ? BTN_EAST : BTN_WEST, wiimote->btn_b);
        libevdev_uinput_write_event(gp->uidev, EV_KEY,
                is_vert ? BTN_WEST : BTN_EAST, wiimote->btn_1);
        libevdev_uinput_write_event(gp->uidev, EV_KEY,
                is_vert ? BTN_NORTH : BTN_SOUTH, wiimote->btn_2);
        libevdev_uinput_write_event(gp->uidev, EV_KEY,
                is_vert ? BTN_DPAD_UP : BTN_DPAD_LEFT, wiimote->btn_up);
        libevdev_uinput_write_event(gp->uidev, EV_KEY,
                is_vert ? BTN_DPAD_DOWN : BTN_DPAD_RIGHT, wiimote->btn_down);
        libevdev_uinput_write_event(gp->uidev, EV_KEY,
                is_vert ? BTN_DPAD_LEFT : BTN_DPAD_DOWN, wiimote->btn_left);
        libevdev_uinput_write_event(gp->uidev, EV_KEY,
                is_vert ? BTN_DPAD_RIGHT : BTN_DPAD_UP, wiimote->btn_right);
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
