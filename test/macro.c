#include "test.h"
#include "include1.h"

char *main_filename1 = __FILE__;
int main_line1 = __LINE__;
#define LINE() __LINE__
int main_line2 = LINE();

#

/* */ #

// #pragma push_macro saves a definition, or that there was none, and
// pop_macro brings back the last one saved.
#define PM 1
#pragma push_macro("PM")
#undef PM
#define PM 2
#pragma push_macro("PM")
#undef PM
#define PM 3
int pm1 = PM;
#pragma pop_macro("PM")
int pm2 = PM;
#pragma pop_macro("PM")
int pm3 = PM;
#pragma push_macro("PM_NONE")
#define PM_NONE 4
#pragma pop_macro("PM_NONE")
#ifdef PM_NONE
int pm4 = 1;
#else
int pm4 = 0;
#endif

// C99's _Pragma: #pragma from a macro. Others are ignored, as #pragma's.
#define HIDDEN_BEGIN _Pragma("GCC visibility push(hidden)")
#define HIDDEN_END _Pragma("GCC visibility pop")
HIDDEN_BEGIN
int pragma_op = 5;
HIDDEN_END
#define PM5 1
_Pragma("push_macro(\"PM5\")")
#undef PM5
_Pragma("pop_macro(\"PM5\")")
int pm5 = PM5;

int ret3(void) { return 3; }
int dbl(int x) { return x*x; }

int add2(int x, int y) {
  return x + y;
}

int add6(int a, int b, int c, int d, int e, int f) {
  return a + b + c + d + e + f;
}

int main() {
  ASSERT(3, pm1);
  ASSERT(2, pm2);
  ASSERT(1, pm3);
  ASSERT(0, pm4);
  ASSERT(5, pragma_op);
  ASSERT(1, pm5);

  ASSERT(5, include1);
  ASSERT(7, include2);

#if 0
#include "/no/such/file"
  ASSERT(0, 1);
#if nested
#endif
#endif

  int m = 0;

#if 1
  m = 5;
#endif
  ASSERT(5, m);

#if 1
# if 0
#  if 1
    foo bar
#  endif
# endif
      m = 3;
#endif
    ASSERT(3, m);

#if 1-1
# if 1
# endif
# if 1
# else
# endif
# if 0
# else
# endif
  m = 2;
#else
# if 1
  m = 3;
# endif
#endif
  ASSERT(3, m);

#if 1
  m = 2;
#else
  m = 3;
#endif
  ASSERT(2, m);

#if 1
  m = 2;
#else
  m = 3;
#endif
  ASSERT(2, m);

#if 0
  m = 1;
#elif 0
  m = 2;
#elif 3+5
  m = 3;
#elif 1*5
  m = 4;
#endif
  ASSERT(3, m);

#if 1+5
  m = 1;
#elif 1
  m = 2;
#elif 3
  m = 2;
#endif
  ASSERT(1, m);

#if 0
  m = 1;
#elif 1
# if 1
  m = 2;
# else
  m = 3;
# endif
#else
  m = 5;
#endif
  ASSERT(2, m);

  int M1 = 5;

#define M1 3
  ASSERT(3, M1);
#define M1 4
  ASSERT(4, M1);

#define M1 3+4+
  ASSERT(12, M1 5);

#define M1 3+4
  ASSERT(23, M1*5);

#define ASSERT_ assert(
#define if 5
#define five "5"
#define END )
  ASSERT_ 5, if, five END;

#undef ASSERT_
#undef if
#undef five
#undef END

  if (0);

#define M 5
#if M
  m = 5;
#else
  m = 6;
#endif
  ASSERT(5, m);

#define M 5
#if M-5
  m = 6;
#elif M
  m = 5;
#endif
  ASSERT(5, m);

  int M2 = 6;
#define M2 M2 + 3
  ASSERT(9, M2);

#define M3 M2 + 3
  ASSERT(12, M3);

  int M4 = 3;
#define M4 M5 * 5
#define M5 M4 + 2
  ASSERT(13, M4);

#ifdef M6
  m = 5;
#else
  m = 3;
#endif
  ASSERT(3, m);

#define M6
#ifdef M6
  m = 5;
#else
  m = 3;
#endif
  ASSERT(5, m);

#ifndef M7
  m = 3;
#else
  m = 5;
#endif
  ASSERT(3, m);

#define M7
#ifndef M7
  m = 3;
#else
  m = 5;
#endif
  ASSERT(5, m);

#if 0
#ifdef NO_SUCH_MACRO
#endif
#ifndef NO_SUCH_MACRO
#endif
#else
#endif

#define M7() 1
  int M7 = 5;
  ASSERT(1, M7());
  ASSERT(5, M7);

#define M7 ()
  ASSERT(3, ret3 M7);

#define M8(x,y) x+y
  ASSERT(7, M8(3, 4));

#define M8(x,y) x*y
  ASSERT(24, M8(3+4, 4+5));

#define M8(x,y) (x)*(y)
  ASSERT(63, M8(3+4, 4+5));

#define M8(x,y) x y
  ASSERT(9, M8(, 4+5));

#define M8(x,y) x*y
  ASSERT(20, M8((2+3), 4));

#define M8(x,y) x*y
  ASSERT(12, M8((2,3), 4));

#define dbl(x) M10(x) * x
#define M10(x) dbl(x) + 3
  ASSERT(10, dbl(2));

#define M11(x) #x
  ASSERT('a', M11( a!b  `""c)[0]);
  ASSERT('!', M11( a!b  `""c)[1]);
  ASSERT('b', M11( a!b  `""c)[2]);
  ASSERT(' ', M11( a!b  `""c)[3]);
  ASSERT('`', M11( a!b  `""c)[4]);
  ASSERT('"', M11( a!b  `""c)[5]);
  ASSERT('"', M11( a!b  `""c)[6]);
  ASSERT('c', M11( a!b  `""c)[7]);
  ASSERT(0, M11( a!b  `""c)[8]);

#define paste(x,y) x##y
  ASSERT(15, paste(1,5));
  ASSERT(255, paste(0,xff));
  ASSERT(3, ({ int foobar=3; paste(foo,bar); }));
  ASSERT(5, paste(5,));
  ASSERT(5, paste(,5));

#define i 5
  ASSERT(101, ({ int i3=100; paste(1+i,3); }));
#undef i

#define paste2(x) x##5
  ASSERT(26, paste2(1+2));

#define paste3(x) 2##x
  ASSERT(23, paste3(1+2));

#define paste4(x, y, z) x##y##z
  ASSERT(123, paste4(1,2,3));

#define M12
#if defined(M12)
  m = 3;
#else
  m = 4;
#endif
  ASSERT(3, m);

#define M12
#if defined M12
  m = 3;
#else
  m = 4;
#endif
  ASSERT(3, m);

#if defined(M12) - 1
  m = 3;
#else
  m = 4;
#endif
  ASSERT(4, m);

#if defined(NO_SUCH_MACRO)
  m = 3;
#else
  m = 4;
#endif
  ASSERT(4, m);

#if no_such_symbol == 0
  m = 5;
#else
  m = 6;
#endif
  ASSERT(5, m);

#define STR(x) #x
#define M12(x) STR(x)
#define M13(x) M12(foo.x)
  ASSERT(0, strcmp(M13(bar), "foo.bar"));

#define M13(x) M12(foo. x)
  ASSERT(0, strcmp(M13(bar), "foo. bar"));

#define M12 foo
#define M13(x) STR(x)
#define M14(x) M13(x.M12)
  ASSERT(0, strcmp(M14(bar), "bar.foo"));

#define M14(x) M13(x. M12)
  ASSERT(0, strcmp(M14(bar), "bar. foo"));

#include "include3.h"
  ASSERT(3, foo);

#include "include4.h"
  ASSERT(4, foo);

#define M13 "include3.h"
#include M13
  ASSERT(3, foo);

#define M13 < include4.h
#include M13 >
  ASSERT(4, foo);

#include "include5.h"
#define INCLUDE5_WANT
#include "include5.h"
  ASSERT(1, include5_second);

#undef foo

  ASSERT(1, __STDC__);

  ASSERT(0, strcmp(main_filename1, "test/macro.c"));
  ASSERT(5, main_line1);
  ASSERT(7, main_line2);
  ASSERT(0, strcmp(include1_filename, "test/include1.h"));
  ASSERT(4, include1_line);

#define M14(...) 3
  ASSERT(3, M14());

#define M14(...) __VA_ARGS__
  ASSERT(2, M14() 2);
  ASSERT(5, M14(5));

#define M14(...) add2(__VA_ARGS__)
  ASSERT(8, M14(2, 6));

#define M14(...) add6(1,2,__VA_ARGS__,6)
  ASSERT(21, M14(3,4,5));

#define M14(x, ...) add6(1,2,x,__VA_ARGS__,6)
  ASSERT(21, M14(3,4,5));

#define M14(args...) 3
  ASSERT(3, M14());

#define M14(x, ...) x
  ASSERT(5, M14(5));

#define M14(args...) args
  ASSERT(2, M14() 2);
  ASSERT(5, M14(5));

#define M14(args...) add2(args)
  ASSERT(8, M14(2, 6));

#define M14(args...) add6(1,2,args,6)
  ASSERT(21, M14(3,4,5));

#define M14(x, args...) add6(1,2,x,args,6)
  ASSERT(21, M14(3,4,5));

#define M14(x, args...) x
  ASSERT(5, M14(5));

#define CONCAT(x,y) x##y
  ASSERT(5, ({ int f0zz=5; CONCAT(f,0zz); }));
  ASSERT(5, ({ CONCAT(4,.57) + 0.5; }));

  ASSERT(11, strlen(__DATE__));
  ASSERT(8, strlen(__TIME__));

  ASSERT(0, __COUNTER__);
  ASSERT(1, __COUNTER__);
  ASSERT(2, __COUNTER__);

  ASSERT(24, strlen(__TIMESTAMP__));

  ASSERT(0, strcmp(__BASE_FILE__, "test/macro.c"));

#define M30(buf, fmt, ...) sprintf(buf, fmt __VA_OPT__(,) __VA_ARGS__)
  ASSERT(0, ({ char buf[100]; M30(buf, "foo"); strcmp(buf, "foo"); }));
  ASSERT(0, ({ char buf[100]; M30(buf, "foo%d", 3); strcmp(buf, "foo3"); }));
  ASSERT(0, ({ char buf[100]; M30(buf, "foo%d%d", 3, 5); strcmp(buf, "foo35"); }));

#define M31(buf, fmt, ...) sprintf(buf, fmt, ## __VA_ARGS__)
  ASSERT(0, ({ char buf[100]; M31(buf, "foo"); strcmp(buf, "foo"); }));
  ASSERT(0, ({ char buf[100]; M31(buf, "foo%d", 3); strcmp(buf, "foo3"); }));
  ASSERT(0, ({ char buf[100]; M31(buf, "foo%d%d", 3, 5); strcmp(buf, "foo35"); }));

#define M31(x, y) (1, ##x y)
  ASSERT(3, M31(, 3));

  // A preprocessing number may hold `_`, so 802_2 is one token to paste,
  // as in BusyBox's ETH_P_##802_2.
#define P_802_2 42
#define M32(x) P_##x
  ASSERT(42, M32(802_2));
  ASSERT(0, strcmp(STR(802_2), "802_2"));

  // Empty arguments around ## are placemarkers (C17 6.10.3.3's example):
  // t(, , 3) is 3, not an error.
#define M33(x, y, z) x##y##z
#define XSTR(x) STR(x)
  ASSERT(0, strcmp(XSTR(M33(1, 2, 3) M33(, 4, 5) M33(6, , ) M33(, , 7) M33(, , ) M33(, 8, )),
                   "123 45 6 7 8"));
  ASSERT(5, ({ int ab = 5; M33(a, , b); }));

  // An object-like macro's ## pastes too, and `# ## #` is one ## token
  // that is not an operator later (C17 6.10.3.3's other example).
#define M34 1 ## 2
  ASSERT(12, M34);
#define hash_hash # ## #
#define mkstr(a) # a
#define in_between(a) mkstr(a)
#define join(c, d) in_between(c hash_hash d)
  ASSERT(0, strcmp(join(x, y), "x ## y"));

  // Parameters inside __VA_OPT__ are replaced, # and ## work there.
#define VO1(a, ...) (a __VA_OPT__(+ __VA_ARGS__ + a))
#define VO2(x, ...) #x __VA_OPT__(" " #x)
#define VO3(x, ...) __VA_OPT__(x ## x)
  ASSERT(5, VO1(5));
  ASSERT(16, VO1(5, 6));
  ASSERT(0, strcmp(VO2(q, 1), "q q"));
  ASSERT(0, strcmp(VO2(q), "q"));
  ASSERT(9, ({ int yy = 9; VO3(y, 1); }));

  // #if arithmetic is in intmax_t and uintmax_t.
  ASSERT(127, ({ int r = 0;
#if (2147483647 + 1) > 0
    r |= 1;
#endif
#if 0xffffffffu + 1 == 0x100000000
    r |= 2;
#endif
#if -1 > 0u
    r |= 4;
#endif
#if (1 << 40) != 0 && (1 << 63) < 0
    r |= 8;
#endif
#if ~0u == 18446744073709551615u
    r |= 16;
#endif
#if 'ab' == 0x6162
    r |= 32;
#endif
#if -2147483648 < 0 && 4294967295 > 0
    r |= 64;
#endif
    r; }));

  // # escapes `\` and `"` only in literals, so #x of \n is a newline.
  // L ## #x is a wide string, and __FILE_NAME__ the file's base name.
#define STR_BS(x) #x
#define WIDE_STR(x) L ## #x
  ASSERT(0, strcmp(STR_BS(\n), "\n"));
  ASSERT(0, strcmp(STR_BS("\n" '\\'), "\"\\n\" '\\\\'"));
  ASSERT(8, sizeof(WIDE_STR(a)));
  ASSERT('b', WIDE_STR(ab)[1]);
  ASSERT(0, strcmp(__FILE_NAME__, "macro.c"));

  printf("OK\n");
  return 0;
}
