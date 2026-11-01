#ifndef AC_CONFIG_H
#define AC_CONFIG_H
#include <stddef.h>

typedef struct {
    char host[128];
    int port;
    char nick[32];
    char system[48];
    char channel[40];
    char avatar_file[256];
    int avatar_index;
    int history;
    int compact;
    int images;      /* fetch and show picture links */
    int osk;         /* 1 system on-screen keyboard first, 2 built-in pad keyboard only */
    int ssl_verify;  /* check HTTPS certificates (needs cacert.pem) */
    int overscan_pct;
    int scale;
} app_config;

void config_defaults(app_config *c);
/* Reads key = value lines; unknown keys are ignored. 1 if the file was read. */
int config_load(app_config *c, const char *path);

#endif
