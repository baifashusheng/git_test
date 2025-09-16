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

static inline void list_splice_init(struct list_head *list, struct list_head *head)
{
    if(!list_empty(list))
    {
        __list_splice(list, head, head->next);
        INIT_LIST_HEAD(list);
    }
}

#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#define list_for_each(pos, head) \
    for(pos = (head)->next; pos != (head); pos = pos->next)


#define list_for_each_prev(pos, head) \
    for(pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_for_each_safe(pos, n, head) \
    for(pos = (head)->next, n = pos->next; pos != (head); \
        pos = n, n = pos->next)

#define list_for_each_entry(pos, head, member) \
    for(pos = list_entry((head)->next, typeof (*pos), member); \
        &pos->member != (head); \
        pos = list_entry(pos->member.next, typeof (*pos), member))


struct slist_node{
    struct slist_node *next;
};

struct slist_head{
    struct slist_node first, *last;
};

#define SLIST_HEAD_INIT(name) {{(struct slist_node *)0}, &(name).first}

#define SLIST_HEAD(name) \
    struct slist_head name = SLIST_HEAD_INIT(name)


static inline void INIT_SLIST_HEAD(struct slist_head *list)
{
    list->first.next = (struct slist_node *)0;
    list->last = &list->first;
}

static inline void slist_add_after(struct slist_node *entry,
                    struct slist_node *prev,
                    struct slist_node *list)
{
    entry->next = prev->next;
    prev->next = entry;
    if(!entry->next)
        list->last = entry;
}

static inline void slist_add_head(struct slist_node *entry,
                    struct slist_head *list)
{
    slist_add_after(entry, &list->first, list);
}


static inline void slist_add_tail(struct slist_node *entry,
                    struct slist_head *list)
{
    entry->next = (struct slist_node *)0;
    list->last->next = entry;
    list->last = entry;
}

static inline void slist_del_after(struct slist_node *prev, struct slist_head *list)
{
    prev->next = prev->next->next;
    if(!prev->next)
        list->last = prev;
}

static inline void slist_del_head(struct slist_head *list)
{
    slist_del_after(&list->first, list);
}

static inline int slist_empty(const struct slist_head *list)
{
    return !list->first.next;
}

static inline void __slist_splice(const struct slist_head *list,
                    struct slist_node *prev,
                    struct slist_head *head)
{
    list->last->next = prev->next;
    prev->next = list->first.next;
    if(!list->last->next)
    {
        head->last = list->last;
    }
}

static inline void slist_splice(const struct slist_head *list,
                    struct slist_node *prev,
                    struct slist_node *head)
{
    if(!slist_empty(list))
    {
        __slist_splice(list,prev,head);
        INIT_SLIST_HEAD(list);
    }
}

#define slist_entry(ptr, type, member) \
    ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#define slist_for_each(pos, head) \
    for(pos = (head)->first.next; pos; pos = pos->next)

#define slist_for_each_safe(pos, prev, head)
    for(prev = &(head)->first, pos = prev->next; pos; \
        prev = prev->next == pos ? pos : prev, pos = prev->next)

#define slist_for_each_entry(pos, head, member) \
    for(pos = slist_entry((head)->first.next, type (*pos), member); \
        &pos->member != (struct slist_node *)0; \
        pos = slist_entry(pos->member.next, typeof (*pos), member))

#endif //_LINUX_LIST_H
