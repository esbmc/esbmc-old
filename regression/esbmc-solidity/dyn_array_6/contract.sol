// SPDX-License-Identifier: GPL-3.0
pragma solidity ^0.8.0;

contract AddressStore {
    uint8[3] public bought;
    uint8[3] z;

    // set the addresses in store
    function setStore(uint8[3] memory _addresses) public {
        bought = _addresses;
    }

    function test() public {
        [1];
        z = [1];
        setStore(z);
    }
}
