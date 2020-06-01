#ifndef NET_H_
#define NET_H_

#include <curl/curl.h>
#include "parser.h"

struct net_global {
    int epollfd;
    int timerfd;
    int running;
    CURLM *multi_curl;
    CURLSH *share_curl;
};

/** Created before net_socket, holds parsing method and data received */
struct net_conn {
    enum conn_type {
        NET_HTTP,
    } type;
    CURL *easy_curl;
    CURLU *url;
    struct net_global *n_global;
    void *on_done_data;
    void (*on_done_data_free)(void *on_done_data);
    void (*on_done_callback)(CURL *easy_curl, CURLcode code, void *on_done_data);
    char error[CURL_ERROR_SIZE+1];
};

/** Set on CURLMOPT_SOCKETFUNCTION, adds new fd to epoll, and is associated with multi by fd */
struct net_socket {
    curl_socket_t socketfd;
    CURL *easy_curl;
    struct net_global *n_global;
    long timeout;
    int action;
};

int curl_socket_cb(CURL *easy_curl, curl_socket_t s, int what, void *userp, void *socketp);
int curl_timer_cb(CURLM *multi_curl, long timeout_ms, struct net_global *n_global);

int net_addsocket(struct net_global *g, curl_socket_t to_add, int flags, CURL *curl_easy);
int net_setsocket(struct net_global *g, struct net_socket *s, curl_socket_t to_add, int flags, CURL *curl_easy);
int net_removesocket(struct net_global *n_global, struct net_socket *n_socket);
int net_check_multi_info(struct net_global *n_global);

struct net_conn*    net_conn_create(void);
int                 net_conn_destroy(struct net_conn *n_conn);
struct net_socket*  net_socket_create(void);
int                 net_global_init(struct net_global *g);

#endif  // NET_H_
