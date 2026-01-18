#ifndef _GOLDEN_CONFIG_H
#define _GOLDEN_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * settings
 * invert-ab: bool
 *   inverts A and B placement in CC mode
 *   to replicate the Nintendo layout
 * upper-limit: int [0; 1023]
 *   the transformation from hid report to uinput will be
 *   [0; 1023] -> [-(analog_upper_limit+1)/2; analog_upper_limit/2]
 *
 */

typedef struct {
    bool inverted;
    int upper_limit;
    float sens;
} program_config_t;

int create_config_dir(char *config_folder, size_t buf_size);
int parse_config(FILE* fp, program_config_t* cfg);
int apply_default_config(program_config_t* cfg);
void log_configuration(const program_config_t *cfg);
#endif
