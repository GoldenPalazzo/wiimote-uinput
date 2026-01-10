#include "spoofer.h"
#include "wiimote.h"
#include "logger.h"

#include <argp.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <linux/uinput.h>
#include <linux/hidraw.h>
#include <libudev.h>
#include <sys/epoll.h>

#define MAX_WIIMOTES 4
#define MAX_GRABBED_DEVICES 8

#define UNUSED(x) (void)(x)
static volatile int keep_running = 1;
void sigint_handler(int _) {
    UNUSED(_);
    keep_running = 0;
}

static int parse_opt(int key, char *arg, struct argp_state *state) {
    UNUSED(arg);
    UNUSED(state);
    switch (key) {
        case 'v':
            enable_module(LOG_LEVEL_DEBUG);
            break;
        case ARGP_KEY_END:
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}
const struct argp_option options[] = {
    {0, 'v', 0, 0, "Enable verbose output"},
    {0}
};
const char *argp_program_version =
    "wiimote-uinput 0.1";

static inline int is_wiimote(struct hidraw_devinfo *info) {
    return (info->vendor == 0x057e && (
                info->product == 0x0306 || info->product == 0x0330));
}

typedef struct {
    int hidraw_fd;
    int uinput_fd;
    uint8_t hid_writable;
    char dev_path[256];
    int8_t active;
    wiimote_state_t state;
    msg_queue_t msg_queue;

    struct udev_monitor *udev_grabber;
    int grabbed_fds[MAX_GRABBED_DEVICES];
    int num_grabbed_fds;
} wiimote_context_t;

int register_wiimote_context(
        int fd,
        int epoll_fd,
        wiimote_context_t *wiimotes,
        const char *dev_path);
void cleanup_wiimote_context(wiimote_context_t *ctx);
int udev_routine(struct udev_device *dev,
        int epoll_fd,
        wiimote_context_t *wiimotes);
int setup_udev_monitor(struct udev **udev_out,
        struct udev_monitor **mon_out);
int init_connected_wiimotes(struct udev *udev,
        int epoll_fd,
        wiimote_context_t *wiimote_contexts);
void grabbing_setup(
        struct udev_device *hidraw_dev,
        wiimote_context_t *ctx,
        int epoll_fd);
void grab_single_device(
        struct udev_device *dev,
        wiimote_context_t *ctx);
void wiimote_routine(
        wiimote_context_t *wm,
        struct epoll_event *event,
        int epoll_fd);

int main(int argc, char *argv[]) {
    const struct argp arguments = {
        .options = options,
        .parser = parse_opt,
        .doc = "Wiimote to uinput",
    };
    argp_parse(&arguments, argc, argv, 0, 0, 0);

    enable_module(LOG_LEVEL_INFO);
    enable_module(LOG_LEVEL_WARN);
    enable_module(LOG_LEVEL_ERROR);

    int ret = 0, mon_fd, epoll_fd;
    struct udev *udev;
    struct udev_monitor *mon;
    struct epoll_event ev, events[10];
    wiimote_context_t wiimote_contexts[MAX_WIIMOTES] = {0};

    if (access("/dev/uinput", F_OK) < 0) {
        LOG_ERROR("/dev/uinput not found. Is uinput module loaded?");
        ret = 1;
        goto failed;
    }

    if (access("/dev/uinput", W_OK) < 0) {
        LOG_ERROR("No write access to /dev/uinput. Make a udev rule or run as root.");
        ret = 1;
        goto failed;
    }

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
    while (keep_running) {
        n_events = epoll_wait(epoll_fd, events, 10, 30000);
        LOG_DEBUG("Epoll wait returned %d events.", n_events);
        if (n_events < 0) {
            LOG_ERROR("epoll_wait failed. (errno=%d)", errno);
            perror("epoll_wait");
            ret = 1;
            break;
        } else if (n_events == 0) {
            continue;
        }

        for (i=0; i<n_events; i++) {
            int ev_fd = events[i].data.fd;
            LOG_DEBUG("Epoll event on fd %d", ev_fd);
            if (ev_fd == mon_fd) { // udev monitor loop
                LOG_DEBUG("Udev monitor event detected.");
                udev_routine(
                    udev_monitor_receive_device(mon),
                    epoll_fd,
                    wiimote_contexts);
            } else { // wiimote loop
                wiimote_context_t *wm = NULL;
                struct udev_monitor *grabber = NULL;
                for (int j=0; j<MAX_WIIMOTES; j++) {
                    if (wiimote_contexts[j].active) {
                        if (wiimote_contexts[j].hidraw_fd == ev_fd) {
                            wm = wiimote_contexts+j;
                            break;
                        } else if (wiimote_contexts[j].udev_grabber
                                && udev_monitor_get_fd(wiimote_contexts[j].udev_grabber) == ev_fd) {
                            // grabber = wiimote_contexts[j].udev_grabber;
                            // wm = wiimote_contexts+j;
                            break;
                        }
                    }
                }
                // if (wm == NULL) {
                //     LOG_ERROR("Unknown wiimote fd %d", ev_fd);
                //     continue;
                // }
                if (grabber != NULL) {
                    // struct udev_device *dev =
                    //     udev_monitor_receive_device(grabber);
                    // if (dev != NULL) {
                    //     LOG_DEBUG("Udev grabber event detected.");
                    //     grab_sibling_devices(dev, wm, epoll_fd);
                    //     udev_device_unref(dev);
                    // }
                    LOG_WARN("Udev grabber event handling not implemented yet.");
                    struct hidraw_devinfo info;
                    struct udev_device *dev =
                        udev_monitor_receive_device(grabber);
                    const char *action = udev_device_get_action(dev);
                    const char *devnode = udev_device_get_devnode(dev);
                    if (devnode) {
                        LOG_INFO("  Grabbing kernel input device: %s", devnode);
                        int fd = open(devnode, O_RDWR | O_NONBLOCK);
                        if (fd >= 0) {
                            if (ioctl(fd, EVIOCGRAB, 1) == 0) {
                                if (wm->num_grabbed_fds < MAX_GRABBED_DEVICES) {
                                    wm->grabbed_fds[wm->num_grabbed_fds++] = fd;
                                    LOG_INFO("    Grabbed successfully.");
                                } else {
                                    LOG_WARN("    Maximum grabbed devices reached, closing.");
                                    close(fd);
                                }
                            } else {
                                LOG_ERROR("    Failed to grab device (errno=%d).", errno);
                                perror("    ioctl EVIOCGRAB");
                                close(fd);
                            }
                        }
                    }

                } else if (wm != NULL) {
                    wiimote_routine(wm, &events[i], epoll_fd);
                }
            }
        }
    }

failed_epoll:
    close(epoll_fd);
failed_udev_monitor:
    udev_monitor_unref(mon);
// failed_udev:
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
    if (ctx->num_grabbed_fds > 0) {
        for (int i=0; i<ctx->num_grabbed_fds; i++) {
            close(ctx->grabbed_fds[i]);
        }
        ctx->num_grabbed_fds = 0;
    }
    if (ctx->udev_grabber) {
        udev_monitor_unref(ctx->udev_grabber);
        ctx->udev_grabber = NULL;
    }
    ctx->active = 0;
    ctx->hid_writable = 0;
    memset(&ctx->state, 0, sizeof(wiimote_state_t));
    memset(ctx->dev_path, 0, sizeof(ctx->dev_path));
    memset(&ctx->msg_queue, 0, sizeof(msg_queue_t));
    ctx->dev_path[0] = '\0';
}

int udev_routine(
        struct udev_device *dev,
        int epoll_fd,
        wiimote_context_t *wiimotes) {
    int ret = 0;
    struct epoll_event ev;
    struct hidraw_devinfo info;
    const char *action = udev_device_get_action(dev);
    const char *devnode = udev_device_get_devnode(dev);
    if (devnode == NULL || action == NULL) {
        goto udev_routine_failed_dev;
    }
    LOG_INFO("Udev event: %s - %s", action, devnode);

    if (strcmp(action, "remove") == 0) {
        LOG_INFO("  Remove action");
        for (size_t i=0; i<MAX_WIIMOTES; i++) {
            if (wiimotes[i].active
                    && strcmp(wiimotes[i].dev_path, devnode) == 0) {
                LOG_INFO("  Wiimote disconnected (fd %d)!",
                        wiimotes[i].hidraw_fd);
                cleanup_wiimote_context(&wiimotes[i]);
                break;
            }
        }
        goto udev_routine_removed_wiimote;
    } else if (strcmp(action, "add") != 0) {
        LOG_INFO("  Ignoring non-add/remove action.");
        goto udev_routine_failed_dev;
    }

    int fd = open(devnode, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open devnode");
        ret = -1;
        goto udev_routine_failed_dev;
    }
    if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
        perror("ioctl HIDIOCGRAWINFO");
        ret = -1;
        goto udev_routine_failed_wiimote;
    }

    LOG_INFO("  Vendor: 0x%04hx, Product: 0x%04hx", info.vendor, info.product);
    if (!is_wiimote(&info)) {
        LOG_INFO("  Not a Wiimote, ignoring.");
        ret = -1;
        goto udev_routine_failed_wiimote;
    }
    LOG_INFO("  Wiimote detected, registering...");
    if (register_wiimote_context(
            fd,
            epoll_fd,
            wiimotes,
            devnode) < 0) {
        goto udev_routine_failed_wiimote;
    }
    goto udev_routine_connected_wiimote;

udev_routine_failed_wiimote:
    close(fd);
udev_routine_connected_wiimote:
udev_routine_removed_wiimote:
udev_routine_failed_dev:
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
        wiimote_context_t *ctxs) {
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
        struct hidraw_devinfo info;
        const char *devnode = udev_device_get_devnode(dev);
        if (devnode == NULL) {
            udev_device_unref(dev);
            continue;
        }
        int fd = open(devnode, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            perror("open devnode");
            udev_device_unref(dev);
            continue;
        }
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            perror("ioctl HIDIOCGRAWINFO");
            udev_device_unref(dev);
            continue;
        }

        LOG_INFO("  Vendor: 0x%04hx, Product: 0x%04hx", info.vendor, info.product);
        if (!is_wiimote(&info)) {
            LOG_INFO("  Not a Wiimote, ignoring.");
            close(fd);
            udev_device_unref(dev);
            continue;
        }
        LOG_INFO("  Wiimote detected, registering...");
        int reg_result = register_wiimote_context(
                fd,
                epoll_fd,
                ctxs,
                devnode);
        if (reg_result < 0) {
            close(fd);
        } else {
            // grabbing_setup(dev, &ctxs[reg_result], epoll_fd);
        }
        udev_device_unref(dev);
    }
init_connected_wiimotes_failed:
    udev_enumerate_unref(enumerate);
    return ret;
}

void grabbing_setup(
        struct udev_device *hidraw_dev,
        wiimote_context_t *ctx,
        int epoll_fd) {
    ctx->num_grabbed_fds = 0;
    struct udev *udev = udev_device_get_udev(hidraw_dev);
    struct udev_device *parent = udev_device_get_parent(hidraw_dev);
    if (!parent) {
        LOG_ERROR("Cannot get parent device for grabbing.");
        return;
    } else {
        const char *parent_sysname = udev_device_get_sysname(parent);
        LOG_INFO("Grabbing sibling devices of parent: %s", parent_sysname);
    }

    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "input", NULL);
    udev_monitor_enable_receiving(mon);

    ctx->udev_grabber = mon;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
        udev_monitor_get_fd(mon),
        &(struct epoll_event){
            .events = EPOLLIN | EPOLLET,
            .data.fd = udev_monitor_get_fd(mon)
        }
    );

    // Enumerate input devices under the same parent

    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_parent(enumerate, parent);
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices, *entry;
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(entry, devices) {
        const char *path = udev_list_entry_get_name(entry);
        struct udev_device *input_dev =
            udev_device_new_from_syspath(udev, path);
        const char *devnode = udev_device_get_devnode(input_dev);
        if (devnode) {
            grab_single_device(input_dev, ctx);
        }
        udev_device_unref(input_dev);
    }
    udev_enumerate_unref(enumerate);
}

void grab_single_device(
        struct udev_device *dev,
        wiimote_context_t *ctx) {
    const char *devnode = udev_device_get_devnode(dev);
    LOG_INFO("  Grabbing kernel input device: %s", devnode);
    int fd = open(devnode, O_RDWR | O_NONBLOCK);
    if (fd >= 0) {
        if (ioctl(fd, EVIOCGRAB, 1) == 0) {
            if (ctx->num_grabbed_fds < MAX_GRABBED_DEVICES) {
                ctx->grabbed_fds[ctx->num_grabbed_fds++] = fd;
                LOG_INFO("    Grabbed successfully.");
            } else {
                LOG_WARN("    Maximum grabbed devices reached, closing.");
                close(fd);
            }
        } else {
            LOG_ERROR("    Failed to grab device (errno=%d).", errno);
            perror("    ioctl EVIOCGRAB");
            close(fd);
        }
    }
}

int register_wiimote_context(
        int fd,
        int epoll_fd,
        wiimote_context_t *wiimotes,
        const char *dev_path) {
    wiimote_context_t *wm = NULL;
    size_t index;
    for (index=0; index<MAX_WIIMOTES; index++) {
        if (wiimotes[index].active == 0) {
            wm = &wiimotes[index];
            break;
        }
    }
    if (wm == NULL) {
        LOG_INFO("  Maximum number of connected Wiimotes reached (%d).", MAX_WIIMOTES);
        // todo: should implement a routine to
        // disconnect the connecting wiimote...
        return -1;
    }
    struct epoll_event ev = {
        .events = EPOLLIN | EPOLLOUT | EPOLLET,
        .data.fd = fd
    };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl: wiimote device");
        return -1;
    }
    LOG_INFO("  Wiimote device added to epoll.");
    wm->uinput_fd =
        create_uinput_device();
    if (wm->uinput_fd < 0) {
        perror("create_uinput_device");
        return -1;
    }
    wm->state =
        (wiimote_state_t){0};
    wm->msg_queue = (msg_queue_t){0};
    wm->hidraw_fd = fd;
    wm->active = 1;
    strncpy(wm->dev_path, dev_path, sizeof(wm->dev_path)-1);
    LOG_INFO("  Wiimote connected (fd %d)! Total connected: %d", fd, (wm-wiimotes)+1);
    enqueue_msg(
            &wm->msg_queue,
            (uint8_t[]){0x11, (uint8_t)(0x10 << index)},
            2);
    enqueue_msg(
            &wm->msg_queue,
            (uint8_t[]){0x15, 0x00},
            2);
    return (int)index;
}

void wiimote_routine(
        wiimote_context_t *wm,
        struct epoll_event *event,
        int epoll_fd) {
    if (event->events & EPOLLOUT) {
        LOG_DEBUG("Wiimote fd %d ready for writing.", wm->hidraw_fd);
        wm->hid_writable = 1;
    }
    while (wm->hid_writable
           && wm->msg_queue.count > 0) {
        const msg_t *msg = &wm->msg_queue.msgs[wm->msg_queue.head];
        ssize_t w_bytes = write(
                wm->hidraw_fd,
                msg->buf, msg->len);
        if (w_bytes < 0) {
            if (errno != EAGAIN) {
                LOG_ERROR("Failed to write wiimote event %d", errno);
            } else if (errno == EAGAIN) {
                LOG_DEBUG(
                        "Wiimote fd %d not ready for writing.",
                        wm->hidraw_fd);
                wm->hid_writable = 0;
            }
        } else {
            LOG_DEBUG("Wrote %zd bytes to wiimote fd %d",
                    w_bytes, wm->hidraw_fd);
            pop_msg(&wm->msg_queue, NULL);
        }
    }

    ssize_t r_bytes = 1;
    if (event->events & EPOLLIN)
        LOG_DEBUG("Wiimote fd %d ready for reading.", wm->hidraw_fd);

    uint8_t event_buffer[64];
    while (r_bytes > 0 && (event->events & EPOLLIN)) {
        r_bytes = read(
                wm->hidraw_fd,
                event_buffer, sizeof(event_buffer));
        char buf_hex[3*64] = {0};
        for (ssize_t k=0; k<r_bytes; k++) {
            sprintf(&buf_hex[k*3], "%02x ", event_buffer[k]);
        }
        LOG_DEBUG("Read %zd bytes from wiimote fd %d: %s",
                r_bytes, wm->hidraw_fd, buf_hex);
        if (handle_wiimote_event(
                &wm->msg_queue,
                &wm->state,
                event_buffer) < 0) {
            LOG_ERROR("Failed to handle wiimote event.");
            continue;
        }
        wiimote_to_uinput(&wm->state, wm->uinput_fd);
    }
    if (r_bytes < 0) {
        if (event->events & (EPOLLERR | EPOLLHUP)) {
            LOG_ERROR(
                    "epoll wiimote error detected, disconnecting.");
            LOG_INFO(
                    "Wiimote disconnected (read %d bytes).",
                    r_bytes);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, wm->hidraw_fd, NULL);
            cleanup_wiimote_context(wm);
        } else if (errno != EAGAIN) {
            LOG_ERROR("Failed to read wiimote event %d", errno);
        } else if (errno == EAGAIN) {
            // LOG_DEBUG(
            //         "No more data to read from wiimote fd %d",
            //         wm->hidraw_fd);
        }
    } else if (r_bytes == 0) {
        LOG_ERROR("unhandled: read 0 bytes from wiimote fd %d",
                wm->hidraw_fd);
    }

    struct input_event ff_ev;
    while (read(wm->uinput_fd, &ff_ev, sizeof(ff_ev)) > 0) {
        LOG_DEBUG("Read event from uinput fd %d: "
                "type=%hu code=%hu value=%d",
                wm->uinput_fd,
                ff_ev.type, ff_ev.code, ff_ev.value);
    }
}


