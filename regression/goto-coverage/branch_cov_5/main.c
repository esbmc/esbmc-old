#include <assert.h>

void loop1()
{
   for (int i = 0; i < 2; i++)
   {
      assert(0);
   }
}

void loop2()
{
   for (int i = 0; i < 4; i++)
   {
      assert(1);
   }
}

int main()

{
   switch (nondet_int())
   {
   case 1:
      loop1();
      break;
   case 2:
      loop2();
      break;
   default:
       ;
   }
   return 0;
}
