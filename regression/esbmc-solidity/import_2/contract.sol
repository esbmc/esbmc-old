// SPDX-License-Identifier: GPL-3.0
import "./contract_import.sol";
pragma solidity >=0.8.0;

contract C {
    function test_override() public {
        A contract_A = new A();
        B contract_B = new B();
        assert(contract_A.func_1() == 21);
        assert(contract_B.func_1() == 42);
    }
}