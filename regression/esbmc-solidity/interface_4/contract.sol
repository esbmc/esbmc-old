pragma solidity >=0.5.0;

interface caculator {
    function getResult() external view ;
}

interface caculator2 {
    function getResult2() external view returns (uint8);
}

contract Test is caculator{
    constructor() {}
    function getResult() external view {
        int8 a = 8;
        int8 b = 8;
        int8 result = 0;
        result = a + b;
        assert(result == 16);
    }
}

contract Test2 is caculator2{
    constructor() {}
    function getResult2() external view returns (uint8) {
        uint8 a = 127;
        uint8 b = 0;
        uint8 result = a / b;
        return result;
    }
}
