// Processes, pipes, signals: fork, exec, wait, pipes, sigaction, alarm,
// signalfd, sigprocmask, posix_spawn, environment.
#define _GNU_SOURCE
#include "check.h"
#include <signal.h>
#include <spawn.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
static volatile sig_atomic_t got_usr1, got_alarm;
static void on_usr1(int sig) { got_usr1 = sig; }
static void on_alarm(int sig) { got_alarm = 1; }

int main(int argc, char **argv) {
  // As a child: echo argv[2] with a prefix and exit 7.
  if (argc == 3 && !strcmp(argv[1], "child")) {
    printf("child says %s, HELLO=%s\n", argv[2], getenv("HELLO"));
    return 7;
  }

  // fork + pipe: the child writes, the parent reads.
  int fd[2];
  CHECK(pipe(fd) == 0);
  pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]);
    write(fd[1], "from child", 10);
    _exit(3);
  }
  close(fd[1]);
  char buf[64] = {0};
  CHECK(read(fd[0], buf, sizeof buf) == 10 && !strcmp(buf, "from child"));
  int status;
  CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 3);

  // fork + exec of ourselves, with stdout through a pipe and a new env var.
  CHECK(pipe(fd) == 0);
  pid = fork();
  if (pid == 0) {
    dup2(fd[1], 1);
    setenv("HELLO", "world", 1);
    execl("/proc/self/exe", argv[0], "child", "hi", (char *)0);
    _exit(99);
  }
  close(fd[1]);
  memset(buf, 0, sizeof buf);
  int n = 0, r;
  while ((r = read(fd[0], buf + n, sizeof buf - 1 - n)) > 0)
    n += r;
  CHECK(!strcmp(buf, "child says hi, HELLO=world\n"));
  CHECK(waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 7);

  // posix_spawn
  char *args[] = {argv[0], "child", "spawned", 0};
  CHECK(posix_spawn(&pid, "/proc/self/exe", 0, 0, args, environ) == 0);
  CHECK(waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 7);

  // sigaction + kill to ourselves, and alarm with pause.
  struct sigaction sa = {0};
  sa.sa_handler = on_usr1;
  CHECK(sigaction(SIGUSR1, &sa, 0) == 0);
  CHECK(kill(getpid(), SIGUSR1) == 0);
  CHECK(got_usr1 == SIGUSR1);
  signal(SIGALRM, on_alarm);
  alarm(1);
  pause();
  CHECK(got_alarm);

  // A blocked signal read through a signalfd.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR2);
  CHECK(sigprocmask(SIG_BLOCK, &set, 0) == 0);
  int sfd = signalfd(-1, &set, 0);
  CHECK(sfd >= 0);
  raise(SIGUSR2);
  struct signalfd_siginfo si;
  CHECK(read(sfd, &si, sizeof si) == sizeof si && si.ssi_signo == SIGUSR2);

  // A child killed by a signal.
  pid = fork();
  if (pid == 0) {
    pause();
    _exit(0);
  }
  kill(pid, SIGTERM);
  CHECK(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

  return done("procs");
}
