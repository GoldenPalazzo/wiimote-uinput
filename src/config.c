#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "toml-c.h"
#include "config.h"
#include "logger.h"

#define INVERSE_STRING          "invert-ab"
#define INVERSE_DEFAULT         false
#define UPPERLIMIT_STRING       "upper-limit"
#define UPPERLIMIT_DEFAULT      1023
#define UPPERLIMIT_LOWER_BOUND  0
#define UPPERLIMIT_UPPER_BOUND  1023
#define ANALOG_SENS_STRING      "analog_sens"
#define ANALOG_SENS_DEFAULT     1.2

int create_config_dir(char *config_folder, size_t buf_size) {
    assert(config_folder != NULL);
    assert(buf_size > 0);
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");

    if (!xdg_config_home) {
        xdg_config_home = getenv("HOME");
        snprintf(config_folder, buf_size,
                "%s/.config/wiimote-uinput", xdg_config_home);
    } else {
        snprintf(config_folder, buf_size,
                "%s/wiimote-uinput", xdg_config_home);
    }

    if (mkdir(config_folder, 0755) < 0 && errno != EEXIST) {
        LOG_ERROR("Error creating config folder %s (%d)", config_folder, errno);
        return -errno;
    }

    return 0;
}

int parse_config(FILE* fp, program_config_t *cfg) {
    assert(fp != NULL);
    assert(cfg != NULL);
	char errbuf[200];
	toml_table_t *tbl = toml_parse_file(fp, errbuf, sizeof(errbuf));
    if (tbl == 0) {
        LOG_ERROR("Error parsing configuration: %s", errbuf);
        return -1;
    }
    toml_value_t invert = toml_table_bool(tbl, INVERSE_STRING);
    if (!invert.ok)
        invert.u.b = INVERSE_DEFAULT;
    toml_value_t upper_limit = toml_table_int(tbl, UPPERLIMIT_STRING);
    if (!upper_limit.ok
            || upper_limit.u.i < UPPERLIMIT_LOWER_BOUND
            || upper_limit.u.i > UPPERLIMIT_UPPER_BOUND)
        upper_limit.u.i = UPPERLIMIT_DEFAULT;
    toml_value_t sens = toml_table_int(tbl, ANALOG_SENS_STRING);
    if (!sens.ok
            || sens.u.d < 1
            || sens.u.d > 2)
        sens.u.d = ANALOG_SENS_DEFAULT;

    cfg->inverted = invert.u.b;
    cfg->upper_limit = (int)upper_limit.u.i;
    cfg->sens = (float)sens.u.d;
    toml_free(tbl);
    return 0;
}

int apply_default_config(program_config_t *cfg) {
    assert(cfg != NULL);
    cfg->upper_limit = UPPERLIMIT_DEFAULT;
    cfg->inverted = INVERSE_DEFAULT;
    cfg->sens = (float)ANALOG_SENS_DEFAULT;
    return 0;
}

void log_configuration(const program_config_t *cfg) {
    LOG_INFO("Logging configuration.");
    LOG_INFO("Inverse: %d", cfg->inverted);
    LOG_INFO("Upper limit: %d", cfg->upper_limit);
    LOG_INFO("Analog sens: %f", cfg->sens);
}
