template <typename a> struct b { a c; };
struct d;
struct e {
  void f(d);
};
struct g {
  e h;
};
struct d : g {};
struct i {
  void j(g);
};
b<i> k;
