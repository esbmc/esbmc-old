// value-initialize for aggregate
#include <cassert>

struct uint3
{
  unsigned int x, y, z;
};

uint3 s1 = {1, 2, 3};

int main()
{
  assert(s1.x == 1);
  assert(s1.y == 2);
  assert(s1.z == 3);
  
  return 0;
}
