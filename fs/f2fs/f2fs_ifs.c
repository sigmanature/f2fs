// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "f2fs_ifs.h"

/*
 * Have to set parameter ifs's type to void*
 * and have to interpret ifs as f2fs_ifs to access its fields because
 * we cannot see iomap_folio_state definition
 */
static void ifs_to_f2fs_ifs(void *ifs, struct f2fs_iomap_folio_state *fifs,
			    struct folio *folio)
{
	struct f2fs_iomap_folio_state *src_ifs =
		(struct f2fs_iomap_folio_state *)ifs;
	size_t iomap_longs = f2fs_ifs_iomap_longs(folio);

	fifs->read_bytes_pending = READ_ONCE(src_ifs->read_bytes_pending);
	atomic_set(&fifs->write_bytes_pending,
		   atomic_read(&src_ifs->write_bytes_pending));
	memcpy(fifs->state, src_ifs->state,
	       iomap_longs * sizeof(unsigned long));
}

static inline bool is_f2fs_ifs(struct folio *folio)
{
	struct f2fs_iomap_folio_state *fifs;

	if (!folio_test_private(folio))
		return false;

	// first directly test no pointer flag is set or not
	if (test_bit(PAGE_PRIVATE_NOT_POINTER,
		     (unsigned long *)&folio->private))
		return false;

	fifs = (struct f2fs_iomap_folio_state *)folio->private;
	if (!fifs)
		return false;

	if (READ_ONCE(fifs->read_bytes_pending) == F2FS_IFS_MAGIC)
		return true;

	return false;
}

struct f2fs_iomap_folio_state *f2fs_ifs_alloc(struct folio *folio, gfp_t gfp,
					      bool force_alloc)
{
	struct inode *inode = folio->mapping->host;
	size_t alloc_size = 0;

	if (!folio_test_large(folio)) {
		if (!force_alloc) {
			WARN_ON_ONCE(1);
			return NULL;
		}
		/*
		 * GC can store private flag in 0 order folio's folio->private
		 * causes iomap buffered write mistakenly interpret as a pointer
		 * we add a bool force_alloc to deal with this case
		 */
		struct f2fs_iomap_folio_state *fifs;

		alloc_size = sizeof(*fifs) + 2 * sizeof(unsigned long);
		fifs = kmalloc(alloc_size, gfp);
		if (!fifs)
			return NULL;
		spin_lock_init(&fifs->state_lock);
		WRITE_ONCE(fifs->read_bytes_pending, F2FS_IFS_MAGIC);
		atomic_set(&fifs->write_bytes_pending, 0);
		unsigned int nr_blocks =
			i_blocks_per_folio(inode, folio);
		if (folio_test_uptodate(folio))
			bitmap_set(fifs->state, 0, nr_blocks);
		if (folio_test_dirty(folio))
			bitmap_set(fifs->state, nr_blocks, nr_blocks);
		*f2fs_ifs_private_flags_ptr(fifs, folio) = 0;
		folio_attach_private(folio, fifs);
		return fifs;
	}

	struct f2fs_iomap_folio_state *fifs;
	void *old_private;
	size_t iomap_longs;
	size_t total_longs;

	WARN_ON_ONCE(!inode); // Should have an inode

	old_private = folio_get_private(folio);

	if (old_private) {
		// Check if it's already our type using the magic number directly
		if (READ_ONCE(((struct f2fs_iomap_folio_state *)old_private)
				      ->read_bytes_pending) == F2FS_IFS_MAGIC) {
			return (struct f2fs_iomap_folio_state *)
				old_private; // Already ours
		}
		// Non-NULL, not ours -> Allocate, Copy, Replace path
		total_longs = f2fs_ifs_total_longs(folio);
		alloc_size = sizeof(*fifs) +
				total_longs * sizeof(unsigned long);

		fifs = kmalloc(alloc_size, gfp);
		if (!fifs)
			return NULL;

		spin_lock_init(&fifs->state_lock);
		*f2fs_ifs_private_flags_ptr(fifs, folio) = 0;
		// Copy data from the presumed iomap_folio_state (old_private)
		ifs_to_f2fs_ifs(old_private, fifs, folio);
		WRITE_ONCE(fifs->read_bytes_pending, F2FS_IFS_MAGIC);
		folio_change_private(folio, fifs);
		kfree(old_private);
		return fifs;
	}

	iomap_longs = f2fs_ifs_iomap_longs(folio);
	total_longs = iomap_longs + 1;
	alloc_size =
		sizeof(*fifs) + total_longs * sizeof(unsigned long);

	fifs = kzalloc(alloc_size, gfp);
	if (!fifs)
		return NULL;

	spin_lock_init(&fifs->state_lock);

	unsigned int nr_blocks = i_blocks_per_folio(inode, folio);

	if (folio_test_uptodate(folio))
		bitmap_set(fifs->state, 0, nr_blocks);
	if (folio_test_dirty(folio))
		bitmap_set(fifs->state, nr_blocks, nr_blocks);
	WRITE_ONCE(fifs->read_bytes_pending, F2FS_IFS_MAGIC);
	atomic_set(&fifs->write_bytes_pending, 0);
	folio_attach_private(folio, fifs);
	return fifs;
}

void folio_detach_f2fs_private(struct folio *folio)
{
	struct f2fs_iomap_folio_state *fifs;

	if (!folio_test_private(folio))
		return;

	// Check if it's using direct flags
	if (test_bit(PAGE_PRIVATE_NOT_POINTER,
		     (unsigned long *)&folio->private)) {
		folio_detach_private(folio);
		return;
	}

	fifs = folio_detach_private(folio);
	if (!fifs)
		return;

	if (is_f2fs_ifs(folio)) {
		WARN_ON_ONCE(READ_ONCE(fifs->read_bytes_pending) !=
			     F2FS_IFS_MAGIC);
		WARN_ON_ONCE(atomic_read(&fifs->write_bytes_pending));
	} else {
		WARN_ON_ONCE(READ_ONCE(fifs->read_bytes_pending) != 0);
		WARN_ON_ONCE(atomic_read(&fifs->write_bytes_pending));
	}

	kfree(fifs);
}

struct f2fs_iomap_folio_state *folio_get_f2fs_ifs(struct folio *folio)
{
	if (!folio_test_private(folio))
		return NULL;

	if (test_bit(PAGE_PRIVATE_NOT_POINTER,
		     (unsigned long *)&folio->private))
		return NULL;
	/*
	 * Note we assume folio->private can be either ifs or f2fs_ifs here.
	 * Compresssed folios should not call this function
	 */
	f2fs_bug_on(F2FS_F_SB(folio),
		    *((u32 *)folio->private) == F2FS_COMPRESSED_PAGE_MAGIC);
	return folio->private;
}

void f2fs_ifs_clear_range_uptodate(struct folio *folio,
				   struct f2fs_iomap_folio_state *fifs,
				   size_t off, size_t len)
{
	struct inode *inode = folio->mapping->host;
	unsigned int first_blk = (off >> inode->i_blkbits);
	unsigned int last_blk = (off + len - 1) >> inode->i_blkbits;
	unsigned int nr_blks = last_blk - first_blk + 1;
	unsigned long flags;

	spin_lock_irqsave(&fifs->state_lock, flags);
	bitmap_clear(fifs->state, first_blk, nr_blks);
	spin_unlock_irqrestore(&fifs->state_lock, flags);
}

void f2fs_iomap_set_range_dirty(struct folio *folio, size_t off, size_t len)
{
	struct f2fs_iomap_folio_state *fifs = folio_get_f2fs_ifs(folio);

	if (fifs) {
		struct inode *inode = folio->mapping->host;
		unsigned int blks_per_folio = i_blocks_per_folio(inode, folio);
		unsigned int first_blk = (off >> inode->i_blkbits);
		unsigned int last_blk = (off + len - 1) >> inode->i_blkbits;
		unsigned int nr_blks = last_blk - first_blk + 1;
		unsigned long flags;

		spin_lock_irqsave(&fifs->state_lock, flags);
		bitmap_set(fifs->state, first_blk + blks_per_folio, nr_blks);
		spin_unlock_irqrestore(&fifs->state_lock, flags);
	}
}
