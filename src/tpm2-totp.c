/* SPDX-License-Identifier: BSD-3 */
/*******************************************************************************
 * Copyright 2018, Fraunhofer SIT
 * All rights reserved.
 *******************************************************************************/

#include <tpm2-totp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>
#include <qrencode.h>

#define VERB(...) if (opt.verbose) fprintf(stderr, __VA_ARGS__)
#define ERR(...) fprintf(stderr, __VA_ARGS__)

#define chkrc(rc, cmd) if (rc != TSS2_RC_SUCCESS) {\
    ERR("ERROR in %s (%s:%i): 0x%08x\n", __func__, __FILE__, __LINE__, rc); cmd; }

char *help =
    "Usage: [options] {generate|calculate|reseal|recover|clean}\n"
    "Options:\n"
    "    -h, --help      print help\n"
    "    -N, --nvindex   TPM NV index to store data (default: 0x018094AF)\n"
    "    -P, --password  Password for recovery/resealing (default: None)\n"
    "    -t, --time      Show the time used for calculation\n"
    "    -v, --verbose   print verbose messages\n"
    "\n";

static const char *optstr = "N:P:tv";

static const struct option long_options[] = {
    {"nvindex",  required_argument, 0, 'N'},
    {"password", required_argument, 0, 'P'},
    {"time",     no_argument,       0, 't'},
    {"verbose",  no_argument,       0, 'v'},
    {0,          0,                 0,  0 }
};

static struct opt {
    enum { CMD_NONE, CMD_GENERATE, CMD_CALCULATE, CMD_RESEAL, CMD_RECOVER, CMD_CLEAN } cmd;
    int nvindex;
    char *password;
    int time;
    int verbose;
} opt;

/** Parse and set command line options.
 *
 * This function parses the command line options and sets the appropriate values
 * in the opt struct.
 * @param argc The argument count.
 * @param argv The arguments.
 * @retval 0 on success
 * @retval 1 on failure
 */
int
parse_opts(int argc, char **argv)
{
    /* set the default values */
    opt.cmd = CMD_NONE;
    opt.nvindex = 0;
    opt.password = NULL;
    opt.time = 0;
    opt.verbose = 0;

    /* parse the options */
    int c;
    int opt_idx = 0;
    while (-1 != (c = getopt_long(argc, argv, optstr,
                                  long_options, &opt_idx))) {
        switch(c) {
        case 'h':
            printf("%s", help);
            exit(0);
        case 'e':
            if (sscanf(optarg, "0x%x", &opt.nvindex) != 1
                && sscanf(optarg, "%i", &opt.nvindex) != 1) {
                ERR("Error parsing nvindex.\n");
                exit(1);
            }
            break;
        case 'P':
            opt.password = optarg;
            break;
        case 't':
            opt.time = 1;
            break;
        case 'v':
            opt.verbose = 1;
            break;
        default:
            ERR("Unknown option at index %i.\n\n", opt_idx);
            ERR("%s", help);
            exit(1);
        }
    }

    /* parse the non-option arguments */
    if (optind >= argc) {
        ERR("Missing command: generate, calculate, reseal, recover, clean.\n\n");
        ERR("%s", help);
        exit(1);
    }
    if (!strcmp(argv[optind], "generate")) {
        opt.cmd = CMD_GENERATE;
    } else if (!strcmp(argv[optind], "calculate")) {
        opt.cmd = CMD_CALCULATE;
    } else if (!strcmp(argv[optind], "reseal")) {
        opt.cmd = CMD_RESEAL;
    } else if (!strcmp(argv[optind], "recover")) {
        opt.cmd = CMD_RECOVER;
    } else if (!strcmp(argv[optind], "clean")) {
        opt.cmd = CMD_CLEAN;
    } else {
        ERR("Unknown command: generate, calculate, reseal, recover, clean.\n\n");
        ERR("%s", help);
        exit(1);
    }        
    optind++;

    if (optind < argc) {
        ERR("Unknown argument provided.\n\n");
        ERR("%s", help);
        exit(1);
    }
    return 0;
}

static char *
base32enc(const uint8_t *in, size_t in_size) {
	static unsigned char base32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    size_t i = 0, j = 0;
    size_t out_size = ((in_size + 4) / 5) * 8;
    unsigned char *r = malloc(out_size + 1);

    while (1) {
        r[i++]  = in[j] >> 3 & 0x1F;
        r[i++]  = in[j] << 2 & 0x1F;
        if (++j >= in_size) break; else i--;
        r[i++] |= in[j] >> 6 & 0x1F;
        r[i++]  = in[j] >> 1 & 0x1F;
        r[i++]  = in[j] << 4 & 0x1F;
        if (++j >= in_size) break; else i--;
        r[i++] |= in[j] >> 4 & 0x1F;
        r[i++]  = in[j] << 1 & 0x1F;
        if (++j >= in_size) break; else i--;
        r[i++] |= in[j] >> 7 & 0x1F;
        r[i++]  = in[j] >> 2 & 0x1F;
        r[i++]  = in[j] << 3 & 0x1F;
        if (++j >= in_size) break; else i--;
        r[i++] |= in[j] >> 5 & 0x1F; 
        r[i++]  = in[j] & 0x1F;
        if (++j >= in_size) break;
    }
    for (j = 0; j < i; j++) {
        r[j] = base32[r[j]];
    }
    while (i < out_size) {
        r[i++] = '=';
    }
    r[i] = 0;
	return (char *)r;
}

char *
qrencode(const char *url)
{
    QRcode *qrcode = QRcode_encodeString(url, 0/*=version*/, QR_ECLEVEL_L,
                                         QR_MODE_8, 1/*=case*/);
    if (!qrcode) { ERR("QRcode failed."); exit(1); }

    char *qrpic = malloc(/* Margins top / bot*/ 2 * (
                            (qrcode->width+2) * 2 - 2 + 
                            strlen("\033[47m%*s\033[0m\n") ) +
                         /* lines */ qrcode->width * (
                            strlen("\033[47m  ") * (qrcode->width + 1) +
                            strlen("\033[47m  \033[0m\n")
                         ) + 1 /* \0 */);
    size_t idx = 0;
    idx += sprintf(&qrpic[idx], "\033[47m%*s\033[0m\n", 2*(qrcode->width+2), "");
    for (int y = 0; y < qrcode->width; y++) {
        idx += sprintf(&qrpic[idx], "\033[47m  ");
        for (int x = 0; x < qrcode->width; x++) {
            if (qrcode->data[y*qrcode->width + x] & 0x01) {
                idx += sprintf(&qrpic[idx], "\033[40m  ");
            } else {
                idx += sprintf(&qrpic[idx], "\033[47m  ");
            }
        }
        idx += sprintf(&qrpic[idx], "\033[47m  \033[0m\n");
    }
    idx += sprintf(&qrpic[idx], "\033[47m%*s\033[0m\n", 2*(qrcode->width+2), "");
    (void)(idx);
    free(qrcode);
    return qrpic;
}

#define URL_PREFIX "otpauth://totp/TPM2-TOTP?secret="

/** Main function
 *
 * This function initializes OpenSSL and then calls the key generation
 * functions.
 * @param argc The argument count.
 * @param argv The arguments.
 * @retval 0 on success
 * @retval 1 on failure
 */
int
main(int argc, char **argv)
{
    if (parse_opts(argc, argv) != 0)
        exit(1);

    int rc;
    uint8_t *secret, *keyBlob, *newBlob;
    size_t secret_size, keyBlob_size, newBlob_size;
    char *base32key, *url, *qrpic;
    uint64_t totp;
    time_t now;
    char timestr[100] = { 0, };

    switch(opt.cmd) {
    case CMD_GENERATE:
        rc = tpm2totp_generateKey(0x00, 0x00, opt.password,
                                  &secret, &secret_size, 
                                  &keyBlob, &keyBlob_size);
        chkrc(rc, exit(1));

        rc = tpm2totp_storeKey_nv(keyBlob, keyBlob_size, opt.nvindex);
        free(keyBlob);
        chkrc(rc, exit(1));

        base32key = base32enc(secret, secret_size);
        url = calloc(1, strlen(base32key) + strlen(URL_PREFIX) + 1);
        sprintf(url, URL_PREFIX "%s", base32key);
        free(base32key);

        qrpic = qrencode(url);

        printf("%s\n", qrpic);
        printf("%s\n", url);
        free(qrpic);
        free(url);
        break;
    case CMD_CALCULATE:
        rc = tpm2totp_loadKey_nv(opt.nvindex, &keyBlob, &keyBlob_size);
        chkrc(rc, exit(1));

        rc = tpm2totp_calculate(keyBlob, keyBlob_size, &now, &totp);
        free(keyBlob);
        chkrc(rc, exit(1));
        if (opt.time) {
            rc = !strftime (timestr, sizeof(timestr)-1, "%Y-%m-%d %H:%M:%S: ",
                            localtime (&now));
            chkrc(rc, exit(1));
        }
        printf("%s%06ld", timestr, totp);
        break;
    case CMD_RESEAL:
        rc = tpm2totp_loadKey_nv(opt.nvindex, &keyBlob, &keyBlob_size);
        chkrc(rc, exit(1));

        rc = tpm2totp_reseal(keyBlob, keyBlob_size, opt.password, 0x00, 0x00,
                             &newBlob, &newBlob_size);
        free(keyBlob);
        chkrc(rc, exit(1));

        //TODO: Are your sure ?
        rc = tpm2totp_deleteKey_nv(opt.nvindex);
        chkrc(rc, exit(1));

        rc = tpm2totp_storeKey_nv(newBlob, newBlob_size, opt.nvindex);
        free(newBlob);
        chkrc(rc, exit(1));
        break;
    case CMD_RECOVER:
        rc = tpm2totp_loadKey_nv(opt.nvindex, &keyBlob, &keyBlob_size);
        chkrc(rc, exit(1));

        rc = tpm2totp_getSecret(keyBlob, keyBlob_size, opt.password,
                                &secret, &secret_size);
        free(keyBlob);
        chkrc(rc, exit(1));

        base32key = base32enc(secret, secret_size);
        url = calloc(1, strlen(base32key) + strlen(URL_PREFIX) + 1);
        sprintf(url, URL_PREFIX "%s", base32key);
        free(base32key);

        qrpic = qrencode(url);

        printf("%s\n", qrpic);
        printf("%s\n", url);
        free(qrpic);
        free(url);
        break;
    case CMD_CLEAN:
        //TODO: Are your sure ?
        rc = tpm2totp_deleteKey_nv(opt.nvindex);
        chkrc(rc, exit(1));
        break;
    default:
        exit(1);
    }

    return 0;
}