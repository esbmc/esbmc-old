struct Base
{
  int ss[128];
};

int main()
{
  struct Base x, *y = &x;
  y->ss;
}
