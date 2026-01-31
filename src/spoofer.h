#ifndef _GSPOOFER_H_
#define _GSPOOFER_H_
#include "config.h"
#include "wiimote.h"
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

typedef struct {
    struct libevdev *dev;
    struct libevdev_uinput *uidev;
} virtual_gamepad_t;

void wiimote_to_uinput(const wiimote_state_t *wiimote,
        const program_config_t *cfg, int uinput_fd);
int create_uinput_device(const program_config_t *cfg);
int destroy_uinput_device(int fd);

int create_virtual_gamepad(virtual_gamepad_t *gamepad);
void update_virtgp_state(const wiimote_state_t *wiimote,
        const program_config_t *cfg, const virtual_gamepad_t *gp);
void destroy_virtual_gamepad(virtual_gamepad_t *gamepad);
#endif
