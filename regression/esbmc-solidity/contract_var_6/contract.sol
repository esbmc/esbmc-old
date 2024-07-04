// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;

contract Book {
    string public title;
    string public author;
    uint public book_id = 1;

    constructor() {
        book_id = 2;
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
        Book book2 = new Book();
        assert(book2.book_id() == 2);
    }

    function setBook() public {
        book = new Book();
        book.setDetails("Learn Java", "TP", 1);
        assert(book.book_id() == 1);
    }
}
