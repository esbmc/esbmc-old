// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.0;

abstract contract BB {}

contract Base {
    uint data;
    constructor() {
        data = 1;
    }
    /// @custom:scribble if_succeeds {:msg "P0"} y == x + 1;
    // function test() external virtual {
    //     assert(data == 3);
    // }
}

contract Derived is Base {
    constructor() Base() {
        data = 2;
    }
    // The overriding function may only change the visibility of the overridden function from external to public
}

contract DD is Base, Derived {
    constructor() {
        assert(data == 2);
    }
    function test1() public view {
        data;
        assert(data == 2);
    }
}
