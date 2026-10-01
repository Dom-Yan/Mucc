// Linux's own headers, for programs that talk to the kernel directly:
// USB through usbfs, input devices, netlink, block devices. The ioctl
// numbers and struct sizes are the x86-64 kernel ABI, so they're the same
// with the bundled headers and with a distribution's.
#include "test.h"
#include <linux/fs.h>
#include <linux/if_ether.h>
#include <linux/input.h>
#include <linux/netlink.h>
#include <linux/usbdevice_fs.h>
#include <linux/version.h>
#include <asm/unistd.h>

int main() {
  ASSERT(24, sizeof(struct usbdevfs_ctrltransfer));
  ASSERT(1, USBDEVFS_CONTROL == 0xc0185500u);
  ASSERT(1, USBDEVFS_BULK == 0xc0185502u);
  ASSERT(1, USBDEVFS_CLAIMINTERFACE == 0x8004550fu);

  ASSERT(24, sizeof(struct input_event));
  ASSERT(1, EVIOCGVERSION == 0x80044501u);
  ASSERT(1, EV_KEY);
  ASSERT(30, KEY_A);

  ASSERT(16, sizeof(struct nlmsghdr));
  ASSERT(0, NETLINK_ROUTE);
  ASSERT(0x800, ETH_P_IP);
  ASSERT(1, BLKGETSIZE64 == 0x80081272u);

  ASSERT(1, __NR_write);
  ASSERT(60, __NR_exit);
  ASSERT(1, LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0));

  printf("OK\n");
  return 0;
}
