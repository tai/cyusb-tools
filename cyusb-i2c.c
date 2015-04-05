/*
 * I2C master test.
 */

#include "cyusb-i2c.h"

void
usage(char *prog) {
    char *p = basename(prog);
        
    fprintf(stderr,
            "Usage: %s [options] <cmd> <args...>\n", p);
    fprintf(stderr,
            "Options:\n"
            "  -h            : show this help\n"
            "  -v            : verbose output\n"
            "  -d <vid>:<pid>: select USB target by vendor and product ID\n"
            "  -i <n>        : select <n>th one if -d option is ambigious\n"
            "  -f <config>   : set I2C configuration\n"
            "  -c <config>   : set data I2C configuration\n"
            "\n"
            "Default I2C config: -f " DEFAULT_CONFIG "\n"
            "                       ^^^^^^frequency\n"
            "                              ^^^^slave-mode address\n"
            "                                   ^isMaster\n"
            "                                    ^isClockStretch\n"
            "Default I2C data config: -c " DEFAULT_DATA_CONFIG "\n"
            "                            ^^^^slave address\n"
            "                                 ^isStopBit\n"
            "                                  ^isNakBit\n");
    fprintf(stderr,
            "Example:\n"
            "  $ %s r 2          # read 2 bytes\n", p);
    fprintf(stderr,
            "  $ %s w 0x12 0x34  # send 2 bytes\n", p);
    exit(1);
}

// Under some configuration, mingw does not define strdup(3).
char *
my_strdup(const char *src) {
    int len = strlen(src) + 1;
    char *tmp = malloc(len);
    return memcpy(tmp, src, len);
}

char *
basename(char *p) {
    char *pn = p;
    char *ps;

    ps = strrchr(p, '/');
    if (pn < ps) pn = ps + 1;
    ps = strrchr(p, '\\');
    if (pn < ps) pn = ps + 1;
    return pn;
}

const char *
cydtype_s(CY_DEVICE_TYPE dt) {
    switch (dt) {
    case CY_TYPE_DISABLED: return "DISABLED";
    case CY_TYPE_UART:     return "UART";
    case CY_TYPE_SPI:      return "SPI";
    case CY_TYPE_I2C:      return "I2C";
    case CY_TYPE_JTAG:     return "JTAG";
    case CY_TYPE_MFG:      return "MFG";
    }
    return "UNKNOWN";
}

const char *
cydclass_s(CY_DEVICE_CLASS dc) {
    switch (dc) {
    case CY_CLASS_DISABLED: return "DISABLED";
    case CY_CLASS_CDC:      return "CDC";
    case CY_CLASS_PHDC:     return "PHDC";
    case CY_CLASS_VENDOR:   return "VENDOR";
    }
    return "UNKNOWN";
}

void
show_device(CY_DEVICE_INFO *info, void *data) {
    printf("=====\n");
    printf("vid=0x%.4X\n", info->vidPid.vid);
    printf("pid=0x%.4X\n", info->vidPid.pid);
    printf("manufacturerName=%s\n", info->manufacturerName);
    printf("productName=%s\n", info->productName);
    printf("serialNum=%s\n", info->serialNum);
    printf("deviceFriendlyName=%s\n", info->deviceFriendlyName);

    printf("numInterfaces=%d\n", info->numInterfaces);
    for (int ifindex = 0; ifindex < info->numInterfaces; ifindex++) {
        const char *dt = cydtype_s(info->deviceType[ifindex]);
        const char *dc = cydclass_s(info->deviceClass[ifindex]);
            
        printf("  if[%d].deviceClass=%s\n", ifindex, dc);
        printf("  if[%d].deviceType=%s\n", ifindex, dt);
    }

#ifdef WIN32
    printf("deviceBlock=0x%X\n", info->deviceBlock);
#endif
}

void
pick_device(CY_DEVICE_INFO *info, void *data) {
    struct app_ctx *ctx = data;

    //show_device(info, data);

    if (info->vidPid.vid != ctx->opt.vid || info->vidPid.pid != ctx->opt.pid) {
        return;
    }
    ctx->nr_dev_found++;

#ifdef WIN32
    if (info->deviceBlock != SerialBlock_SCB0) {
        return;
    }
#endif
    ctx->nr_dev_match++;

    if (ctx->nr_dev_match == ctx->opt.index + 1) {
        ctx->selected.devnum = ctx->nr_dev_found - 1;
#ifdef WIN32
        ctx->selected.ifnum = 0; // On Windows, there is no interface to claim
#else
        ctx->selected.ifnum = 0;
        for (int ifindex = 0; ifindex < info->numInterfaces; ifindex++) {
            ctx->selected.ifnum = 0; // TODO: Which interface should be used?
        }
#endif
    }
}

void
scan_device(void (*scan)(CY_DEVICE_INFO *, void *), void *data) {
    CY_RETURN_STATUS rc;
    UINT8 nr;

    rc = CyGetListofDevices(&nr);
    if (rc != CY_SUCCESS) {
        return;
    }

    for (int i = 0; i < nr; i++) {
        CY_DEVICE_INFO info;

        rc = CyGetDeviceInfo(i, &info);
        if (rc == CY_SUCCESS) {
            scan(&info, data);
        }
    }
}

int
parse_i2c_config(const char *spec, CY_I2C_CONFIG *config) {
    char *ep;

    config->frequency = strtoul(spec, &ep, 10);

    if (*ep++ != ':') {
        return 0;
    }

    config->slaveAddress = strtoul(ep, &ep, 0);

    if (*ep++ != ':') {
        return 0;
    }

    config->isMaster       = (*ep++ == '1');
    config->isClockStretch = (*ep++ == '1');

    return 0;
}

int
parse_i2c_data_config(const char *spec, CY_I2C_DATA_CONFIG *dc) {
    char *ep;

    dc->slaveAddress = strtoul(spec, &ep, 0);
    ep++;

    dc->isStopBit = (*ep++ == '1');
    dc->isNakBit  = (*ep++ == '1');

    return 0;
}

int
parse_args(struct app_ctx *ctx, int argc, char **argv) {

    if (argc <= 1) {
        usage(argv[0]);
    }

    // defaults
    ctx->opt.vid = DEFAULT_VID;
    ctx->opt.pid = DEFAULT_PID;
    ctx->opt.index = 0;
    ctx->opt.config = DEFAULT_CONFIG;
    ctx->opt.data_config = DEFAULT_DATA_CONFIG;

    int opt;
    while ((opt = getopt(argc, argv, "hvd:i:f:c:")) != -1) {
        switch (opt) {
        case 'h':
            usage(argv[0]);
            break;
        case 'v':
            ctx->opt.verbose = 1;
            break;
        case 'd': {
            char *ep;
            ctx->opt.vid = strtol(optarg, &ep, 0);
            ctx->opt.pid = strtol(ep + 1, NULL, 0);
            break;
        }
        case 'i':
            ctx->opt.index = atoi(optarg);
            break;
        case 'f':
            ctx->opt.config = my_strdup(optarg);
            break;
        case 'c':
            ctx->opt.data_config = my_strdup(optarg);
            break;
        default:
            usage(argv[0]);
        }
    }

    if (parse_i2c_config(ctx->opt.config, &ctx->config) != 0) {
        usage(argv[0]);
    }

    if (parse_i2c_data_config(ctx->opt.data_config, &ctx->data_config) != 0) {
        usage(argv[0]);
    }

    return optind;
}

void
run(struct app_ctx *ctx, int argc, char **argv) {
    DO(CySetI2cConfig, ctx->handle, &ctx->config);

    uint8_t buf[8];

    CY_DATA_BUFFER db = {
        .buffer = buf,
        .length = sizeof(buf),
        .transferCount = 0,
    };

    if (! argc) return;

    // Usage: cyusb-i2c r 2
    if (strcmp(argv[0], "r") == 0) {
        db.length        = atoi(argv[1]);
        db.transferCount = 0;
        DO(CyI2cRead, ctx->handle, &ctx->data_config, &db, 1000);

        log("recv:");
        for (int i = 0; i < db.transferCount; i++) {
            log(" 0x%.2X", db.buffer[i]);
        }
        log("\n");
    }
    // Usage: cyusb-i2c w 0x12 0x23 0x34 ...
    else {
        memset(buf, 0, sizeof(buf));
        for (int i = 1; i < argc && i <= sizeof(buf); i++) {
            buf[i - 1] = strtol(argv[i], NULL, 0);
        }
        db.length        = argc - 1;
        db.transferCount = 0;
        DO(CyI2cWrite, ctx->handle, &ctx->data_config, &db, 1000);
        log("sent: %d bytes\n", db.transferCount);
    }
}

int
main(int argc, char **argv) {
    static struct app_ctx ctx;

    int optind = parse_args(&ctx, argc, argv);

    ctx.selected.devnum = -1;
    ctx.selected.ifnum  = -1;
    scan_device(pick_device, &ctx);

    DO(CyOpen, ctx.selected.devnum, ctx.selected.ifnum, &ctx.handle);
    run(&ctx, argc - optind, argv + optind);
    DO(CyClose, ctx.handle);

    return 0;
}
