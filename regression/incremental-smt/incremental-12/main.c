#include <assert.h>

int main()
{
  unsigned int x = 100;
  while(x>0)
  {
   __ESBMC_assume(x>100);
   x--;
  }
  assert(0);
  return 0;
}
