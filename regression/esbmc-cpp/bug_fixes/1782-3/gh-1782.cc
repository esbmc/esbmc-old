template <typename> struct a;
struct b {
  a<char> *c;
};
template <typename> struct a {};
a<char> cout;