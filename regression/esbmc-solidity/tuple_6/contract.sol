// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;

contract test {
    uint[3] public t = [0, 0];
    uint8 xx = 1;
    uint yy = 2;
    //uint[] public tt;

    constructor() {
        (t, t) = ([1], [2]);
    }
}

contract test1 is test {
    function test2() public view {
        assert(t[0] == 1);
    }
}
