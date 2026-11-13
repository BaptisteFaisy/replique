#!/bin/sh
# guest-fg.sh - invite QEMU au premier plan (service systemd).
set -u
QPREFIX="$HOME/qnx71-vm/qprefix"
LOG="$HOME/replique-demo"
export LD_LIBRARY_PATH="$QPREFIX/usr/lib/x86_64-linux-gnu:$QPREFIX/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
exec "$QPREFIX/usr/bin/qemu-system-x86_64" \
  --cpu max -smp 4 -m 2048 \
  -L "$QPREFIX/usr/share/qemu" \
  -kernel "$HOME/replique-img/ifs.bin" \
  -object rng-random,filename=/dev/urandom,id=rng0 \
  -device virtio-rng-pci,rng=rng0 \
  -display none -vga std \
  -chardev socket,id=ser0,path="$LOG/ser.sock",server=on,wait=off,logfile="$LOG/serial-kernel.log" \
  -serial chardev:ser0 \
  -monitor unix:"$LOG/mon-kernel.sock",server,nowait
