#include "redirector.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <db.h>

/* global options variable */
rd_options options;

#define CHECK(op, msg) {status=op; if (status){err=msg; goto fail;}}
#define LOG(level, ...) {\
    if (level<=options.verbose) {\
        time_t _t = time(NULL);\
        struct tm *_tm = localtime(&_t);\
        char _ts[128];\
        strftime(_ts, 127, "%Y-%m-%d %H:%M:%S", _tm); \
        printf("%s\t", _ts);\
        printf(__VA_ARGS__);\
    }\
};

int main(int ac, char **av)
{
    int status;
    struct event_base *base = event_init();
    struct evhttp *http = evhttp_new(base);
    char *err = NULL;
    DB *db = NULL;

    status = get_options(ac, av, &options, &err);
    if (status) {
        print_help(err);
        return 1;
    };
    CHECK(evhttp_bind_socket(http, options.ip, options.port), "bind socket");

    if (options.username) {
        CHECK(setgid(options.gid), "setgid");
        CHECK(setuid(options.uid), "setuid");
    }
    CHECK(db_create(&db, NULL, 0), "db create");
    CHECK(db->open(db, NULL, options.filename, NULL, DB_UNKNOWN, DB_RDONLY, 0),
            "open database");
    evhttp_set_gencb(http, on_request, db);
    event_base_dispatch(base);
    return 0;
fail:
    perror(err);
    if (db) db->close(db, 0);
    return 1;
}

/*
 * Get command line options.
 * On failure return != 0 and write error message in 'err'
 */
int get_options(int ac, char **av, rd_options *opts, char **err)
{

    static char options[] = "f:i:p:u:vh";
    static struct option long_options[] = {
       {"file", 1, 0, 'f'},
       {"ip", 1, 0, 'i'},
       {"port", 1, 0, 'p'},
       {"user", 1, 0, 'u'},
       {"verbose", 0, 0, 'v'},
       {"help", 0, 0, 'h'},
    };
    int c;
    int has_error = 0;
    struct passwd *pwd;
    /* set up default values */
    opts->ip = "0.0.0.0";
    opts->port = 80;
    /* read values from args */
    while (1) {
        int option_index = 0;
        c = getopt_long(ac, av, options, long_options, &option_index);
        if (c == -1 || has_error)
            break;
        switch (c) {
        case 'f':
            opts->filename = strdup(optarg);
            break;
        case 'i':
            opts->ip = strdup(optarg);
            break;
        case 'p':
            opts->port = atoi(optarg);
            if (opts->port <= 0 || opts->port > 65535) {
                *err = "incorrect port number";
                has_error ++;
            }
            break;
        case 'u':
            opts->username = strdup(optarg);
            pwd = getpwnam(opts->username);
            if (!pwd) {
                *err = "user with given name not found";
                has_error ++;
            } else {
                opts->uid = pwd->pw_uid;
                opts->gid = pwd->pw_gid;
            }
            break;
        case 'v':
            opts->verbose ++;
            break;
        case 'h':
            has_error ++; /* just show help text */ 
            break;
        default:
            has_error ++;
            break;
        }
    }
    if (has_error)
        return 1;
    if (!opts->filename) {
        *err = "required option --file (-f) is not set";
        return 1;
    }
    return 0;
}


void print_help(const char *err) {
    if (err)
        fprintf(stderr, "Error: %s\n\n", err);
    fprintf(stderr, ""
        "USAGE: redirector [options]\n"
        "\n"
        "Options: \n"
        "-f, --file (required). Set the BDB filename\n"
        "-i, --ip (optional, default \"0.0.0.0\"). Set the IP to bind to\n"
        "-p, --port (optional, default 80). Set the port number\n"
        "-u, --user (optional). Set the effective UID\n"
        "-v, --verbose (optional). Make the redirector log every request to stdout\n"
        "-h, --help. This help.\n"
    );
}

void on_request(struct evhttp_request *req, void *arg)
{
    int status;
    DB *db = (DB*)arg;
    DBT key, data;
    const char *hostname; 
    char data_buf[2048];
    size_t data_buf_size = 2047;

    /* response data */
    int http_status_code = 0;
    char *http_explanation = NULL;
    char *location = NULL;
    char *text = NULL;

    /* initialization */
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    memset(&data_buf, 0, sizeof(data_buf));

    /* get the hostname */
    hostname = evhttp_find_header(req->input_headers, "Host");
    if (!hostname) return;

    /* make a db request */
    key.data = (char *)hostname;
    key.size = strlen(hostname); /* without trailing \0 */
    status = db->get(db, NULL, &key, &data, 0);

    if (status == 0) {
        size_t sz = data_buf_size < data.size ? data_buf_size : data.size;
        memcpy(data_buf, data.data, sz);
        data_buf[3] = '\0';
        http_status_code = atoi(data_buf);
        http_explanation = "Redirect";
        location = &(data_buf[4]);
    } else if (status == DB_NOTFOUND) {
        http_status_code = 404; 
        http_explanation = "Not found";
        text = "404. Redirect not found";
    } else {
        http_status_code = 500;
        http_explanation = "Internal server error";
        text = "500. Internal server error";
    }
    LOG(1, "%s\t%d\t%s\n", hostname, http_status_code, location ? location : "-");
    /* create response buffer */
    struct evbuffer *evb = evbuffer_new();
    if (!evb) return;

    evhttp_add_header(req->output_headers, "Server", "Redirector/0.1");
    evhttp_add_header(req->output_headers, "Connection", "close");
    if (location) {
        evhttp_add_header(req->output_headers, "Location", location);
    }
    if (text) {
        evbuffer_add_printf(evb, "%s", text);
    }
    /* send reply */
    evhttp_send_reply(req, http_status_code, http_explanation, evb);
    evbuffer_free(evb);
}
