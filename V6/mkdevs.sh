/etc/mknod /dev/rk0 b 0 0
/etc/mknod /dev/rk1 b 0 1
/etc/mknod /dev/rk2 b 0 2
/etc/mknod /dev/mt0 b 3 0
/etc/mknod /dev/tap0 b 4 0
/etc/mknod /dev/rrk0 c 9 0
/etc/mknod /dev/rrk1 c 9 1
/etc/mknod /dev/rrk2 c 9 2
/etc/mknod /dev/rmt0 c 12 0
/etc/mknod /dev/lp0 c 2 0
/etc/mknod /dev/tty0 c 3 0
/etc/mknod /dev/tty1 c 3 1
/etc/mknod /dev/tty2 c 3 2
/etc/mknod /dev/tty3 c 3 3
/etc/mknod /dev/tty4 c 3 4
/etc/mknod /dev/tty5 c 3 5
/etc/mknod /dev/tty6 c 3 6
/etc/mknod /dev/tty7 c 3 7
chmod 640 /dev/*rk*
chmod 640 /dev/*mt*
chmod 640 /dev/*tap*
