// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;

contract BASE{

    function test() public pure
    {
        int a;
        uint b;
        uint256 c;
        bytes1 d;
        uint8[] memory f = new uint8[](2);

        assert(a == 0);
        assert(b == 0);
        assert(c == 0);
        assert(d == 0x00);
        assert(f[0] == 0);
        assert(f[1] == 0);
    }
}