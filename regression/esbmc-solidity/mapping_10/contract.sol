// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;

contract Base {
    mapping(int => int) test;

    function tes2t() public {
        uint y = 1;
        test[int(y)] += re();
        assert(test[re()] == re());
    }

    function re() public view returns (int) {
        return test[1];
    }
}
