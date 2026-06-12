#ifndef _CURL_CURL_H
#define _CURL_CURL_H

#include <stddef.h>
#include <stdint.h>

typedef void CURL;
typedef void CURLM;
typedef void CURLMsg;
typedef void CURLSH;

typedef enum {
    CURLE_OK = 0,
    CURLE_UNSUPPORTED_PROTOCOL = 1,
    CURLE_FAILED_INIT = 2,
    CURLE_URL_MALFORMAT = 3,
    CURLE_COULDNT_RESOLVE_HOST = 6,
    CURLE_COULDNT_CONNECT = 7,
    CURLE_OPERATION_TIMEDOUT = 28,
    CURLE_WRITE_ERROR = 23,
    CURLE_READ_ERROR = 26,
    CURLE_OUT_OF_MEMORY = 27,
} CURLcode;

typedef enum {
    CURLINFO_EFFECTIVE_URL = 0,
    CURLINFO_RESPONSE_CODE = 1,
    CURLINFO_CONTENT_TYPE = 2,
    CURLINFO_TOTAL_TIME = 3,
    CURLINFO_CONTENT_LENGTH_DOWNLOAD = 4,
    CURLINFO_PRIMARY_IP = 5,
} CURLINFO;

typedef enum {
    CURLOPT_URL = 10000,
    CURLOPT_WRITEFUNCTION = 20000,
    CURLOPT_WRITEDATA = 10001,
    CURLOPT_HEADERFUNCTION = 20002,
    CURLOPT_HEADERDATA = 10003,
    CURLOPT_READFUNCTION = 20012,
    CURLOPT_READDATA = 10009,
    CURLOPT_POSTFIELDS = 10015,
    CURLOPT_POSTFIELDSIZE = 10016,
    CURLOPT_HTTPHEADER = 10023,
    CURLOPT_SSL_VERIFYPEER = 64,
    CURLOPT_SSL_VERIFYHOST = 81,
    CURLOPT_CAINFO = 10065,
    CURLOPT_FOLLOWLOCATION = 52,
    CURLOPT_MAXREDIRS = 68,
    CURLOPT_TIMEOUT = 13,
    CURLOPT_CONNECTTIMEOUT = 78,
    CURLOPT_USERAGENT = 10018,
    CURLOPT_VERBOSE = 41,
    CURLOPT_NOSIGNAL = 99,
    CURLOPT_PROXY = 10004,
    CURLOPT_COOKIE = 10022,
    CURLOPT_COOKIEFILE = 10031,
    CURLOPT_COOKIEJAR = 10082,
} CURLoption;

typedef enum {
    CURLM_OK = 0,
    CURLM_BAD_HANDLE = 1,
    CURLM_OUT_OF_MEMORY = 2,
    CURLM_INTERNAL_ERROR = 3,
    CURLM_CALL_MULTI_PERFORM = -1,
} CURLMcode;

typedef struct curl_slist {
    char *data;
    struct curl_slist *next;
} curl_slist;

typedef size_t (*curl_write_callback)(char *ptr, size_t size, size_t nmemb, void *userdata);

#define CURL_ERROR_SIZE 256

typedef enum {
    CURLFORM_OK = 0,
    CURLFORM_MEMORY = 1,
} CURLFORMcode;

#define CURLFORM_BUFFERPTR 5
#define CURLFORM_COPYNAME 1
#define CURLFORM_BUFFER 2
#define CURLFORM_FILENAME 3
#define CURLFORM_CONTENTTYPE 4
#define CURLFORM_PTRNAME 5
#define CURLFORM_PTRCONTENTS 6
#define CURLFORM_COPYCONTENTS 7
#define CURLFORM_CONTENTSLENGTH 8
#define CURLFORM_CONTENTHEADER 9
#define CURLFORM_END 10

CURL *curl_easy_init(void);
void curl_easy_cleanup(CURL *curl);
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...);
CURLcode curl_easy_perform(CURL *curl);
const char *curl_easy_strerror(CURLcode err);
CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...);
CURLM *curl_multi_init(void);
CURLMcode curl_multi_add_handle(CURLM *multi, CURL *curl);
CURLMcode curl_multi_remove_handle(CURLM *multi, CURL *curl);
CURLMcode curl_multi_perform(CURLM *multi, int *running);
CURLMcode curl_multi_fdset(CURLM *multi, fd_set *r, fd_set *w, fd_set *e, int *max);
CURLMcode curl_multi_timeout(CURLM *multi, long *timeout);
CURLMsg *curl_multi_info_read(CURLM *multi, int *msgs);
CURLMcode curl_multi_cleanup(CURLM *multi);
curl_slist *curl_slist_append(curl_slist *list, const char *str);
void curl_slist_free_all(curl_slist *list);
const char *curl_version(void);
void curl_global_init(long flags);

#endif
