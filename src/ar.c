//============================================================================
// ar.c - an archiver for static libraries
//
// `mucc -ar rcs libfoo.a a.o b.o` writes a static library in the format
// GNU ar writes, which mucc's linker, ld and nm read. After the magic
// string come
//   - the member "/": the symbol index, saying which member defines each
//     global symbol, so a linker needn't read them all,
//   - the member "//": names longer than 15 characters,
//   - the members, each with a 60-byte header.
// Like GNU ar by default, it is deterministic: every member's date,
// owner and group are 0, and its mode 644. `mucc -ranlib libfoo.a`
// rewrites an archive's symbol index.
//============================================================================

#include "mucc.h"
#include <elf.h>

typedef struct {
  char *name;
  unsigned char *data;
  size_t size;
} ArMember;

static ArMember *members;
static int nmembers;

static void add_member(char *name, unsigned char *data, size_t size) {
  members = realloc(members, sizeof(ArMember) * (nmembers + 1));
  members[nmembers++] = (ArMember){name, data, size};
}

static unsigned char *read_whole(char *path, size_t *size) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    error("%s: %s", path, strerror(errno));
  fseek(fp, 0, SEEK_END);
  *size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  unsigned char *buf = malloc(*size + 1);
  if (fread(buf, 1, *size, fp) != *size)
    error("%s: read error", path);
  fclose(fp);
  return buf;
}

// Reads the members of the archive at `path`, leaving out its symbol
// index and long-name table.
static void read_members(char *path) {
  size_t size;
  unsigned char *data = read_whole(path, &size);
  if (size < 8 || memcmp(data, "!<arch>\n", 8))
    error("%s: not an archive", path);

  char *longnames = NULL;
  for (size_t off = 8; off + 60 <= size;) {
    unsigned char *hdr = data + off;
    size_t len = strtoull((char *)hdr + 48, NULL, 10);
    if (off + 60 + len > size)
      error("%s: truncated archive", path);
    char name[17] = {0};
    memcpy(name, hdr, 16);

    if (!strcmp(name, "//              ")) {
      longnames = (char *)hdr + 60;
    } else if (name[0] == '/' && isdigit(name[1])) {
      if (!longnames)
        error("%s: long member name without a name table", path);
      char *s = longnames + atoi(name + 1);
      add_member(strndup(s, strcspn(s, "/\n")), hdr + 60, len);
    } else if (name[0] != '/') {
      add_member(strndup(name, strcspn(name, "/ ")), hdr + 60, len);
    }
    off += 60 + len + (len & 1);
  }
}

static int find_member(char *name) {
  for (int i = 0; i < nmembers; i++)
    if (!strcmp(members[i].name, name))
      return i;
  return -1;
}

// Appends the global symbols `m` defines (if it is an ELF object) to
// `names`, and returns how many.
static int member_symbols(ArMember *m, StringArray *names) {
  Elf64_Ehdr *eh = (Elf64_Ehdr *)m->data;
  if (m->size < sizeof(Elf64_Ehdr) || memcmp(eh->e_ident, ELFMAG, SELFMAG) ||
      eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_type != ET_REL)
    return 0;

  Elf64_Shdr *sh = (Elf64_Shdr *)(m->data + eh->e_shoff);
  int n = 0;
  for (int i = 0; i < eh->e_shnum; i++) {
    if (sh[i].sh_type != SHT_SYMTAB)
      continue;
    Elf64_Sym *syms = (Elf64_Sym *)(m->data + sh[i].sh_offset);
    char *strtab = (char *)m->data + sh[sh[i].sh_link].sh_offset;
    for (int j = 0; j < sh[i].sh_size / sizeof(Elf64_Sym); j++) {
      int bind = ELF64_ST_BIND(syms[j].st_info);
      if ((bind == STB_GLOBAL || bind == STB_WEAK || bind == STB_GNU_UNIQUE) &&
          syms[j].st_shndx != SHN_UNDEF) {
        strarray_push(names, strtab + syms[j].st_name);
        n++;
      }
    }
  }
  return n;
}

static void put_be32(FILE *out, uint32_t v) {
  fputc(v >> 24, out);
  fputc(v >> 16, out);
  fputc(v >> 8, out);
  fputc(v, out);
}

static void put_header(FILE *out, char *name, char *date, char *uid, char *gid,
                       char *mode, size_t size) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%-16s%-12s%-6s%-6s%-8s%-10zu`\n", name, date,
           uid, gid, mode, size);
  fwrite(buf, 1, 60, out);
}

static void write_archive(char *path, bool with_index, mode_t mode) {
  // Names that don't fit in the header's 16 bytes (with the '/' after
  // them) go in the long-name table.
  char *longnames;
  size_t longlen;
  FILE *lf = open_memstream(&longnames, &longlen);
  int *longoff = calloc(nmembers, sizeof(int));
  for (int i = 0; i < nmembers; i++) {
    longoff[i] = -1;
    if (strlen(members[i].name) > 15) {
      longoff[i] = ftell(lf);
      fprintf(lf, "%s/\n", members[i].name);
    }
  }
  if (ftell(lf) & 1)
    fputc('\n', lf); // padding, counted in its size as GNU ar does
  fclose(lf);

  // The symbol index: a count, the offset of each symbol's member, then
  // the names, padded to an even size.
  StringArray syms = {};
  int *nsyms = calloc(nmembers, sizeof(int));
  size_t namelen = 0;
  if (with_index)
    for (int i = 0; i < nmembers; i++)
      nsyms[i] = member_symbols(&members[i], &syms);
  for (int i = 0; i < syms.len; i++)
    namelen += strlen(syms.data[i]) + 1;
  size_t indexlen = 4 + 4 * syms.len + namelen;
  indexlen += indexlen & 1;
  bool has_index = with_index && syms.len > 0;

  // Each member's offset in the file
  size_t off = 8;
  if (has_index)
    off += 60 + indexlen;
  if (longlen)
    off += 60 + longlen;
  size_t *memoff = calloc(nmembers, sizeof(size_t));
  for (int i = 0; i < nmembers; i++) {
    memoff[i] = off;
    off += 60 + members[i].size + (members[i].size & 1);
  }
  if (off > UINT32_MAX)
    error("%s: an archive over 4 GiB is not supported", path);

  // Write a temporary file next to the archive, then rename it.
  char *tmp = format("%s.tmpXXXXXX", path);
  int fd = mkstemp(tmp);
  if (fd < 0)
    error("%s: %s", tmp, strerror(errno));
  fchmod(fd, mode);
  FILE *out = fdopen(fd, "wb");
  fputs("!<arch>\n", out);

  if (has_index) {
    put_header(out, "/", "0", "0", "0", "0", indexlen);
    put_be32(out, syms.len);
    for (int i = 0; i < nmembers; i++)
      for (int j = 0; j < nsyms[i]; j++)
        put_be32(out, memoff[i]);
    for (int i = 0; i < syms.len; i++)
      fwrite(syms.data[i], 1, strlen(syms.data[i]) + 1, out);
    if ((4 + 4 * syms.len + namelen) & 1)
      fputc('\0', out);
  }

  if (longlen) {
    put_header(out, "//", "", "", "", "", longlen);
    fwrite(longnames, 1, longlen, out);
  }

  for (int i = 0; i < nmembers; i++) {
    ArMember *m = &members[i];
    char *name = longoff[i] >= 0 ? format("/%d", longoff[i]) : format("%s/", m->name);
    put_header(out, name, "0", "0", "0", "644", m->size);
    fwrite(m->data, 1, m->size, out);
    if (m->size & 1)
      fputc('\n', out);
  }

  if (fclose(out))
    error("%s: %s", tmp, strerror(errno));
  if (rename(tmp, path))
    error("%s: %s", path, strerror(errno));
}

static char *base_name(char *path) {
  char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}

// The mode for a new archive, as a new file gets: 0666 without the umask.
static mode_t new_file_mode(void) {
  mode_t mask = umask(0);
  umask(mask);
  return 0666 & ~mask;
}

// mucc -ar <operation><modifiers> archive [files...]
//
// Operations: r (insert or replace members), q (append), d (delete),
// t (list). Modifiers: c (don't say when the archive is created), s
// (write a symbol index, the default), S (don't), u and D (accepted;
// every member is replaced and the output is always deterministic),
// v (ignored).
int run_ar(int argc, char **argv) {
  if (argc < 2)
    error("usage: mucc -ar {r|q|d|t}[csSuDv] archive [files...]");

  char op = 0;
  bool quiet = false, with_index = true;
  for (char *p = argv[0] + (argv[0][0] == '-'); *p; p++) {
    if (strchr("rqdt", *p)) {
      if (op)
        error("-ar: more than one operation in '%s'", argv[0]);
      op = *p;
    } else if (*p == 'c') {
      quiet = true;
    } else if (*p == 's') {
      with_index = true;
    } else if (*p == 'S') {
      with_index = false;
    } else if (!strchr("uDv", *p)) {
      error("-ar: unknown option '%c'", *p);
    }
  }
  if (!op)
    error("-ar: no operation (r, q, d or t) in '%s'", argv[0]);

  char *path = argv[1];
  struct stat st;
  bool exists = !stat(path, &st);
  if (exists)
    read_members(path);
  else if (op == 'd' || op == 't')
    error("%s: %s", path, strerror(errno));
  else if (!quiet)
    fprintf(stderr, "mucc: creating %s\n", path);

  if (op == 't') {
    for (int i = 0; i < nmembers; i++)
      printf("%s\n", members[i].name);
    return 0;
  }

  for (int i = 2; i < argc; i++) {
    char *name = base_name(argv[i]);
    if (op == 'd') {
      int j = find_member(name);
      if (j < 0)
        error("%s: no member named %s", path, name);
      memmove(members + j, members + j + 1, sizeof(ArMember) * (nmembers - j - 1));
      nmembers--;
      continue;
    }

    size_t size;
    unsigned char *data = read_whole(argv[i], &size);
    int j = (op == 'r') ? find_member(name) : -1;
    if (j >= 0)
      members[j] = (ArMember){name, data, size};
    else
      add_member(name, data, size);
  }

  write_archive(path, with_index, exists ? st.st_mode & 07777 : new_file_mode());
  return 0;
}

// mucc -ranlib archive: rewrites the archive's symbol index.
int run_ranlib(int argc, char **argv) {
  if (argc != 1)
    error("usage: mucc -ranlib archive");
  struct stat st;
  if (stat(argv[0], &st))
    error("%s: %s", argv[0], strerror(errno));
  read_members(argv[0]);
  write_archive(argv[0], true, st.st_mode & 07777);
  return 0;
}
