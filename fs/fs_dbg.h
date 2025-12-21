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
#include <linux/bio.h>
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


static void stat_folios_bio(struct bio *bio)
{
	struct folio_iter fi;
	struct folio *folio;
	unsigned long first_idx = ULONG_MAX;
	unsigned long last_idx  = 0;
	unsigned int folio_cnt  = 0;
	unsigned int used_vec;
	unsigned int total_vec;
	unsigned int waste;

	if (!bio) {
		pr_debug("bio is NULL, may not alloc");
		return;
	}

	bio_for_each_folio_all(fi, bio) {
		folio = fi.folio;
		folio_cnt++;
		if (folio_index(folio) < first_idx)
			first_idx = folio_index(folio);
		if (folio_next_index(folio)-1 > last_idx)
			last_idx = folio_next_index(folio)-1;
	}

	used_vec  = bio->bi_vcnt;
	total_vec = bio->bi_max_vecs;
	waste     = total_vec ?
		    (total_vec - used_vec) * 100 / total_vec : 0;

	pr_debug("bio %p folio_cnt %u idx [%lu-%lu] "
		 "bvec %u/%u waste %u%%",
		 bio, folio_cnt, first_idx, last_idx,
		 used_vec, total_vec, waste);
}
#endif