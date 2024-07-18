#include <cassert>

class Base
{
public:
  int ss[3];
  Base() : ss{0, 1, 2}
  {
    ss[2] = ss[1] * 3;
  }
};

int main()
{
  Base x, *y = &x;
  y->ss;
  assert(y->ss);
  assert(!y->ss[0]);
  assert(y->ss[1] == 1);
  assert(y->ss[2] == 3);
}
  
