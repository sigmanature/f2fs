// SPDX-License-Identifier: GPL-2.0
#ifndef F2FS_IFS_H
#define F2FS_IFS_H

#include <linux/fs.h>
#include <linux/bug.h>
#include <linux/f2fs_fs.h>
#include <linux/mm.h>
#include <linux/iomap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

#include "f2fs.h"

#define F2FS_IFS_MAGIC 0xf2f5
#define F2FS_IFS_PRIVATE_LONGS 1

/*
 * F2FS structure for folio private data, mimicking iomap_folio_state layout.
 * F2FS private flags/data are stored in extra space allocated at the end
 */
struct f2fs_iomap_folio_state {
	spinlock_t state_lock;
	unsigned int read_bytes_pending;
	atomic_t write_bytes_pending;
	/*
	 * Flexible array member.
	 * Holds [0...iomap_longs-1] for iomap uptodate/dirty bits.
	 * Holds [iomap_longs] for F2FS private flags/data (unsigned long).
	 */
	unsigned long state[];
};

static inline bool
f2fs_ifs_block_is_uptodate(struct f2fs_iomap_folio_state *ifs,
			   unsigned int block)
{
	return test_bit(block, ifs->state);
}

static inline size_t f2fs_ifs_iomap_longs(const struct folio *folio)
{
	struct inode *inode = folio->mapping->host;

	WARN_ON_ONCE(!inode);
	unsigned int nr_blocks =
		i_blocks_per_folio(inode, (struct folio *)folio);
	return BITS_TO_LONGS(2 * nr_blocks);
}

static inline size_t f2fs_ifs_total_longs(struct folio *folio)
{
	return f2fs_ifs_iomap_longs(folio) + F2FS_IFS_PRIVATE_LONGS;
}

static inline unsigned long *
f2fs_ifs_private_flags_ptr(struct f2fs_iomap_folio_state *fifs,
			   const struct folio *folio)
{
	return &fifs->state[f2fs_ifs_iomap_longs(folio)];
}

struct f2fs_iomap_folio_state *f2fs_ifs_alloc(struct folio *folio, gfp_t gfp,
					      bool force_alloc);
void folio_detach_f2fs_private(struct folio *folio);
struct f2fs_iomap_folio_state *folio_get_f2fs_ifs(struct folio *folio);

/*
 * 0-order and fully dirty folio has no fifs
 * they store private flag directly in their folio->private field
 * as original f2fs page private behaviour
 */
void f2fs_ifs_clear_range_uptodate(struct folio *folio,
				   struct f2fs_iomap_folio_state *fifs,
				   size_t off, size_t len);
void f2fs_iomap_set_range_dirty(struct folio *folio, size_t off, size_t len);

#endif /* F2FS_IFS_H */
