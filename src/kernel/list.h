//
// Created by 陈家阔 on 2025/9/15.
//

#ifndef _LINUX_LIST_H
#define _LINUX_LIST_H

struct list_head{
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static inline void __list_add(struct list_head *entry,
                           struct list_head *prev,
                           struct list_head *next)
{
    next->prev = entry;
    entry->next = next;
    entry->prev = prev;
    prev->next = entry;
}

static inline void list_add(struct list_head *entry, struct list_head *head)
{
    __list_add(entry,head,head->next);
}

static inline void list_add_tail(struct list_head *entry, struct list_head *head)
{
    __list_add(entry, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev =prev;
    prev->next = next;
}

static inline void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
}

struct inline void list_move(struct list_head *entry, struct list_head *head)
{
    __list_del(entry->prev, entry->next);
    list_add(entry, head);
}

static inline void list_move_tail(struct list_head *entry, struct list_head *head)
{
    __list_del(entry->prev, entry->next);
    list_add_tail(entry, head);
}

static inline int list_empty(const struct list_head *head)
{
    return head->next == head;
}

static inline void __list_splice(const struct list_head *list,
                struct list_head *prev,
                struct list_head *next)
{
    struct list_head *first = list->next;
    struct list_head *last = list->prev;
    first->prev = prev;
    prev->next = first;

    last->next = next;
    next->prev = last;
}

static inline void list_splice(const struct list_head *list, struct list_head *head)
{
    if(!list_empty(list))
    {
        __list_splice(list, head, head->next);
    }
}

#endif //_LINUX_LIST_H
