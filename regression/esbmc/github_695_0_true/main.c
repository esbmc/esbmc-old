#define PRESENT  0x01
#define _4KB        0x1000
#define _32KB       0x8000

#define _4KB_MASK   (~(_4KB - 1))
#include <assert.h>
#define UINT32 unsigned long
#define UINT64 unsigned long long

int main() {
    void *Heap = __builtin_alloca(_32KB);
    __ESBMC_assume((UINT64)Heap < 0xFF);

    UINT64 *TabPhy = (UINT64*)(((UINT64)Heap + _4KB) );
    UINT64 var = (UINT64)Heap + _4KB;

    for (int i = 0; i < 4; i++)
    {
        *((UINT64*)(var + i*sizeof(UINT64))) = i;
    }

    for (int i = 0; i < 4; i++)
        assert(TabPhy[i] == i);

    return 0;
}