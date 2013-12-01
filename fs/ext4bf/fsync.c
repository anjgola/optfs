/*
 *  linux/fs/ext4bf/fsync.c
 *
 *  Copyright (C) 1993  Stephen Tweedie (sct@redhat.com)
 *  from
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *                      Laboratoire MASI - Institut Blaise Pascal
 *                      Universite Pierre et Marie Curie (Paris VI)
 *  from
 *  linux/fs/minix/truncate.c   Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4bffs fsync primitive
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 *  Removed unnecessary code duplication for little endian machines
 *  and excessive __inline__s.
 *        Andi Kleen, 1997
 *
 * Major simplications and cleanup - we only need to do the metadata, because
 * we can depend on generic_block_fdatasync() to sync the data blocks.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include "jbdbf.h"
#include <linux/blkdev.h>

#include "ext4bf.h"
#include "ext4bf_jbdbf.h"

#define OSYNC_COMMIT 0
#define DSYNC_COMMIT 1

static void dump_completed_IO(struct inode * inode)
{
#ifdef	EXT4FS_DEBUG
	struct list_head *cur, *before, *after;
	ext4bf_io_end_t *io, *io0, *io1;
	unsigned long flags;

	if (list_empty(&EXT4_I(inode)->i_completed_io_list)){
		ext4bf_debug("inode %lu completed_io list is empty\n", inode->i_ino);
		return;
	}

	ext4bf_debug("Dump inode %lu completed_io list \n", inode->i_ino);
	spin_lock_irqsave(&EXT4_I(inode)->i_completed_io_lock, flags);
	list_for_each_entry(io, &EXT4_I(inode)->i_completed_io_list, list){
		cur = &io->list;
		before = cur->prev;
		io0 = container_of(before, ext4bf_io_end_t, list);
		after = cur->next;
		io1 = container_of(after, ext4bf_io_end_t, list);

		ext4bf_debug("io 0x%p from inode %lu,prev 0x%p,next 0x%p\n",
			    io, inode->i_ino, io0, io1);
	}
	spin_unlock_irqrestore(&EXT4_I(inode)->i_completed_io_lock, flags);
#endif
}

/*
 * This function is called from ext4bf_sync_file().
 *
 * When IO is completed, the work to convert unwritten extents to
 * written is queued on workqueue but may not get immediately
 * scheduled. When fsync is called, we need to ensure the
 * conversion is complete before fsync returns.
 * The inode keeps track of a list of pending/completed IO that
 * might needs to do the conversion. This function walks through
 * the list and convert the related unwritten extents for completed IO
 * to written.
 * The function return the number of pending IOs on success.
 */
int ext4bf_flush_completed_IO(struct inode *inode)
{
	ext4bf_io_end_t *io;
	struct ext4bf_inode_info *ei = EXT4_I(inode);
	unsigned long flags;
	int ret = 0;
	int ret2 = 0;

	dump_completed_IO(inode);
	spin_lock_irqsave(&ei->i_completed_io_lock, flags);
	while (!list_empty(&ei->i_completed_io_list)){
		io = list_entry(ei->i_completed_io_list.next,
				ext4bf_io_end_t, list);
		list_del_init(&io->list);
		/*
		 * Calling ext4bf_end_io_nolock() to convert completed
		 * IO to written.
		 *
		 * When ext4bf_sync_file() is called, run_queue() may already
		 * about to flush the work corresponding to this io structure.
		 * It will be upset if it founds the io structure related
		 * to the work-to-be schedule is freed.
		 *
		 * Thus we need to keep the io structure still valid here after
		 * conversion finished. The io structure has a flag to
		 * avoid double converting from both fsync and background work
		 * queue work.
		 */
		spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);
		ret = ext4bf_end_io_nolock(io);
		if (ret < 0)
			ret2 = ret;
		spin_lock_irqsave(&ei->i_completed_io_lock, flags);
	}
	spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);
	return (ret2 < 0) ? ret2 : 0;
}

/*
 * If we're not journaling and this is a just-created file, we have to
 * sync our parent directory (if it was freshly created) since
 * otherwise it will only be written by writeback, leaving a huge
 * window during which a crash may lose the file.  This may apply for
 * the parent directory's parent as well, and so on recursively, if
 * they are also freshly created.
 */
static int ext4bf_sync_parent(struct inode *inode)
{
	struct writeback_control wbc;
	struct dentry *dentry = NULL;
	struct inode *next;
	int ret = 0;

	if (!ext4bf_test_inode_state(inode, EXT4_STATE_NEWENTRY))
		return 0;
	inode = igrab(inode);
	while (ext4bf_test_inode_state(inode, EXT4_STATE_NEWENTRY)) {
		ext4bf_clear_inode_state(inode, EXT4_STATE_NEWENTRY);
		dentry = NULL;
		spin_lock(&inode->i_lock);
		if (!list_empty(&inode->i_dentry)) {
			dentry = list_first_entry(&inode->i_dentry,
						  struct dentry, d_alias);
			dget(dentry);
		}
		spin_unlock(&inode->i_lock);
		if (!dentry)
			break;
		next = igrab(dentry->d_parent->d_inode);
		dput(dentry);
		if (!next)
			break;
		iput(inode);
		inode = next;
		ret = sync_mapping_buffers(inode->i_mapping);
		if (ret)
			break;
		memset(&wbc, 0, sizeof(wbc));
		wbc.sync_mode = WB_SYNC_ALL;
		wbc.nr_to_write = 0;         /* only write out the inode */
		ret = sync_inode(inode, &wbc);
		if (ret)
			break;
	}
	iput(inode);
	return ret;
}

/**
 * __sync_file - generic_file_fsync without the locking and filemap_write
 * @inode:	inode to sync
 * @datasync:	only sync essential metadata if true
 *
 * This is just generic_file_fsync without the locking.  This is needed for
 * nojournal mode to make sure this inodes data/metadata makes it to disk
 * properly.  The i_mutex should be held already.
 */
static int __sync_inode(struct inode *inode, int datasync)
{
	int err;
	int ret;

	ret = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY))
		return ret;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return ret;

	err = sync_inode_metadata(inode, 1);
	if (ret == 0)
		ret = err;
	return ret;
}

/*
 * akpm: A new design for ext4bf_sync_file().
 *
 * This is only called from sys_fsync(), sys_fdatasync() and sys_msync().
 * There cannot be a transaction open by this task.
 * Another task could have dirtied this inode.  Its data can be in any
 * state in the journalling system.
 *
 * What we do is just kick off a commit and wait on it.  This will snapshot the
 * inode to disk.
 *
 * i_mutex lock is held when entering and exiting this function
 */

int ext4bf_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
#if PROJ_736
    unsigned static int fsync_cnt = 0;
    printk("736: ext4bf_sync_file fsync.c : %d \n", ++fsync_cnt)
#endif
    TIMESTAMP("START", "ext4bf_sync_file", "")
    struct inode *inode = file->f_mapping->host;
	struct ext4bf_inode_info *ei = EXT4_I(inode);
	journal_t *journal = EXT4_SB(inode->i_sb)->s_journal;
	int ret;
	tid_t commit_tid;
	bool needs_barrier = false;

	J_ASSERT(ext4bf_journal_current_handle() == NULL);

	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret)
		return ret;
	mutex_lock(&inode->i_mutex);

	if (inode->i_sb->s_flags & MS_RDONLY)
		goto out;

	ret = ext4bf_flush_completed_IO(inode);
	if (ret < 0)
		goto out;

	if (!journal) {
		ret = __sync_inode(inode, datasync);
		if (!ret && !list_empty(&inode->i_dentry))
			ret = ext4bf_sync_parent(inode);
		goto out;
	}

	/*
	 * data=writeback,ordered:
	 *  The caller's filemap_fdatawrite()/wait will sync the data.
	 *  Metadata is in the journal, we wait for proper transaction to
	 *  commit here.
	 *
	 * data=journal:
	 *  filemap_fdatawrite won't do anything (the buffers are clean).
	 *  ext4bf_force_commit will write the file data into the journal and
	 *  will wait on that.
	 *  filemap_fdatawait() will encounter a ton of newly-dirtied pages
	 *  (they were dirtied by commit).  But that's OK - the blocks are
	 *  safe in-journal, which is all fsync() needs to ensure.
	 */
	if (ext4bf_should_journal_data(inode)) {
		ret = ext4bf_force_commit(inode->i_sb);
		goto out;
	}

	commit_tid = datasync ? ei->i_datasync_tid : ei->i_sync_tid;
	if (journal->j_flags & JBD2_BARRIER &&
	    !jbdbf_trans_will_send_data_barrier(journal, commit_tid))
		needs_barrier = true;
	jbdbf_log_start_commit(journal, commit_tid);
	ret = jbdbf_log_wait_commit(journal, commit_tid);
	if (needs_barrier)
		blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL, NULL);
 out:
	mutex_unlock(&inode->i_mutex);
    TIMESTAMP("Normal End", "ext4bf_sync_file", "")
	return ret;
}

int ext4bf_osync_file(struct file *file, loff_t start, loff_t end)
{

#if PROJ_736
    unsigned static int osync_cnt = 0;
    osync_cnt++;
    printk("736: ext4bf_osync_file called times: %d \n");
#endif
    struct inode *inode = file->f_mapping->host;
	struct ext4bf_inode_info *ei = EXT4_I(inode);
	journal_t *journal = EXT4_SB(inode->i_sb)->s_journal;
	int ret;
	tid_t commit_tid;
	bool needs_barrier = false;
 	int datasync = 0;

	J_ASSERT(ext4bf_journal_current_handle() == NULL);

	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret)
		return ret;
	mutex_lock(&inode->i_mutex);

	if (inode->i_sb->s_flags & MS_RDONLY)
		goto out;

	ret = ext4bf_flush_completed_IO(inode);
	if (ret < 0)
		goto out;

	if (!journal) {
		ret = __sync_inode(inode, datasync);
		if (!ret && !list_empty(&inode->i_dentry))
			ret = ext4bf_sync_parent(inode);
		goto out;
	}

	/*
	 * data=writeback,ordered:
	 *  The caller's filemap_fdatawrite()/wait will sync the data.
	 *  Metadata is in the journal, we wait for proper transaction to
	 *  commit here.
	 *
	 * data=journal:
	 *  filemap_fdatawrite won't do anything (the buffers are clean).
	 *  ext4bf_force_commit will write the file data into the journal and
	 *  will wait on that.
	 *  filemap_fdatawait() will encounter a ton of newly-dirtied pages
	 *  (they were dirtied by commit).  But that's OK - the blocks are
	 *  safe in-journal, which is all fsync() needs to ensure.
	 */
	if (ext4bf_should_journal_data(inode)) {
		ret = ext4bf_force_commit(inode->i_sb);
		goto out;
	}

	commit_tid = ei->i_sync_tid;
	jbdbf_log_start_optfs_commit(journal, commit_tid, OSYNC_COMMIT);
	ret = jbdbf_log_wait_commit(journal, commit_tid);
 out:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

int ext4bf_dsync_file(struct file *file, loff_t start, loff_t end)
{
#if PROJ_736
    unsigned static int dsync_cnt = 0;
    printk("736: ext4bf_dsync_file called times: %d \n", ++dsync_cnt);
#endif
    struct inode *inode = file->f_mapping->host;
	struct ext4bf_inode_info *ei = EXT4_I(inode);
	journal_t *journal = EXT4_SB(inode->i_sb)->s_journal;
	int ret;
	tid_t commit_tid;
	bool needs_barrier = false;
    int datasync = 0;

	J_ASSERT(ext4bf_journal_current_handle() == NULL);
    
    ext4bf_debug("Calling dsync() for inode %lu\n", inode->i_ino);

	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret)
		return ret;
	mutex_lock(&inode->i_mutex);

	if (inode->i_sb->s_flags & MS_RDONLY)
		goto out;

	ret = ext4bf_flush_completed_IO(inode);
	if (ret < 0)
		goto out;

	if (!journal) {
		ret = __sync_inode(inode, datasync);
		if (!ret && !list_empty(&inode->i_dentry))
			ret = ext4bf_sync_parent(inode);
		goto out;
	}

	/*
	 * data=writeback,ordered:
	 *  The caller's filemap_fdatawrite()/wait will sync the data.
	 *  Metadata is in the journal, we wait for proper transaction to
	 *  commit here.
	 *
	 * data=journal:
	 *  filemap_fdatawrite won't do anything (the buffers are clean).
	 *  ext4bf_force_commit will write the file data into the journal and
	 *  will wait on that.
	 *  filemap_fdatawait() will encounter a ton of newly-dirtied pages
	 *  (they were dirtied by commit).  But that's OK - the blocks are
	 *  safe in-journal, which is all fsync() needs to ensure.
	 */
	if (ext4bf_should_journal_data(inode)) {
		ret = ext4bf_force_dsync_commit(inode->i_sb);
		goto out;
	}

	commit_tid = datasync ? ei->i_datasync_tid : ei->i_sync_tid;
	jbdbf_log_start_optfs_commit(journal, commit_tid, DSYNC_COMMIT);
	ret = jbdbf_log_wait_commit(journal, commit_tid);

 out:
	/* Issue a flush because this is dsync. */
	// blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL, NULL);

	mutex_unlock(&inode->i_mutex);
	return ret;
}
