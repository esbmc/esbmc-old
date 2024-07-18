n = 10 # Infer type of lhs from constant value (int)
p = n  # Infer type of lhs from rhs variable type (int)

def foo(a:int) -> None:
  b = 10
  c = a # Infer type of lhs from function arg type (int)
  d = n # Infer from global scope variable

v:uint64 = 16
y = 1
x = (v + y)//2

f = -1

def bar() -> int:
    return 1

a = bar() # Infer from function return
assert a == 1