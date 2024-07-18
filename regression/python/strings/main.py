str1 = 'abcde'
str2 = "python"

assert len(str1) == 5
assert len(str2) == 6

assert str1[0] == 'a'
assert str1[1] == 'b'
assert str1[2] == 'c'
assert str1[3] == 'd'
assert str1[4] == 'e'

str3 = str("test")
assert len(str3) == 4

other = str("test")
assert str3 == other
assert other == "test"