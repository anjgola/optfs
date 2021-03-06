/*
 *  fs/ext4bf/mballoc.h
 *
 *  Written by: Alex Tomas <alex@clusterfs.com>
 *
 */
#ifndef _EXT4_MBALLOC_H
#define _EXT4_MBALLOC_H

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include "ext4bf_jbdbf.h"
#include "ext4bf.h"

/*
 * with AGGRESSIVE_CHECK allocator runs consistency checks over
 * structures. these checks slow things down a lot
 */
#define AGGRESSIVE_CHECK__

/*
 * with DOUBLE_CHECK defined mballoc creates persistent in-core
 * bitmaps, maintains and uses them to check for double allocations
 */
#define DOUBLE_CHECK__

/*
 */
#undef MB_DEBUG_EXT4BF
#ifdef MB_DEBUG_EXT4BF
extern u8 mb_enable_debug;

#define mb_debug(n, fmt, a...)	                                        \
	do {								\
		if ((n) <= 1) {		        	\
			printk(KERN_DEBUG "(%s, %d): %s: ",		\
			       __FILE__, __LINE__, __func__);		\
			printk(fmt, ## a);				\
		}							\
	} while (0)
#else
#define mb_debug(n, fmt, a...)
#endif

#define EXT4_MB_HISTORY_ALLOC		1	/* allocation */
#define EXT4_MB_HISTORY_PREALLOC	2	/* preallocated blocks used */

/*
 * How long mballoc can look for a best extent (in found extents)
 */
#define MB_DEFAULT_MAX_TO_SCAN		200

/*
 * How long mballoc must look for a best extent
 */
#define MB_DEFAULT_MIN_TO_SCAN		10

/*
 * How many groups mballoc will scan looking for the best chunk
 */
#define MB_DEFAULT_MAX_GROUPS_TO_SCAN	5

/*
 * with 'ext4bf_mb_stats' allocator will collect stats that will be
 * shown at umount. The collecting costs though!
 */
#define MB_DEFAULT_STATS		0

/*
 * files smaller than MB_DEFAULT_STREAM_THRESHOLD are served
 * by the stream allocator, which purpose is to pack requests
 * as close each to other as possible to produce smooth I/O traffic
 * We use locality group prealloc space for stream request.
 * We can tune the same via /proc/fs/ext4bf/<parition>/stream_req
 */
#define MB_DEFAULT_STREAM_THRESHOLD	16	/* 64K */

/*
 * for which requests use 2^N search using buddies
 */
#define MB_DEFAULT_ORDER2_REQS		2

/*
 * default group prealloc size 512 blocks
 */
#define MB_DEFAULT_GROUP_PREALLOC	512


struct ext4bf_free_data {
	/* this links the free block information from group_info */
	struct rb_node node;

	/* this links the free block information from ext4bf_sb_info */
	struct list_head list;

	/* group which free block extent belongs */
	ext4bf_group_t group;

	/* free block extent */
	ext4bf_grpblk_t start_cluster;
	ext4bf_grpblk_t count;

	/* transaction which freed this extent */
	tid_t	t_tid;

	/* ext4bf: time when this block was freed. */
	unsigned int d_ftime;	/* Free time */
};

struct ext4bf_prealloc_space {
	struct list_head	pa_inode_list;
	struct list_head	pa_group_list;
	union {
		struct list_head pa_tmp_list;
		struct rcu_head	pa_rcu;
	} u;
	spinlock_t		pa_lock;
	atomic_t		pa_count;
	unsigned		pa_deleted;
	ext4bf_fsblk_t		pa_pstart;	/* phys. block */
	ext4bf_lblk_t		pa_lstart;	/* log. block */
	ext4bf_grpblk_t		pa_len;		/* len of preallocated chunk */
	ext4bf_grpblk_t		pa_free;	/* how many blocks are free */
	unsigned short		pa_type;	/* pa type. inode or group */
	spinlock_t		*pa_obj_lock;
	struct inode		*pa_inode;	/* hack, for history only */
};

enum {
	MB_INODE_PA = 0,
	MB_GROUP_PA = 1
};

struct ext4bf_free_extent {
	ext4bf_lblk_t fe_logical;
	ext4bf_grpblk_t fe_start;	/* In cluster units */
	ext4bf_group_t fe_group;
	ext4bf_grpblk_t fe_len;	/* In cluster units */
};

/*
 * Locality group:
 *   we try to group all related changes together
 *   so that writeback can flush/allocate them together as well
 *   Size of lg_prealloc_list hash is determined by MB_DEFAULT_GROUP_PREALLOC
 *   (512). We store prealloc space into the hash based on the pa_free blocks
 *   order value.ie, fls(pa_free)-1;
 */
#define PREALLOC_TB_SIZE 10
struct ext4bf_locality_group {
	/* for allocator */
	/* to serialize allocates */
	struct mutex		lg_mutex;
	/* list of preallocations */
	struct list_head	lg_prealloc_list[PREALLOC_TB_SIZE];
	spinlock_t		lg_prealloc_lock;
};

struct ext4bf_allocation_context {
	struct inode *ac_inode;
	struct super_block *ac_sb;

	/* original request */
	struct ext4bf_free_extent ac_o_ex;

	/* goal request (normalized ac_o_ex) */
	struct ext4bf_free_extent ac_g_ex;

	/* the best found extent */
	struct ext4bf_free_extent ac_b_ex;

	/* copy of the best found extent taken before preallocation efforts */
	struct ext4bf_free_extent ac_f_ex;

	/* number of iterations done. we have to track to limit searching */
	unsigned long ac_ex_scanned;
	__u16 ac_groups_scanned;
	__u16 ac_found;
	__u16 ac_tail;
	__u16 ac_buddy;
	__u16 ac_flags;		/* allocation hints */
	__u8 ac_status;
	__u8 ac_criteria;
	__u8 ac_2order;		/* if request is to allocate 2^N blocks and
				 * N > 0, the field stores N, otherwise 0 */
	__u8 ac_op;		/* operation, for history only */
	struct page *ac_bitmap_page;
	struct page *ac_buddy_page;
	struct ext4bf_prealloc_space *ac_pa;
	struct ext4bf_locality_group *ac_lg;
};

#define AC_STATUS_CONTINUE	1
#define AC_STATUS_FOUND		2
#define AC_STATUS_BREAK		3

struct ext4bf_buddy {
	struct page *bd_buddy_page;
	void *bd_buddy;
	struct page *bd_bitmap_page;
	void *bd_bitmap;
	struct ext4bf_group_info *bd_info;
	struct super_block *bd_sb;
	__u16 bd_blkbits;
	ext4bf_group_t bd_group;
};
#define EXT4_MB_BITMAP(e4b)	((e4b)->bd_bitmap)
#define EXT4_MB_BUDDY(e4b)	((e4b)->bd_buddy)
/* ext4bf: adding for ext4bf. */
extern int ext4bf_mb_load_buddy(struct super_block *sb, ext4bf_group_t group,
        struct ext4bf_buddy *e4b);
extern void ext4bf_mb_unload_buddy(struct ext4bf_buddy *e4b);
extern void mb_free_blocks(struct inode *inode, struct ext4bf_buddy *e4b,
        int first, int count);
/* */

static inline ext4bf_fsblk_t ext4bf_grp_offs_to_block(struct super_block *sb,
					struct ext4bf_free_extent *fex)
{
	return ext4bf_group_first_block_no(sb, fex->fe_group) +
		(fex->fe_start << EXT4_SB(sb)->s_cluster_bits);
}
#endif
