#include "spoofer.h"
#include "wiimote.h"
#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <linux/hidraw.h>
#include <libudev.h>
#include <sys/epoll.h>

#define MAX_WIIMOTES 4

#define UNUSED(x) (void)(x)
static volatile int keep_running = 1;
void sigint_handler(int _) {
    UNUSED(_);
    keep_running = 0;
}

static inline int is_wiimote(struct hidraw_devinfo *info) {
    return (info->vendor == 0x057e && (
                info->product == 0x0306 || info->product == 0x0330));
}

int register_wiimote_device(struct udev_device *dev,
        int epoll_fd,
        int *connected_wiimotes,
        int connected_wiimotes_fds[],
        int connected_wiimotes_uinputs_fds[],
        wiimote_state_t connected_wiimotes_states[]) {
    int ret = 0;
    struct epoll_event ev;
    struct hidraw_devinfo info;
    const char *action = udev_device_get_action(dev);
    const char *devnode = udev_device_get_devnode(dev);
    if (devnode == NULL) {
        goto reg_wiimote_failed_dev;
    }
    LOG_INFO("Udev event: %s - %s", action, devnode);

    if (action != NULL && strcmp(action, "remove") == 0) {
        LOG_INFO("  Remove action, ignoring.");
        goto reg_wiimote_failed_dev;
    }
    int fd = open(devnode, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open devnode");
        ret = -1;
        goto reg_wiimote_failed_dev;
    }
    if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
        perror("ioctl HIDIOCGRAWINFO");
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }

    LOG_INFO("  Vendor: 0x%04hx, Product: 0x%04hx", info.vendor, info.product);
    if (!is_wiimote(&info)) {
        LOG_INFO("  Not a Wiimote, ignoring.");
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }
    if (*connected_wiimotes+1 > MAX_WIIMOTES) {
        LOG_INFO("  Maximum number of connected Wiimotes reached (%d).", MAX_WIIMOTES);
        // todo: should implement a routine to
        // disconnect the connecting wiimote...
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl: wiimote device");
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }
    LOG_INFO("  Wiimote device added to epoll.");
    connected_wiimotes_uinputs_fds[*connected_wiimotes] =
        create_uinput_device();
    if (connected_wiimotes_uinputs_fds[*connected_wiimotes] < 0) {
        perror("create_uinput_device");
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }
    connected_wiimotes_states[*connected_wiimotes] =
        (wiimote_state_t){0};
    connected_wiimotes_fds[*connected_wiimotes] = fd;
    (*connected_wiimotes)++;
    LOG_INFO("  Wiimote connected (fd %d)! Total connected: %d", fd, *connected_wiimotes);
    goto reg_wiimote_success;

reg_wiimote_failed_wiimote:
    close(fd);
reg_wiimote_failed_dev:
reg_wiimote_success:
    udev_device_unref(dev);
    return ret;
}

int main(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);
    enable_module(LOG_LEVEL_INFO);
    enable_module(LOG_LEVEL_ERROR);
    int ret = 0;

    struct udev *udev = udev_new();
    if (!udev) {
        LOG_ERROR("Cannot create udev context.");
        ret = 1;
        goto failed;
    }
    LOG_INFO("Udev context created.");
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon) {
        LOG_ERROR("Cannot create udev monitor.");
        udev_unref(udev);
        ret = 1;
        goto failed_udev;
    }
    LOG_INFO("Udev monitor created.");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "hidraw", NULL);
    udev_monitor_enable_receiving(mon);
    int mon_fd = udev_monitor_get_fd(mon);
    if (mon_fd < 0) {
        LOG_ERROR("Cannot get udev monitor file descriptor.");
        udev_monitor_unref(mon);
        udev_unref(udev);
        ret = 1;
        goto failed_udev_monitor;
    }
    LOG_INFO("Udev monitor receiving enabled.");

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        udev_monitor_unref(mon);
        udev_unref(udev);
        ret = 1;
        goto failed_udev_monitor;
    }
    LOG_INFO("Epoll instance created.");

    struct epoll_event ev, events[10];
    ev.events = EPOLLIN;
    ev.data.fd = mon_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mon_fd, &ev) < 0) {
        perror("epoll_ctl: udev monitor");
        ret = 1;
        goto failed_epoll;
    }
    LOG_INFO("Udev monitor added to epoll.");

    int connected_wiimotes = 0;
    int connected_wiimotes_fds[MAX_WIIMOTES] = {-1, -1, -1, -1};
    int connected_wiimotes_uinputs_fds[MAX_WIIMOTES] = {-1, -1, -1, -1};
    wiimote_state_t connected_wiimotes_states[MAX_WIIMOTES] = {0};

    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "hidraw");
    udev_enumerate_scan_devices(enumerate);
    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry;
    udev_list_entry_foreach(entry, devices) {
        const char *path = udev_list_entry_get_name(entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);
        LOG_INFO("Found device: %s", path);
        register_wiimote_device(dev,
            epoll_fd,
            &connected_wiimotes,
            connected_wiimotes_fds,
            connected_wiimotes_uinputs_fds,
            connected_wiimotes_states);
    }

    signal(SIGINT, sigint_handler);
    int n_events, i;
    char event_buffer[64];
    while (keep_running) {
        n_events = epoll_wait(epoll_fd, events, 10, 1000);
        LOG_DEBUG("Epoll wait returned %d events.", n_events);
        if (n_events < 0) {
            perror("epoll_wait");
            ret = 1;
            break;
        } else if (n_events == 0) {
            continue;
        }

        for (i=0; i<n_events; i++) {
            if (events[i].data.fd == mon_fd) { // udev monitor loop
                register_wiimote_device(
                    udev_monitor_receive_device(mon),
                    epoll_fd,
                    &connected_wiimotes,
                    connected_wiimotes_fds,
                    connected_wiimotes_uinputs_fds,
                    connected_wiimotes_states);
            } else { // wiimote loop
                // continue; // temporarily disabled
                int wiimote_fd = events[i].data.fd;
                int wiimote_index = -1;
                for (int j=0; j<connected_wiimotes; j++) {
                    if (connected_wiimotes_fds[j] == wiimote_fd) {
                        wiimote_index = j;
                        break;
                    }
                }
                if (wiimote_index == -1) {
                    LOG_ERROR("Unknown wiimote fd %d", wiimote_fd);
                    continue;
                }

                wiimote_state_t *state = &connected_wiimotes_states[wiimote_index];
                if (events[i].events & EPOLLOUT && state->initialized == 0) {
                    LOG_INFO("Initializing Wiimote...", wiimote_fd);
                    write(wiimote_fd, (char[]){STATUS_INFO_REQUEST, 0x00}, 2);
                }

                int uinput_fd = connected_wiimotes_uinputs_fds[wiimote_index];

                ssize_t r_bytes = read(
                        wiimote_fd, event_buffer, sizeof(event_buffer));
                if (r_bytes < 0) {
                    // perror("read wiimote event");
                    if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                        LOG_ERROR("epoll wiimote error detected, disconnecting.");
                        LOG_INFO("Wiimote disconnected (read %d bytes).", r_bytes);
                        close(wiimote_fd);
                        destroy_uinput_device(uinput_fd);
                        for (int k=wiimote_index; k<connected_wiimotes-1; k++) {
                            connected_wiimotes_fds[k] = connected_wiimotes_fds[k+1];
                            connected_wiimotes_uinputs_fds[k] =
                                connected_wiimotes_uinputs_fds[k+1];
                            connected_wiimotes_states[k] =
                                connected_wiimotes_states[k+1];
                            // todo: update lights on wiimotes
                        }
                        connected_wiimotes_fds[connected_wiimotes-1] = -1;
                        connected_wiimotes_uinputs_fds[connected_wiimotes-1] = -1;
                        connected_wiimotes_states[connected_wiimotes-1] = (wiimote_state_t){0};
                        connected_wiimotes--;
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, wiimote_fd, NULL);
                    } else if (errno != EAGAIN) {
                        LOG_ERROR("Failed to read wiimote event %d", errno);
                    }
                    continue;
                } else if (r_bytes == 0) {
                    LOG_ERROR("unhandled: read 0 bytes from wiimote fd %d", wiimote_fd);
                    continue;
                }

                // char debug_log_event[128];
                // char *p = debug_log_event;
                // size_t remaining = sizeof(debug_log_event);
                // for (int k=0; k<r_bytes; k++) {
                //     int written = snprintf(p,
                //         remaining, "%02hhx ", event_buffer[k]);
                //     remaining -= written;
                //     p += written;
                // }
                // LOG_INFO("Wiimote event (%d bytes): %s", r_bytes, debug_log_event);
                if (handle_wiimote_event(
                    wiimote_fd, state, event_buffer, 0) < 0) {
                    LOG_ERROR("Failed to handle wiimote event.");
                    continue;
                }
                wiimote_to_uinput(state, uinput_fd);
            }
        }
    }

    // close(wiimote_fd);
    // destroy_uinput_device(uinput_fd);

failed_epoll:
    close(epoll_fd);
failed_udev_monitor:
    udev_monitor_unref(mon);
failed_udev:
    udev_unref(udev);
failed:
    return ret;
}
