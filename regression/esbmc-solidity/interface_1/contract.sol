pragma solidity >=0.5.0;

interface caculator {
    function getResult() external view ;
}

contract Test is caculator{
    function getResult() external view {
        int8 y0 = 127;
        int8 y1 = 1;
        y0 = y0 + y1;
		assert(y0 > 0);
    }
}
