umount /mnt/mydisk
mkfs.ext4 -j -J size=8192 -E lazy_itable_init=0,lazy_journal_init=0 /dev/sdc
