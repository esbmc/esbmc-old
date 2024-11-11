#include <assert.h>

int main()
{
  char buffer[14] = "Hello, World!";

  // Overlapping src and dest pointers
  __builtin_memmove(buffer + 2, buffer, 5);

  // Expected result: "HeHelloWorld!"
  assert(buffer[0] == 'H');
  assert(buffer[1] == 'e');
  assert(buffer[2] == 'H');
  assert(buffer[3] == 'e');
  assert(buffer[4] == 'l');
  assert(buffer[5] == 'l');
  assert(buffer[6] == 'o');
  assert(buffer[7] == 'W');
  assert(buffer[8] == 'o');
  assert(buffer[9] == 'r');
  assert(buffer[10] == 'l');
  assert(buffer[11] == 'd');
  assert(buffer[12] == '!');
  assert(buffer[13] == '\0');

  return 0;
}