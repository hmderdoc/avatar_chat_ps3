#ifndef SHIM_CURL_H
#define SHIM_CURL_H
#include <stddef.h>
typedef void CURL; typedef void CURLM;
typedef enum { CURLE_OK = 0 } CURLcode;
typedef enum { CURLM_OK = 0 } CURLMcode;
typedef enum { CURLMSG_DONE = 1 } CURLMSG;
typedef struct { CURLMSG msg; CURL *easy_handle; union { void *whatever; CURLcode result; } data; } CURLMsg;
enum { CURLOPT_URL = 1, CURLOPT_WRITEFUNCTION, CURLOPT_FOLLOWLOCATION, CURLOPT_MAXREDIRS, CURLOPT_PROTOCOLS, CURLOPT_REDIR_PROTOCOLS,
       CURLOPT_USERAGENT, CURLOPT_CONNECTTIMEOUT, CURLOPT_TIMEOUT, CURLOPT_MAXFILESIZE, CURLOPT_NOSIGNAL, CURLOPT_CAINFO, CURLOPT_SSL_VERIFYPEER, CURLOPT_SSL_VERIFYHOST };
enum { CURLINFO_RESPONSE_CODE = 1 };
#define CURL_GLOBAL_ALL 3
#define CURLPROTO_HTTP 1
#define CURLPROTO_HTTPS 2
int curl_global_init(long);
CURL *curl_easy_init(void);
void curl_easy_cleanup(CURL *);
CURLcode curl_easy_setopt(CURL *, int, ...);
CURLcode curl_easy_getinfo(CURL *, int, ...);
const char *curl_easy_strerror(CURLcode);
CURLM *curl_multi_init(void);
CURLMcode curl_multi_add_handle(CURLM *, CURL *);
CURLMcode curl_multi_remove_handle(CURLM *, CURL *);
CURLMcode curl_multi_perform(CURLM *, int *);
CURLMsg *curl_multi_info_read(CURLM *, int *);
#endif
