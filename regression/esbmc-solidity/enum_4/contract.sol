// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.5.0;

contract Book {
    string public title;
    string public author;
    uint public book_id;

    event t();
    enum Status {
        Pending,
        Shipped,
        Accepted,
        Rejected,
        Canceled
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

contract Base  {
    function test() public
    {
        assert(uint(Book.Status.Accepted) == 2);
    }
}
