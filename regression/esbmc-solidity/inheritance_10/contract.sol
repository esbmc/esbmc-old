// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.0;


contract A
{
    constructor()
    {
        x = 6;
    }
    uint x = 3;
}

contract B is A 
{
    constructor()
    {
        assert(x == 6);
    }
    function test() public
    {
        assert(x==6);
    }
}
