#include <pthread.h>
#include <assert.h>
pthread_t c;
int d;
void *e(void *) { d = 6; }
int main() {
  if (nondet_bool()) {
    pthread_create(&c, NULL, e, NULL);
    return 0;
  }
  d = 3;
  assert(d == 0);  // should be fail
  return 0;
}