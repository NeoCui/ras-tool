mcelog --daemon
modprobe einj param_extension=1
mount -t debugfs none /sys/kernel/debug
