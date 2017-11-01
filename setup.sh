#! /bin/sh

insmod ramssd.ko
fdisk -l /dev/ramssd0
mkfs -t ext3 /dev/ramssd0
mount -t ext3 /dev/ramssd0 /mnt/test
