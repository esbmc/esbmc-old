// SPDX-License-Identifier: GPL-3.0
import "./contract_import.sol";
pragma solidity >=0.8.0;

contract Derive {
    constructor() {
        Base x = new Base(2);
    }
}