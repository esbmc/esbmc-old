// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.0;

contract Base {
    uint8 data = 1;

    constructor(uint8 x) {
        assert(data + 2 == x);
    }
}