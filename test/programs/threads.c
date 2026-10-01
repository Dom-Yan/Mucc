// Threads: pthread mutexes, condition variables, rwlocks, barriers,
// semaphores, thread-local storage, C11 <threads.h>, atomics, once.
#include "check.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <threads.h>

#define N 8
#define ITER 100000

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_barrier_t bar;
static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
static long counter, rw_value;
static atomic_long acounter;
static _Thread_local int tls = 5;
static int ready;
static sem_t sem;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int once_runs;
static void init_once(void) { once_runs++; }

static void *worker(void *arg) {
  long id = (long)arg;
  pthread_once(&once, init_once);
  tls = id; // each thread its own
  pthread_barrier_wait(&bar);
  for (int i = 0; i < ITER; i++) {
    pthread_mutex_lock(&mu);
    counter++;
    pthread_mutex_unlock(&mu);
    atomic_fetch_add(&acounter, 1);
  }
  pthread_rwlock_wrlock(&rw);
  rw_value += id;
  pthread_rwlock_unlock(&rw);
  sem_post(&sem);
  return (void *)(long)(tls == id);
}

static void *waiter(void *arg) {
  pthread_mutex_lock(&mu);
  while (!ready)
    pthread_cond_wait(&cv, &mu);
  pthread_mutex_unlock(&mu);
  return (void *)42;
}

static int c11_thread(void *arg) { return *(int *)arg * 2; }

int main(void) {
  pthread_barrier_init(&bar, 0, N);
  sem_init(&sem, 0, 0);
  pthread_t t[N];
  for (long i = 0; i < N; i++)
    CHECK(pthread_create(&t[i], 0, worker, (void *)i) == 0);
  for (int i = 0; i < N; i++)
    sem_wait(&sem);
  int tls_ok = 1;
  for (int i = 0; i < N; i++) {
    void *r;
    pthread_join(t[i], &r);
    tls_ok &= (long)r;
  }
  CHECK(counter == N * ITER);
  CHECK(acounter == N * ITER);
  CHECK(rw_value == N * (N - 1) / 2);
  CHECK(tls_ok && tls == 5);
  CHECK(once_runs == 1);

  pthread_t w;
  pthread_create(&w, 0, waiter, 0);
  pthread_mutex_lock(&mu);
  ready = 1;
  pthread_cond_signal(&cv);
  pthread_mutex_unlock(&mu);
  void *r;
  pthread_join(w, &r);
  CHECK(r == (void *)42);

  // C11 threads
  thrd_t ct;
  int arg = 21, res;
  CHECK(thrd_create(&ct, c11_thread, &arg) == thrd_success);
  CHECK(thrd_join(ct, &res) == thrd_success && res == 42);
  mtx_t m;
  CHECK(mtx_init(&m, mtx_plain) == thrd_success);
  mtx_lock(&m);
  mtx_unlock(&m);

  return done("threads");
}
