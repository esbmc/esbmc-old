#include <pthread.h>
#include <assert.h>

int x1;

void *t1(void *arg)
{
  x1++;
}

void *t2(void *arg)
{
  x1 = x1 + 5;
}

void *t3(void *arg)
{
  if(x1 == 1)
    x1--;

  if(x1 == 5)
    x1 = 2;
}

void *t4(void *arg)
{
  assert(x1 == 0 || x1 == 5 || x1 == 4 || x1 == 2 || x1 == 6 || x1 == 1);
}

int main()
{
  pthread_t id1, id2, id3, id4;

  pthread_create(&id1, NULL, t1, NULL);
  pthread_create(&id2, NULL, t2, NULL);
  pthread_create(&id3, NULL, t3, NULL);
  pthread_create(&id4, NULL, t4, NULL);

  return 0;
}
