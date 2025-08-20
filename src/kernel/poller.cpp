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

static inline int __poller_add_fd(int fd, int event, void *data, poller_t *poller)
{
    struct epoll_event ev = {
            .events = event,
            .data   = {
                    .ptr = data
            }
    };

    return epoll_ctl(poller->pfd, EPOLL_CTL_ADD, fd, &ev);
}

static inline int __poller_del_fd(int fd, int event, poller_t *poller)
{
    return epoll_ctl(poller->pfd, EPOLL_CTL_DEL, fd, NULL);
}

static inline int __poller_mod_fd(int fd, int old_event, int new_event, void *data, poller_t *poller)
{
    struct epoll_event ev = {
        .events  = new_event,
        .data    = {
            .ptr = data
        }
    };
    return epoll_ctl(poller->pfd, EPOLL_CTL_MOD, fd, &ev);
}

static inline int __poller_create_timerfd()
{
    return timerfd_create(CLOCK_MONOTONIC, 0);
}

static inline int __poller_close_timerfd(int fd)
{
    return close(fd);
}

static inline int __poller_add_timerfd(int fd, poller_t *poller)
{
    struct epoll_event ev = {
        .events = EPOLLIN | EPOLLET,
        .data   = {
           .ptr = NULL
         }
    };
    return epoll_ctl(poller->pfd, EPOLL_CTL_ADD, fd, &ev);
}

static inline int __poller_set_timerfd(int fd, const struct timespace *abstime, poller_t *poller)
{
    struct itimerspec timer ={
            .it_interval = {},
            .it_value    = *abstime
    };
    return timerfd_settime(fd, TFD_TIMER_ABSTIME, &timer, NULL);
}

typedef struct epoll_event __poller_event_t;

static inline int __poller_wait(__poller_event_t *events, int maxevents, poller_t *poller)
{
    return epoll_wait(poller->pfd, events, maxevents, -1);
}

static inline void *__poller_event_data(const __poller_event_t *event)
{
    return event->data.ptr;
}

#else /*BSD,macOS*/

static inline int __poller_create_pfd()
{
    return kqueue();
}

static inline int __poller_close_pfd(int fd)
{
    return close(fd);
}

static inline int __poller_add_fd(int fd, int event, void *data, poller_t *poller)
{
    struct kevent ev;
    EV_SET(&ev, fd, event, EV_ADD, 0, 0, data);
    return kevent(poller->fd, &ev, 1, NULL, 0 , NULL);
}

static inline int __poller_del_fd(int fd, int event, poller_t *poller)
{
    struct kevent ev;
    EV_SET(&ev, fd, event, EV_DELETE, 0, 0, NULL);
    return kevent(poller->fd, &ev, 1, NULL, 0, NULL);
}

static inline int __poller_mod_fd(int fd, int old_event, int new_event, void *data, poller_t *poller)
{
    struct kevent ev[2];
    EV_SET(&ev[0], fd, old_event, EV_DELETE, 0, 0, NULL);
    EV_SET(&ev[0], fd, new_event, EV_ADD, 0, 0, data);
    return kevent(poller->fd, ev, 2, NULL, 0, NULL);
}

static inline int __poller_create_timerfd()
{
    return 0;
}

static inline int __poller_close_timerfd(int fd)
{
    return 0;
}

static inline int __poller_add_timerfd(int fd, poller_t *poller)
{
    return 0;
}

static int __poller_set_timerfd(int fd, const struct timespec *abstime, poller_t *poller)
{
    struct timespec cutime;
    long long nseconds;
    struct kevent ev;
    int flags;

    if(abstime->tv_sec || abstime->tv_nsec)
    {
        clock_gettime(CLOCK_MONOTONIC, &curtime);
        nseconds = 1000000000LL * (abstime->tv_sec - curtime.tv_sec);
        nseconds += abstime->tv_nsec - curtime.tv_nsec;
        flags = EV_ADD;
    }
    else
    {
        nseconds = 0;
        flags = EV_SET;
    }

    EV_SET(&ev, fd, EVFILT_TIMER, flags, NOTE_NSECONDS, nseconds, NULL);
    return kevent(poller->pfd, &ev, 1, NULL, 0, NULL);
}

typedef struct kevent __poller_event_t;

#endif