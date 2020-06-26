#include <assert.h>
#include <pthread.h>
void *t1(void *arg) {
  int *ptr = arg;
  assert(*ptr==1);
  return 0;
}
int main() {
  int *p, incr=0;
  pthread_t id;
  incr++;
  p = &incr;
  pthread_create(&id, NULL, t1, &incr);
  assert(*p==2);
  return 0;
}
