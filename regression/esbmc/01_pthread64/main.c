#include <pthread.h>

int i = 1, j = 1;

#define NUM 5

void *t1(void *arg)
{
  int k = 0;

  for(k = 0; k < NUM; k++)
  {
    __VERIFIER_atomic_begin();
    i += j;
    __VERIFIER_atomic_end();
  }
  pthread_exit(NULL);
}

void *t2(void *arg)
{
  int k = 0;

  for(k = 0; k < NUM; k++)
  {
    __VERIFIER_atomic_begin();
    j += i;
    __VERIFIER_atomic_end();
  }
  pthread_exit(NULL);
}

int main(int argc, char **argv)
{
  pthread_t id1, id2;

  pthread_create(&id1, NULL, t1, NULL);
  pthread_create(&id2, NULL, t2, NULL);

  __VERIFIER_atomic_begin();
  int condI = i >= 144;
  __VERIFIER_atomic_end();

  __VERIFIER_atomic_begin();
  int condJ = j >= 144;
  __VERIFIER_atomic_end();

  if(condI || condJ)
  {
  ERROR:
  {
    __VERIFIER_error();
    abort();
  }
  }

  return 0;
}
