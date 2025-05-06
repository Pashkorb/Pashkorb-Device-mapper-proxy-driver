# Device mapper proxy (dmp) driver
Linux kernel module for monitoring block device operations statistics.

## Features
- Counts read/write requests
- Tracks average request sizes
- SysFS interface for statistics
- Statistics reset capability

## Build & Install
```bash
cd driver
make
sudo insmod dmp.ko
```

## Usage
```bash
# Create base device
sudo dmsetup create my-device --table "0 <size> zero"

# Create proxy device
sudo dmsetup create dmp-device --table "0 <size> dmp /dev/mapper/my-device"

# View statistics
cat /sys/module/dmp/stat/volumes

# Reset statistics
echo 1 | sudo tee /sys/module/dmp/stat/reset
```

## Testing
```bash
cd test
sudo ./test.sh
```
