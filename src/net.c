#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <json-c/json.h>
#include <curl/curl.h>
#include "net.h"

#ifdef __linux__
#include <unistd.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#endif  // __linux__

int
net_check_multi_info(struct net_global *n_global) {
    CURL *easy_curl = NULL;
    CURLMsg *msg = NULL;
    CURLcode result;
    struct net_conn *n_conn;
    int msg_left = 0;

    while ((msg = curl_multi_info_read(n_global->multi_curl, &msg_left))) {
        easy_curl = msg->easy_handle;
        result = msg->data.result;
        result = result;

        if (CURLMSG_DONE == msg->msg) {
            curl_easy_getinfo(easy_curl, CURLINFO_PRIVATE, &n_conn);
            if (n_conn) {
                if (n_conn->on_done_callback) {
                    n_conn->on_done_callback(easy_curl, msg->data.result, n_conn->on_done_data);
                }
                if (n_conn->on_done_data_free) {
                    n_conn->on_done_data_free(n_conn->on_done_data);
                }
                net_conn_destroy(n_conn);
            }
        }
    }
    return 0;
}

int
net_addsocket(struct net_global *n_global, curl_socket_t socket, int what, CURL *easy_curl) {
    struct net_socket *n_socket = net_socket_create();

    n_socket->n_global = n_global;
    net_setsocket(n_global, n_socket, socket, what, easy_curl);
    curl_multi_assign(n_global->multi_curl, socket, n_socket);
    return 0;
}

int
net_setsocket(struct net_global *n_global, struct net_socket *n_socket, curl_socket_t socket, int what, CURL *easy_curl) {
    struct epoll_event ev;
    int status = 0;
    int action = ((what & CURL_POLL_IN) ? EPOLLIN : 0) |
                 ((what & CURL_POLL_OUT) ? EPOLLOUT : 0);
    memset(&ev, 0, sizeof(ev));

    if (NULL != n_socket && n_socket->socketfd) {
        status = epoll_ctl(n_global->epollfd, EPOLL_CTL_DEL, n_socket->socketfd, NULL);
        if (0 != status) {
            fprintf(stderr, "error: EPOLL_CTL_DEL failed for fd(%d) - %s\n", n_socket->socketfd, strerror(errno));
        }
    }

    n_socket->socketfd = socket;
    n_socket->action = what;
    n_socket->easy_curl = easy_curl;

    ev.events = action;
    ev.data.fd = socket;
    status = epoll_ctl(n_global->epollfd, EPOLL_CTL_ADD, socket, &ev);
    assert(0 == status);
    return status;
}

int
net_removesocket(struct net_global *n_global, struct net_socket *n_socket) {
    int status = 0;
    if (NULL != n_socket) {
        if (n_socket->socketfd) {
            status = epoll_ctl(n_global->epollfd, EPOLL_CTL_DEL, n_socket->socketfd, NULL);
            if (status != 0) {
                fprintf(stderr, "error: (%d) %s\n", errno, strerror(errno));
            }
        }
        free(n_socket);
    }
    return 0;
}

int
curl_socket_cb(CURL *easy_curl, curl_socket_t socket, int what, void *userp, void *socketp) {
    struct net_global *n_global = (struct net_global *)userp;
    struct net_socket *n_socket = (struct net_socket *)socketp;

    if (what == CURL_POLL_REMOVE) {
        /** Remove socket */
        // printf("epollfd(%d) socket remove (%d)\n", n_global->epollfd, n_socket->socketfd);
        net_removesocket(n_global, n_socket);
    } else {
        if (NULL == n_socket) {
            /** Add socket */
            // printf("epollfd(%d) socket add (%d)\n", n_global->epollfd, socket);
            net_addsocket(n_global, socket, what, easy_curl);
        } else {
            /** Change socket info */
            // printf("epollfd(%d) socket update (%d)->(%d)\n", n_global->epollfd, n_socket->socketfd, socket);
            net_setsocket(n_global, n_socket, socket, what, easy_curl);
        }
    }
    return 0;
}

int
curl_timer_cb(CURLM *multi_curl, long timeout_ms, struct net_global *n_global) {
    int status = 0;
    struct itimerspec timer;

    // printf("epollfd(%d) setting timer: (%ldms)\n", n_global->epollfd, timeout_ms);
    if (timeout_ms > 0) {
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_nsec = 0;
        timer.it_value.tv_sec = timeout_ms / 1000;
        timer.it_value.tv_nsec = (timeout_ms % 1000) * 1000 * 1000;
    } else if (timeout_ms == 0) {
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_nsec = 0;
        timer.it_value.tv_sec = 0;
        timer.it_value.tv_nsec = 1;
    } else {
        memset(&timer, 0, sizeof(timer));
    }
    status = timerfd_settime(n_global->timerfd, 0, &timer, NULL);
    if (0 != status) {
        fprintf(stderr, "error: (%d) %s\n", errno, strerror(errno));
    }
    assert(0 == status);

    return 0;
}

int
net_global_init(struct net_global *g) {
    if (NULL == g)
        return -1;

    memset(g, 0, sizeof(struct net_global));
    return 0;
}


struct net_conn*
net_conn_create(void) {
    struct net_conn *n_conn = malloc(sizeof(struct net_conn));

    if (NULL == n_conn)
        return NULL;

    memset(n_conn, 0, sizeof(struct net_conn));
    n_conn->easy_curl = NULL;
    n_conn->url = NULL;
    n_conn->n_global = NULL;
    n_conn->on_done_data = NULL;
    n_conn->on_done_data_free = NULL;
    n_conn->on_done_callback = NULL;
    n_conn->error[0] = '\0';
    return n_conn;
}

struct net_socket*
net_socket_create(void) {
    struct net_socket *n_socket = malloc(sizeof(struct net_socket));

    if (NULL == n_socket)
        return NULL;

    memset(n_socket, 0, sizeof(struct net_socket));
    return n_socket;
}

int
net_conn_destroy(struct net_conn *n_conn) {
    if (NULL == n_conn) {
        return 0;
    }

    if (NULL != n_conn->easy_curl) {
        curl_easy_cleanup(n_conn->easy_curl);
    }
    if (NULL != n_conn->url) {
        curl_url_cleanup(n_conn->url);
    }
    free(n_conn);
    return 0;
}
