#include<pthread.h>
#include<assert.h>

typedef struct SafeStackItem {
  volatile int Value;
  int Next;
} SafeStackItem;
typedef struct SafeStack {
  SafeStackItem array[3];
  int head;
  int count;
} SafeStack;
pthread_t threads[3];
SafeStack stack;

int exchange(int *obj, int v) {
  int t = *obj;
  *obj = v;
  return t;
}

int Pop(void) {
  int head1 = stack.head;
  int next1 = exchange(&stack.array[head1].Next, -1);
  if (next1 >= 0)
    return head1;
  return -1;
}
void *thread(void *arg) {
  int idx = (int)(size_t)arg;
    int elem;
    while (1) {
      elem = Pop();
      if (elem >= 0)
        break;
    }
    stack.array[elem].Value = idx;
    if (!(stack.array[elem].Value == idx)) {
      assert(0);
    }
  return ((void *)0);
}
int main(void) {
  pthread_create(&threads[0], ((void *)0), thread, (void *)0);
  pthread_create(&threads[1], ((void *)0), thread, (void *)1);
  return 0;
}

