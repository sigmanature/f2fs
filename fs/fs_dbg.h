#ifndef FS_DBG_H
#define FS_DBG_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/iomap.h>
#define FUNC(func, ...) \
    do { \
        printk(KERN_EMERG "In %s\n", __func__); \
        func(__VA_ARGS__); \
    } while (0)
#define DBG_LOCK(_where, _ifs)                                   \
do {                                                             \
        struct lockdep_map *m = &_ifs->state_lock.dep_map;       \
        pr_err("%s: ifs=%px lock=%px key=%px class_cache=%px\n", \
               _where, _ifs, m, m->key, m->class_cache);         \
} while (0)
#define PRINT_FOLIO_IFS(folio, ifs) \
    do { \
        if ((ifs)) { \
            pr_err("folio ifs write_bytes_pending: %x, read_bytes_pending: %x", \
                  atomic_read(&(ifs)->write_bytes_pending), \
                  (ifs)->read_bytes_pending); \
        } \
    } while (0)
#endif
