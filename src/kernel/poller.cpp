//
// Created by 陈家阔 on 2025/8/18.
//
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#ifdef __linux__
#include <sys/epoll.h>
#include <sys/timerfd.h>
#else
#include <sys/event.h>
# undef LIST_HEAD
# undef SLIST_HEAD
#endif
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <openssl/ssl.h>
#include "list.h"
#include "rbtree.h"
#include "poller.h"

#define POLLER_BUFSIZE (256 * 1024)
#define POLLER_EVENTS_MAX 256

struct __poller_node{
    int state;
    int error;
    struct poller_data data;
#pragma pack(1)
    union{
        struct list_head list;
        struct rb_node rb;
    };
#pragma pack()
    char in_rbtree;
    char rbmoved;
    int event;
    struct timespec timeout;
    struct __poller_node *res;
};

struct __poller{
    size_t max_open_files;
    void (*callback)(struct poller_result *, void *);
    void *context;

    pthread_t tid;
    int pfd;
    int timerfd;
    int pipe_rd;
    int pipe_wr;
    int stopped;
    struct rb_root timeo_tree;
    struct rb_node *tree_first;
    struct rb_node *tree_last;
    struct list_head timeo_list;
    struct list_head no_timeo_list;
    struct __poller_node **nodes;
    char buf[POLLER_BUFSIZE];
};

#ifdef __linux__

static inline int __poller_create_pfd()
{
    return epoll_create(1);
}

static inline int __poller_close_pfd(int fd)
{
    return close(fd);
}

//测试是否为红黑树的根节点