#include <linux/uinput.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <memory.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#define UNUSED(x) (void)(x)

static volatile int keep_running = 1;
void sigint_handler(int _) {
    UNUSED(_);
    keep_running = 0;
}

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

void press_btn(int fd, int code) {
    emit(fd, EV_KEY, code, 1);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    sleep(1);
    emit(fd, EV_KEY, code, 0);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    sleep(1);
}

int main(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    struct uinput_setup usetup;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open fallita");
        exit(fd);
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
    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    sleep(1);

    char devname[16];
    ioctl(fd, UI_GET_SYSNAME(sizeof(devname)), devname);
    printf("created device %s\n", devname);

    signal(SIGINT, sigint_handler);

    while (keep_running) {
        press_btn(fd, BTN_A);
    }

    printf("received sigint. stopping...\n");
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}

