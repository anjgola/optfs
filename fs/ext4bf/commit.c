/*
 * linux/fs/jbdbf/commit.c
 *
 * Written by Stephen C. Tweedie <sct@redhat.com>, 1998
 *
 * Copyright 1998 Red Hat corp --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Journal commit routines for the generic filesystem journaling code;
 * part of the ext2fs journaling system.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include "jbdbf.h"
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/jiffies.h>
#include <linux/crc32.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include "ext4bf.h"
//#include <zlib.h>
#if TIME_736_1
struct timespec clock_time;
#endif
 uint32_t fletcher32( uint32_t const crc32_sum, uint16_t const *data, size_t len );
uint32_t fletcher32( uint32_t const crc32_sum, uint16_t const *data, size_t len )
{
    uint32_t sum1 = crc32_sum & 0xffff, sum2 = (crc32_sum >> 16) & 0xffff ;
    size_t words = len/4;
    while (words) {
        unsigned tlen = words > 359 ? 359 : words;
        words -= tlen;
        do {
            sum2 += sum1 += *data++;
        } while (--tlen);
        sum1 = (sum1 & 0xffff) + (sum1 >> 16);
        sum2 = (sum2 & 0xffff) + (sum2 >> 16);
    }

    sum1 = (sum1 & 0xffff) + (sum1 >> 16);
    sum2 = (sum2 & 0xffff) + (sum2 >> 16);
    return sum2 << 16 | sum1;
}
/*
 * Default IO end handler for temporary BJ_IO buffer_heads.
 */
static void journal_end_buffer_io_sync(struct buffer_head *bh, int uptodate)
{
	BUFFER_TRACE(bh, "");
	if (uptodate)
		set_buffer_uptodate(bh);
	else
		clear_buffer_uptodate(bh);
	unlock_buffer(bh);
}

/*
 * When an ext4 file is truncated, it is possible that some pages are not
 * successfully freed, because they are attached to a committing transaction.
 * After the transaction commits, these pages are left on the LRU, with no
 * ->mapping, and with attached buffers.  These pages are trivially reclaimable
 * by the VM, but their apparent absence upsets the VM accounting, and it makes
 * the numbers in /proc/meminfo look odd.
 *
 * So here, we have a buffer which has just come off the forget list.  Look to
 * see if we can strip all buffers from the backing page.
 *
 * Called under lock_journal(), and possibly under journal_datalist_lock.  The
 * caller provided us with a ref against the buffer, and we drop that here.
 */
static void release_buffer_page(struct buffer_head *bh)
{
	struct page *page;

	if (buffer_dirty(bh))
		goto nope;
	if (atomic_read(&bh->b_count) != 1)
		goto nope;
	page = bh->b_page;
	if (!page)
		goto nope;
	if (page->mapping)
		goto nope;

	/* OK, it's a truncated page */
	if (!trylock_page(page))
		goto nope;

	page_cache_get(page);
	__brelse(bh);
	try_to_free_buffers(page);
	unlock_page(page);
	page_cache_release(page);
	return;

nope:
	__brelse(bh);
}

/*
 * Done it all: now submit the commit record.  We should have
 * cleaned up our previous buffers by now, so if we are in abort
 * mode we can now just skip the rest of the journal write
 * entirely.
 *
 * Returns 1 if the journal needs to be aborted or 0 on success

 */
static int journal_submit_commit_record(journal_t *journal,
					transaction_bf_t *commit_transaction,
					struct buffer_head **cbh,
					__u32 crc32_sum)
{
    
   // TIMESTAMP("START", "journal_submit_commit_record", "");

    struct journal_bf_head *descriptor;
	struct commit_header *tmp;
	struct buffer_head *bh;
	int ret;
	struct timespec now = current_kernel_time();

	*cbh = NULL;

	if (is_journal_aborted(journal))
		return 0;

	descriptor = jbdbf_journal_get_descriptor_buffer(journal);
	if (!descriptor)
		return 1;

	bh = jh2bhbf(descriptor);

	tmp = (struct commit_header *)bh->b_data;
	tmp->h_magic = cpu_to_be32(JBD2_MAGIC_NUMBER);
	tmp->h_blocktype = cpu_to_be32(JBD2_COMMIT_BLOCK);
	tmp->h_sequence = cpu_to_be32(commit_transaction->t_tid);
	tmp->h_commit_sec = cpu_to_be64(now.tv_sec);
	tmp->h_commit_nsec = cpu_to_be32(now.tv_nsec);

	if (JBD2_HAS_COMPAT_FEATURE(journal,
				    JBD2_FEATURE_COMPAT_CHECKSUM)) {
		tmp->h_chksum_type 	= JBD2_CRC32_CHKSUM;
		tmp->h_chksum_size 	= JBD2_CRC32_CHKSUM_SIZE;
		tmp->h_chksum[0] 	= cpu_to_be32(crc32_sum);
	}

	JBUFFER_TRACE(descriptor, "submit commit block");
	lock_buffer(bh);
	clear_buffer_dirty(bh);
	set_buffer_uptodate(bh);
	bh->b_end_io = journal_end_buffer_io_sync;

	if (journal->j_flags & JBD2_BARRIER &&
	    !JBD2_HAS_INCOMPAT_FEATURE(journal,
				       JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT))
		ret = submit_bh(WRITE_SYNC | WRITE_FLUSH_FUA, bh);
	else
		ret = submit_bh(WRITE_SYNC, bh);
	*cbh = bh;

//    TIMESTAMP("END", "journal_submit_commit_record", "")
    return ret; 
}

/*
 * This function along with journal_submit_commit_record
 * allows to write the commit record asynchronously.
 */
static int journal_wait_on_commit_record(journal_t *journal,
					 struct buffer_head *bh)
{
	int ret = 0;

	clear_buffer_dirty(bh);
	wait_on_buffer(bh);

	if (unlikely(!buffer_uptodate(bh)))
		ret = -EIO;
	put_bh(bh);            /* One for getblk() */
	jbdbf_journal_put_journal_bf_head(bh2jhbf(bh));

	return ret;
}

/*
 * write the filemap data using writepage() address_space_operations.
 * We don't do block allocation here even for delalloc. We don't
 * use writepages() because with dealyed allocation we may be doing
 * block allocation in writepages().
 */
static int journal_submit_inode_data_buffers(struct address_space *mapping)
{
	int ret;
	struct writeback_control wbc = {
		.sync_mode =  WB_SYNC_ALL,
		.nr_to_write = mapping->nrpages * 2,
		.range_start = 0,
		.range_end = i_size_read(mapping->host),
	};

	ret = generic_writepages(mapping, &wbc);
	return ret;
}

/*
 * Submit all the data buffers of inode associated with the transaction to
 * disk.
 *
 * We are in a committing transaction. Therefore no new inode can be added to
 * our inode list. We use JI_COMMIT_RUNNING flag to protect inode we currently
 * operate on from being released while we write out pages.
 */
static int journal_submit_data_buffers(journal_t *journal,
		transaction_bf_t *commit_transaction)
{
	struct jbdbf_inode *jinode;
	int err, ret = 0;
	struct address_space *mapping;

	spin_lock(&journal->j_list_lock);
	list_for_each_entry(jinode, &commit_transaction->t_inode_list, i_list) {
		mapping = jinode->i_vfs_inode->i_mapping;
		set_bit(__JI_COMMIT_RUNNING, &jinode->i_flags);
		spin_unlock(&journal->j_list_lock);
		/*
		 * submit the inode data buffers. We use writepage
		 * instead of writepages. Because writepages can do
		 * block allocation  with delalloc. We need to write
		 * only allocated blocks here.
		 */
		err = journal_submit_inode_data_buffers(mapping);
		if (!ret)
			ret = err;
		spin_lock(&journal->j_list_lock);
		J_ASSERT(jinode->i_transaction == commit_transaction);
		clear_bit(__JI_COMMIT_RUNNING, &jinode->i_flags);
		smp_mb__after_clear_bit();
		wake_up_bit(&jinode->i_flags, __JI_COMMIT_RUNNING);
	}
	spin_unlock(&journal->j_list_lock);
	return ret;
}

/*
 * Wait for data submitted for writeout, refile inodes to proper
 * transaction if needed.
 *
 */
static int journal_finish_inode_data_buffers(journal_t *journal,
		transaction_bf_t *commit_transaction)
{
	struct jbdbf_inode *jinode, *next_i;
	int err, ret = 0;

	/* For locking, see the comment in journal_submit_data_buffers() */
	spin_lock(&journal->j_list_lock);
	list_for_each_entry(jinode, &commit_transaction->t_inode_list, i_list) {
		set_bit(__JI_COMMIT_RUNNING, &jinode->i_flags);
		spin_unlock(&journal->j_list_lock);
		err = filemap_fdatawait(jinode->i_vfs_inode->i_mapping);
		if (err) {
			/*
			 * Because AS_EIO is cleared by
			 * filemap_fdatawait_range(), set it again so
			 * that user process can get -EIO from fsync().
			 */
			set_bit(AS_EIO,
				&jinode->i_vfs_inode->i_mapping->flags);

			if (!ret)
				ret = err;
		}
		spin_lock(&journal->j_list_lock);
		clear_bit(__JI_COMMIT_RUNNING, &jinode->i_flags);
		smp_mb__after_clear_bit();
		wake_up_bit(&jinode->i_flags, __JI_COMMIT_RUNNING);
	}

	/* Now refile inode to proper lists */
	list_for_each_entry_safe(jinode, next_i,
				 &commit_transaction->t_inode_list, i_list) {
		list_del(&jinode->i_list);
		if (jinode->i_next_transaction) {
			jinode->i_transaction = jinode->i_next_transaction;
			jinode->i_next_transaction = NULL;
			list_add(&jinode->i_list,
				&jinode->i_transaction->t_inode_list);
		} else {
			jinode->i_transaction = NULL;
		}
	}
	spin_unlock(&journal->j_list_lock);

	return ret;
}

/*736  : Calculates the checksum of the buffer head*/
__u32 jbdbf_checksum_data(__u32 crc32_sum, struct buffer_head *bh)
{
	struct page *page = bh->b_page;
	char *addr;
	__u32 checksum;
#if OPT_CHECKSUM_736
//736 Trying to optimize checksum. hah!
    return 0;
#endif
	addr = kmap_atomic(page, KM_USER0);
#if OPT_CHECKSUM_FLETCHER
    //checksum = adler32(crc32_sum, (void *)(addr + offset_in_page(bh->b_data)), bh->b_size);
    checksum = fletcher32(crc32_sum,		(void *)(addr + offset_in_page(bh->b_data)), bh->b_size);
#else
    checksum = crc32_be(crc32_sum,		(void *)(addr + offset_in_page(bh->b_data)), bh->b_size);
    
#endif
	kunmap_atomic(addr, KM_USER0);
	return checksum;
}

static void write_tag_block(int tag_bytes, journal_block_tag_t *tag,
				   unsigned long long block, __u32 data_checksum, __u32 block_type)
{
	tag->t_blocknr = cpu_to_be32(block & (u32)~0);
	if (tag_bytes > JBD2_TAG_SIZE32) {
		tag->t_blocknr_high = cpu_to_be32((block >> 31) >> 1);
		/* ext4bf: write the checksum into the tag; */
		tag->t_chksum_type 	= JBD2_CRC32_CHKSUM;
		tag->t_chksum_size 	= JBD2_CRC32_CHKSUM_SIZE;
		tag->t_chksum[0] 	= cpu_to_be32(data_checksum & (u32)~0);
		tag->t_blocktype    = cpu_to_be32(block_type & (u32)~0);
    }
}

struct buffer_head	*j_data_bhs[EXT4BF_DATA_BATCH];
/* ext4bf: routine to write out data blocks listed in t_forget list of each
 * transactions. Mirrors __flush_batch from checkpoint.c
 */
static void
__flush_data_batch(int *batch_count)
{
	int i;

#if PLUG_736
	struct blk_plug plug;
	blk_start_plug(&plug);
#endif

	for (i = 0; i < *batch_count; i++)
		write_dirty_buffer(j_data_bhs[i], WRITE_SYNC);
#if PLUG_736
	blk_finish_plug(&plug);
#endif

	for (i = 0; i < *batch_count; i++) {
		struct buffer_head *bh = j_data_bhs[i];
		clear_buffer_jwrite(bh);
		BUFFER_TRACE(bh, "brelse");
		__brelse(bh);
	}
	*batch_count = 0;
}

/*
 * jbdbf_journal_commit_transaction
 *
 * The primary function for committing a transaction to the log.  This
 * function is called by the journal thread to begin a complete commit.
 */
void jbdbf_journal_commit_transaction(journal_t *journal)
{
    struct transaction_bf_stats_s stats;
	transaction_bf_t *commit_transaction;
	struct journal_bf_head *jh, *new_jh, *descriptor;
	struct buffer_head **wbuf = journal->j_wbuf;
	int bufs;
	int flags;
	int err;
	unsigned long long blocknr;
	ktime_t start_time;
	u64 commit_time;
	char *tagp = NULL;
	journal_bf_header_t *header;
	journal_block_tag_t *tag = NULL;
	int space_left = 0;
	int first_tag = 0;
	int tag_flag;
	int i, to_free = 0;
	int tag_bytes = journal_tag_bytes(journal);
	struct buffer_head *cbh = NULL; /* For transactional checksums */
	__u32 crc32_sum = ~0;
	__u32 crc32_data_sum = ~0;
#if PLUG_736
	struct blk_plug plug;
#endif

	/*
	 * First job: lock down the current transaction and wait for
	 * all outstanding updates to complete.
	 */

	/* Do we need to erase the effects of a prior jbdbf_journal_flush? */
    TIMESTAMP("START", "Phase 1","");
	if (journal->j_flags & JBD2_FLUSHED) {
		jbd_debug(3, "super block updated\n");
		jbdbf_journal_update_superblock(journal, 1);
	} else {
		jbd_debug(3, "superblock not updated\n");
	}

	J_ASSERT(journal->j_running_transaction != NULL);
	J_ASSERT(journal->j_committing_transaction == NULL);

	commit_transaction = journal->j_running_transaction;
	J_ASSERT(commit_transaction->t_state == T_RUNNING);

    int durable_commit = commit_transaction->t_durable_commit;

    mutex_lock(&commit_transaction->t_dirty_data_mutex);
	jbd_debug(1, "JBD2: starting commit of transaction %d\n",
			commit_transaction->t_tid);

	write_lock(&journal->j_state_lock);
	commit_transaction->t_state = T_LOCKED;

	stats.run.rs_wait = commit_transaction->t_max_wait;
	stats.run.rs_locked = jiffies;
	stats.run.rs_running = jbdbf_time_diff(commit_transaction->t_start,
					      stats.run.rs_locked);

	spin_lock(&commit_transaction->t_handle_lock);
	while (atomic_read(&commit_transaction->t_updates)) {
		DEFINE_WAIT(wait);

		prepare_to_wait(&journal->j_wait_updates, &wait,
					TASK_UNINTERRUPTIBLE);
		if (atomic_read(&commit_transaction->t_updates)) {
			spin_unlock(&commit_transaction->t_handle_lock);
			write_unlock(&journal->j_state_lock);
			schedule();
			write_lock(&journal->j_state_lock);
			spin_lock(&commit_transaction->t_handle_lock);
		}
		finish_wait(&journal->j_wait_updates, &wait);
	}
	spin_unlock(&commit_transaction->t_handle_lock);

	J_ASSERT (atomic_read(&commit_transaction->t_outstanding_credits) <=
			journal->j_max_transaction_buffers);

	/*
	 * First thing we are allowed to do is to discard any remaining
	 * BJ_Reserved buffers.  Note, it is _not_ permissible to assume
	 * that there are no such buffers: if a large filesystem
	 * operation like a truncate needs to split itself over multiple
	 * transactions, then it may try to do a jbdbf_journal_restart() while
	 * there are still BJ_Reserved buffers outstanding.  These must
	 * be released cleanly from the current transaction.
	 *
	 * In this case, the filesystem must still reserve write access
	 * again before modifying the buffer in the new transaction, but
	 * we do not require it to remember exactly which old buffers it
	 * has reserved.  This is consistent with the existing behaviour
	 * that multiple jbdbf_journal_get_write_access() calls to the same
	 * buffer are perfectly permissible.
	 */
	while (commit_transaction->t_reserved_list) {
		jh = commit_transaction->t_reserved_list;
		JBUFFER_TRACE(jh, "reserved, unused: refile");
		/*
		 * A jbdbf_journal_get_undo_access()+jbdbf_journal_release_buffer() may
		 * leave undo-committed data.
		 */
		if (jh->b_committed_data) {
			struct buffer_head *bh = jh2bhbf(jh);

			jbdbf_lock_bh_state(bh);
			jbdbf_free(jh->b_committed_data, bh->b_size);
			jh->b_committed_data = NULL;
			jbdbf_unlock_bh_state(bh);
		}
		jbdbf_journal_refile_buffer(journal, jh);
	}

	/*
	 * Now try to drop any written-back buffers from the journal's
	 * checkpoint lists.  We do this *before* commit because it potentially
	 * frees some memory
	 */
	spin_lock(&journal->j_list_lock);
	__jbdbf_journal_clean_checkpoint_list(journal);
	spin_unlock(&journal->j_list_lock);

    TIMESTAMP("END", "phase 1","");
    TIMESTAMP("START", "phase 2","");
	jbd_debug(3, "JBD2: commit phase 1\n");

	/*
	 * Switch to a new revoke table.
	 */
	jbdbf_journal_switch_revoke_table(journal);

	stats.run.rs_flushing = jiffies;
	stats.run.rs_locked = jbdbf_time_diff(stats.run.rs_locked,
					     stats.run.rs_flushing);

	commit_transaction->t_state = T_FLUSH;
	journal->j_committing_transaction = commit_transaction;
	journal->j_running_transaction = NULL;
	start_time = ktime_get();
	commit_transaction->t_log_start = journal->j_head;
	wake_up(&journal->j_wait_transaction_locked);
	write_unlock(&journal->j_state_lock);
    
    TIMESTAMP("END", "phase 2","");
   TIMESTAMP("START", "phase 3","");

	jbd_debug(3, "JBD2: commit phase 2\n");

#ifdef DCHECKSUM
    /* ext4bf: attempt to read the data blocks inside the t_forget list of the
     * the current transaction. */
        
    jh = commit_transaction->t_dirty_data_list;
    int data_batch_count = 0;
    struct journal_bf_head *jh_next;
    jbd_debug(6, "EXT4BF: Starting to issue the data blocks: %lu\n", commit_transaction->t_num_dirty_blocks);

    /* List of buffer heads to submit. */
    while(1) {
        if (!jh) {
            break;
        }

        struct buffer_head *bh = jh2bhbf(jh);
        if (!bh) break;
        
        if (bh->b_blocktype == B_BLOCKTYPE_DATA){
            /* Process the data buffer. */
            get_bh(bh);
            set_buffer_jwrite(bh);
            j_data_bhs[data_batch_count++] = bh;
            if (data_batch_count == EXT4BF_DATA_BATCH) {
                __flush_data_batch(&data_batch_count);
            }
        }
        /* If we are looping back, break */
        if (jh->b_tnext == commit_transaction->t_dirty_data_list) {
            /* We're done; flush remaining buffers and exit. */
            if (data_batch_count) {
                __flush_data_batch(&data_batch_count);
            }
            if (bh->b_blocktype != B_BLOCKTYPE_DATA)
                jbdbf_journal_refile_buffer(journal, jh);
            break;
        }
        jh_next = jh->b_tnext;
        /* Don't refile journal heads which are type 1. We will check for them
         * later.*/
        if (bh->b_blocktype != B_BLOCKTYPE_DATA)
            jbdbf_journal_refile_buffer(journal, jh);
        jh = jh_next;
    }
    jbd_debug(6, "EXT4BF: Ending the issue of data blocks\n");
#endif

    TIMESTAMP("END", "phase 3","");
    TIMESTAMP("START", "phase 4","");
	/*
	 * Now start flushing things to disk, in the order they appear
	 * on the transaction lists.  Data blocks go first.
	 */
	err = journal_submit_data_buffers(journal, commit_transaction);
	if (err){
	    jbd_debug(6, "EXT4BF: aborting journal because of errors in journal_submit_inode_data_buffers");
		jbdbf_journal_abort(journal, err);
    }

#if PLUG_736
    blk_start_plug(&plug);
#endif
	jbdbf_journal_write_revoke_records(journal, commit_transaction,
					  WRITE_SYNC);
#if PLUG_736
    blk_finish_plug(&plug);
#endif

	jbd_debug(3, "JBD2: commit phase 2\n");

    TIMESTAMP("END", "phase 4","");
	/*
	 * Way to go: we have now written out all of the data for a
	 * transaction!  Now comes the tricky part: we need to write out
	 * metadata.  Loop over the transaction's entire buffer list:
	 */
	write_lock(&journal->j_state_lock);
	commit_transaction->t_state = T_COMMIT;
	write_unlock(&journal->j_state_lock);

	stats.run.rs_logging = jiffies;
	stats.run.rs_flushing = jbdbf_time_diff(stats.run.rs_flushing,
					       stats.run.rs_logging);
	stats.run.rs_blocks =
		atomic_read(&commit_transaction->t_outstanding_credits);
	stats.run.rs_blocks_logged = 0;

	J_ASSERT(commit_transaction->t_nr_buffers <=
		 atomic_read(&commit_transaction->t_outstanding_credits));

	err = 0;
	descriptor = NULL;
	bufs = 0;
#if PLUG_736
	blk_start_plug(&plug);
#endif
	while (commit_transaction->t_buffers) {

		/* Find the next buffer to be journaled... */

		jh = commit_transaction->t_buffers;

        if (jh2bhbf(jh)) 
            jbd_debug(6, "EXT4BF: inside t_buffers block %lu\n", (jh2bhbf(jh))->b_blocknr);

		/* If we're in abort mode, we just un-journal the buffer and
		   release it. */

		if (is_journal_aborted(journal)) {
			clear_buffer_jbddirty(jh2bhbf(jh));
			JBUFFER_TRACE(jh, "journal is aborting: refile");
			jbdbf_buffer_abort_trigger(jh,
						  jh->b_frozen_data ?
						  jh->b_frozen_triggers :
						  jh->b_triggers);
			jbdbf_journal_refile_buffer(journal, jh);
			/* If that was the last one, we need to clean up
			 * any descriptor buffers which may have been
			 * already allocated, even if we are now
			 * aborting. */
			if (!commit_transaction->t_buffers)
				goto start_journal_io;
			continue;
		}

		/* Make sure we have a descriptor block in which to
		   record the metadata buffer. */

		if (!descriptor) {
		        TIMESTAMP("START", "phase 5","1");
		        TIMESTAMP1("START", "phase 5","1A");
			struct buffer_head *bh;

			J_ASSERT (bufs == 0);

			jbd_debug(4, "JBD2: get descriptor\n");

			descriptor = jbdbf_journal_get_descriptor_buffer(journal);
			if (!descriptor) {
			    jbd_debug(6, "EXT4BF: aborting because we couldn't get space for desc block.");
				jbdbf_journal_abort(journal, -EIO);
				continue;
			}

            TIMESTAMP1("END", "phase 5","1A");
            TIMESTAMP1("START", "phase 5","1B");
           	 
			bh = jh2bhbf(descriptor);
            TIMESTAMP1("END", "phase 5","1B");
            TIMESTAMP1("START", "phase 5","1C");
			jbd_debug(4, "JBD2: got buffer %llu (%p)\n",
				(unsigned long long)bh->b_blocknr, bh->b_data);
			header = (journal_bf_header_t *)&bh->b_data[0];
			header->h_magic     = cpu_to_be32(JBD2_MAGIC_NUMBER);
			header->h_blocktype = cpu_to_be32(JBD2_DESCRIPTOR_BLOCK);
			header->h_sequence  = cpu_to_be32(commit_transaction->t_tid);

			tagp = &bh->b_data[sizeof(journal_bf_header_t)];
			space_left = bh->b_size - sizeof(journal_bf_header_t);
			first_tag = 1;
			set_buffer_jwrite(bh);
			set_buffer_dirty(bh);
			wbuf[bufs++] = bh;

			/* Record it so that we can wait for IO
                           completion later */
			BUFFER_TRACE(bh, "ph3: file as descriptor");
			jbdbf_journal_file_buffer(descriptor, commit_transaction,
					BJ_LogCtl);
            TIMESTAMP1("END", "phase 5","1C");
#ifdef DCHECKSUM
            TIMESTAMP1("START", "phase 5","1D");
			/* EXT4BF */
            /* Add the data tags to the descriptor. */
            struct jbdbf_data_tag *entry;
            struct list_head *l, *ltmp;

            list_for_each_safe(l, ltmp, &commit_transaction->t_data_tag_list) {
                entry = list_entry(l, struct jbdbf_data_tag, list);
                jbd_debug(6, "EXT4BF: data tag blocknr: %lu\n", entry->b_blocknr);
                jbd_debug(6, "EXT4BF: data tag checksum: %u\n", entry->crc32_data_sum);

                if (space_left < tag_bytes + 16) goto done_with_tags;
                /* Write tags out */
                tag_flag = 0;
                if (flags & 1)
                    tag_flag |= JBD2_FLAG_ESCAPE;
                if (!first_tag)
                    tag_flag |= JBD2_FLAG_SAME_UUID;

                tag = (journal_block_tag_t *) tagp;
                write_tag_block(tag_bytes, tag, entry->b_blocknr,
                        entry->crc32_data_sum, T_BLOCKTYPE_NEWLYAPPENDEDDATA);
                tag->t_flags = cpu_to_be32(tag_flag);
                tagp += tag_bytes;
                space_left -= tag_bytes;
                if (first_tag) {
                    memcpy (tagp, journal->j_uuid, 16);
                    tagp += 16;
                    space_left -= 16;
                    first_tag = 0;
                }
                list_del(l);
                jbdbf_free_data_tag(entry);
            }
            TIMESTAMP1("END", "phase 5","1D");
#endif
        TIMESTAMP("END", "5","1");
        }
        /* Where is the buffer to be written? */

        jbd_debug(6, "EXT4BF: processing a metadata block\n");

        TIMESTAMP("START", "phase 5","2");
        /* ext4bf: continue with normal procesing. */
		err = jbdbf_journal_next_log_block(journal, &blocknr);
		
		/* If the block mapping failed, just abandon the buffer
		   and repeat this loop: we'll fall into the
		   refile-on-abort condition above. */
		if (err) {
		    jbd_debug(6, "EXT4BF: aborting because of error in getting next log block.");
			jbdbf_journal_abort(journal, err);
		}

		/*
		 * start_this_handle() uses t_outstanding_credits to determine
		 * the free space in the log, but this counter is changed
		 * by jbdbf_journal_next_log_block() also.
		 */
		atomic_dec(&commit_transaction->t_outstanding_credits);

		/* Bump b_count to prevent truncate from stumbling over
                   the shadowed buffer!  @@@ This can go if we ever get
                   rid of the BJ_IO/BJ_Shadow pairing of buffers. */
		atomic_inc(&jh2bhbf(jh)->b_count);

		/* Make a temporary IO buffer with which to write it out
                   (this will requeue both the metadata buffer and the
                   temporary IO buffer). new_bh goes on BJ_IO*/

		set_bit(BH_JWrite, &jh2bhbf(jh)->b_state);
		/*
		 * akpm: jbdbf_journal_write_metadata_buffer() sets
		 * new_bh->b_transaction to commit_transaction.
		 * We need to clean this up before we release new_bh
		 * (which is of type BJ_IO)
		 */
		JBUFFER_TRACE(jh, "ph3: write metadata");
		flags = jbdbf_journal_write_metadata_buffer(commit_transaction,
						      jh, &new_jh, blocknr);
		if (flags < 0) {
		    jbd_debug(6, "EXT4BF: aborting because of error in journal_write_metadata_buffer");
			jbdbf_journal_abort(journal, flags);
			continue;
		}
		set_bit(BH_JWrite, &jh2bhbf(new_jh)->b_state);
		wbuf[bufs++] = jh2bhbf(new_jh);

		/* Record the new block's tag in the current descriptor
                   buffer */

		tag_flag = 0;
		if (flags & 1)
			tag_flag |= JBD2_FLAG_ESCAPE;
		if (!first_tag)
			tag_flag |= JBD2_FLAG_SAME_UUID;

        tag = (journal_block_tag_t *) tagp;
		if (jh2bhbf(jh)->b_blocktype == B_BLOCKTYPE_DATA)
			write_tag_block(tag_bytes, tag, jh2bhbf(jh)->b_blocknr, 0, T_BLOCKTYPE_OVERWRITTENDATA);
		else
			write_tag_block(tag_bytes, tag, jh2bhbf(jh)->b_blocknr, 0, T_BLOCKTYPE_NOTDATA);
        tag->t_flags = cpu_to_be32(tag_flag);
        tagp += tag_bytes;
        space_left -= tag_bytes;

        if (first_tag) {
            memcpy (tagp, journal->j_uuid, 16);
            tagp += 16;
            space_left -= 16;
            first_tag = 0;
        }
        jbd_debug(6, "EXT4BF: finished writing tags.");

        TIMESTAMP("END", "phase 5","2");
        /* ext4bf: continue with normal procesing. */
		/* If there's no more to do, or if the descriptor is full,
		   let the IO rip! */
done_with_tags:

        TIMESTAMP("START","phase 5","3");
        /* ext4bf: continue with normal procesing. */
        jbd_debug(6, "EXT4BF: gonna submit the I/Os\n");
		if (bufs == journal->j_wbufsize ||
		    commit_transaction->t_buffers == NULL ||
		    space_left < tag_bytes + 16) {
            TIMESTAMP1("START", "phase 5","3A");
			jbd_debug(4, "JBD2: Submit %d IOs\n", bufs);

			/* Write an end-of-descriptor marker before
                           submitting the IOs.  "tag" still points to
                           the last tag we set up. */

			tag->t_flags |= cpu_to_be32(JBD2_FLAG_LAST_TAG);
            TIMESTAMP1("END", "phase 5","3A");
start_journal_io:
			for (i = 0; i < bufs; i++) {
				struct buffer_head *bh = wbuf[i];
				/*
				 * Compute checksum.
				 */
			        TIMESTAMP1("START", "phase 5, 3B",i);
				if (JBD2_HAS_COMPAT_FEATURE(journal,
					JBD2_FEATURE_COMPAT_CHECKSUM)) {
					crc32_sum =
					    jbdbf_checksum_data(crc32_sum, bh);
				}
			        TIMESTAMP1("END", "phase 5, 3B",i);
			        TIMESTAMP1("START", "phase 5, 3C",i);
				lock_buffer(bh);
				clear_buffer_dirty(bh);
				set_buffer_uptodate(bh);
				bh->b_end_io = journal_end_buffer_io_sync;
				submit_bh(WRITE_SYNC, bh);
			        TIMESTAMP1("END", "phase 5, 3C",i);
			}
			cond_resched();
			stats.run.rs_blocks_logged += bufs;

			/* Force a new descriptor to be generated next
                           time round the loop. */
			descriptor = NULL;
			bufs = 0;
		}
	}

        TIMESTAMP1("START", "phase 5","3D");
	err = journal_finish_inode_data_buffers(journal, commit_transaction);
	if (err) {
		printk(KERN_WARNING
			"JBD2: Detected IO errors while flushing file data "
		       "on %s\n", journal->j_devname);
		if (journal->j_flags & JBD2_ABORT_ON_SYNCDATA_ERR)
			jbdbf_journal_abort(journal, err);
		err = 0;
	}
        TIMESTAMP1("END", "phase 5","3D");
    TIMESTAMP("END", "phase 5","3");
wait_for_data:
    /* ext4bf: Wait for previous I/O to complete.*/
    TIMESTAMP("START", "phase 5","4");
    while (commit_transaction->t_dirty_data_list) {
        struct buffer_head *bh;

        jh = commit_transaction->t_dirty_data_list->b_tprev;
        bh = jh2bhbf(jh);
        jbd_debug(6, "EXT4BF: waiting for write of data block %lu\n", bh->b_blocknr);

        if (buffer_locked(bh)) {
            wait_on_buffer(bh);
            goto wait_for_data;
        }
        if (cond_resched())
            goto wait_for_data;

        if (unlikely(!buffer_uptodate(bh)))
            err = -EIO;

        clear_buffer_jwrite(bh);

        JBUFFER_TRACE(jh, "ph4: unfile after journal write");
        jbdbf_journal_refile_buffer(journal, jh);
    }

	write_lock(&journal->j_state_lock);
	J_ASSERT(commit_transaction->t_state == T_COMMIT);
	commit_transaction->t_state = T_COMMIT_DFLUSH;
	write_unlock(&journal->j_state_lock);
    TIMESTAMP("END", "phase 5","4");
    TIMESTAMP("START", "phase 5","5");
    TIMESTAMP1("START", "phase 5","5A");
	/* 
	 * If the journal is not located on the file system device,
	 * then we must flush the file system device before we issue
	 * the commit record
	 */
	if (commit_transaction->t_need_data_flush &&
	    (journal->j_fs_dev != journal->j_dev) &&
	    (journal->j_flags & JBD2_BARRIER))
		blkdev_issue_flush(journal->j_fs_dev, GFP_KERNEL, NULL);

    TIMESTAMP1("END", "phase 5","5A");
    TIMESTAMP1("START", "phase 5","5B");
	/* Done it all: now write the commit record asynchronously. */
	if (JBD2_HAS_INCOMPAT_FEATURE(journal,
				      JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT)) {
		err = journal_submit_commit_record(journal, commit_transaction,
						 &cbh, crc32_sum);
		if (err)
			__jbdbf_journal_abort_hard(journal);
	}

#if PLUG_736
    blk_finish_plug(&plug);
#endif
    TIMESTAMP1("END", "phase 5","5B");
    TIMESTAMP("END", "phase 5","5");
    TIMESTAMP("START", "phase 5","6");
    

	/* Lo and behold: we have just managed to send a transaction to
           the log.  Before we can commit it, wait for the IO so far to
           complete.  Control buffers being written are on the
           transaction's t_log_list queue, and metadata buffers are on
           the t_iobuf_list queue.

	   Wait for the buffers in reverse order.  That way we are
	   less likely to be woken up until all IOs have completed, and
	   so we incur less scheduling load.
	*/

	jbd_debug(3, "JBD2: commit phase 3\n");

	/*
	 * akpm: these are BJ_IO, and j_list_lock is not needed.
	 * See __journal_try_to_free_buffer.
	 */
wait_for_iobuf:

    TIMESTAMP("START", "phase 5","6");
	while (commit_transaction->t_iobuf_list != NULL) {
		struct buffer_head *bh;

		jh = commit_transaction->t_iobuf_list->b_tprev;
		bh = jh2bhbf(jh);
        jbd_debug(6, "EXT4BF: waiting for write of journal block %lu\n", bh->b_blocknr);

		if (buffer_locked(bh)) {
			wait_on_buffer(bh);
			goto wait_for_iobuf;
		}
		if (cond_resched())
			goto wait_for_iobuf;

		if (unlikely(!buffer_uptodate(bh)))
			err = -EIO;

		clear_buffer_jwrite(bh);

		JBUFFER_TRACE(jh, "ph4: unfile after journal write");
		jbdbf_journal_unfile_buffer(journal, jh);

		/*
		 * ->t_iobuf_list should contain only dummy buffer_heads
		 * which were created by jbdbf_journal_write_metadata_buffer().
		 */
		BUFFER_TRACE(bh, "dumping temporary bh");
		jbdbf_journal_put_journal_bf_head(jh);
		__brelse(bh);
		J_ASSERT_BH(bh, atomic_read(&bh->b_count) == 0);
		free_buffer_head(bh);

		/* We also have to unlock and free the corresponding
                   shadowed buffer */
		jh = commit_transaction->t_shadow_list->b_tprev;
		bh = jh2bhbf(jh);
		clear_bit(BH_JWrite, &bh->b_state);
		J_ASSERT_BH(bh, buffer_jbddirty(bh));

		/* The metadata is now released for reuse, but we need
                   to remember it against this transaction so that when
                   we finally commit, we can do any checkpointing
                   required. */
		JBUFFER_TRACE(jh, "file as BJ_Forget");
		jbdbf_journal_file_buffer(jh, commit_transaction, BJ_Forget);
		/*
		 * Wake up any transactions which were waiting for this IO to
		 * complete. The barrier must be here so that changes by
		 * jbdbf_journal_file_buffer() take effect before wake_up_bit()
		 * does the waitqueue check.
		 */
		smp_mb();
		wake_up_bit(&bh->b_state, BH_Unshadow);
		JBUFFER_TRACE(jh, "brelse shadowed buffer");
		__brelse(bh);
	}

    TIMESTAMP("END", "phase 5","6");
	J_ASSERT (commit_transaction->t_shadow_list == NULL);

	jbd_debug(3, "JBD2: commit phase 4\n");

	/* Here we wait for the revoke record and descriptor record buffers */
 wait_for_ctlbuf:
    TIMESTAMP("START", "phase 5","7");
	while (commit_transaction->t_log_list != NULL) {
		struct buffer_head *bh;

		jh = commit_transaction->t_log_list->b_tprev;
		bh = jh2bhbf(jh);
        jbd_debug(6, "EXT4BF: waiting for write of de/re block %lu\n", bh->b_blocknr);
		if (buffer_locked(bh)) {
			wait_on_buffer(bh);
			goto wait_for_ctlbuf;
		}
		if (cond_resched())
			goto wait_for_ctlbuf;

        jbd_debug(6, "EXT4BF: checking block type %lu\n", bh->b_blocktype);
		if (unlikely(!buffer_uptodate(bh)))
			err = -EIO;

		BUFFER_TRACE(bh, "ph5: control buffer writeout done: unfile");
		clear_buffer_jwrite(bh);
		jbdbf_journal_unfile_buffer(journal, jh);
		jbdbf_journal_put_journal_bf_head(jh);
		__brelse(bh);		/* One for getblk */
		/* AKPM: bforget here */
	}

    if (err) { 
        jbd_debug(6, "EXT4BF: aborting because of error in writing journal log blocks.");
        jbdbf_journal_abort(journal, err);
    }

	jbd_debug(3, "JBD2: commit phase 5\n");
	write_lock(&journal->j_state_lock);
	J_ASSERT(commit_transaction->t_state == T_COMMIT_DFLUSH);
	commit_transaction->t_state = T_COMMIT_JFLUSH;
	write_unlock(&journal->j_state_lock);

	if (!JBD2_HAS_INCOMPAT_FEATURE(journal,
				       JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT)) {
		err = journal_submit_commit_record(journal, commit_transaction,
						&cbh, crc32_sum);
		if (err)
			__jbdbf_journal_abort_hard(journal);
	}
	if (cbh)
		err = journal_wait_on_commit_record(journal, cbh);

	if ((JBD2_HAS_INCOMPAT_FEATURE(journal,
				      JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT) &&
	    journal->j_flags & JBD2_BARRIER)
	    || (durable_commit == 1))
	{
		blkdev_issue_flush(journal->j_dev, GFP_KERNEL, NULL);
	}

    if (err) {
        jbd_debug(6, "EXT4BF: aborting because of error in writing commit record.");
        jbdbf_journal_abort(journal, err);
    }

	/* End of a transaction!  Finally, we can do checkpoint
           processing: any buffers committed as a result of this
           transaction can be removed from any checkpoint list it was on
           before. */

	jbd_debug(3, "JBD2: commit phase 6\n");

	J_ASSERT(list_empty(&commit_transaction->t_inode_list));
	J_ASSERT(commit_transaction->t_buffers == NULL);
	J_ASSERT(commit_transaction->t_checkpoint_list == NULL);
	J_ASSERT(commit_transaction->t_iobuf_list == NULL);
	J_ASSERT(commit_transaction->t_shadow_list == NULL);
	J_ASSERT(commit_transaction->t_log_list == NULL);
   
    TIMESTAMP("END", "phase 5","7");

    /* ext4bf: set checkpoint time for the whole transaction. */
    if (durable_commit == 1) {
        commit_transaction->t_checkpoint_time = jiffies; 
    } else {
        commit_transaction->t_checkpoint_time = jiffies
            + msecs_to_jiffies(JBDBF_CHECKPOINT_INTERVAL); 
    }

restart_loop:
    TIMESTAMP("START", "phase 6","");

	/*
	 * As there are other places (journal_unmap_buffer()) adding buffers
	 * to this list we have to be careful and hold the j_list_lock.
	 */
	spin_lock(&journal->j_list_lock);
	while (commit_transaction->t_forget) {
		transaction_bf_t *cp_transaction;
		struct buffer_head *bh;
		int try_to_free = 0;

		jh = commit_transaction->t_forget;
		spin_unlock(&journal->j_list_lock);
		bh = jh2bhbf(jh);

		/*
		 * Get a reference so that bh cannot be freed before we are
		 * done with it.
		 */
		get_bh(bh);
		jbdbf_lock_bh_state(bh);
		J_ASSERT_JH(jh,	jh->b_transaction == commit_transaction);

        /* ext4bf: tagging the block so that it will not be written by the VM
         * subsystem. The VM subsystem will write this out after the checkpoint
         * time embedded in the block. */
        if (durable_commit != 1) {
            bh->b_blocktype = B_BLOCKTYPE_DURABLECHECKPOINT;
            bh->b_checkpoint_time = jiffies + msecs_to_jiffies(JBDBF_CHECKPOINT_INTERVAL); 
            bh->b_delayed_write = 1;
        }

		/*
		 * If there is undo-protected committed data against
		 * this buffer, then we can remove it now.  If it is a
		 * buffer needing such protection, the old frozen_data
		 * field now points to a committed version of the
		 * buffer, so rotate that field to the new committed
		 * data.
		 *
		 * Otherwise, we can just throw away the frozen data now.
		 *
		 * We also know that the frozen data has already fired
		 * its triggers if they exist, so we can clear that too.
		 */
		if (jh->b_committed_data) {
			jbdbf_free(jh->b_committed_data, bh->b_size);
			jh->b_committed_data = NULL;
			if (jh->b_frozen_data) {
				jh->b_committed_data = jh->b_frozen_data;
				jh->b_frozen_data = NULL;
				jh->b_frozen_triggers = NULL;
			}
		} else if (jh->b_frozen_data) {
			jbdbf_free(jh->b_frozen_data, bh->b_size);
			jh->b_frozen_data = NULL;
			jh->b_frozen_triggers = NULL;
		}

		spin_lock(&journal->j_list_lock);
		cp_transaction = jh->b_cp_transaction;
		if (cp_transaction) {
			JBUFFER_TRACE(jh, "remove from old cp transaction");
			cp_transaction->t_chp_stats.cs_dropped++;
			__jbdbf_journal_remove_checkpoint(jh);
		}

		/* Only re-checkpoint the buffer_head if it is marked
		 * dirty.  If the buffer was added to the BJ_Forget list
		 * by jbdbf_journal_forget, it may no longer be dirty and
		 * there's no point in keeping a checkpoint record for
		 * it. */

		/* A buffer which has been freed while still being
		 * journaled by a previous transaction may end up still
		 * being dirty here, but we want to avoid writing back
		 * that buffer in the future after the "add to orphan"
		 * operation been committed,  That's not only a performance
		 * gain, it also stops aliasing problems if the buffer is
		 * left behind for writeback and gets reallocated for another
		 * use in a different page. */
		if (buffer_freed(bh) && !jh->b_next_transaction) {
			clear_buffer_freed(bh);
			clear_buffer_jbddirty(bh);
		}

		if (buffer_jbddirty(bh)) {
			JBUFFER_TRACE(jh, "add to new checkpointing trans");
			__jbdbf_journal_insert_checkpoint(jh, commit_transaction);
			if (is_journal_aborted(journal))
                clear_buffer_jbddirty(bh);
        } else {
            J_ASSERT_BH(bh, !buffer_dirty(bh));
                /*
                 * The buffer on BJ_Forget list and not jbddirty means
                 * it has been freed by this transaction and hence it
                 * could not have been reallocated until this
                 * transaction has committed. *BUT* it could be
                 * reallocated once we have written all the data to
                 * disk and before we process the buffer on BJ_Forget
                 * list.
                 */
            if (!jh->b_next_transaction)
                try_to_free = 1;
        }
        JBUFFER_TRACE(jh, "refile or unfile buffer");
        __jbdbf_journal_refile_buffer(jh);
        jbdbf_unlock_bh_state(bh);
        if (try_to_free)
            release_buffer_page(bh);    /* Drops bh reference */
        else
            __brelse(bh);
        cond_resched_lock(&journal->j_list_lock);
    }
    spin_unlock(&journal->j_list_lock);
 
    /*
     * 
     * This is a bit sleazy.  We use j_list_lock to protect transition
     * of a transaction into T_FINISHED state and calling
     * __jbdbf_journal_drop_transaction(). Otherwise we could race with
     * other checkpointing code processing the transaction...
     */
	write_lock(&journal->j_state_lock);
	spin_lock(&journal->j_list_lock);

	/*
	 * Now recheck if some buffers did not get attached to the transaction
	 * while the lock was dropped...
	 */
	if (commit_transaction->t_forget) {
		spin_unlock(&journal->j_list_lock);
		write_unlock(&journal->j_state_lock);
		goto restart_loop;
	}
    TIMESTAMP("END", "phase 6","");

	/* Done with this transaction! */

	jbd_debug(3, "JBD2: commit phase 7\n");

	J_ASSERT(commit_transaction->t_state == T_COMMIT_JFLUSH);

	commit_transaction->t_start = jiffies;
	stats.run.rs_logging = jbdbf_time_diff(stats.run.rs_logging,
					      commit_transaction->t_start);

	/*
	 * File the transaction statistics
	 */
	stats.ts_tid = commit_transaction->t_tid;
	stats.run.rs_handle_count =
		atomic_read(&commit_transaction->t_handle_count);

	/*
	 * Calculate overall stats
	 */
	spin_lock(&journal->j_history_lock);
	journal->j_stats.ts_tid++;
	journal->j_stats.run.rs_wait += stats.run.rs_wait;
	journal->j_stats.run.rs_running += stats.run.rs_running;
	journal->j_stats.run.rs_locked += stats.run.rs_locked;
	journal->j_stats.run.rs_flushing += stats.run.rs_flushing;
	journal->j_stats.run.rs_logging += stats.run.rs_logging;
	journal->j_stats.run.rs_handle_count += stats.run.rs_handle_count;
	journal->j_stats.run.rs_blocks += stats.run.rs_blocks;
	journal->j_stats.run.rs_blocks_logged += stats.run.rs_blocks_logged;
	spin_unlock(&journal->j_history_lock);

	commit_transaction->t_state = T_FINISHED;
	J_ASSERT(commit_transaction == journal->j_committing_transaction);
	journal->j_commit_sequence = commit_transaction->t_tid;
	journal->j_committing_transaction = NULL;
	commit_time = ktime_to_ns(ktime_sub(ktime_get(), start_time));

	/*
	 * weight the commit time higher than the average time so we don't
	 * react too strongly to vast changes in the commit time
	 */
	if (likely(journal->j_average_commit_time))
		journal->j_average_commit_time = (commit_time +
				journal->j_average_commit_time*3) / 4;
	else
		journal->j_average_commit_time = commit_time;
	write_unlock(&journal->j_state_lock);

	if (commit_transaction->t_checkpoint_list == NULL &&
	    commit_transaction->t_checkpoint_io_list == NULL) {
		__jbdbf_journal_drop_transaction(journal, commit_transaction);
		to_free = 1;
	} else {
		if (journal->j_checkpoint_transactions == NULL) {
			journal->j_checkpoint_transactions = commit_transaction;
			commit_transaction->t_cpnext = commit_transaction;
			commit_transaction->t_cpprev = commit_transaction;
		} else {
			commit_transaction->t_cpnext =
				journal->j_checkpoint_transactions;
			commit_transaction->t_cpprev =
				commit_transaction->t_cpnext->t_cpprev;
			commit_transaction->t_cpnext->t_cpprev =
				commit_transaction;
			commit_transaction->t_cpprev->t_cpnext =
				commit_transaction;
		}
	}
	spin_unlock(&journal->j_list_lock);

	if (journal->j_commit_callback)
		journal->j_commit_callback(journal, commit_transaction);

    mutex_unlock(&commit_transaction->t_dirty_data_mutex);
	jbd_debug(1, "JBD2: commit %d complete, head %d\n",
		  journal->j_commit_sequence, journal->j_tail_sequence);
	if (to_free)
		kfree(commit_transaction);

	wake_up(&journal->j_wait_done_commit);
}
