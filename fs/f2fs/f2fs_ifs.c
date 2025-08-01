//f2fs_ifs.c
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/mm.h>
#include <linux/iomap.h>
#include <linux/slab.h>
#include <linux/sched.h> // For cond_resched()
#include "f2fs.h"
#ifdef CONFIG_F2FS_IOMAP_FOLIO_STATE
#include "f2fs_ifs.h"
#endif 
struct f2fs_iomap_folio_state *f2fs_ifs_alloc(struct folio *folio, gfp_t gfp,bool force_alloc)
{
	struct inode* inode= folio->mapping->host;
	size_t alloc_size=0;
	if (folio_order(folio) == 0) {
		if(!force_alloc)
		{
			WARN_ON_ONCE(1); 
			return NULL;
		}
		else
		{/* GC can store private flag in 0 order folio's folio->private
			causes iomap buffered write mistakenly interpret as a pointer
			we add a bool force_alloc to deal with this case
		*/
			struct f2fs_iomap_folio_state *fifs;
			alloc_size = sizeof(*fifs) + 2*sizeof(unsigned long);
			fifs = kmalloc(alloc_size, gfp); 
			if (!fifs)
				return NULL;
			spin_lock_init(&fifs->state_lock);
			WRITE_ONCE(fifs->read_bytes_pending, F2FS_IFS_MAGIC);
        	atomic_set(&fifs->write_bytes_pending, 0); 
			unsigned int nr_blocks = i_blocks_per_folio(inode, folio);
			if (folio_test_uptodate(folio))
				bitmap_set(fifs->state, 0, nr_blocks);
			if (folio_test_dirty(folio))
				(fifs->state, nr_blocks, nr_blocks);
			*f2fs_ifs_private_flags_ptr(fifs, folio) = 0; 
			folio_attach_private(folio, fifs);
		}
	}
	struct f2fs_iomap_folio_state *fifs;
	void *old_private;
	size_t iomap_longs;
	size_t total_longs;	
	WARN_ON_ONCE(!inode); // Should have an inode

	old_private = folio_get_private(folio);

	if (old_private) {
		// Check if it's already our type using the magic number directly
		if (READ_ONCE(((struct f2fs_iomap_folio_state *)old_private)->read_bytes_pending) == F2FS_IFS_MAGIC) {
			return (struct f2fs_iomap_folio_state *)old_private; // Already ours
		} else {
			// Non-NULL, not ours -> Allocate, Copy, Replace path
			total_longs = f2fs_ifs_total_longs(folio);
			alloc_size = sizeof(*fifs) + total_longs * sizeof(unsigned long);

			fifs = kmalloc(alloc_size, gfp); 
			if (!fifs)
				return NULL;

			spin_lock_init(&fifs->state_lock);
			*f2fs_ifs_private_flags_ptr(fifs, folio) = 0; 
			// Copy data from the presumed iomap_folio_state (old_private)
			ifs_to_f2fs_ifs(old_private, fifs, folio);
			WRITE_ONCE(fifs->read_bytes_pending, F2FS_IFS_MAGIC);
            atomic_set(f2fs_ifs_dirty_bytes_pending_ptr(fifs, folio), 0);
			folio_change_private(folio, fifs);
			kfree(old_private); 
			return fifs;
		}
	} else {
		iomap_longs = f2fs_ifs_iomap_longs(folio);
		total_longs = iomap_longs + 1;
		alloc_size = sizeof(*fifs) + total_longs * sizeof(unsigned long);

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
        atomic_set(f2fs_ifs_dirty_bytes_pending_ptr(fifs, folio), 0);
		folio_attach_private(folio, fifs);
		return fifs;
	}
}
void folio_detach_f2fs_private(struct folio *folio)
{
	struct f2fs_iomap_folio_state*fifs;
	
	if (!folio_test_private(folio))
		return;
		
	// Check if it's using direct flags
	if (test_bit(PAGE_PRIVATE_NOT_POINTER, (unsigned long *)&folio->private)) {
		folio_detach_private(folio);
		return;
	}
	
	fifs = folio_detach_private(folio);
	if (!fifs)
		return; 
	
	if(is_f2fs_ifs(folio))
	{	
		WARN_ON_ONCE(READ_ONCE(fifs->read_bytes_pending) != F2FS_IFS_MAGIC);
		WARN_ON_ONCE(atomic_read(&fifs->write_bytes_pending));
	}
	else
	{
		WARN_ON_ONCE(READ_ONCE(fifs->read_bytes_pending) != 0);
		WARN_ON_ONCE(atomic_read(&fifs->write_bytes_pending));
	}
	
	kfree(fifs);
}
struct f2fs_iomap_folio_state *folio_get_f2fs_ifs(struct folio *folio)
{
    if (!folio_test_private(folio))
        return NULL;
        
    // 检查是否为标志位使用
    if (test_bit(PAGE_PRIVATE_NOT_POINTER, (unsigned long *)&folio->private))
        return NULL;
	/* Note we assume folio->private can be either ifs or f2fs_ifs here. Compresssed folios
	should not call this function */
    f2fs_bug_on(F2FS_F_SB(folio),
		*((u32 *)folio->private) == F2FS_COMPRESSED_PAGE_MAGIC)    
    return folio->private;
}
void f2fs_ifs_clear_range_uptodate(struct folio *folio, struct f2fs_iomap_folio_state*fifs,size_t off, size_t len)
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

