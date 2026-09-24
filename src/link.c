//============================================================================
// link.c - STAGE 6: LINK (static executables)
//
// With -static, mucc links the object files and static libraries into an
// executable itself instead of running `ld`: it reads the objects and the
// archive members they need, gives every section an address, applies the
// relocations and writes an ELF executable Linux can run directly.
// Dynamic linking (against .so libraries) still uses `ld`, and so does
// anything this linker doesn't support: link_static() returns false and
// the driver runs `ld` instead.
//
// It works in three steps:
//   1. Read the inputs and resolve symbols, pulling in each archive
//      member that defines a symbol still needed.
//   2. Put every input section into an output section (.text, .data,
//      ...), make room for the GOT and the IFUNC stubs, and give
//      everything an address.
//   3. Copy the sections into the file, apply the relocations, and write
//      the headers and a symbol table (for debuggers).
//============================================================================

#include "mucc.h"
#include <elf.h>

//---------- Input files, sections and symbols -------------------------------

typedef struct ObjFile ObjFile;
typedef struct InSec InSec;
typedef struct OutSec OutSec;
typedef struct GSym GSym;

// An object file, or an archive member.
struct ObjFile {
  char *name;
  unsigned char *data;
  Elf64_Shdr *sh;
  int nsh;
  char *shstr;
  Elf64_Sym *syms;
  int nsyms;
  int first_global;
  char *strtab;
  InSec **secs;      // [nsh]: sections that go into the output, else NULL
  GSym **gsyms;      // [nsyms]: the global symbol for each global entry
};

// A section of an input file that goes into the output.
struct InSec {
  ObjFile *file;
  Elf64_Shdr *sh;
  char *name;
  OutSec *out;
  uint64_t offset;   // within out
  Elf64_Rela *rels;
  int nrels;
};

// Segments, in address order.
enum { SEG_R, SEG_RX, SEG_RODATA, SEG_RW, SEG_NONE };

struct OutSec {
  char *name;
  int type;
  uint64_t flags;
  uint64_t align;
  int seg;
  InSec **in;
  int nin, capin;
  uint64_t size;
  uint64_t addr;
  uint64_t offset;   // in the file
  int shndx;         // in the section header table
};

// A global symbol: one per name, across all files.
struct GSym {
  char *name;
  ObjFile *file;     // where it's defined (NULL if linker-defined or not)
  int symidx;
  bool is_defined;
  bool is_weak;      // defined as weak (a strong definition may replace it)
  bool strong_ref;   // referenced by a non-weak undefined symbol
  bool is_common;
  uint64_t common_size, common_align, common_off;
  uint64_t addr;     // after layout
  int iplt;          // IFUNC: index of its stub, else -1
};

static ObjFile **files;
static int nfiles, capfiles;
static HashMap gsyms;
static HashMap warnings;   // .gnu.warning.SYM sections: SYM -> message
static GSym **gsymlist;
static int ngsyms, capgsyms;
static HashMap comdat_groups;

static OutSec **outs;
static int nouts, capouts;

static jmp_buf *fail_jmp;
static char *fail_msg;

// Gives up on linking here; see link_static().
__attribute__((format(printf, 1, 2)))
static noreturn void fail(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fail_msg = vformat(fmt, ap);
  va_end(ap);
  longjmp(*fail_jmp, 1);
}

// Makes room for one more element in a growing array.
static void *grow(void *arr, int n, int *cap, int elem_size) {
  if (n < *cap)
    return arr;
  *cap = *cap ? *cap * 2 : 64;
  return realloc(arr, (size_t)*cap * elem_size);
}

static bool startswith(char *s, char *prefix) {
  return !strncmp(s, prefix, strlen(prefix));
}

static GSym *get_gsym(char *name) {
  GSym *g = hashmap_get(&gsyms, name);
  if (g)
    return g;
  g = calloc(1, sizeof(GSym));
  g->name = name;
  g->iplt = -1;
  hashmap_put(&gsyms, name, g);
  gsymlist = grow(gsymlist, ngsyms, &capgsyms, sizeof(GSym *));
  gsymlist[ngsyms++] = g;
  return g;
}

//---------- Which output section an input section goes to -------------------

// The output sections, in address order, and their segments. Input
// sections are sorted into them by out_name().
static struct { char *name; int seg; } out_order[] = {
  {".note.ABI-tag", SEG_R}, {".rela.plt", SEG_R},
  {".init", SEG_RX}, {".plt", SEG_RX}, {".text", SEG_RX}, {".fini", SEG_RX},
  {".rodata", SEG_RODATA}, {".eh_frame", SEG_RODATA},
  {".gcc_except_table", SEG_RODATA},
  {".tdata", SEG_RW}, {".tbss", SEG_RW}, {".preinit_array", SEG_RW},
  {".init_array", SEG_RW}, {".fini_array", SEG_RW}, {".data.rel.ro", SEG_RW},
  {".got", SEG_RW}, {".got.plt", SEG_RW}, {".data", SEG_RW}, {".bss", SEG_RW},
};

static bool strip_all; // -s: no symbol table or debug sections

// The output section for an input section, or NULL to leave it out.
static char *out_name(char *name, Elf64_Shdr *sh) {
  if (!(sh->sh_flags & SHF_ALLOC))
    return (!strip_all && startswith(name, ".debug_")) ? name : NULL;
  if (sh->sh_type == SHT_NOTE)
    return !strcmp(name, ".note.ABI-tag") ? ".note.ABI-tag" : NULL;
  if (startswith(name, ".gnu.warning") || !strcmp(name, ".stapsdt.base"))
    return NULL;

  if (sh->sh_flags & SHF_TLS)
    return sh->sh_type == SHT_NOBITS ? ".tbss" : ".tdata";
  if (sh->sh_type == SHT_PREINIT_ARRAY)
    return ".preinit_array";
  if (sh->sh_type == SHT_INIT_ARRAY)
    return ".init_array";
  if (sh->sh_type == SHT_FINI_ARRAY)
    return ".fini_array";

  if (sh->sh_flags & SHF_EXECINSTR) {
    if (!strcmp(name, ".init") || !strcmp(name, ".fini"))
      return name;
    return ".text";
  }

  if (!(sh->sh_flags & SHF_WRITE)) {
    if (!strcmp(name, ".eh_frame"))
      return ".eh_frame";
    if (startswith(name, ".gcc_except_table"))
      return ".gcc_except_table";
    return ".rodata";
  }

  if (sh->sh_type == SHT_NOBITS)
    return ".bss";
  if (startswith(name, ".data.rel.ro"))
    return ".data.rel.ro";
  return ".data";
}

static OutSec *find_out(char *name) {
  for (int i = 0; i < nouts; i++)
    if (!strcmp(outs[i]->name, name))
      return outs[i];
  return NULL;
}

// Gets output section `name`, making it if needed.
static OutSec *get_out(char *name, int type, uint64_t flags) {
  OutSec *out = find_out(name);
  if (out)
    return out;

  out = calloc(1, sizeof(OutSec));
  out->name = name;
  out->type = type;
  out->flags = flags;
  out->align = 1;
  out->seg = SEG_NONE;
  for (int i = 0; i < sizeof(out_order) / sizeof(*out_order); i++)
    if (!strcmp(out_order[i].name, name))
      out->seg = out_order[i].seg;
  outs = grow(outs, nouts, &capouts, sizeof(OutSec *));
  outs[nouts++] = out;
  return out;
}

//---------- Reading object files --------------------------------------------

static bool is_elf_object(unsigned char *data, size_t size) {
  return size >= sizeof(Elf64_Ehdr) && !memcmp(data, ELFMAG, SELFMAG);
}

// Registers file `f`'s global symbols: definitions, and references that
// may need something pulled in from an archive.
static void add_symbols(ObjFile *f) {
  for (int i = f->first_global; i < f->nsyms; i++) {
    Elf64_Sym *s = &f->syms[i];
    char *name = f->strtab + s->st_name;
    GSym *g = get_gsym(name);
    f->gsyms[i] = g;

    int bind = ELF64_ST_BIND(s->st_info);
    int shndx = s->st_shndx;
    bool in_dropped = shndx != SHN_UNDEF && shndx < SHN_LORESERVE && !f->secs[shndx];

    // A reference (or a definition in a duplicate COMDAT group).
    if (shndx == SHN_UNDEF || in_dropped) {
      if (bind == STB_GLOBAL && !in_dropped)
        g->strong_ref = true;
      continue;
    }

    if (shndx == SHN_COMMON) {
      if (g->is_defined && !g->is_common)
        continue; // a real definition beats a common one
      g->is_defined = g->is_common = true;
      g->common_size = MAX(g->common_size, s->st_size);
      g->common_align = MAX(g->common_align, s->st_value);
      g->file = f;
      g->symidx = i;
      continue;
    }

    bool weak = bind == STB_WEAK;
    if (g->is_defined && !g->is_common) {
      if (weak || !g->is_weak) {
        if (!weak && !g->is_weak)
          error("multiple definition of '%s' (in %s and %s)", name, g->file->name, f->name);
        continue;
      }
    }
    g->is_defined = true;
    g->is_common = false;
    g->is_weak = weak;
    g->file = f;
    g->symidx = i;
  }
}

static void read_object(char *name, unsigned char *data, size_t size) {
  Elf64_Ehdr *eh = (Elf64_Ehdr *)data;
  if (!is_elf_object(data, size) || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
      eh->e_machine != EM_X86_64)
    fail("%s: not an x86-64 ELF object", name);
  if (eh->e_type != ET_REL)
    fail("%s: not a relocatable object", name);
  if (eh->e_shnum == 0 || eh->e_shstrndx >= SHN_LORESERVE)
    fail("%s: unsupported section table", name);

  ObjFile *f = calloc(1, sizeof(ObjFile));
  f->name = name;
  f->data = data;
  f->sh = (Elf64_Shdr *)(data + eh->e_shoff);
  f->nsh = eh->e_shnum;
  f->shstr = (char *)data + f->sh[eh->e_shstrndx].sh_offset;
  f->secs = calloc(f->nsh, sizeof(InSec *));

  for (int i = 0; i < f->nsh; i++) {
    Elf64_Shdr *sh = &f->sh[i];
    if (sh->sh_type == SHT_SYMTAB) {
      f->syms = (Elf64_Sym *)(data + sh->sh_offset);
      f->nsyms = sh->sh_size / sizeof(Elf64_Sym);
      f->first_global = sh->sh_info;
      f->strtab = (char *)data + f->sh[sh->sh_link].sh_offset;
    }
  }
  f->gsyms = calloc(f->nsyms + 1, sizeof(GSym *));

  for (int i = 1; i < f->nsh; i++) {
    Elf64_Shdr *sh = &f->sh[i];
    char *secname = f->shstr + sh->sh_name;

    // A warning to print if the program uses this symbol, like glibc's
    // about dlopen() in static programs.
    if (startswith(secname, ".gnu.warning.")) {
      char *msg = strndup((char *)data + sh->sh_offset, sh->sh_size);
      hashmap_put(&warnings, secname + 13, msg);
      continue;
    }

    switch (sh->sh_type) {
    case SHT_PROGBITS: case SHT_NOBITS: case SHT_NOTE: case SHT_INIT_ARRAY:
    case SHT_FINI_ARRAY: case SHT_PREINIT_ARRAY: case SHT_X86_64_UNWIND:
      break;
    case SHT_RELA: case SHT_SYMTAB: case SHT_STRTAB: case SHT_GROUP:
    case SHT_NULL:
      continue;
    default:
      fail("%s: unsupported section type 0x%x (%s)", name, sh->sh_type, secname);
    }
    if (!out_name(secname, sh))
      continue;

    InSec *sec = calloc(1, sizeof(InSec));
    sec->file = f;
    sec->sh = sh;
    sec->name = secname;
    f->secs[i] = sec;
  }

  // A COMDAT group seen before (by its signature symbol) is a duplicate:
  // leave all its sections out.
  for (int i = 1; i < f->nsh; i++) {
    Elf64_Shdr *sh = &f->sh[i];
    if (sh->sh_type != SHT_GROUP)
      continue;
    uint32_t *words = (uint32_t *)(data + sh->sh_offset);
    if (!(words[0] & GRP_COMDAT))
      continue;
    char *sig = f->strtab + f->syms[sh->sh_info].st_name;
    if (!hashmap_get(&comdat_groups, sig)) {
      hashmap_put(&comdat_groups, sig, f);
      continue;
    }
    for (int k = 1; k < sh->sh_size / 4; k++)
      f->secs[words[k]] = NULL;
  }

  // Attach relocations to the sections they apply to.
  for (int i = 1; i < f->nsh; i++) {
    Elf64_Shdr *sh = &f->sh[i];
    if (sh->sh_type == SHT_RELA && f->secs[sh->sh_info]) {
      InSec *sec = f->secs[sh->sh_info];
      sec->rels = (Elf64_Rela *)(data + sh->sh_offset);
      sec->nrels = sh->sh_size / sizeof(Elf64_Rela);
    }
  }

  // (crtn.o has no symbol table: just the tail bytes of .init and .fini.)
  add_symbols(f);
  files = grow(files, nfiles, &capfiles, sizeof(ObjFile *));
  files[nfiles++] = f;
}

//---------- Archives and linker scripts -------------------------------------

typedef struct {
  char *path;
  unsigned char *data;
  size_t size;
  char *longnames;     // the "//" member: names too long for the header
  int nsyms;
  char **symnames;     // from the symbol index ("/" member)
  uint64_t *offsets;   // where the member defining each symbol starts
  HashMap loaded;      // member offsets already read
} Archive;

static Archive **archives;
static int narchives, caparchives;

static uint64_t read_be(unsigned char *p, int n) {
  uint64_t v = 0;
  for (int i = 0; i < n; i++)
    v = v << 8 | p[i];
  return v;
}

// The size field of the archive member header at `hdr`.
static uint64_t member_size(unsigned char *hdr) {
  return strtoull((char *)hdr + 48, NULL, 10);
}

static void read_archive(char *path, unsigned char *data, size_t size) {
  Archive *ar = calloc(1, sizeof(Archive));
  ar->path = path;
  ar->data = data;
  ar->size = size;

  for (uint64_t off = 8; off + 60 <= size;) {
    unsigned char *hdr = data + off;
    uint64_t len = member_size(hdr);
    unsigned char *body = hdr + 60;

    if (!memcmp(hdr, "/               ", 16) || !memcmp(hdr, "/SYM64/         ", 16)) {
      int w = hdr[1] == 'S' ? 8 : 4; // 32- or 64-bit symbol index
      ar->nsyms = read_be(body, w);
      ar->offsets = calloc(ar->nsyms, sizeof(uint64_t));
      ar->symnames = calloc(ar->nsyms, sizeof(char *));
      char *names = (char *)body + w + (uint64_t)ar->nsyms * w;
      for (int i = 0; i < ar->nsyms; i++) {
        ar->offsets[i] = read_be(body + w + (uint64_t)i * w, w);
        ar->symnames[i] = names;
        names += strlen(names) + 1;
      }
    } else if (!memcmp(hdr, "//              ", 16)) {
      ar->longnames = (char *)body;
    }
    off += 60 + len + (len & 1);
  }

  archives = grow(archives, narchives, &caparchives, sizeof(Archive *));
  archives[narchives++] = ar;
}

// Reads the archive member at `off` as an object file.
static void load_member(Archive *ar, uint64_t off) {
  char *key = format("%lu", (unsigned long)off);
  if (hashmap_get(&ar->loaded, key))
    return;
  hashmap_put(&ar->loaded, key, (void *)1);

  unsigned char *hdr = ar->data + off;
  char name[17] = {0};
  memcpy(name, hdr, 16);
  char *member;
  if (name[0] == '/' && ar->longnames) {
    char *s = ar->longnames + atoi(name + 1);
    member = strndup(s, strcspn(s, "/\n"));
  } else {
    member = strndup(name, strcspn(name, "/ "));
  }
  read_object(format("%s(%s)", ar->path, member), hdr + 60, member_size(hdr));
}

static unsigned char *read_file(char *path, size_t *size) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    error("%s: %s", path, strerror(errno));
  fseek(fp, 0, SEEK_END);
  *size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  unsigned char *buf = malloc(*size + 1);
  if (fread(buf, 1, *size, fp) != *size)
    error("%s: read error", path);
  buf[*size] = '\0';
  fclose(fp);
  return buf;
}

static char *find_library(char *name, StringArray *lib_paths);
static void read_input(char *path, char *display, StringArray *lib_paths);

// A linker script, as some .a "libraries" are:
//   OUTPUT_FORMAT(elf64-x86-64)
//   GROUP ( /usr/lib/.../libm-2.43.a /usr/lib/.../libmvec.a )
// Only the file names in GROUP(...) and INPUT(...) are used; other
// commands, like OUTPUT_FORMAT(...), are skipped.
static void read_script(char *path, char *text, StringArray *lib_paths) {
  for (char *p = text; *p;) {
    if (startswith(p, "/*")) {
      char *end = strstr(p, "*/");
      p = end ? end + 2 : p + strlen(p);
      continue;
    }
    if (startswith(p, "OUTPUT_FORMAT") || startswith(p, "OUTPUT_ARCH") ||
        startswith(p, "SEARCH_DIR")) {
      char *end = strchr(p, ')');
      if (!end)
        fail("%s: unsupported linker script", path);
      p = end + 1;
      continue;
    }
    if (startswith(p, "GROUP") || startswith(p, "INPUT")) {
      p = strchr(p, '(');
      if (!p)
        fail("%s: unsupported linker script", path);
      p++;
      // File names, possibly inside AS_NEEDED( ... ), up to the ')'.
      for (int depth = 0;;) {
        while (isspace(*p) || *p == ',')
          p++;
        if (!*p)
          fail("%s: unterminated linker script", path);
        if (*p == ')') {
          p++;
          if (depth-- == 0)
            break;
          continue;
        }
        if (*p == '(') {
          p++;
          depth++;
          continue;
        }
        if (startswith(p, "AS_NEEDED")) {
          p += 9;
          continue;
        }
        char *start = p;
        while (*p && !isspace(*p) && *p != ',' && *p != ')')
          p++;
        char *arg = strndup(start, p - start);
        if (startswith(arg, "-l"))
          read_input(find_library(arg + 2, lib_paths), NULL, lib_paths);
        else
          read_input(arg, NULL, lib_paths);
      }
      continue;
    }
    if (isspace(*p)) {
      p++;
      continue;
    }
    fail("%s: unsupported linker script", path);
  }
}

// Reads an object file, an archive or a linker script. `display` is the
// name for messages (a temporary object's source file), or NULL.
static void read_input(char *path, char *display, StringArray *lib_paths) {
  size_t size;
  unsigned char *data = read_file(path, &size);
  if (size >= 8 && !memcmp(data, "!<arch>\n", 8))
    read_archive(path, data, size);
  else if (is_elf_object(data, size))
    read_object(display ? display : path, data, size);
  else
    read_script(path, (char *)data, lib_paths);
}

// Finds libNAME.a for -lNAME.
static char *find_library(char *name, StringArray *lib_paths) {
  for (int i = 0; i < lib_paths->len; i++) {
    char *path = format("%s/lib%s.a", lib_paths->data[i], name);
    if (file_exists(path))
      return path;
  }
  error("cannot find -l%s", name);
}

// Pulls in the archive members that define symbols still needed, until
// no more are. All archives are searched together, in any order, as if
// in one --start-group.
static void load_archive_members(void) {
  for (bool changed = true; changed;) {
    changed = false;
    for (int i = 0; i < narchives; i++) {
      Archive *ar = archives[i];
      for (int k = 0; k < ar->nsyms; k++) {
        GSym *g = hashmap_get(&gsyms, ar->symnames[k]);
        if (!g || g->is_defined || !g->strong_ref)
          continue;
        char *key = format("%lu", (unsigned long)ar->offsets[k]);
        if (hashmap_get(&ar->loaded, key))
          continue;
        load_member(ar, ar->offsets[k]);
        changed = true;
      }
    }
  }
}

//---------- Symbols the linker defines --------------------------------------

// Symbols that programs and the C library refer to but no object
// defines: section boundaries and the like. Their values are set in
// set_special_symbols(), after layout.
static char *special_names[] = {
  "__ehdr_start", "__executable_start", "_GLOBAL_OFFSET_TABLE_",
  "__init_array_start", "__init_array_end", "__fini_array_start",
  "__fini_array_end", "__preinit_array_start", "__preinit_array_end",
  "__rela_iplt_start", "__rela_iplt_end", "_etext", "etext", "__etext",
  "_edata", "edata", "__bss_start", "_end", "end",
};

static void define_specials(void) {
  for (int i = 0; i < sizeof(special_names) / sizeof(*special_names); i++) {
    GSym *g = hashmap_get(&gsyms, special_names[i]);
    if (g && !g->is_defined) {
      g->is_defined = true;
      g->file = NULL;
    }
  }
}

//---------- GOT entries and IFUNC stubs -------------------------------------

// GOT entries hold a symbol's address (for GOTPCREL relocations) or its
// offset from the thread pointer (for GOTTPOFF). Each is made once.
enum { GOT_ADDR, GOT_TPOFF };

typedef struct {
  ObjFile *file;
  int symidx;
  int kind;
} GotEntry;

static GotEntry *got;
static int ngot, capgot;
static HashMap got_map;

static GSym **iplt;  // IFUNC symbols that got a stub
static int niplt, capiplt;

static char *got_key(ObjFile *f, int i, int kind) {
  if (i >= f->first_global)
    return format("%s %d", f->gsyms[i]->name, kind);
  return format("%p %d %d", (void *)f, i, kind);
}

static int got_index(ObjFile *f, int i, int kind) {
  char *key = got_key(f, i, kind);
  intptr_t idx = (intptr_t)hashmap_get(&got_map, key);
  if (idx)
    return idx - 1;
  got = grow(got, ngot, &capgot, sizeof(GotEntry));
  got[ngot] = (GotEntry){f, i, kind};
  hashmap_put(&got_map, key, (void *)(intptr_t)(ngot + 1));
  return ngot++;
}

// Is symbol i of f an IFUNC: a function whose address a resolver
// function returns at startup (glibc uses them to pick memcpy etc.)?
static bool is_ifunc(ObjFile *f, int i) {
  if (i < f->first_global)
    return ELF64_ST_TYPE(f->syms[i].st_info) == STT_GNU_IFUNC;
  GSym *g = f->gsyms[i];
  return g->file && ELF64_ST_TYPE(g->file->syms[g->symidx].st_info) == STT_GNU_IFUNC;
}

// Makes the GOT entries and IFUNC stubs the relocations need.
static void scan_relocations(void) {
  for (int i = 0; i < nfiles; i++) {
    ObjFile *f = files[i];
    for (int k = 0; k < f->nsh; k++) {
      InSec *sec = f->secs[k];
      if (!sec || !(sec->sh->sh_flags & SHF_ALLOC))
        continue;
      for (int r = 0; r < sec->nrels; r++) {
        Elf64_Rela *rel = &sec->rels[r];
        int type = ELF64_R_TYPE(rel->r_info);
        int s = ELF64_R_SYM(rel->r_info);

        if (is_ifunc(f, s)) {
          if (s < f->first_global)
            fail("%s: local IFUNC symbols are not supported", f->name);
          GSym *g = f->gsyms[s];
          if (g->iplt < 0) {
            g->iplt = niplt;
            iplt = grow(iplt, niplt, &capiplt, sizeof(GSym *));
            iplt[niplt++] = g;
          }
        }

        switch (type) {
        case R_X86_64_GOTPCREL:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
          got_index(f, s, GOT_ADDR);
          break;
        case R_X86_64_GOTTPOFF:
          got_index(f, s, GOT_TPOFF);
          break;
        }
      }
    }
  }
}

//---------- Layout ----------------------------------------------------------

#define BASE 0x400000
#define PAGE 0x1000

static uint64_t align_up(uint64_t n, uint64_t align) {
  return align ? (n + align - 1) / align * align : n;
}

// Commons and synthesized contents go at the end of their sections.
static OutSec *bss, *got_sec, *gotplt_sec, *plt_sec, *relaplt_sec;
static uint64_t common_start; // in .bss, after the input sections

// Puts each input section in its output section, in input order.
static void assign_sections(void) {
  for (int i = 0; i < nfiles; i++) {
    ObjFile *f = files[i];
    for (int k = 0; k < f->nsh; k++) {
      InSec *sec = f->secs[k];
      if (!sec)
        continue;
      Elf64_Shdr *sh = sec->sh;
      int type = sh->sh_type;
      if (type == SHT_X86_64_UNWIND)
        type = SHT_PROGBITS;
      if (type == SHT_NOTE && !(sh->sh_flags & SHF_ALLOC))
        continue;
      uint64_t flags = sh->sh_flags & (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR | SHF_TLS);
      OutSec *out = get_out(out_name(sec->name, sh), type, flags);
      out->align = MAX(out->align, MAX(1, sh->sh_addralign));
      out->flags |= flags;
      sec->out = out;
      sec->offset = align_up(out->size, MAX(1, sh->sh_addralign));
      out->size = sec->offset + sh->sh_size;
      out->in = grow(out->in, out->nin, &out->capin, sizeof(InSec *));
      out->in[out->nin++] = sec;
    }
  }

  // Common symbols: at the end of .bss.
  bss = get_out(".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
  common_start = bss->size;
  for (int i = 0; i < ngsyms; i++) {
    GSym *g = gsymlist[i];
    if (!g->is_common)
      continue;
    bss->align = MAX(bss->align, g->common_align);
    g->common_off = align_up(bss->size, MAX(1, g->common_align));
    bss->size = g->common_off + g->common_size;
  }

  if (ngot) {
    got_sec = get_out(".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    got_sec->align = 8;
    got_sec->size = ngot * 8;
  }
  if (niplt) {
    relaplt_sec = get_out(".rela.plt", SHT_RELA, SHF_ALLOC);
    relaplt_sec->align = 8;
    relaplt_sec->size = niplt * sizeof(Elf64_Rela);
    plt_sec = get_out(".plt", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    plt_sec->align = 16;
    plt_sec->size = niplt * 16;
    gotplt_sec = get_out(".got.plt", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    gotplt_sec->align = 8;
    gotplt_sec->size = niplt * 8;
  }
}

static int out_rank(OutSec *out) {
  for (int i = 0; i < sizeof(out_order) / sizeof(*out_order); i++)
    if (!strcmp(out_order[i].name, out->name))
      return i;
  return 1000; // non-alloc (debug) sections: after the rest
}

// Sorts the output sections into address order.
static void sort_sections(void) {
  for (int i = 1; i < nouts; i++)
    for (int j = i; j > 0 && out_rank(outs[j - 1]) > out_rank(outs[j]); j--) {
      OutSec *t = outs[j];
      outs[j] = outs[j - 1];
      outs[j - 1] = t;
    }
}

static int nphdrs;
static uint64_t tls_start, tls_filesz, tls_memsz, tls_align;
static uint64_t file_end; // end of the allocated sections in the file

static bool has_out(char *name) {
  OutSec *out = find_out(name);
  return out && out->size;
}

// Gives each output section its address and file offset. Each segment
// starts on a new page, and addresses are BASE + file offset, so
// everything maps with one mmap per segment.
static void layout(void) {
  // Program headers: one PT_LOAD per segment in use, then PT_TLS, PT_NOTE
  // and PT_GNU_STACK.
  bool used[SEG_NONE] = {true}; // the first also holds the headers
  for (int i = 0; i < nouts; i++)
    if (outs[i]->seg != SEG_NONE && outs[i]->size)
      used[outs[i]->seg] = true;
  nphdrs = 1;
  for (int s = 0; s < SEG_NONE; s++)
    nphdrs += used[s];
  nphdrs += has_out(".tdata") || has_out(".tbss");
  nphdrs += has_out(".note.ABI-tag");

  uint64_t off = sizeof(Elf64_Ehdr) + nphdrs * sizeof(Elf64_Phdr);
  int seg = SEG_R;
  uint64_t tdata_end = 0;

  for (int i = 0; i < nouts; i++) {
    OutSec *out = outs[i];
    if (out->seg == SEG_NONE)
      continue;
    if (out->seg != seg) {
      seg = out->seg;
      off = align_up(off, PAGE);
    }

    // .tbss is only the TLS template's tail: it takes no space here.
    if (!strcmp(out->name, ".tbss")) {
      out->addr = align_up(BASE + off, out->align);
      out->offset = out->addr - BASE;
      tdata_end = out->addr + out->size;
      continue;
    }

    off = align_up(off, out->align);
    out->offset = off;
    out->addr = BASE + off;
    if (out->type != SHT_NOBITS)
      off += out->size;
  }
  file_end = off;

  // The TLS template: .tdata then .tbss.
  OutSec *tdata = find_out(".tdata"), *tbss = find_out(".tbss");
  if ((tdata && tdata->size) || (tbss && tbss->size)) {
    OutSec *first = (tdata && tdata->size) ? tdata : tbss;
    tls_start = first->addr;
    tls_filesz = (tdata && tdata->size) ? tdata->size : 0;
    uint64_t end = tbss && tbss->size ? tdata_end : tls_start + tls_filesz;
    tls_memsz = end - tls_start;
    tls_align = MAX(tdata ? tdata->align : 1, tbss ? tbss->align : 1);
  }

  // Non-allocated (debug) sections follow, at address 0.
  for (int i = 0; i < nouts; i++) {
    OutSec *out = outs[i];
    if (out->seg != SEG_NONE)
      continue;
    off = align_up(off, out->align);
    out->offset = off;
    out->addr = 0;
    off += out->size;
  }
}

//---------- Symbol addresses ------------------------------------------------

static uint64_t end_of(char *name, bool end) {
  OutSec *out = find_out(name);
  if (!out)
    return 0;
  return out->addr + (end ? out->size : 0);
}

// The end of the last section of segment `seg` (in memory).
static uint64_t segment_end(int seg) {
  uint64_t end = 0;
  for (int i = 0; i < nouts; i++)
    if (outs[i]->seg == seg && strcmp(outs[i]->name, ".tbss"))
      end = MAX(end, outs[i]->addr + outs[i]->size);
  return end;
}

static void set_special(char *name, uint64_t addr) {
  GSym *g = hashmap_get(&gsyms, name);
  if (g && !g->file)
    g->addr = addr;
}

static void set_symbol_addresses(void) {
  for (int i = 0; i < ngsyms; i++) {
    GSym *g = gsymlist[i];
    if (!g->file)
      continue;
    if (g->is_common) {
      g->addr = bss->addr + g->common_off;
      continue;
    }
    Elf64_Sym *s = &g->file->syms[g->symidx];
    if (s->st_shndx == SHN_ABS) {
      g->addr = s->st_value;
      continue;
    }
    InSec *sec = g->file->secs[s->st_shndx];
    g->addr = sec->out->addr + sec->offset + s->st_value;
  }

  set_special("__ehdr_start", BASE);
  set_special("__executable_start", BASE);
  set_special("_GLOBAL_OFFSET_TABLE_", gotplt_sec ? gotplt_sec->addr : end_of(".got", false));
  set_special("__preinit_array_start", end_of(".preinit_array", false));
  set_special("__preinit_array_end", end_of(".preinit_array", true));
  set_special("__init_array_start", end_of(".init_array", false));
  set_special("__init_array_end", end_of(".init_array", true));
  set_special("__fini_array_start", end_of(".fini_array", false));
  set_special("__fini_array_end", end_of(".fini_array", true));
  set_special("__rela_iplt_start", end_of(".rela.plt", false));
  set_special("__rela_iplt_end", end_of(".rela.plt", true));
  uint64_t etext = segment_end(SEG_RX);
  set_special("_etext", etext);
  set_special("etext", etext);
  set_special("__etext", etext);
  uint64_t edata = bss->addr;
  set_special("_edata", edata);
  set_special("edata", edata);
  set_special("__bss_start", bss->addr);
  set_special("_end", bss->addr + bss->size);
  set_special("end", bss->addr + bss->size);
}

// The address of symbol i of file f, as relocations see it: an IFUNC's
// is its stub's, and an undefined weak symbol's is 0.
static uint64_t sym_value(ObjFile *f, int i) {
  if (i >= f->first_global) {
    GSym *g = f->gsyms[i];
    if (g->iplt >= 0)
      return plt_sec->addr + g->iplt * 16;
    return g->addr;
  }

  Elf64_Sym *s = &f->syms[i];
  if (s->st_shndx == SHN_ABS)
    return s->st_value;
  if (s->st_shndx == SHN_UNDEF)
    return 0;
  InSec *sec = f->secs[s->st_shndx];
  if (!sec)
    fail("%s: reference to a section that was left out", f->name);
  uint64_t base = sec->out->addr + sec->offset;
  return ELF64_ST_TYPE(s->st_info) == STT_SECTION ? base : base + s->st_value;
}

// A thread-local variable's offset from the thread pointer, which on
// x86-64 points just past the (aligned) TLS block.
static int64_t tpoff(uint64_t addr) {
  return (int64_t)(addr - tls_start) - (int64_t)align_up(tls_memsz, tls_align);
}

//---------- Relocations -----------------------------------------------------

static unsigned char *image; // the output file
static uint64_t image_size;  // its sections' part (the tables follow)

static void put32(unsigned char *p, int64_t val, bool is_signed, InSec *sec, int type) {
  bool fits = is_signed ? val == (int32_t)val : (uint64_t)val == (uint32_t)val;
  if (!fits)
    fail("%s: relocation type %d in %s out of range", sec->file->name, type, sec->name);
  memcpy(p, &val, 4);
}

static void put64(unsigned char *p, uint64_t val) {
  memcpy(p, &val, 8);
}

static void apply_relocations(InSec *sec) {
  ObjFile *f = sec->file;
  unsigned char *base = image + sec->out->offset + sec->offset;
  uint64_t addr = sec->out->addr + sec->offset;

  for (int r = 0; r < sec->nrels; r++) {
    Elf64_Rela *rel = &sec->rels[r];
    int type = ELF64_R_TYPE(rel->r_info);
    int s = ELF64_R_SYM(rel->r_info);
    unsigned char *loc = base + rel->r_offset;
    uint64_t S = sym_value(f, s);
    int64_t A = rel->r_addend;
    uint64_t P = addr + rel->r_offset;

    switch (type) {
    case R_X86_64_NONE:
      break;
    case R_X86_64_64:
      put64(loc, S + A);
      break;
    case R_X86_64_PC32:
    case R_X86_64_PLT32:
      put32(loc, S + A - P, true, sec, type);
      break;
    case R_X86_64_PC64:
      put64(loc, S + A - P);
      break;
    case R_X86_64_32:
      put32(loc, S + A, false, sec, type);
      break;
    case R_X86_64_32S:
      put32(loc, S + A, true, sec, type);
      break;
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX: {
      uint64_t G = got_sec->addr + got_index(f, s, GOT_ADDR) * 8;
      put32(loc, G + A - P, true, sec, type);
      break;
    }
    case R_X86_64_GOTTPOFF: {
      uint64_t G = got_sec->addr + got_index(f, s, GOT_TPOFF) * 8;
      put32(loc, G + A - P, true, sec, type);
      break;
    }
    case R_X86_64_TPOFF32:
      put32(loc, tpoff(S) + A, true, sec, type);
      break;
    case R_X86_64_TPOFF64:
      put64(loc, tpoff(S) + A);
      break;
    case R_X86_64_DTPOFF32:
      put32(loc, S + A - tls_start, true, sec, type);
      break;
    case R_X86_64_DTPOFF64:
      put64(loc, S + A - tls_start);
      break;
    default:
      fail("%s: unsupported relocation type %d in %s", f->name, type, sec->name);
    }
  }
}

//---------- Writing the executable ------------------------------------------

typedef struct {
  unsigned char *data;
  int len, cap;
} Buf;

static void buf_add(Buf *b, void *data, int len) {
  while (b->len + len > b->cap) {
    b->cap = b->cap ? b->cap * 2 : 4096;
    b->data = realloc(b->data, b->cap);
  }
  memcpy(b->data + b->len, data, len);
  b->len += len;
}

static int buf_str(Buf *b, char *s) {
  int off = b->len;
  buf_add(b, s, strlen(s) + 1);
  return off;
}

static void add_sym(Buf *symtab, Buf *strtab, char *name, int bind, int type,
                    int shndx, uint64_t value, uint64_t size) {
  Elf64_Sym s = {0};
  s.st_name = buf_str(strtab, name);
  s.st_info = ELF64_ST_INFO(bind, type);
  s.st_shndx = shndx;
  s.st_value = value;
  s.st_size = size;
  buf_add(symtab, &s, sizeof(s));
}

// The symbol table: functions and objects, so debuggers can name them.
static void make_symtab(Buf *symtab, Buf *strtab, int *first_global) {
  Elf64_Sym null = {0};
  buf_add(symtab, &null, sizeof(null));
  buf_add(strtab, "", 1);

  for (int i = 0; i < nfiles; i++) {
    ObjFile *f = files[i];
    for (int k = 1; k < f->first_global; k++) {
      Elf64_Sym *s = &f->syms[k];
      int type = ELF64_ST_TYPE(s->st_info);
      char *name = f->strtab + s->st_name;
      if ((type != STT_FUNC && type != STT_OBJECT && type != STT_TLS) || !*name ||
          s->st_shndx == SHN_UNDEF || s->st_shndx >= SHN_LORESERVE)
        continue;
      InSec *sec = f->secs[s->st_shndx];
      if (!sec)
        continue;
      add_sym(symtab, strtab, name, STB_LOCAL, type, sec->out->shndx,
              sym_value(f, k), s->st_size);
    }
  }

  *first_global = symtab->len / sizeof(Elf64_Sym);
  for (int i = 0; i < ngsyms; i++) {
    GSym *g = gsymlist[i];
    if (!g->is_defined || !g->file)
      continue;
    Elf64_Sym *s = &g->file->syms[g->symidx];
    int shndx = SHN_ABS;
    if (g->is_common)
      shndx = bss->shndx;
    else if (s->st_shndx != SHN_ABS)
      shndx = g->file->secs[s->st_shndx]->out->shndx;
    int type = ELF64_ST_TYPE(s->st_info);
    add_sym(symtab, strtab, g->name, g->is_weak ? STB_WEAK : STB_GLOBAL,
            type == STT_GNU_IFUNC ? STT_FUNC : type, shndx, g->addr,
            g->is_common ? g->common_size : s->st_size);
  }
}

static void write_executable(char *path) {
  bool strip = strip_all;

  // Section headers: the output sections, then .symtab, .strtab and
  // .shstrtab (just .shstrtab if stripped).
  int nsh = 1;
  for (int i = 0; i < nouts; i++)
    outs[i]->shndx = nsh++;

  Buf symtab = {0}, strtab = {0}, shstrtab = {0};
  int first_global = 0;
  if (!strip)
    make_symtab(&symtab, &strtab, &first_global);
  buf_str(&shstrtab, "");

  uint64_t symtab_off = align_up(image_size, 8);
  uint64_t strtab_off = symtab_off + symtab.len;
  uint64_t shstr_off = strtab_off + strtab.len;

  int *names = calloc(nouts, sizeof(int));
  for (int i = 0; i < nouts; i++)
    names[i] = buf_str(&shstrtab, outs[i]->name);
  int symtab_name = buf_str(&shstrtab, ".symtab");
  int strtab_name = buf_str(&shstrtab, ".strtab");
  int shstr_name = buf_str(&shstrtab, ".shstrtab");
  int nsh_total = nsh + (strip ? 1 : 3);
  uint64_t shoff = align_up(shstr_off + shstrtab.len, 8);
  uint64_t size = shoff + nsh_total * sizeof(Elf64_Shdr);

  // The sections are in `image` already; add the tables after them.
  image = realloc(image, size);
  memset(image + image_size, 0, size - image_size);
  memcpy(image + symtab_off, symtab.data, symtab.len);
  memcpy(image + strtab_off, strtab.data, strtab.len);
  memcpy(image + shstr_off, shstrtab.data, shstrtab.len);

  Elf64_Shdr *sh = (Elf64_Shdr *)(image + shoff);
  for (int i = 0; i < nouts; i++) {
    OutSec *out = outs[i];
    Elf64_Shdr *s = &sh[out->shndx];
    s->sh_name = names[i];
    s->sh_type = out->type;
    s->sh_flags = out->flags;
    s->sh_addr = out->addr;
    s->sh_offset = out->offset;
    s->sh_size = out->size;
    s->sh_addralign = out->align;
    if (out->type == SHT_RELA)
      s->sh_entsize = sizeof(Elf64_Rela);
  }
  int shstrndx = nsh;
  if (!strip) {
    Elf64_Shdr *s = &sh[nsh];
    s->sh_name = symtab_name;
    s->sh_type = SHT_SYMTAB;
    s->sh_offset = symtab_off;
    s->sh_size = symtab.len;
    s->sh_link = nsh + 1;
    s->sh_info = first_global;
    s->sh_addralign = 8;
    s->sh_entsize = sizeof(Elf64_Sym);
    s = &sh[nsh + 1];
    s->sh_name = strtab_name;
    s->sh_type = SHT_STRTAB;
    s->sh_offset = strtab_off;
    s->sh_size = strtab.len;
    s->sh_addralign = 1;
    shstrndx = nsh + 2;
  }
  Elf64_Shdr *s = &sh[shstrndx];
  s->sh_name = shstr_name;
  s->sh_type = SHT_STRTAB;
  s->sh_offset = shstr_off;
  s->sh_size = shstrtab.len;
  s->sh_addralign = 1;

  // Program headers.
  Elf64_Phdr *ph = (Elf64_Phdr *)(image + sizeof(Elf64_Ehdr));
  int n = 0;
  static int seg_flags[] = {PF_R, PF_R | PF_X, PF_R, PF_R | PF_W};
  for (int seg = 0; seg < SEG_NONE; seg++) {
    uint64_t start = UINT64_MAX, fend = 0, mend = 0;
    for (int i = 0; i < nouts; i++) {
      OutSec *out = outs[i];
      if (out->seg != seg || !out->size || !strcmp(out->name, ".tbss"))
        continue;
      start = MIN(start, out->offset);
      if (out->type != SHT_NOBITS)
        fend = MAX(fend, out->offset + out->size);
      mend = MAX(mend, out->addr + out->size);
    }
    // The first segment also maps the ELF and program headers, which
    // the C library reads at startup (to find PT_TLS, for one).
    if (seg == SEG_R) {
      start = 0;
      fend = MAX(fend, sizeof(Elf64_Ehdr) + nphdrs * sizeof(Elf64_Phdr));
      mend = MAX(mend, BASE + fend);
    }
    if (start == UINT64_MAX)
      continue;
    start = start / PAGE * PAGE;
    ph[n++] = (Elf64_Phdr){
      .p_type = PT_LOAD, .p_flags = seg_flags[seg], .p_offset = start,
      .p_vaddr = BASE + start, .p_paddr = BASE + start,
      .p_filesz = fend > start ? fend - start : 0,
      .p_memsz = mend - (BASE + start), .p_align = PAGE,
    };
  }
  if (tls_memsz || tls_filesz) {
    ph[n++] = (Elf64_Phdr){
      .p_type = PT_TLS, .p_flags = PF_R, .p_offset = tls_start - BASE,
      .p_vaddr = tls_start, .p_paddr = tls_start, .p_filesz = tls_filesz,
      .p_memsz = tls_memsz, .p_align = tls_align,
    };
  }
  OutSec *note = find_out(".note.ABI-tag");
  if (note && note->size) {
    ph[n++] = (Elf64_Phdr){
      .p_type = PT_NOTE, .p_flags = PF_R, .p_offset = note->offset,
      .p_vaddr = note->addr, .p_paddr = note->addr, .p_filesz = note->size,
      .p_memsz = note->size, .p_align = note->align,
    };
  }
  ph[n++] = (Elf64_Phdr){.p_type = PT_GNU_STACK, .p_flags = PF_R | PF_W, .p_align = 16};
  assert(n == nphdrs);

  GSym *start = hashmap_get(&gsyms, "_start");
  if (!start || !start->is_defined)
    error("undefined reference to '_start'");

  Elf64_Ehdr *eh = (Elf64_Ehdr *)image;
  memset(eh, 0, sizeof(*eh));
  memcpy(eh->e_ident, ELFMAG, SELFMAG);
  eh->e_ident[EI_CLASS] = ELFCLASS64;
  eh->e_ident[EI_DATA] = ELFDATA2LSB;
  eh->e_ident[EI_VERSION] = EV_CURRENT;
  eh->e_ident[EI_OSABI] = ELFOSABI_SYSV;
  eh->e_type = ET_EXEC;
  eh->e_machine = EM_X86_64;
  eh->e_version = EV_CURRENT;
  eh->e_entry = start->addr;
  eh->e_phoff = sizeof(Elf64_Ehdr);
  eh->e_shoff = shoff;
  eh->e_ehsize = sizeof(Elf64_Ehdr);
  eh->e_phentsize = sizeof(Elf64_Phdr);
  eh->e_phnum = nphdrs;
  eh->e_shentsize = sizeof(Elf64_Shdr);
  eh->e_shnum = nsh_total;
  eh->e_shstrndx = shstrndx;

  unlink(path);
  FILE *fp = fopen(path, "wb");
  if (!fp)
    error("cannot open output file: %s: %s", path, strerror(errno));
  fwrite(image, 1, size, fp);
  fclose(fp);
  chmod(path, 0755);
}

// Copies the sections into the image, and fills in the GOT and the
// IFUNC stubs and their relocations.
static void fill_image(void) {
  image_size = file_end;
  for (int i = 0; i < nouts; i++)
    if (outs[i]->type != SHT_NOBITS)
      image_size = MAX(image_size, outs[i]->offset + outs[i]->size);
  image = calloc(1, image_size + 1);

  for (int i = 0; i < nouts; i++) {
    OutSec *out = outs[i];
    if (out->type == SHT_NOBITS)
      continue;
    for (int k = 0; k < out->nin; k++) {
      InSec *sec = out->in[k];
      memcpy(image + out->offset + sec->offset, sec->file->data + sec->sh->sh_offset,
             sec->sh->sh_size);
    }
  }

  for (int i = 0; i < ngot; i++) {
    GotEntry *e = &got[i];
    uint64_t addr = sym_value(e->file, e->symidx);
    put64(image + got_sec->offset + i * 8, e->kind == GOT_TPOFF ? tpoff(addr) : addr);
  }

  // Each IFUNC stub jumps through its .got.plt slot, which glibc fills at
  // startup from the IRELATIVE relocations in .rela.plt, by calling the
  // IFUNC's resolver.
  for (int i = 0; i < niplt; i++) {
    GSym *g = iplt[i];
    uint64_t slot = gotplt_sec->addr + i * 8;
    unsigned char *stub = image + plt_sec->offset + i * 16;
    uint64_t stub_addr = plt_sec->addr + i * 16;
    stub[0] = 0xff; // jmp *slot(%rip)
    stub[1] = 0x25;
    int32_t disp = slot - (stub_addr + 6);
    memcpy(stub + 2, &disp, 4);
    memset(stub + 6, 0xcc, 10);

    Elf64_Rela rela = {slot, ELF64_R_INFO(0, R_X86_64_IRELATIVE), g->addr};
    memcpy(image + relaplt_sec->offset + i * sizeof(rela), &rela, sizeof(rela));
  }
}

//---------- Entry point -----------------------------------------------------

static void reset(void) {
  files = NULL;
  nfiles = capfiles = 0;
  gsyms = (HashMap){0};
  warnings = (HashMap){0};
  gsymlist = NULL;
  ngsyms = capgsyms = 0;
  comdat_groups = (HashMap){0};
  outs = NULL;
  nouts = capouts = 0;
  archives = NULL;
  narchives = caparchives = 0;
  got = NULL;
  ngot = capgot = 0;
  got_map = (HashMap){0};
  iplt = NULL;
  niplt = capiplt = 0;
  bss = got_sec = gotplt_sec = plt_sec = relaplt_sec = NULL;
  tls_start = tls_filesz = tls_memsz = 0;
  tls_align = 1;
  image = NULL;
}

// Links `inputs` (object files, archives, linker scripts and -lNAME)
// into a static executable at `path`, finding libraries in `lib_paths`.
// `names[i]`, if not NULL, is what to call inputs[i] in messages.
// Returns false, writing nothing, if it meets something it doesn't
// support; then *why says what. Real errors, like undefined symbols,
// are reported and end mucc, as ld's would.
bool link_static(StringArray *inputs, StringArray *names, StringArray *lib_paths,
                 char *path, bool strip, char **why) {
  reset();
  strip_all = strip;
  jmp_buf here;
  fail_jmp = &here;
  if (setjmp(here)) {
    *why = fail_msg;
    return false;
  }

  for (int i = 0; i < inputs->len; i++) {
    char *in = inputs->data[i];
    if (startswith(in, "-l"))
      read_input(find_library(in + 2, lib_paths), NULL, lib_paths);
    else
      read_input(in, names->data[i], lib_paths);
  }
  load_archive_members();
  define_specials();

  bool undefined = false;
  for (int i = 0; i < ngsyms; i++) {
    GSym *g = gsymlist[i];
    if (g->strong_ref && !g->is_defined) {
      fprintf(stderr, "mucc: error: undefined reference to '%s'\n", g->name);
      undefined = true;
    }
  }
  if (undefined)
    exit(1);

  for (int i = 0; i < ngsyms; i++) {
    char *msg = hashmap_get(&warnings, gsymlist[i]->name);
    if (msg && gsymlist[i]->strong_ref)
      fprintf(stderr, "mucc: warning: %s\n", msg);
  }

  scan_relocations();
  assign_sections();
  sort_sections();
  layout();
  set_symbol_addresses();
  fill_image();

  for (int i = 0; i < nfiles; i++)
    for (int k = 0; k < files[i]->nsh; k++)
      if (files[i]->secs[k] && files[i]->secs[k]->out->type != SHT_NOBITS)
        apply_relocations(files[i]->secs[k]);

  write_executable(path);
  return true;
}
