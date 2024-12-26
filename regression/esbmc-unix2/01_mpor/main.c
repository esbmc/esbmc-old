#include<pthread.h>
#include<assert.h>
#include<stdlib.h>
void assume_abort_if_not(b) {
  if (!b)
    abort();
}
int e, f, g = 0;
void *thread1() {
  e = 1;
  g = 3;
  return NULL;
}
void *thread2() {
  assume_abort_if_not(e == 0);
  f++;
  if (g)
    assert(0);
  return NULL;
}
int main() {
  pthread_t t,t1;
  pthread_create(&t, 0, thread1, 0);
  pthread_create(&t1, 0, thread2, 0);
  return 0;
}
