#ifndef CKLOS_HTTP_H
#define CKLOS_HTTP_H

#include "types.h"

typedef struct {
    int status;
    uint32_t bytes;
    uint32_t header_bytes;
    uint32_t redirects;
    char content_type[96];
    char location[512];
    char error[96];
} http_result;

typedef struct {
    const char *url;
    const char *output_path;
    const char *method;
    const char *data;
    const char *headers;
    const char *user_agent;
    const char *referer;
    const char *authorization;
    const char *cookie;
    bool verbose;
    bool follow_redirects;
    bool include_headers;
    bool fail_on_http_error;
    bool resume;
    uint32_t resume_from;
    uint32_t connect_timeout_ms;
    uint32_t transfer_timeout_ms;
    uint32_t max_redirects;
} http_options;

bool http_perform(const http_options *options, http_result *result);

#endif
