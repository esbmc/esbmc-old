pragma solidity >=0.5.0;

interface caculator {
    struct Book {
        string title;
        string author;
        uint book_id;
    }
}

contract Test is caculator{
    Book book;
    function setBook() public {
        book = Book("Learn Java", "TP", 1);
        assert(getBook() == 2);
    }

    function getBook() public returns (uint) {
        return book.book_id;
    }
}