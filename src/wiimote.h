#ifndef _GWIIMOTE_H_
#define _GWIIMOTE_H_
#include <stddef.h>

/* Wii reports */
#define WII_RUMBLE 0x10
#define WII_LEDS   0x11
#define WII_REPORTMODE 0x12
#define WII_REPORTONCHANGE 0x00
#define WII_REPORTCONTINUOUS 0x04

/* Input report types */
#define WII_STATUSREPORT 0x20
#define WII_READMEMDATA 0x21
#define WII_ACKOUTRETURN 0x22

/* Input report types with data */
typedef enum {
    COREBTNS = 0x30,
    COREEXT8 = 0x32
} WiimoteInputReportType;

const char *corebtns_mask = "---pudrlh--mab12";
const char *flags_mask = "iseb";

const char *bus_str(int bus);
void parse_wiimsg(const char *buf);
int send_msg(int fd, const char *buf, size_t buf_size);
int get_msg(int fd, char *buf, size_t buf_size);
void send_rumble(int fd, int secs);
int wiimote_connect(const char *hidraw_file, WiimoteInputReportType intype);
#endif


