#ifndef F2FS_DBG_H
#define F2FS_DBG_H

#include <linux/fs.h>
#include <linux/bug.h>
#include <linux/f2fs_fs.h>
#include <linux/mm.h>
#include <linux/iomap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/atomic.h> // For atomic_t and bitops
#include <linux/delay.h>
#include "f2fs.h"
#include "f2fs_ifs.h"
#ifdef CONFIG_FS_IOMAP_DEBUG_PRINT
#include "../fs_dbg.h"
#endif
static void f2fs_is_folio_writeback(struct folio *folio)
{
    if (folio_test_writeback(folio))
        f2fs_err(F2FS_I_SB(folio->mapping->host),
            "folio %p,index %d,order %d host inode ino %d is already in writeback state", folio);
}
static void f2fs_check_inode_folios_writeback(struct inode *inode)
{
    struct address_space *mapping = inode->i_mapping;
    struct folio *folio;
    pgoff_t start = 0;
    pgoff_t end = -1;
    struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

    XA_STATE(xas, &mapping->i_pages, start);

    rcu_read_lock();
    xas_for_each(&xas, folio, end) {
        if (!xa_is_value(folio)) {
            if (folio_test_writeback(folio)) {
                f2fs_err(sbi,
                    "Error:inode(%lu): folio %p, index %lu, order %u is in writeback state",
                    inode->i_ino, folio, folio->index,
                    folio_order(folio));
                // 如果只需要找到第一个就退出，可以在这里加上 break;
            }
            if(folio_test_locked(folio))
            {
                f2fs_err(sbi,
                    "Error:inode(%lu): folio %p, index %lu, order %u is locked",
                    inode->i_ino, folio, folio->index,
                    folio_order(folio));
                ssleep(1);
                f2fs_bug_on(sbi, 1);
            }
            else
            {
                f2fs_err(sbi,
                    "inode(%lu): folio %p, index %lu, order %u is not locked",
                    inode->i_ino, folio, folio->index,
                    folio_order(folio));
            }
        }
    }
    rcu_read_unlock();
}
/*note:don't safe in truncate context*/
static void noinline print_folio_private(struct folio *folio)
{
    if(folio_order(folio)==0)
    {
        f2fs_err(F2FS_I_SB(folio->mapping->host), "folio private %p", folio->private);
        return;
    }
    struct f2fs_iomap_folio_state *fifs=folio->private;
    if(fifs)
    {
        f2fs_err(F2FS_I_SB(folio->mapping->host), "folio ifs write_bytes_pending: %x,read_bytes_pending: %x", atomic_read(&fifs->write_bytes_pending)
    ,fifs->read_bytes_pending);
    }
    return;
}
static void print_wbp(struct folio*folio)
{
    struct f2fs_iomap_folio_state* fifs=folio->private;
    struct inode*inode=folio->mapping->host;
    if(fifs&&folio_order(folio)>0)
    {
    f2fs_err(F2FS_I_SB(folio->mapping->host),"folio index %lu, order %d, host inode ino %d,write_bytes_pending %u",folio->index, folio_order(folio),inode->i_ino,atomic_read(&fifs->write_bytes_pending));
    }
}
static void print_rbp(struct folio*folio)
{
    struct f2fs_iomap_folio_state* fifs=folio->private;
    struct inode*inode=folio->mapping->host;
    if(fifs&&folio_order(folio)>0)
    {
        f2fs_err(F2FS_I_SB(inode),"folio index %lu, order %d, host inode ino %d,rbp %d rbp minus magic %u",folio->index, folio_order(folio),inode->i_ino,fifs->read_bytes_pending,fifs->read_bytes_pending-F2FS_IFS_MAGIC);
    }
}
static void f2fs_list_folios_bio(struct bio *bio)
{
    struct folio *folio;
    struct f2fs_iomap_folio_state *fifs;
    struct folio_iter fi;
    int i = 0;

    if (!bio)
    {
        f2fs_err(F2FS_I_SB(folio->mapping->host),"bio is NULL,may not alloc");
        return;
    }
    bio_for_each_folio_all(fi, bio) {
        folio = fi.folio;
        fifs = folio->private;
        if (fifs&&folio_order(folio)>0) {
            f2fs_err(F2FS_I_SB(folio->mapping->host),
                "in bio %p,folio %p, index %lu, order %d, host inode ino %d, fi.length:%d,write_bytes_pending %u",
                bio,folio, folio->index, folio_order(folio),
                folio->mapping->host->i_ino,fi.length,
                atomic_read(&fifs->write_bytes_pending));
        }
        else
        {
            f2fs_err(F2FS_I_SB(folio->mapping->host),
                "in bio %p,folio %p, index %lu, order %d, host inode ino %d, fi.length:%d",
                bio,folio, folio->index, folio_order(folio),
                folio->mapping->host->i_ino,fi.length);
        }
    }
}
static void noinline print_folio(struct folio *folio)
{
    struct inode *inode = folio->mapping->host;
    pr_err("folio %p, index %lu, order %d, host inode ino %d",
             folio, folio->index, folio_order(folio), inode->i_ino);
}
static void noinline print_folio_mapping(struct folio *folio)
{
    pr_err("folio %p, index %lu, order %d, host mapping %p",
             folio, folio->index, folio_order(folio), folio->mapping);
}
static void noinline dump_no_lock_or_null_mapping(struct folio *folio)
{
    if (WARN_ON_ONCE(!folio_test_locked(folio))) {
        dump_stack();
    }
    if (WARN_ON_ONCE(!folio->mapping)) {
        dump_stack();
    }
}
static void inline f2fs_list_folios_cc(struct compress_ctx *cc)
{
    int i;
    unsigned int num_to_skip = 0;
    f2fs_err(F2FS_I_SB(cc->inode), "Begin Listing folios in compress_ctx %p", cc);
    if (!cc || !cc->rpages) {
        f2fs_err(F2FS_I_SB(cc->inode), "compress_ctx or rpages is NULL");
        return;
    }
    /* Iterate through the cluster pages */
    num_to_skip = 1; // Reset num_to_skip for the first iteration
    for (i = 0; i < cc->cluster_size; i += num_to_skip) {
		num_to_skip = 1;
		if (!cc->rpages[i]) {
			continue;
		}
		struct folio *folio = page_folio(cc->rpages[i]);
        #ifdef CONFIG_FS_IOMAP_DEBUG_PRINT
		print_folio_mapping(folio);
        #endif
        f2fs_err(F2FS_I_SB(cc->inode),"folio_page_idx:%d",folio_page_idx(folio, cc->rpages[i]));
        struct f2fs_iomap_folio_state *fifs = folio->private;
		/*f2fs_write_raw_pages can have discontinuous cluster,
		we must count all page that belongs to the same folio to skip here*/
		while ((i + num_to_skip) < cc->cluster_size && cc->rpages[i + num_to_skip] &&
		       page_folio(cc->rpages[i + num_to_skip]) == folio) {
			num_to_skip++;
		}
	}
    f2fs_err(F2FS_I_SB(cc->inode), "End Listing folios in compress_ctx %p", cc);
}
static void inline f2fs_list_folios_cic(struct compress_io_ctx *cc)
{
    int i;
    unsigned int num_to_skip = 0;
    f2fs_err(F2FS_I_SB(cc->inode), "Begin Listing folios in compress_io_ctx %p", cc);
    if (!cc || !cc->rpages) {
        f2fs_err(F2FS_I_SB(cc->inode), "compress_io_ctx or rpages is NULL");
        return;
    }
    /* Iterate through the cluster pages */
    num_to_skip = 1; // Reset num_to_skip for the first iteration
    for (i = 0; i < cc->nr_rpages; i += num_to_skip) {
		num_to_skip = 1;
		if (!cc->rpages[i]) {
			continue;
		}
		struct folio *folio = page_folio(cc->rpages[i]);
        print_folio(folio);
        f2fs_err(F2FS_I_SB(cc->inode),"folio_page_idx:%d",folio_page_idx(folio, cc->rpages[i]));
        struct f2fs_iomap_folio_state *fifs = folio->private;
        print_wbp(folio);
		/*f2fs_write_raw_pages can have discontinuous cluster,
		we must count all page that belongs to the same folio to skip here*/
		while ((i + num_to_skip) < cc->nr_rpages && cc->rpages[i + num_to_skip] &&
		       page_folio(cc->rpages[i + num_to_skip]) == folio) {
			num_to_skip++;
		}
	}
    f2fs_err(F2FS_I_SB(cc->inode), "End Listing folios in compress_io_ctx %p", cc);
}
static void f2fs_ifs_print_uptodate_status(struct folio *folio)
{
    struct f2fs_iomap_folio_state *fifs;
    struct inode *inode;
    unsigned int nr_blocks;
    unsigned long flags;
    int i;

    if (!folio || !folio->mapping) {
        printk(KERN_EMERG "f2fs_ifs: folio or mapping is NULL\n");
        return;
    }

    inode = folio->mapping->host;
    if (!inode) {
        printk(KERN_EMERG "f2fs_ifs: inode is NULL\n");
        return;
    }

    if (!folio_test_private(folio)) {
        printk(KERN_EMERG "f2fs_ifs: folio order=%u has no private data\n",
               folio_order(folio));
        return;
    }

    // Check if it's using direct flags
    if (test_bit(PAGE_PRIVATE_NOT_POINTER, (unsigned long *)&folio->private)) {
        printk(KERN_EMERG "f2fs_ifs: folio order=%u using direct flags, uptodate=%s\n",
               folio_order(folio), folio_test_uptodate(folio) ? "true" : "false");
        return;
    }

    fifs = (struct f2fs_iomap_folio_state *)folio->private;
    if (!fifs) {
        printk(KERN_EMERG "f2fs_ifs: fifs is NULL\n");
        return;
    }

    // Check magic number
    if (READ_ONCE(fifs->read_bytes_pending) != F2FS_IFS_MAGIC) {
        printk(KERN_EMERG "f2fs_ifs: not f2fs ifs (magic=0x%x)\n",
               READ_ONCE(fifs->read_bytes_pending));
        return;
    }

    nr_blocks = i_blocks_per_folio(inode, folio);

    // Lock to ensure concurrent safety
    spin_lock_irqsave(&fifs->state_lock, flags);

    printk(KERN_EMERG "f2fs_ifs: folio order=%u, blocks=%u, uptodate bits: [",
           folio_order(folio), nr_blocks);

    // Print uptodate status for each block
    for (i = 0; i < nr_blocks; i++) {
        if (test_bit(i, fifs->state)) {
            printk(KERN_CONT "1");
        } else {
            printk(KERN_CONT "0");
        }
        if (i < nr_blocks - 1) {
            printk(KERN_CONT ",");
        }
    }

    printk(KERN_CONT "]\n");

    spin_unlock_irqrestore(&fifs->state_lock, flags);
}

static void f2fs_print_dn_blkaddr(struct dnode_of_data* dn,pgoff_t index)
{
    f2fs_err(F2FS_I_SB(dn->inode),"in f2fs_print_dn_blkaddr\n");
    dump_stack();
    f2fs_err(F2FS_I_SB(dn->inode),"node_base addr is %p:",get_dnode_addr(dn->inode, dn->node_folio));
    f2fs_err(F2FS_I_SB(dn->inode),"data_blkaddr is %d:",le32_to_cpu(*(get_dnode_addr(dn->inode, dn->node_folio) + dn->ofs_in_node)));
    f2fs_err(F2FS_I_SB(dn->inode), "index:%d ofs_in_node:%d blk_addr: %lx",index,dn->ofs_in_node,dn->data_blkaddr);
}
#endif /*F2FS_DBG_H*/
