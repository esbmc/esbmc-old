#include <assert.h>

int main()
{
  int src = 1;
  int dest = 2;
  __builtin_memcpy(&dest, &src, sizeof(int));
  assert(dest == 1);
}