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

typedef struct {
    int hidraw_fd;
    int uinput_fd;
    wiimote_state_t state;
    int active;
    char dev_path[256];
} wiimote_context_t;

void cleanup_wiimote_context(wiimote_context_t *ctx);
int register_wiimote_device(struct udev_device *dev,
        int epoll_fd,
        wiimote_context_t *wiimotes);
int setup_udev_monitor(struct udev **udev_out,
        struct udev_monitor **mon_out);
int init_connected_wiimotes(struct udev *udev,
        int epoll_fd,
        wiimote_context_t *wiimote_contexts);

int main(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);
    enable_module(LOG_LEVEL_INFO);
    enable_module(LOG_LEVEL_ERROR);

    int ret = 0, mon_fd, epoll_fd;
    struct udev *udev;
    struct udev_monitor *mon;
    struct epoll_event ev, events[10];
    wiimote_context_t wiimote_contexts[MAX_WIIMOTES] = {0};

    if (setup_udev_monitor(&udev, &mon) < 0) {
        ret = 1;
        goto failed;
    }
    mon_fd = udev_monitor_get_fd(mon);
    if (mon_fd < 0) {
        LOG_ERROR("Cannot get udev monitor file descriptor.");
        udev_monitor_unref(mon);
        udev_unref(udev);
        ret = 1;
        goto failed_udev_monitor;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        udev_monitor_unref(mon);
        udev_unref(udev);
        ret = 1;
        goto failed_udev_monitor;
    }
    LOG_INFO("Epoll instance created.");

    ev.events = EPOLLIN;
    ev.data.fd = mon_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mon_fd, &ev) < 0) {
        perror("epoll_ctl: udev monitor");
        ret = 1;
        goto failed_epoll;
    }
    LOG_INFO("Udev monitor added to epoll.");

    init_connected_wiimotes(udev, epoll_fd, wiimote_contexts);

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
                    wiimote_contexts);
            } else { // wiimote loop
                int ev_fd = events[i].data.fd;
                wiimote_context_t *wm = NULL;
                for (int j=0; j<MAX_WIIMOTES; j++) {
                    if (wiimote_contexts[j].active
                        && wiimote_contexts[j].hidraw_fd == ev_fd) {
                        wm = wiimote_contexts+j;
                        break;
                    }
                }
                if (wm == NULL) {
                    LOG_ERROR("Unknown wiimote fd %d", ev_fd);
                    continue;
                }

                if (events[i].events & EPOLLOUT && wm->state.initialized == 0) {
                    LOG_INFO("Initializing Wiimote...", wm->hidraw_fd);
                    write(wm->hidraw_fd, (char[]){STATUS_INFO_REQUEST, 0x00}, 2);
                    // todo: should check it's properly initialized...
                }

                ssize_t r_bytes = 1;
                while (r_bytes > 0) {
                    r_bytes = read(
                            wm->hidraw_fd, event_buffer, sizeof(event_buffer));
                    if (handle_wiimote_event(
                        wm->hidraw_fd, &wm->state, event_buffer) < 0) {
                        LOG_ERROR("Failed to handle wiimote event.");
                        continue;
                    }
                    wiimote_to_uinput(&wm->state, wm->uinput_fd);
                }
                if (r_bytes < 0) {
                    if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                        LOG_ERROR("epoll wiimote error detected, disconnecting.");
                        LOG_INFO("Wiimote disconnected (read %d bytes).", r_bytes);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, wm->hidraw_fd, NULL);
                        cleanup_wiimote_context(wm);
                    } else if (errno != EAGAIN) {
                        LOG_ERROR("Failed to read wiimote event %d", errno);
                    } else if (errno == EAGAIN) {
                        LOG_DEBUG("No more data to read from wiimote fd %d", wm->hidraw_fd);
                    }
                } else if (r_bytes == 0) {
                    LOG_ERROR("unhandled: read 0 bytes from wiimote fd %d", wm->hidraw_fd);
                }
            }
        }
    }

failed_epoll:
    close(epoll_fd);
failed_udev_monitor:
    udev_monitor_unref(mon);
failed_udev:
    udev_unref(udev);
failed:
    return ret;
}

void cleanup_wiimote_context(wiimote_context_t *ctx) {
    if (ctx->hidraw_fd >= 0) {
        close(ctx->hidraw_fd);
        ctx->hidraw_fd = -1;
    }
    if (ctx->uinput_fd >= 0) {
        destroy_uinput_device(ctx->uinput_fd);
        ctx->uinput_fd = -1;
    }
    ctx->active = 0;
    memset(&ctx->state, 0, sizeof(wiimote_state_t));
    memset(ctx->dev_path, 0, sizeof(ctx->dev_path));
}

int register_wiimote_device(struct udev_device *dev,
        int epoll_fd,
        wiimote_context_t *wiimotes) {
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
    wiimote_context_t *wm = NULL;
    for (size_t i=0; i<MAX_WIIMOTES; i++) {
        if (wiimotes[i].active == 0) {
            wm = &wiimotes[i];
            break;
        }
    }
    if (wm == NULL) {
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
    wm->uinput_fd =
        create_uinput_device();
    if (wm->uinput_fd < 0) {
        perror("create_uinput_device");
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }
    wm->state =
        (wiimote_state_t){0};
    wm->hidraw_fd = fd;
    wm->active = 1;
    LOG_INFO("  Wiimote connected (fd %d)! Total connected: %d", fd, (wm-wiimotes)+1);
    goto reg_wiimote_success;

reg_wiimote_failed_wiimote:
    close(fd);
reg_wiimote_failed_dev:
reg_wiimote_success:
    udev_device_unref(dev);
    return ret;
}

int setup_udev_monitor(struct udev **udev_out,
        struct udev_monitor **mon_out) {
    struct udev *udev = udev_new();
    if (!udev) {
        LOG_ERROR("Cannot create udev context.");
        return -1;
    }
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon) {
        LOG_ERROR("Cannot create udev monitor.");
        udev_unref(udev);
        return -1;
    }
    udev_monitor_filter_add_match_subsystem_devtype(mon, "hidraw", NULL);
    udev_monitor_enable_receiving(mon);
    *udev_out = udev;
    *mon_out = mon;
    return 0;
}

int init_connected_wiimotes(struct udev *udev,
        int epoll_fd,
        wiimote_context_t *wiimote_contexts) {
    int ret = 0;
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    if (enumerate == NULL) {
        LOG_ERROR("Cannot create udev enumerate.");
        ret = -1;
        goto init_connected_wiimotes_failed;
    }
    if (udev_enumerate_add_match_subsystem(enumerate, "hidraw") < 0) {
        LOG_ERROR("Cannot add match subsystem to udev enumerate.");
        ret = -1;
        goto init_connected_wiimotes_failed;
    }
    if (udev_enumerate_scan_devices(enumerate) < 0) {
        LOG_ERROR("Cannot scan devices with udev enumerate.");
        ret = -1;
        goto init_connected_wiimotes_failed;
    }
    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry;
    if (devices == NULL) {
        LOG_INFO("No hidraw devices found.");
        goto init_connected_wiimotes_failed;
    }
    udev_list_entry_foreach(entry, devices) {
        const char *path = udev_list_entry_get_name(entry);
        if (path == NULL) {
            continue;
        }
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);
        if (dev == NULL) {
            continue;
        }
        LOG_INFO("Found device: %s", path);
        register_wiimote_device(dev,
            epoll_fd,
            wiimote_contexts
        );
    }
init_connected_wiimotes_failed:
    udev_enumerate_unref(enumerate);
    return 0;
}


