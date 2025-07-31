//f2fs_ifs.c
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/mm.h>
#include <linux/iomap.h>
#include <linux/slab.h>
#include <linux/sched.h> // For cond_resched()
#include "f2fs.h"
#include "f2fs_ifs.h" 
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
void f2fs_ifs_free(struct folio *folio)
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
struct f2fs_iomap_folio_state *f2fs_folio_get_private(struct folio *folio)
{
    if (!folio_test_private(folio))
        return NULL;
        
    // 检查是否为标志位使用
    if (test_bit(PAGE_PRIVATE_NOT_POINTER, (unsigned long *)&folio->private))
        return NULL;
        
    void *private_data = folio->private;
    if (!private_data)
        return NULL;
        
    // 安全地检查 magic
    struct f2fs_iomap_folio_state *fifs = private_data;
    if (READ_ONCE(fifs->read_bytes_pending) == F2FS_IFS_MAGIC)
        return fifs;
    
    return NULL;
}
unsigned f2fs_iomap_find_dirty_range(struct folio *folio, u64 *range_start,
		u64 range_end)
{
	struct inode* inode=folio->mapping->host;
	
	if(folio_order(folio) == 0) 
	 	return range_end-*range_start;
	if(f2fs_compressed_file(inode))
	{	
		/*clamp range end to a cluster's size*/
		int a=*range_start>>PAGE_SHIFT;
		int b=cluster_i_idx(inode, a)<<F2FS_I(inode)->i_cluster_size;
		int c=(F2FS_I(inode)->i_cluster_size - 1);
		range_end=min(range_end,(i_end_idx_of_cluster(inode,*range_start>>PAGE_SHIFT)+1) << PAGE_SHIFT);
	}
	return iomap_find_dirty_range(folio, range_start, range_end);
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
inline unsigned long f2fs_get_folio_private_data(struct folio *folio)
{
	struct f2fs_iomap_folio_state *fifs = 
		(struct f2fs_iomap_folio_state *)folio->private;
	unsigned long *private_p;
	unsigned long data_val;
	if (!folio->mapping)
		return 0;
	f2fs_bug_on(F2FS_I_SB(folio_inode(folio)),!fifs);
	if (READ_ONCE(fifs->read_bytes_pending) != F2FS_IFS_MAGIC)
		return 0;

	private_p = f2fs_ifs_private_flags_ptr(fifs, folio);
	if (!private_p)
		return 0;

	data_val = READ_ONCE(*private_p); // Read atomically

	if (!test_bit(PAGE_PRIVATE_NOT_POINTER, &data_val))
		return 0; // Return 0 if NOT_POINTER isn't set

	return data_val >> PAGE_PRIVATE_MAX;
}

inline int f2fs_set_folio_private_data(struct folio *folio, unsigned long data)
{
	
	if (unlikely(!folio->mapping))
		return -ENOENT;
	
	struct f2fs_iomap_folio_state *fifs = f2fs_ifs_alloc(folio, GFP_NOFS, true);
	if (unlikely(!fifs))
		return -ENOMEM;
	
	unsigned long *private_p;
	unsigned long old_val, new_val;
	
	private_p = f2fs_ifs_private_flags_ptr(fifs, folio);
	if (!private_p)
		return -EINVAL;

	// Atomically set the data part and the NOT_POINTER bit using cmpxchg loop
	do {
		old_val = READ_ONCE(*private_p);
		new_val = old_val;
		// Clear old data bits (bits >= PAGE_PRIVATE_MAX)
		new_val &= GENMASK(PAGE_PRIVATE_MAX - 1, 0);
		// Set new data bits
		new_val |= (data << PAGE_PRIVATE_MAX);
		// Ensure NOT_POINTER is set
		__set_bit(PAGE_PRIVATE_NOT_POINTER, &new_val);
	} while (cmpxchg(private_p, old_val, new_val) != old_val);

	return 0;
}

inline void f2fs_clear_folio_private_data(struct folio *folio)
{	
	struct f2fs_iomap_folio_state *fifs = 
		(struct f2fs_iomap_folio_state *)folio->private;
	unsigned long *private_p;
	unsigned long old_val, new_val;
	f2fs_bug_on(F2FS_I_SB(folio_inode(folio)),!fifs);
	if (!folio->mapping)
		return;
	if (READ_ONCE(fifs->read_bytes_pending) != F2FS_IFS_MAGIC)
		return;

	private_p = f2fs_ifs_private_flags_ptr(fifs, folio);
	if (!private_p)
		return;

	// Atomically clear the data part, leave flags untouched
	do {
		old_val = READ_ONCE(*private_p);
		// If already no data bits set, nothing to do
		if ((old_val >> PAGE_PRIVATE_MAX) == 0)
			break;
		new_val = old_val;
		// Clear data bits
		new_val &= GENMASK(PAGE_PRIVATE_MAX - 1, 0);
	} while (cmpxchg(private_p, old_val, new_val) != old_val);
}

inline void f2fs_clear_folio_private_all(struct folio *folio)
{
	struct f2fs_iomap_folio_state *fifs = 
		(struct f2fs_iomap_folio_state *)folio->private;
	unsigned long *private_p;

	if (unlikely(!fifs || !folio->mapping))
		return;
	if (READ_ONCE(fifs->read_bytes_pending) != F2FS_IFS_MAGIC)
		return;

	private_p = f2fs_ifs_private_flags_ptr(fifs, folio);
	if (!private_p)
		return;
		
	// Clear all private flags/data
	*private_p = 0;
}
