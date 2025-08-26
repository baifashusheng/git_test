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

static inline int __poller_wait(__poller_event_t *events, int maxevents, poller_t *poller)
{
    return kevent(poller->pfd, NULL, 0, events, maxevents, NULL);
}

static inline void *__poller_event_data(const __poller_event_t *event)
{
    return event->udata;
}

#define EPOLLIN  EVFILT_READ
#define EPOLLOUT EVFILT_WRITE
#define EPOLLET  0

#endif

static inline long __timeout_cmp(const struct __Poller_node *node1, const struct __poller_node *node2)
{
    long ret = node1->timeout.tv_sec - node2->timeout.tv_sec;
    if(ret == 0)
    {
        ret = node1->timeout.tv_sec - node2->timeout.tv_sec;
    }
    return ret;
}

static void __poller_tree_insert(struct __poller_node *node, poller_t *poller)
{
    struct rb_node **p = &poller->timeo_tree.rb_node;
    struct rb_node *parent = NULL;
    struct __poller_node *entry;

    entry = rb_entry(poller->tree_last, struct __poller_node, rb);
    if(!*p)
    {
        poller->tree_first = &node->rb;
        poller->tree_last  = &node->rb;
    }
    else if(__timeout_cmp(node, entry) >= 0)
    {
        parent = poller->tree_last;
        p = &parent->rb_right;
        poller->tree_last = &node->rb;
    }
    else
    {
        do {
            parent = *p;
            entry = rb_entry(*p, struct __poller_node, rb);
            if(__timeout_cmp(node,entry) < 0)
            {
                p = &(*p)->rb_left;
            }
            else
            {
                p = &(*p)->rb_right;
            }
        }while(*p);

        if(p == &poller->tree_first->rb_left)
        {
            poller->tree_first = & node->rb;
        }
    }

    node->in_rbtree = 1;
    rb_link_node(&node->rb, parent, p);
    rb_insert_color(&node->rb, &poller->timeo_tree);
}

static inline void __poller_tree_erase(struct __poller_node *node, poller_t *poller)
{
    if(&node->rb == poller->tree_first)
    {
        poller->tree_first = rb_next(&node->rb)
    }

    if(&node->rb == poller->tree_last)
    {
        poller->tree_last = rb_prev(&node->rb);
    }

    rb_erase(&node->rb, &poller->timeo_tree);
    node->in_rbtree = 0;
}

static int __poller_remove_node(struct __poller_node *node, poller_t *poller)
{
    int removed;

    pthread_mutex_lock(&poller->mutex);
    removed = node->removed;
    if(!removed)
    {
        poller->nodes[node->data.fd] = NULL;

        if(node->in_rbtree)
        {
            __poller_tree_erase(node, poller);
        }
        else
        {
            list_del(&node->list);
        }

        __poller_del_fd(node->data.fd, node->event, poller);
    }

    pthread_mutex_unlock(&poller->mutex);
    return removed;
}

static int __poller_append_message(const void *buf, size_t *n,struct __poller_node *node, poller_t *poller)
{
    poller_message_t *mgs = node->data.message;
    struct __poller_node *res;
    int ret;

    if(!msg)
    {
        res = (stuct __poller_node *)malloc(sizeof (struct __poller_node));
        if(!res)
            return -1;

        msg = node->data.create_messgae(node->data.context);
        if(!msg)
        {
            free(!msg);
            return -1;
        }

        node->data.message = msg;
        node->res = res;
    }
    else
    {
        res = node->res;
    }

    ret = msg->append(buf, n, msg);
    if(ret > 0)
    {
        res->data = node->data;
        res->error = 0;
        res->state = PR_ST_SUCCESS;
        poller->callback((struct poller_result *)res,poller->context);

        node->data.message = NULL;
        node->res = NULL;
    }

    return ret;
}

static int __poller_handle_ssl_error(struct __poller_node *node, int ret, poller_t *poller)
{
    int error = SSL_get_error(node->data.ssl, ret);
    int event;

    switch(error)
    {
        case SSL_ERROR_WANT_READ:
            event = EPOLLIN | EPOLLET;
            break;

        case SSL_ERROR_WANT_WRITE:
            event = EPOLLOUT | EPOLLET;
            break;

        default:
            error = -error;

        case SSL_ERROR_SYSCALL:
            return -1;
    }

    pthread_mutex_lock(&poller->mutex);
    if(!node->removed)
    {
        ret = __poller_mod_fd(node->data.fd, node->event, event, node, poller);
        if(ret >= 0)
            node->event = event;
    }
    else
    {
        ret = 0;
    }
    pthread_mutex_unlock(&poller->mutex);

    return ret;
}

static void __poller_handler_read(struct __poller_node *node, poller_t *poller)
{
    ssize_t nleft;
    size_t n;
    char *p;

    while(1)
    {
        p = poller->buf;
        if(!node->data.ssl)
        {
            nleft = read(node->data.fd, p, POLLER_BUFSIZE);
            if(nleft < 0)
            {
                if(errno == EAGAIN)
                {
                    return;
                }
            }
        }
        else
        {
            nleft = SSL_read(node->data.ssl, p, POLLER_BUFSIZE);
            if(nleft < 0)
            {
                if(__poller_handle_ssl_error(node,nleft,poller) >= 0)
                    return;
            }
        }

        if(nleft <= 0)
            break;

        do
        {
            n = nleft;
            if(__poller_append_message(p, &n, node, poller) >=0)
            {
                nleft -= n;
                p += n;
            }
            else
                nleft = -1;
        }while(nleft > 0);

        if(node->removed)
            return;
    }

    if(__poller_remove_node(node,poller))
        return;

    if(nleft == 0)
    {
        node->error = 0;
        node->state = PR_ST_FINISHED;
    }
    else
    {
        node->error = errno;
        node->state = PR_ST_ERROR;
    }

    free(node->res);
    poller->callback((struct poller_result *)node, poller->context);
}

#ifndef TDV_MAX
# ifdef UIO_MAXIOV
#  define IOV_MAX UIO_MAXIOV
# else
#  define IOV_MAX 1024
# endif
#endif

static void __poller_handler_write(struct __poller_node *node, poller_t *poller)
{
    struct iovec *iov = node->data.wirite.iov;
    size_t count = 0;
    size_t nleft;
    int iovcnt;
    int ret;

    while(node->data.iovcnt > 0)
    {
        if(!node->data.ssl)
        {
            iovcnt = node->data.iovcnt;
            if(iovcnt > IOV_MAX)
                iovcnt = IOV_MAX;

            nleft = writev(node->data.fd, iov, iovcnt);
            if(nleft < 0)
            {
                ret = errno == ENGAIN ? 0:-1;
                break;
            }
        }
        else if(iov->iov_len > 0)
        {
            nleft = SSL_write(node->data.ssl, iov->iov_base, iov->iov_len);
            if(nleft <= 0)
            {
                ret = __poller_handle_ssl_error(node, nleft, poller);
                break;
            }
        }
        else
            nleft = 0;

        count += nleft;

        do{
            if(nleft >= iov->iov_len)
            {
                nleft -= iov->iov_len;
                iov->iov_base = (char *)iov->iov_base + iov_len;
                iov->iov_len = 0;
                iov++;
                node->data.iovcnt--;
            }
            else{
                iov->iov_base = (char *)iov->iov_base + nleft;
                iov->iov_len -= nleft;
                break;
            }
        }while(node->data.iovcnt > 0);
    }

    node->data.write_iov = iov;
    if(node->data.iovcnt > 0 && ret >= 0)
    {
        if(count == 0)
            return;
        if(node->data.partial_written(count, node->data.context) >= 0)
            return;
    }

    if(__poller_remove_node(node, poller))
        return;

    if(node->data.iovcnt == 0)
    {
        node->error =0;
        node->state = PR_ST_FINISHED;
    }
    else
    {
        node->error = errno;
        node->state = PR_ST_ERROR;
    }

    poller->callback((struct poller_result *)node, poller->context);
}