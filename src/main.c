#include "config.h"
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
#define MAX_EVENTS   10

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

program_config_t global_config;

typedef struct {
    int hidraw_fd;
    int uinput_fd;
    uint8_t hid_writable;
    char dev_path[256];
    int8_t active;
    wiimote_state_t state;
    msg_queue_t msg_queue;

    virtual_gamepad_t virt_gp;
} wiimote_context_t;

int setup_global_config(void);
void cleanup_wiimote_context(wiimote_context_t *ctx);
int register_wiimote_device(const char *devnode,
        int epoll_fd,
        wiimote_context_t *wiimotes);
int setup_udev_monitor(struct udev **udev_out,
        struct udev_monitor **mon_out);
int init_connected_wiimotes(struct udev *udev,
        int epoll_fd,
        wiimote_context_t *wiimote_contexts);
int test_uinput_access(void);

int main(int argc, char *argv[]) {
    int ret = 0;
    enable_module(LOG_LEVEL_INFO);
    enable_module(LOG_LEVEL_WARN);
    enable_module(LOG_LEVEL_ERROR);

    const struct argp arguments = {
        .options = options,
        .parser = parse_opt,
        .doc = "Wiimote to uinput",
    };
    argp_parse(&arguments, argc, argv, 0, 0, 0);

    if (setup_global_config() < 0) {
        ret = 1;
        goto failed;
    }

    if (test_uinput_access() < 0) {
        ret = 1;
        goto failed;
    }

    int mon_fd, epoll_fd;
    struct udev *udev;
    struct udev_monitor *mon;
    struct epoll_event ev, events[MAX_EVENTS];
    wiimote_context_t wiimote_contexts[MAX_WIIMOTES] = {0};

    if (setup_udev_monitor(&udev, &mon) < 0) {
        ret = 1;
        goto failed;
    }
    mon_fd = udev_monitor_get_fd(mon);

    if (mon_fd < 0) {
        LOG_ERROR("Cannot get udev monitor file descriptor.");
        ret = 1;
        goto failed_udev_monitor;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
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
    uint8_t event_buffer[64];
    while (keep_running) {
        n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 30000);
        if (n_events < 0) {
            LOG_ERROR("epoll_wait failed. (errno=%d)", errno);
            ret = 1;
            break;
        } else if (n_events == 0) {
            continue;
        }

        for (i=0; i<n_events; i++) {
            if (events[i].data.fd == mon_fd) { // udev monitor loop
                LOG_DEBUG("Udev monitor event detected.");
                struct udev_device *dev = udev_monitor_receive_device(mon);
                if (dev == NULL)
                    continue;
                const char *action = udev_device_get_action(dev);
                const char *devnode = udev_device_get_devnode(dev);
                if (devnode == NULL || action == NULL) {
                    udev_device_unref(dev);
                    continue;
                }
                LOG_DEBUG("Udev event: %s - %s", action, devnode);

                if (strcmp(action, "remove") == 0) {
                    wiimote_context_t *wm = NULL;
                    for (int j=0; j<MAX_WIIMOTES; j++) {
                        if (!wiimote_contexts[j].active)
                            continue;
                        if (strcmp(wiimote_contexts[j].dev_path, devnode) == 0) {
                            wm = wiimote_contexts+j;
                            break;
                        }
                    }
                    if (wm != NULL) {
                        // put a non-null pointer in this epoll_ctl
                        // to keep backwards compatibility with Linux pre 2.6.9
                        // see epoll_ctl(2) BUGS section
                        LOG_INFO("Disconnecting wiimote %d...", wiimote_contexts-wm);
                        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, wm->hidraw_fd, &ev) < 0)
                            LOG_ERROR("Error while removing epoll wiimote (%d)", errno);
                        cleanup_wiimote_context(wm);
                        LOG_INFO("Disconnected wiimote %d", wiimote_contexts-wm);
                    }
                } else if (strcmp(action, "add") == 0) {
                    register_wiimote_device(
                        devnode,
                        epoll_fd,
                        wiimote_contexts);
                }
                udev_device_unref(dev);


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

                if (events[i].events & EPOLLOUT) {
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
                if (events[i].events & EPOLLIN)
                    LOG_DEBUG("Wiimote fd %d ready for reading.", wm->hidraw_fd);
                while (r_bytes > 0 && (events[i].events & EPOLLIN)) {
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
                    // wiimote_to_uinput(&wm->state, &global_config, wm->uinput_fd);
                    update_virtgp_state(&wm->state, &global_config, &wm->virt_gp);
                }
                if (r_bytes < 0) {
                    if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                        LOG_ERROR(
                                "epoll wiimote error detected, disconnecting.");
                        LOG_INFO(
                                "Wiimote disconnected (read %d bytes).",
                                r_bytes);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, wm->hidraw_fd, &ev);
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

                // struct input_event ff_ev;
                // while (read(wm->uinput_fd, &ff_ev, sizeof(ff_ev)) > 0) {
                //     LOG_DEBUG("Read event from uinput fd %d: "
                //             "type=%hu code=%hu value=%d",
                //             wm->uinput_fd,
                //             ff_ev.type, ff_ev.code, ff_ev.value);
                // }
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
    // if (ctx->uinput_fd >= 0) {
    //     destroy_uinput_device(ctx->uinput_fd);
    //     ctx->uinput_fd = -1;
    // }
    ctx->active = 0;
    ctx->hid_writable = 0;
    destroy_virtual_gamepad(&ctx->virt_gp);
    memset(&ctx->state, 0, sizeof(wiimote_state_t));
    memset(ctx->dev_path, 0, sizeof(ctx->dev_path));
    memset(&ctx->msg_queue, 0, sizeof(ctx->msg_queue));
    memset(&ctx->virt_gp, 0, sizeof(virtual_gamepad_t));
}

int register_wiimote_device(const char *devnode,
        int epoll_fd,
        wiimote_context_t *wiimotes) {
    int ret = 0;
    struct epoll_event ev;
    struct hidraw_devinfo info;
    int fd = open(devnode, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        LOG_DEBUG("Error while opening device %s (errno=%d), "
                "probably innocuous.", devnode, errno);
        ret = -1;
        goto reg_wiimote_failed_dev;
    }
    if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
        LOG_WARN("Couldn't get device %s info (errno=%d)", devnode, errno);
        perror("ioctl HIDIOCGRAWINFO");
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }

    LOG_DEBUG("  Vendor: 0x%04hx, Product: 0x%04hx", info.vendor, info.product);
    if (!is_wiimote(&info)) {
        LOG_INFO("  Not a Wiimote, ignoring.");
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }
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
    LOG_DEBUG("  Wiimote device added to epoll.");
    // wm->uinput_fd =
    //     create_uinput_device(&global_config);
    // if (wm->uinput_fd < 0) {
    //     perror("create_uinput_device");
    //     ret = -1;
    //     goto reg_wiimote_failed_wiimote;
    // }
    if (create_virtual_gamepad(&wm->virt_gp) < 0) {
        ret = -1;
        goto reg_wiimote_failed_wiimote;
    }
    wm->state =
        (wiimote_state_t){0};
    wm->msg_queue = (msg_queue_t){0};
    wm->hidraw_fd = fd;
    wm->active = 1;
    strcpy(wm->dev_path, devnode);
    LOG_INFO("  Wiimote connected (fd %d)! Total connected: %d", fd, (wm-wiimotes)+1);
    enqueue_msg(
            &wm->msg_queue,
            (uint8_t[]){0x11, (uint8_t)(0x10 << index)},
            2);
    enqueue_msg(
            &wm->msg_queue,
            (uint8_t[]){0x15, 0x00},
            2);
    goto reg_wiimote_success;

reg_wiimote_failed_wiimote:
    close(fd);
reg_wiimote_failed_dev:
reg_wiimote_success:
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
        LOG_DEBUG("Found device: %s", path);
        const char *devnode = udev_device_get_devnode(dev);
        register_wiimote_device(devnode,
            epoll_fd,
            wiimote_contexts
        );
        udev_device_unref(dev);
    }
init_connected_wiimotes_failed:
    udev_enumerate_unref(enumerate);
    return ret;
}

int setup_global_config(void) {
    char config_path[1024];
    if (create_config_dir(config_path, sizeof(config_path)) < 0) {
        LOG_ERROR("Failed initializing config folder");
    } else {
        strcat(config_path, "/config.toml");
        LOG_INFO("Searching for %s", config_path);
    }

    FILE* config_file = fopen(config_path, "r");
    if (config_file == NULL) {
        LOG_WARN("No configuration found, applying default");
        apply_default_config(&global_config);
    } else if (parse_config(config_file, &global_config) < 0) {
        LOG_ERROR("Failed initializing global configuration");
        return -1;
    }
    log_configuration(&global_config);
    return 0;
}

int test_uinput_access(void) {
    if (access("/dev/uinput", F_OK) < 0) {
        LOG_ERROR("/dev/uinput not found. Is uinput module loaded?");
        return -1;
    }

    if (access("/dev/uinput", W_OK) < 0) {
        LOG_ERROR("No write access to /dev/uinput. Make a udev rule or run as root.");
        return -1;
    }
    return 0;
}


