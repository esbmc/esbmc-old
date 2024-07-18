pragma solidity >=0.5.0;

interface caculator {
    function getResult() external view ;
}

contract Test is caculator{
    constructor() {}
    function getResult() external view {
        int8 a = 8;
        int8 b = 8;
        int8 result = 0;
        result = a + b;
        assert(result == 17);
    }
}
