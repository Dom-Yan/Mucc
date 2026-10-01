// Networking: a TCP server and client over loopback with threads, epoll,
// UDP, getaddrinfo, a Unix socket passing a file descriptor (SCM_RIGHTS),
// non-blocking connect.
#define _GNU_SOURCE
#include "check.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int port;

static void *client(void *arg) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
  inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  if (connect(s, (struct sockaddr *)&a, sizeof a))
    return (void *)1;
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  for (int i = 0; i < 100; i++) {
    char msg[32], reply[32] = {0};
    int len = snprintf(msg, sizeof msg, "ping %d", i);
    write(s, msg, len);
    int n = 0, r;
    while (n < len && (r = read(s, reply + n, len - n)) > 0)
      n += r;
    if (strncmp(reply, msg, len))
      return (void *)2;
  }
  close(s);
  return 0;
}

int main(void) {
  // An echo server with epoll, and a client thread.
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  CHECK(bind(ls, (struct sockaddr *)&a, sizeof a) == 0);
  socklen_t alen = sizeof a;
  CHECK(getsockname(ls, (struct sockaddr *)&a, &alen) == 0);
  port = ntohs(a.sin_port);
  CHECK(listen(ls, 8) == 0);

  pthread_t t;
  pthread_create(&t, 0, client, 0);

  int ep = epoll_create1(0);
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = ls};
  CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, ls, &ev) == 0);
  int conn = -1, echoed = 0, closed = 0;
  while (!closed) {
    struct epoll_event evs[4];
    int n = epoll_wait(ep, evs, 4, 5000);
    if (n <= 0)
      break;
    for (int i = 0; i < n; i++) {
      if (evs[i].data.fd == ls) {
        conn = accept4(ls, 0, 0, SOCK_NONBLOCK);
        ev.data.fd = conn;
        epoll_ctl(ep, EPOLL_CTL_ADD, conn, &ev);
      } else {
        char buf[256];
        int r = read(conn, buf, sizeof buf);
        if (r <= 0) {
          closed = 1;
        } else {
          write(conn, buf, r);
          echoed += r;
        }
      }
    }
  }
  void *ret;
  pthread_join(t, &ret);
  CHECK(ret == 0);
  CHECK(echoed > 600);

  // UDP to ourselves.
  int u = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in ua = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  CHECK(bind(u, (struct sockaddr *)&ua, sizeof ua) == 0);
  alen = sizeof ua;
  getsockname(u, (struct sockaddr *)&ua, &alen);
  CHECK(sendto(u, "datagram", 8, 0, (struct sockaddr *)&ua, sizeof ua) == 8);
  char buf[16] = {0};
  CHECK(recv(u, buf, sizeof buf, 0) == 8 && !strcmp(buf, "datagram"));

  // getaddrinfo of a numeric address, and of localhost from /etc/hosts.
  struct addrinfo *res, hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
  CHECK(getaddrinfo("127.0.0.1", "80", &hints, &res) == 0);
  CHECK(ntohs(((struct sockaddr_in *)res->ai_addr)->sin_port) == 80);
  freeaddrinfo(res);
  CHECK(getaddrinfo("localhost", 0, &hints, &res) == 0);
  freeaddrinfo(res);

  // A file descriptor passed over a Unix socket pair.
  int sp[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
  int pfd[2];
  pipe(pfd);
  char cbuf[CMSG_SPACE(sizeof(int))] = {0};
  struct iovec iov = {.iov_base = "x", .iov_len = 1};
  struct msghdr m = {.msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof cbuf};
  struct cmsghdr *c = CMSG_FIRSTHDR(&m);
  c->cmsg_level = SOL_SOCKET;
  c->cmsg_type = SCM_RIGHTS;
  c->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(c), &pfd[1], sizeof(int));
  CHECK(sendmsg(sp[0], &m, 0) == 1);
  char x;
  char rbuf[CMSG_SPACE(sizeof(int))];
  struct iovec riov = {.iov_base = &x, .iov_len = 1};
  struct msghdr rm = {.msg_iov = &riov, .msg_iovlen = 1, .msg_control = rbuf, .msg_controllen = sizeof rbuf};
  CHECK(recvmsg(sp[1], &rm, 0) == 1);
  int got;
  memcpy(&got, CMSG_DATA(CMSG_FIRSTHDR(&rm)), sizeof(int));
  CHECK(write(got, "via fd", 6) == 6);
  memset(buf, 0, sizeof buf);
  CHECK(read(pfd[0], buf, sizeof buf) == 6 && !strcmp(buf, "via fd"));

  return done("net");
}
