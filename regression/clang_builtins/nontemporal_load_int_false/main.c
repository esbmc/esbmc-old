int main() {
  int expected = nondet_int();
  int actual = __builtin_nontemporal_load(&expected);
  expected++;
  __ESBMC_assert(expected == actual, "check");
  return 0;
}
