// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.0;
import "./contract_import2.sol";

contract B is A {
    function func_1() public override pure returns (int8) {
        return 42;
    }
}

