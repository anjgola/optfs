umount /mnt/mydisk
modprobe -r ext4bf
#cd ..
make
make modules_install
cp ./ext4bf.ko /lib/modules/3.2.0vijaycbf-g0c7464b-dirty/
echo "Loading module"
modprobe ext4bf
echo "Mounting fs"
mount -t ext4bf -o nodelalloc,nobarrier,nouser_xattr,noacl,data=journal  /dev/sda /mnt/mydisk
#echo "Fs contents"
#ls /mnt/mydisk/
