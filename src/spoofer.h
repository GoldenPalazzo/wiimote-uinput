#ifndef _GSPOOFER_H_
#define _GSPOOFER_H_
#include "config.h"
#include "wiimote.h"

void wiimote_to_uinput(const wiimote_state_t *wiimote,
        const program_config_t *cfg, int uinput_fd);
int create_uinput_device(const program_config_t *cfg);
int destroy_uinput_device(int fd);
#endif
