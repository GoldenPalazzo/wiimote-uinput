#include "spoofer.h"
#include "wiimote.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#define UNUSED(x) (void)(x)
static volatile int keep_running = 1;
void sigint_handler(int _) {
    UNUSED(_);
    keep_running = 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /dev/hidrawX\n", argv[0]);
        return 1;
    }
    int uinput_fd, wiimote_fd;
    wiimote_state_t wiimote_state = {0};

    uinput_fd = create_uinput_device();
    if (uinput_fd < 0) {
        return uinput_fd;
    }

    wiimote_fd = connect_wiimote(argv[1], &wiimote_state);
    if (wiimote_fd < 0) {
        perror("open wiimote device");
        destroy_uinput_device(uinput_fd);
        return wiimote_fd;
    }

    signal(SIGINT, sigint_handler);
    while (keep_running) {
        update_wiimote_state(wiimote_fd, &wiimote_state, DEBUG_TYPE_WIIMOTE);

        // Mappa lo stato del Wiimote ai comandi di uinput
        wiimote_to_uinput(&wiimote_state, uinput_fd);

        usleep(10000); // Attendi 100ms
    }

    close(wiimote_fd);
    destroy_uinput_device(uinput_fd);
    return 0;
}
