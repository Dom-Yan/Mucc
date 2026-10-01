// Terminals, what a text editor is built on: a pseudo-terminal, raw mode
// with termios, window size ioctls, poll, and escape sequences going
// through the line discipline.
#define _GNU_SOURCE
#include "check.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int main(void) {
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  CHECK(master >= 0);
  CHECK(grantpt(master) == 0 && unlockpt(master) == 0);
  char *name = ptsname(master);
  CHECK(name && !strncmp(name, "/dev/pts/", 9));
  int slave = open(name, O_RDWR | O_NOCTTY);
  CHECK(slave >= 0);
  CHECK(isatty(slave));

  // Window size, as an editor reads it with TIOCGWINSZ.
  struct winsize ws = {.ws_row = 40, .ws_col = 120};
  CHECK(ioctl(master, TIOCSWINSZ, &ws) == 0);
  struct winsize got;
  CHECK(ioctl(slave, TIOCGWINSZ, &got) == 0 && got.ws_row == 40 && got.ws_col == 120);

  // Cooked mode: input arrives a line at a time, and is echoed.
  write(master, "abc\n", 4);
  char buf[64] = {0};
  CHECK(read(slave, buf, sizeof buf) == 4 && !strcmp(buf, "abc\n"));
  struct pollfd pf = {.fd = master, .events = POLLIN};
  CHECK(poll(&pf, 1, 1000) == 1);
  memset(buf, 0, sizeof buf);
  CHECK(read(master, buf, sizeof buf) > 0 && strstr(buf, "abc"));

  // Raw mode, as an editor sets it: bytes one at a time, no echo, no
  // signals for Ctrl-C, no CR/NL translation.
  struct termios t;
  CHECK(tcgetattr(slave, &t) == 0);
  struct termios orig = t;
  t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  t.c_oflag &= ~(OPOST);
  t.c_cflag |= CS8;
  t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 1;
  CHECK(tcsetattr(slave, TCSAFLUSH, &t) == 0);
  write(master, "\x03\x1b[A", 4); // Ctrl-C, then the up arrow
  memset(buf, 0, sizeof buf);
  int n = 0, r;
  while (n < 4 && (r = read(slave, buf + n, sizeof buf - n)) > 0)
    n += r;
  CHECK(n == 4 && buf[0] == 3 && !strcmp(buf + 1, "\x1b[A"));
  pf.fd = master;
  CHECK(poll(&pf, 1, 100) == 0); // nothing echoed

  // Output goes through unchanged: a screen clear and cursor move.
  write(slave, "\x1b[2J\x1b[H", 7);
  memset(buf, 0, sizeof buf);
  CHECK(read(master, buf, sizeof buf) == 7 && !memcmp(buf, "\x1b[2J\x1b[H", 7));

  CHECK(tcsetattr(slave, TCSAFLUSH, &orig) == 0);
  CHECK(cfgetospeed(&orig) != (speed_t)-1);
  return done("term");
}
