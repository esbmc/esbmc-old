#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#define N 3

unsigned int timer = 0;
void __ESBMC_atomic_begin();
void __ESBMC_atomic_end();

int count;
pthread_mutex_t frk[N];

void *phil(void *arg)
{
  int id, left, right;

  __ESBMC_atomic_begin();
  timer += 3;
  id = *((int *)arg);
  left = id;
  right = (id + 1) % N;
  __ESBMC_atomic_end();

  __ESBMC_atomic_begin();
  timer += 8;
  pthread_mutex_lock(&frk[right]);
  pthread_mutex_lock(&frk[left]);
  pthread_mutex_unlock(&frk[left]);
  pthread_mutex_unlock(&frk[right]);
  __ESBMC_atomic_end();

  __ESBMC_atomic_begin();
  timer += 1;
  ++count;
  __ESBMC_atomic_end();
  assert(timer <= 150);
}

int main()
{
  int arg, i;
  pthread_t phil_id[N];

  for (i = 0; i < N; i++)
    pthread_mutex_init(&frk[i], NULL);

  for (i = 0; i < N; i++)
  {
    arg = i;
    pthread_create(&phil_id[i], 0, phil, &i);
  }

  for (i = 0; i < N; i++)
    pthread_join(phil_id[i], 0);

  return 0;
}
