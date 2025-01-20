extern "C" void memcpy(void *, void *, int);
float a = __builtin_nanf(0);
double b = a;
double other = 1;
int main()
{
  memcpy(&other, &b, 8);
}
