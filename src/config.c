#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

void config_defaults(app_config *c) {
    memset(c, 0, sizeof *c);
    strcpy(c->host, "futureland.today");
    c->port = 10088;
    strcpy(c->system, "PS3");
    strcpy(c->channel, "main");
    strcpy(c->avatar_file, "avatar.bin");
    c->history = 40;
    c->overscan_pct = 5;
    c->images = 1;
    c->cards = 1;
    c->ssl_verify = 1;
    c->osk = 1;
}

static char *trim(char *s) {
    char *e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int on_off(const char *v) {
    return !strcmp(v, "1") || !strcasecmp(v, "on") || !strcasecmp(v, "yes") || !strcasecmp(v, "true");
}

int config_load(app_config *c, const char *path) {
    char line[512];
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *k, *v, *eq;
        k = trim(line);
        if (!*k || *k == ';' || *k == '#' || *k == '[') continue;
        eq = strchr(k, '=');
        if (!eq) continue;
        *eq = 0;
        k = trim(k);
        v = trim(eq + 1);
        if (!strcasecmp(k, "host")) snprintf(c->host, sizeof c->host, "%s", v);
        else if (!strcasecmp(k, "port")) c->port = atoi(v);
        else if (!strcasecmp(k, "nick") || !strcasecmp(k, "user")) snprintf(c->nick, sizeof c->nick, "%s", v);
        else if (!strcasecmp(k, "system") || !strcasecmp(k, "bbs_name")) snprintf(c->system, sizeof c->system, "%s", v);
        else if (!strcasecmp(k, "channel") || !strcasecmp(k, "room")) snprintf(c->channel, sizeof c->channel, "%s", v);
        else if (!strcasecmp(k, "avatar")) snprintf(c->avatar_file, sizeof c->avatar_file, "%s", v);
        else if (!strcasecmp(k, "avatar_index")) c->avatar_index = atoi(v);
        else if (!strcasecmp(k, "history")) c->history = atoi(v);
        else if (!strcasecmp(k, "compact")) c->compact = on_off(v);
        else if (!strcasecmp(k, "images")) c->images = on_off(v);
        else if (!strcasecmp(k, "link_previews") || !strcasecmp(k, "cards")) c->cards = on_off(v);
        else if (!strcasecmp(k, "nsfw_keywords")) snprintf(c->nsfw_keywords, sizeof c->nsfw_keywords, "%s", v);
        else if (!strcasecmp(k, "osk") || !strcasecmp(k, "keyboard")) c->osk = !strcasecmp(v, "builtin") || !strcasecmp(v, "pad") ? 2 : 1;
        else if (!strcasecmp(k, "ssl_verify")) c->ssl_verify = on_off(v);
        else if (!strcasecmp(k, "overscan")) c->overscan_pct = atoi(v);
        else if (!strcasecmp(k, "scale")) c->scale = atoi(v);
    }
    fclose(f);
    if (c->port <= 0 || c->port > 65535) c->port = 10088;
    if (c->history < 0) c->history = 0;
    if (c->history > 100) c->history = 100;
    if (c->overscan_pct < 0) c->overscan_pct = 0;
    if (c->overscan_pct > 15) c->overscan_pct = 15;
    return 1;
}
