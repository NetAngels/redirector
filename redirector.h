#include <event.h>
#include <evhttp.h>
#include <sys/types.h>
#include <pwd.h>

typedef struct __rd_options {
    char *filename;
    char *ip;
    char *username;
    uid_t uid;
    gid_t gid;
    unsigned short port;
    unsigned int verbose;
} rd_options;

extern rd_options options;

int get_options(int ac, char **av, rd_options *opts, char **err);
void print_help(const char *err);
void on_request(struct evhttp_request *http, void *arg);
