
#include <jimple-frontend/AST/jimple_globals.h>
#include <iostream>
namespace jimple
{
size_t get_reference(std::string v)
{
  if (jimple::class_reference.size() == 0)
    jimple::class_reference.push_back("__ESBMC_default_str");
  int i = 0;
  for (; i < jimple::class_reference.size(); i++)
  {
    if (jimple::class_reference[i] == v)
    {
      std::cout << "Found " << v << " as " << i << "\n";
      return i;
    }
  }
  std::cout << "Adding " << v << " as " << jimple::class_reference.size()
            << "\n";
  jimple::class_reference.push_back(v);
  return i;
}
} // namespace jimple