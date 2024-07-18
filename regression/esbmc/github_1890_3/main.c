#include <assert.h>

void func()
{
   for (int i = 0; i <= 1; i++)
      assert(0 && "1");
}
void func2()
{

   assert(0 && "2");
}
int main()
{
   func();
   int x = 0;
   10 / x;
   func2();
}