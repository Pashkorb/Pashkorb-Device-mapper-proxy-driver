#!/bin/bash
set -e

# delete all device and rmmod module
echo "1) Cleanup"
sudo dmsetup remove_all
sudo rmmod dmp 2>/dev/null || true

# Load the dmp kernel module using insmod
echo "2) Loading dmp module"
sudo insmod dmp.ko

# Create a device mapper device named zero1 with a zero target
echo "3) Creating zero1"
sudo dmsetup create zero1 --table "0 1024 zero"

# Create proxy device dmp1
echo "4) Creating dmp1"
sudo dmsetup create dmp1 --table "0 1024 dmp /dev/mapper/zero1"

# Read and output base volume write and avg size
echo "5) Read base counters"
write_base=$(awk '/write:/ {getline; print $2}' /sys/module/dmp/stat/volumes)
avg_base=$(awk '/write:/ {getline; getline; print $3}' /sys/module/dmp/stat/volumes)
echo "   write_reqs = $write_base"
echo "   avg_write_size = $avg_base"

# Test writing one 4KB block to the dmp1 device
echo "6) Test write 4K"
sudo dd if=/dev/zero of=/dev/mapper/dmp1 bs=4K count=1 status=none

# Read counters after the 4KB write and calculate the delta for write requests and average write size
echo "7) Read counters after write"
write_after1=$(awk '/write:/ {getline; print $2}' /sys/module/dmp/stat/volumes)
avg_after1=$(awk '/write:/ {getline; getline; print $3}' /sys/module/dmp/stat/volumes)
echo "   write_reqs delta = $((write_after1 - write_base))  (expected 1)"
echo "   avg_write_size = $avg_after1  (expected 4096)"

# Test writing one 8KB block to the dmp1 device
echo "8) Test write 8K"
sudo dd if=/dev/zero of=/dev/mapper/dmp1 bs=8K count=1 status=none

# Read counters after the 8KB write and calculate the delta for write requests and average write size
echo "9) Read counters after second write"
write_after2=$(awk '/write:/ {getline; print $2}' /sys/module/dmp/stat/volumes)
avg_after2=$(awk '/write:/ {getline; getline; print $3}' /sys/module/dmp/stat/volumes)
echo "   write_reqs delta = $((write_after2 - write_after1))  (expected 1)"
echo "   avg_write_size = $avg_after2  (expected 8192)"

# Finish
echo "All write tests completed."
