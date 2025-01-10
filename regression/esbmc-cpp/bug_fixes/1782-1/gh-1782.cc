template <typename> struct a;
struct b;
template <typename> struct a { b *c; };
struct b : a<int> {};