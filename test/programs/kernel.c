// Linux-specific interfaces, with Linux's own headers: netlink (the
// network interfaces), timerfd, eventfd, memfd, getrandom, raw syscalls,
// prctl, /proc, CPU affinity, input and usbfs structures.
#define _GNU_SOURCE
#include "check.h"
#include <linux/if_link.h>
#include <linux/input.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/usbdevice_fs.h>
#include <net/if.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

// Lists the network interfaces by asking the kernel over netlink, as
// `ip link` does. Returns whether "lo" was among them.
static int has_loopback(void) {
  int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (s < 0)
    return 0;
  struct {
    struct nlmsghdr nh;
    struct ifinfomsg ifi;
  } req = {
    .nh = {.nlmsg_len = sizeof req, .nlmsg_type = RTM_GETLINK,
           .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP, .nlmsg_seq = 1},
    .ifi = {.ifi_family = AF_UNSPEC},
  };
  send(s, &req, sizeof req, 0);
  char buf[16384] __attribute__((aligned(4)));
  int found = 0;
  for (;;) {
    int len = recv(s, buf, sizeof buf, 0);
    if (len <= 0)
      break;
    for (struct nlmsghdr *nh = (void *)buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
      if (nh->nlmsg_type == NLMSG_DONE) {
        close(s);
        return found;
      }
      struct ifinfomsg *ifi = NLMSG_DATA(nh);
      int alen = IFLA_PAYLOAD(nh);
      for (struct rtattr *a = IFLA_RTA(ifi); RTA_OK(a, alen); a = RTA_NEXT(a, alen))
        if (a->rta_type == IFLA_IFNAME && !strcmp(RTA_DATA(a), "lo"))
          found = 1;
    }
  }
  close(s);
  return found;
}

int main(void) {
  CHECK(has_loopback());
  CHECK(if_nametoindex("lo") > 0);

  // timerfd fires after 20 ms.
  int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
  struct itimerspec its = {.it_value = {0, 20 * 1000 * 1000}};
  CHECK(timerfd_settime(tfd, 0, &its, 0) == 0);
  uint64_t ticks;
  CHECK(read(tfd, &ticks, 8) == 8 && ticks == 1);

  // eventfd counts.
  int efd = eventfd(0, 0);
  uint64_t v = 5;
  write(efd, &v, 8);
  write(efd, &v, 8);
  CHECK(read(efd, &v, 8) == 8 && v == 10);

  // memfd, mapped
  int mfd = memfd_create("suite", 0);
  CHECK(mfd >= 0 && ftruncate(mfd, 4096) == 0);
  char *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
  strcpy(p, "in memory");
  char buf[16] = {0};
  CHECK(pread(mfd, buf, 9, 0) == 9 && !strcmp(buf, "in memory"));

  // getrandom, and raw syscalls by number
  unsigned char rnd[32] = {0};
  CHECK(getrandom(rnd, sizeof rnd, 0) == 32);
  CHECK(syscall(SYS_getpid) == getpid());
  CHECK(syscall(SYS_gettid) == gettid());
  CHECK(syscall(SYS_write, 1, "", 0) == 0);

  // prctl renames the thread, as /proc shows.
  CHECK(prctl(PR_SET_NAME, "suite-kernel") == 0);
  FILE *f = fopen("/proc/self/comm", "r");
  char comm[32] = {0};
  CHECK(f && fgets(comm, sizeof comm, f) && !strcmp(comm, "suite-kernel\n"));
  fclose(f);

  // CPU affinity
  cpu_set_t set;
  CHECK(sched_getaffinity(0, sizeof set, &set) == 0 && CPU_COUNT(&set) >= 1);

  // Kernel ABI structures from Linux's headers
  CHECK(sizeof(struct input_event) == 24);
  CHECK(sizeof(struct usbdevfs_ctrltransfer) == 24);
  CHECK(USBDEVFS_CONTROL == 0xc0185500u);

  // clock_gettime and nanosleep
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  nanosleep(&(struct timespec){0, 10 * 1000 * 1000}, 0);
  clock_gettime(CLOCK_MONOTONIC, &b);
  long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
  CHECK(ms >= 10 && ms < 1000);

  return done("kernel");
}
