// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;

contract A{
    constructor(){assert(1==0);}
}

contract Book is A {
    string public title;
    string public author;
    uint public book_id;

    constructor() {
    }
    function setDetails(
        string memory _title,
        string memory _author,
        uint _book_id
    ) public {
        title = _title;
        author = _author;
        book_id = _book_id;
    }
}

contract Base {
    constructor() {
        Book book2 = new Book();
    }
}
