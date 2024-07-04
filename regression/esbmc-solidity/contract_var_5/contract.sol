// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;


contract Book {
    string public title;
    string public author;
    uint public book_id;
    uint public  s;

    constructor() {
        s=10;
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
    Book public book;
    constructor() {
    }

    function setBook() public {
        book.setDetails("Learn Java", "TP", 1);
        assert(book.book_id() == 0 || book.book_id() == 1);
    }
}
