pragma solidity >=0.5.0;

interface caculator {
    function getResult() external view returns (int8);
}

contract Test is caculator{
    constructor() {}
    function getResult() external view returns (int8) {
        int8 x ;
        int8 y ;
        for (int i = 0; i < 10; i++) x++;
        if (x == 10) x = 1;
        while (x-- >= 0) y /= x;
        return 1;
    }
}
