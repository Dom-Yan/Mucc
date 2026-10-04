#define ASSERT(x, y) assert(x, y, #y)

void assert(int expected, int actual, char *code);
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, void *ap);
int strcmp(const char *p, const char *q);
int strncmp(const char *p, const char *q, long n);
int memcmp(const void *p, const void *q, long n);
void exit(int n);
int vsprintf();
long strlen(const char *s);
void *memcpy(void *dest, const void *src, long n);
void *memset(void *s, int c, long n);
