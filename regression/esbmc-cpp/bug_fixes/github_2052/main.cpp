#include <cassert>

class A
{
public:
  int num;
  A (int n) : num(n)
  {
  }
  ~A()
  {
  }
};

A func(int n)
{
  return A(n);
}

int main()
{
  A a = func(1);
  assert(a.num == 1);
}
