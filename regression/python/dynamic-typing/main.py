n = 10 # Infer type of lhs from constant value (int)
p = n  # Infer type of lhs from rhs variable type (int)

def foo(a:int) -> None:
  b = 10
  c = a # Infer type of lhs from function arg type (int)

x = 4
y = (x + 1) // 2