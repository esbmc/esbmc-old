def foo(arg:bytes) -> None:
  assert arg[0] == 72
  assert arg[1] == 101
  assert arg[2] == 108
  assert arg[3] == 108
  assert arg[4] == 111


data_arr = b'Hello'
#b = bytes([72, 101, 108, 108, 111])

assert data_arr[0] == 72
assert data_arr[1] == 101
assert data_arr[2] == 108
assert data_arr[3] == 108
assert data_arr[4] == 111

foo(data_arr)