// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;

// Return array of uint8 type
contract A {
    uint8[2] z; // int *z = calloc(2,1);
    // int tmp1[2] = {1,2};
    // int *zz = calloc(2,1);
    uint8[2] zz = [1, 2]; // memcpy(zzzz, tmp2, 2);

    uint8[] zzz; // int* zzz = 0;
    // int tmp2[2] = {1,2};
    // int* zzzz = calloc(2,1);
    uint8[] zzzz = [1, 2]; // memcpy(zzzz, tmp2, 2);

    function int8_call() public virtual returns (uint8[2] memory) {
        uint8[2] memory a; // int *a = calloc(2,1);

        z = [0]; // int tmp3[2] = {0,1}
        // memcpy = (z, tmp3, 2);
        // int tmp4[1] = {0};
        zzz = [0]; // memcpy(zzz, tmp4, 1);
        assert(a[0] == 0);
        assert(a[1] == 0);
        a[0] = 21;
        a[1] = 42;
        return a;
    }
}
