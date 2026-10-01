// Files: read/write/seek, mmap, directories (nftw), links, renames, locks,
// inotify, stat, realpath, temp files, stdio buffering.
#define _GNU_SOURCE
#include "check.h"
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int nfiles;
static int count(const char *path, const struct stat *st, int type, struct FTW *f) {
  if (type == FTW_F || type == FTW_SL)
    nfiles++;
  return 0;
}
static int rm(const char *path, const struct stat *st, int type, struct FTW *f) {
  return remove(path);
}

int main(void) {
  char dir[] = "/tmp/suite-files-XXXXXX";
  CHECK(mkdtemp(dir) != 0);
  CHECK(chdir(dir) == 0);

  // write, seek, read
  int fd = open("a.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
  CHECK(fd >= 0);
  CHECK(write(fd, "hello world\n", 12) == 12);
  CHECK(lseek(fd, 6, SEEK_SET) == 6);
  char buf[64] = {0};
  CHECK(read(fd, buf, 5) == 5 && !strcmp(buf, "world"));
  CHECK(pwrite(fd, "W", 1, 6) == 1);

  // mmap the file and change it through memory.
  CHECK(ftruncate(fd, 4096) == 0);
  char *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  CHECK(p != MAP_FAILED);
  CHECK(!memcmp(p, "hello World", 11));
  p[0] = 'H';
  CHECK(msync(p, 4096, MS_SYNC) == 0);
  munmap(p, 4096);
  memset(buf, 0, sizeof buf);
  CHECK(pread(fd, buf, 11, 0) == 11 && !strcmp(buf, "Hello World"));

  // An anonymous mapping, grown with mremap.
  char *q = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  q[4095] = 7;
  q = mremap(q, 4096, 1 << 20, MREMAP_MAYMOVE);
  CHECK(q != MAP_FAILED && q[4095] == 7);
  q[(1 << 20) - 1] = 1;

  // stat, fstat, links
  struct stat st;
  CHECK(fstat(fd, &st) == 0 && st.st_size == 4096 && S_ISREG(st.st_mode));
  CHECK(link("a.txt", "hard") == 0);
  CHECK(symlink("a.txt", "soft") == 0);
  CHECK(readlink("soft", buf, sizeof buf) == 5);
  CHECK(lstat("soft", &st) == 0 && S_ISLNK(st.st_mode));
  CHECK(stat("hard", &st) == 0 && st.st_nlink == 2);
  char real[PATH_MAX];
  CHECK(realpath("soft", real) && strstr(real, "/a.txt"));

  // flock and fcntl locks
  CHECK(flock(fd, LOCK_EX | LOCK_NB) == 0);
  struct flock fl = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10};
  CHECK(fcntl(fd, F_SETLK, &fl) == 0);
  close(fd);

  // inotify sees a file being created.
  int in = inotify_init1(IN_NONBLOCK);
  CHECK(in >= 0);
  CHECK(inotify_add_watch(in, ".", IN_CREATE) >= 0);
  close(open("new.txt", O_CREAT | O_WRONLY, 0644));
  char ibuf[4096] __attribute__((aligned(8)));
  int n = read(in, ibuf, sizeof ibuf);
  CHECK(n > 0 && !strcmp(((struct inotify_event *)ibuf)->name, "new.txt"));

  // directories, rename, nftw
  CHECK(mkdir("sub", 0755) == 0 && mkdir("sub/deep", 0755) == 0);
  CHECK(rename("new.txt", "sub/deep/moved.txt") == 0);
  FILE *f = fopen("sub/b.txt", "w");
  for (int i = 0; i < 1000; i++)
    fprintf(f, "line %d\n", i);
  fclose(f);
  f = fopen("sub/b.txt", "r");
  char line[32];
  int lines = 0;
  while (fgets(line, sizeof line, f))
    lines++;
  fclose(f);
  CHECK(lines == 1000);
  CHECK(nftw(".", count, 8, FTW_PHYS) == 0);
  CHECK(nfiles == 5); // a.txt, hard, soft, b.txt, moved.txt
  DIR *d = opendir("sub");
  int entries = 0;
  for (struct dirent *e; (e = readdir(d));)
    entries++;
  closedir(d);
  CHECK(entries == 4); // . .. deep b.txt

  // tmpfile and getline
  FILE *t = tmpfile();
  fputs("one\ntwo\n", t);
  rewind(t);
  char *l = 0;
  size_t cap = 0;
  CHECK(getline(&l, &cap, t) == 4 && !strcmp(l, "one\n"));
  free(l);
  fclose(t);

  chdir("/");
  CHECK(nftw(dir, rm, 8, FTW_DEPTH | FTW_PHYS) == 0);
  return done("files");
}
