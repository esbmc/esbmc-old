// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.8.0;

interface caculator {
    function getResult() external view ;
}

interface caculator2 {
    function getResult2() external view returns (uint8);
}