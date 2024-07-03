// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;

contract Book {
    Book public book;
    string public title;
    string public author;
    uint public book_id;

    constructor(
        string memory _title,
        string memory _author,
        uint _book_id
    ) public {
        title = _title;
        author = _author;
        book_id = 1;
    }
    function test() public {
        // u cannot do this
        // book = new Book("1", "2", 10);
        assert(book_id == 1);
    }
}
