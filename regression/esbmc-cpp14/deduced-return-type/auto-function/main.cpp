#include <cassert>
struct OneVector
{
  float element;
  OneVector(float e) : element(e)
  {
  }

  auto begin()
  {
    return element;
  }
  float get_one()
  {
    return element;
  }
};
OneVector vector(22.22f);
int main()
{
  assert(vector.get_one() == 22.22f);
}
