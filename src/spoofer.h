#ifndef _GSPOOFER_H_
#define _GSPOOFER_H_
#include "wiimote.h"

void wiimote_to_uinput(const wiimote_state_t *wiimote, int uinput_fd);
int create_uinput_device(void);
int destroy_uinput_device(int fd);
#endif
