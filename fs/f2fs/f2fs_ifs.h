#ifndef F2FS_IFS_H
#define F2FS_IFS_H

#include <linux/fs.h>
#include <linux/bug.h>
#include <linux/f2fs_fs.h>
#include <linux/mm.h>
#include <linux/iomap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/atomic.h> // For atomic_t and bitops
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
static inline bool f2fs_ifs_block_is_uptodate(struct f2fs_iomap_folio_state *ifs,
		unsigned int block)
{
	return test_bit(block, ifs->state);
}
static inline size_t f2fs_ifs_iomap_longs(struct folio *folio)
{
	struct inode* inode=folio->mapping->host;
	WARN_ON_ONCE(inode==NULL);
	unsigned int nr_blocks = i_blocks_per_folio(inode, folio);
	return BITS_TO_LONGS(2 * nr_blocks);
}
static inline size_t f2fs_ifs_total_longs(struct folio *folio)
{
	return f2fs_ifs_iomap_longs(folio) + F2FS_IFS_PRIVATE_LONGS;
}

static inline unsigned long *
f2fs_ifs_private_flags_ptr(struct f2fs_iomap_folio_state *fifs, struct folio *folio)
{
	return &fifs->state[f2fs_ifs_iomap_longs(folio)];
}

/*Have to set parameter ifs's type to void*
and have to interpret ifs as f2fs_ifs to acess it's fields because
we cannot see iomap_folio_state definition*/
static void ifs_to_f2fs_ifs(void *ifs, struct f2fs_iomap_folio_state *fifs, struct folio *folio)
{
	struct f2fs_iomap_folio_state *src_ifs = (struct f2fs_iomap_folio_state *)ifs;
	size_t iomap_longs = f2fs_ifs_iomap_longs(folio);
	fifs->read_bytes_pending = READ_ONCE(src_ifs->read_bytes_pending);
	atomic_set(&fifs->write_bytes_pending, atomic_read(&src_ifs->write_bytes_pending));
	memcpy(fifs->state, src_ifs->state, iomap_longs * sizeof(unsigned long));
}
struct f2fs_iomap_folio_state *f2fs_ifs_alloc(struct folio *folio, gfp_t gfp,bool force_alloc);
void f2fs_ifs_free(struct folio *folio);
struct f2fs_iomap_folio_state *f2fs_folio_get_fifs(struct folio *folio);
inline unsigned long f2fs_get_folio_private_data(struct folio *folio);
inline int f2fs_set_folio_private_data(struct folio *folio,unsigned long data);
inline void f2fs_clear_folio_private_data(struct folio *folio);
inline void f2fs_clear_folio_private_all(struct folio *folio);
/*0-order and fully dirty folio has no fifs
they store private flag directly in their folio->private field
as original f2fs page private behaviour*/
void f2fs_ifs_clear_range_uptodate(struct folio *folio, struct f2fs_iomap_folio_state*fifs,size_t off, size_t len);
static inline bool is_f2fs_ifs(struct folio *folio)
{
    if (!folio_test_private(folio))
        return false;
        
    // first directly test no pointer flag is set or not
    if (test_bit(PAGE_PRIVATE_NOT_POINTER, (unsigned long *)&folio->private))
        return false;
        
    struct f2fs_iomap_folio_state *fifs;
    fifs = (struct f2fs_iomap_folio_state *)folio->private;
    if (!fifs)
        return false;
    if (READ_ONCE(fifs->read_bytes_pending) == F2FS_IFS_MAGIC) {
        return true;
    }
    return false;
}
#endif /* F2FS_IFS_H */

