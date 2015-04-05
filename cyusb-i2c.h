#ifndef CYUSB_I2C_H
#define CYUSB_I2C_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>

#ifdef WIN32
#include <windows.h>
#endif

#include "CyUSBSerial.h"

#define DEFAULT_VID 0x04B4
#define DEFAULT_PID 0x0004

#define DEFAULT_CONFIG "100000:0x10:10"
#define DEFAULT_DATA_CONFIG "0x10:00"

#define log(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
#define die(...) do { log(__VA_ARGS__); exit(1); } while (0)

#define DO(api, ...)                            \
    do {                                        \
        log(#api ": calling\n");                \
        CY_RETURN_STATUS cs = api(__VA_ARGS__); \
        if (cs != CY_SUCCESS) {                 \
            die(#api ": cs=%d", cs);            \
        }                                       \
        log(#api ": OK\n");                     \
    } while (0)
    
struct app_opt {
    int verbose;
    int vid, pid;
    int index;
    char *config;
    char *data_config;
};

struct app_ctx {
    struct app_opt opt;

    int nr_dev_found, nr_dev_match;

    struct {
        int devnum, ifnum;
    } selected;

    CY_HANDLE handle;
    CY_I2C_CONFIG config;
    CY_I2C_DATA_CONFIG data_config;
};

extern char *
basename(char *p);

#endif
