/*
 * SPI master test.
 */

#include "cyusb-spi.h"

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
            "  -c <config>   : set SPI configuration (below)\n"
            "\n"
            "Default SPI config: -c " DEFAULT_CONFIG "\n"
            "                       ^^^^^^frequency-in-HZ\n"
            "                              ^data width in bits (4-16)\n"
            "                                ^M(otorola), T(I), or N(S)\n"
            "                                  ^isMSBFirst\n"
            "                                   ^isMaster\n"
            "                                    ^isContinuous\n"
            "                                     ^isSelectPrecede\n"
            "                                      ^isCpha\n"
            "                                       ^isCpol\n");
    fprintf(stderr,
            "Example:\n"
            "  $ %s rw 7        # run 7 clocks, writing 0000000\n", p);
    fprintf(stderr,
            "  $ %s rw 7 0b1011 # run 7 clocks, writing 1011000\n", p);
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
            ctx->selected.ifnum = 0; // TODO: Which interface should I use?
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
parse_spi_config(const char *spec, CY_SPI_CONFIG *cfg) {
    CY_SPI_CONFIG tmp;
    char c, *ep;

    tmp.frequency = strtoul(spec, &ep, 10);
    ep++;

    tmp.dataWidth = strtoul(ep, &ep, 10);
    ep++;

    switch (c = *ep++) {
    case 'M': tmp.protocol = CY_SPI_MOTOROLA; break;
    case 'T': tmp.protocol = CY_SPI_TI; break;
    case 'N': tmp.protocol = CY_SPI_NS; break;
    default:
        log("Unknown SPI protocol. Must be M/T/N: %c\n", c);
        return -1;
    }
    ep++;

    tmp.isMsbFirst       = (*ep++ == '1');
    tmp.isMaster         = (*ep++ == '1');
    tmp.isContinuousMode = (*ep++ == '1');
    tmp.isSelectPrecede  = (*ep++ == '1');
    tmp.isCpha           = (*ep++ == '1');
    tmp.isCpol           = (*ep++ == '1');

    if (*ep != '\0')
        return -1;

    *cfg = tmp;

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

    int opt;
    while ((opt = getopt(argc, argv, "hvd:i:c:")) != -1) {
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
        case 'c':
            ctx->opt.config = my_strdup(optarg);
            break;
        default:
            usage(argv[0]);
        }
    }

    if (parse_spi_config(ctx->opt.config, &ctx->config) != 0) {
        usage(argv[0]);
    }

    return optind;
}

void
run(struct app_ctx *ctx, int argc, char **argv) {
    if (argc < 2) {
        return;
    }

    // Usage: cyusb-spi rw 123 0x12 0b10111 ...
    if (strcmp(argv[0], "rw") != 0) {
        return;
    }

    int bitlen = atoi(argv[1]);
    int buflen = (bitlen >> 3) + !!(bitlen >> 3);

    uint8_t rbuf[buflen], wbuf[buflen];

    CY_DATA_BUFFER rb = { .buffer = rbuf, .length = buflen };
    CY_DATA_BUFFER wb = { .buffer = wbuf, .length = buflen };

    memset(wbuf, 0, buflen);

    int bpos = 0;
    for (int i = 2; i < argc; i++) {
        char *arg = argv[i];
        int      blen = 0;
        uint64_t bval;

        // read bitvec value
        if (strncmp(arg, "0b", 2) == 0) {
            blen = strlen(arg + 2);
            bval = strtoull(arg + 2, &arg, 2);
        }
        else if (strncmp(arg, "0x", 2) == 0) {
            blen = strlen(arg + 2) << 2;
            bval = strtoull(arg, &arg, 16);
        }
        else {
            bval = strtoull(arg, &arg, 0);
            blen = ((bval <=       0xFF) ?  8 :
                    (bval <=     0xFFFF) ? 16 :
                    (bval <= 0xFFFFFFFF) ? 32 : 64);
        }

        // if given, read trailing bitvec length
        if (*arg == ':') {
            blen = strtol(arg + 1, NULL, 0);
        }

        if (bpos + blen > bitlen) {
            die("Bit length too short for given value(s): %d\n", bitlen);
        }

        // fill bits in MSByte-first + MSbit-first order
        while (blen--) {
            if (bval & (1 << blen)) {
                bit_set(wbuf, bpos++);
            }
            else {
                bit_clr(wbuf, bpos++);
            }
        }
    }

    // bit reverse each BYTE to make it MSByte-first + LSbit-first order
    for (int i = 0; i < buflen; i++) {
        wbuf[i] = bit_rev(wbuf[i]);
    }

    log("send: 0b");
    for (int i = bitlen - 1; i >= 0; i--) {
        log("%d", bit_get(wbuf, i));
    }
    log("\n");

    DO(CySetSpiConfig, ctx->handle, &ctx->config);
    DO(CySpiReadWrite, ctx->handle, &rb, &wb, 1000);
    log("recv:");
    for (int i = 0; i < rb.transferCount; i++) {
        log(" 0x%.2X", rb.buffer[i]);
    }
    log("\n");
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
