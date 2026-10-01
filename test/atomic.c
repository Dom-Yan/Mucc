#include "test.h"
#include <stdatomic.h>
#include <pthread.h>

#if !__has_builtin(__sync_fetch_and_add) || !__has_builtin(__atomic_load_n)
#error
#endif

static int incr(_Atomic int *p) {
  int oldval = *p;
  int newval;
  do {
    newval = oldval + 1;
  } while (!atomic_compare_exchange_weak(p, &oldval, newval));
  return newval;
}

static void *add1(void *arg) {
  _Atomic int *x = arg;
  for (int i = 0; i < 1000*1000; i++)
    incr(x);
  return 0;
}

static void *add2(void *arg) {
  _Atomic int *x = arg;
  for (int i = 0; i < 1000*1000; i++)
    (*x)++;
  return 0;
}

static void *add3(void *arg) {
  _Atomic int *x = arg;
  for (int i = 0; i < 1000*1000; i++)
    *x += 5;
  return 0;
}

static int add_millions(void) {
  _Atomic int x = 0;

  pthread_t thr1;
  pthread_t thr2;
  pthread_t thr3;

  pthread_create(&thr1, NULL, add1, &x);
  pthread_create(&thr2, NULL, add2, &x);
  pthread_create(&thr3, NULL, add3, &x);

  for (int i = 0; i < 1000*1000; i++)
    x--;

  pthread_join(thr1, NULL);
  pthread_join(thr2, NULL);
  pthread_join(thr3, NULL);
  return x;
}

// Threads taking and dropping references, as refcounting code does:
// fetch_sub returns the count from before, so exactly one drop sees 1.
static _Atomic int refs;
static _Atomic int last_drops;

static void *take_and_drop(void *arg) {
  for (int i = 0; i < 100*1000; i++) {
    atomic_fetch_add(&refs, 1);
    if (atomic_fetch_sub(&refs, 1) == 1)
      atomic_fetch_add(&last_drops, 1);
  }
  return 0;
}

static int refcount(void) {
  refs = 1;
  pthread_t thr[3];
  for (int i = 0; i < 3; i++)
    pthread_create(&thr[i], NULL, take_and_drop, NULL);
  for (int i = 0; i < 3; i++)
    pthread_join(thr[i], NULL);
  if (atomic_fetch_sub(&refs, 1) == 1)
    last_drops++;
  return last_drops;
}

static void *sync_add(void *arg) {
  long *x = arg;
  for (int i = 0; i < 1000*1000; i++)
    __sync_fetch_and_add(x, 2);
  return 0;
}

static long sync_add_millions(void) {
  long x = 0;
  pthread_t thr1, thr2;
  pthread_create(&thr1, NULL, sync_add, &x);
  pthread_create(&thr2, NULL, sync_add, &x);
  for (int i = 0; i < 1000*1000; i++)
    __atomic_sub_fetch(&x, 1, __ATOMIC_SEQ_CST);
  pthread_join(thr1, NULL);
  pthread_join(thr2, NULL);
  return x;
}

int main() {
  ASSERT(6*1000*1000, add_millions());
  ASSERT(1, refcount());
  ASSERT(3*1000*1000, sync_add_millions());

  // The fetch_op forms return the value from before; the op_fetch and
  // op_and_fetch forms the value after.
  ASSERT(2, ({ atomic_int x=2; atomic_fetch_add(&x, 3); }));
  ASSERT(5, ({ atomic_int x=2; atomic_fetch_add(&x, 3); x; }));
  ASSERT(2, ({ atomic_int x=2; atomic_fetch_sub(&x, 3); }));
  ASSERT(-1, ({ atomic_int x=2; atomic_fetch_sub(&x, 3); x; }));
  ASSERT(12, ({ atomic_int x=12; atomic_fetch_or(&x, 3); }));
  ASSERT(15, ({ atomic_int x=12; atomic_fetch_or(&x, 3); x; }));
  ASSERT(12, ({ atomic_int x=12; atomic_fetch_xor_explicit(&x, 6, memory_order_relaxed); }));
  ASSERT(10, ({ atomic_int x=12; atomic_fetch_xor_explicit(&x, 6, memory_order_relaxed); x; }));
  ASSERT(12, ({ atomic_int x=12; atomic_fetch_and(&x, 6); }));
  ASSERT(4, ({ atomic_int x=12; atomic_fetch_and(&x, 6); x; }));
  ASSERT(200, ({ _Atomic unsigned char x=200; atomic_fetch_add(&x, 100); }));
  ASSERT(44, ({ _Atomic unsigned char x=200; atomic_fetch_add(&x, 100); x; }));
  ASSERT(5, ({ int x=5; __sync_fetch_and_add(&x, 1); }));
  ASSERT(6, ({ int x=5; __sync_add_and_fetch(&x, 1); }));
  ASSERT(5, ({ int x=5; __atomic_fetch_or(&x, 2, __ATOMIC_SEQ_CST); }));
  ASSERT(7, ({ int x=5; __atomic_or_fetch(&x, 2, __ATOMIC_SEQ_CST); }));
  ASSERT(1, ({ long x=1L<<40; __sync_sub_and_fetch(&x, 1) == (1L<<40) - 1; }));

  // C11's atomic_fetch_add on a pointer steps by elements; gcc's
  // __atomic_fetch_add by bytes.
  ASSERT(8, ({ int a[4]; int *_Atomic p=a; atomic_fetch_add(&p, 2); (char *)p - (char *)a; }));
  ASSERT(2, ({ int a[4]; int *p=a; __atomic_fetch_add(&p, 2, 5); (char *)p - (char *)a; }));

  // Compare-and-swap and exchange
  ASSERT(5, ({ int x=5; __sync_val_compare_and_swap(&x, 5, 9); }));
  ASSERT(9, ({ int x=5; __sync_val_compare_and_swap(&x, 5, 9); x; }));
  ASSERT(5, ({ int x=5; __sync_val_compare_and_swap(&x, 4, 9); x; }));
  ASSERT(1, ({ int x=5; __sync_bool_compare_and_swap(&x, 5, 9); }));
  ASSERT(0, ({ int x=5; __sync_bool_compare_and_swap(&x, 4, 9); }));
  ASSERT(1, ({ long x=1L<<40; __sync_bool_compare_and_swap(&x, 1L<<40, -1); x == -1; }));
  ASSERT(0, ({ int x=5, e=4; __atomic_compare_exchange_n(&x, &e, 9, 0, 5, 5); }));
  ASSERT(5, ({ int x=5, e=4; __atomic_compare_exchange_n(&x, &e, 9, 0, 5, 5); e; }));
  ASSERT(1, ({ int x=5, e=5; __atomic_compare_exchange_n(&x, &e, 9, 0, 5, 5); }));
  ASSERT(3, ({ int x=3; __atomic_exchange_n(&x, 5, __ATOMIC_SEQ_CST); }));
  ASSERT(5, ({ int x=3; __atomic_exchange_n(&x, 5, __ATOMIC_SEQ_CST); x; }));
  ASSERT(0, ({ int x=0; __sync_lock_test_and_set(&x, 1); }));
  ASSERT(0, ({ int x=1; __sync_lock_release(&x); x; }));
  ASSERT(1, ({ long x=0; __atomic_store_n(&x, -1, 5); x == -1; }));
  ASSERT(7, ({ int x=7; __atomic_load_n(&x, __ATOMIC_ACQUIRE); }));
  ASSERT(0, ({ char x=0; __atomic_test_and_set(&x, 5); }));
  ASSERT(1, ({ char x=0; __atomic_test_and_set(&x, 5); __atomic_test_and_set(&x, 5); }));
  ASSERT(0, ({ char x=1; __atomic_clear(&x, 5); x; }));
  ASSERT(5, ({ atomic_int x=3; atomic_store(&x, 5); atomic_load(&x); }));
  ASSERT(1, ({ atomic_flag f=0; atomic_flag_test_and_set(&f); atomic_flag_test_and_set(&f); }));
  ASSERT(0, ({ atomic_flag f=1; atomic_flag_clear(&f); f; }));

  // Fences
  __sync_synchronize();
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
  atomic_thread_fence(memory_order_seq_cst);
  ASSERT(1, __atomic_always_lock_free(sizeof(long), 0));
  ASSERT(0, __atomic_always_lock_free(16, 0));

  ASSERT(3, ({ int x=3; atomic_exchange(&x, 5); }));
  ASSERT(5, ({ int x=3; atomic_exchange(&x, 5); x; }));

  printf("OK\n");
  return 0;
}
