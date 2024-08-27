#include <string>
#include <cassert>

using namespace std;

int main()
{
  for (int i = 0; i < 2; ++i)
  {
    string expectedSound = (i == 0) ? "Woof!" : "Meow!";
    if (i == 0)
      assert(expectedSound == "Woof!");
    else
      assert(expectedSound == "Meow!");
  }

  return 0;
}
