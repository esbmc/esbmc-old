#include <assert.h>
struct obj {
    int a;
    int b;
};

#define A_ELEMENT(o) o
#define B_ELEMENT(o) o + sizeof(int)

int main() {
    struct obj A[4];
    unsigned int obj_ptr = &A[0];

    for (int i = 0; i < 4; i++)
    {
        unsigned int var = obj_ptr + i*sizeof(struct obj);
        *((int*)(A_ELEMENT(var))) = i;
        *((int*)(B_ELEMENT(var))) = i;

        struct obj *ref = (struct obj*) var;
        assert(ref->a == i);
        assert(ref->b == i+1);
    }

    for (int i = 0; i < 4; i++)
    {
        assert(A[i].a == i);
        assert(A[i].b == i+1);
    }


    return 0;
}