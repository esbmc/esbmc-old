#include <assert.h>

union foo {
    int elem1;
    long long elem2;
};

struct obj {
    union foo a; // this will use elem1
    union foo b; // this will use elem2
};

#define A_ELEMENT(o) o
#define B_ELEMENT(o) o + sizeof(union foo)

int main() {
    struct obj A[4];
    unsigned int obj_ptr = &A[0];

    for (int i = 0; i < 4; i++)
    {
        unsigned int var = obj_ptr + i*sizeof(struct obj);
        ((union foo*)(A_ELEMENT(var)))->elem1 = i;
        ((union foo*)(B_ELEMENT(var)))->elem2 = i+1;

        struct obj *ref = (struct obj*) var;
        assert(ref->a.elem1 == i);
        assert(ref->b.elem2 == i);
    }

    for (int i = 0; i < 4; i++)
    {
        assert(A[i].a.elem1 == i);
        assert(A[i].b.elem2 == i+1);
    }


    return 0;
}