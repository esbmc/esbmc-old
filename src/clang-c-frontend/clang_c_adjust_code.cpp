#include <clang-c-frontend/clang_c_adjust.h>
#include <clang-c-frontend/typecast.h>
#include <util/bitvector.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/expr_util.h>
#include <util/prefix.h>
#include <util/std_code.h>

void clang_c_adjust::adjust_code(codet &code)
{
  const irep_idt &statement = code.statement();

  if (statement == "ifthenelse")
  {
    adjust_ifthenelse(code);
  }
  else if (statement == "while" || statement == "dowhile")
  {
    adjust_while(code);
  }
  else if (statement == "for")
  {
    adjust_for(code);
  }
  else if (statement == "switch")
  {
    adjust_switch(code);
  }
  else if (statement == "assign")
  {
    adjust_assign(code);
  }
  else if (statement == "decl")
  {
    adjust_decl(code);
  }
  else if (statement == "function_call")
  {
  }
  else if (statement == "decl-block")
    adjust_decl_block(code);
  else
  {
    if (statement == "expression" && is_array_like(code.op0().type()))
    {
      /* An array-type'd statement like "y->ss;" where y is a pointer to
       *
       *   struct { int ss[128]; }
       *
       * is not assumed to not exist by the dereference code. Thus, convert it
       * to
       *
       *   &y->ss[0];
       *
       * instead. This is fine, because the value of the expression statement is
       * unused.
       */
      exprt &op = code.op0();
      op = address_of_exprt(index_exprt(op, constant_exprt(0, index_type())));
    }
    adjust_operands(code);
  }
}

void clang_c_adjust::adjust_decl_block(codet &code)
{
  Forall_operands (it, code)
    adjust_expr(*it);
}

void clang_c_adjust::adjust_decl(codet &code)
{
  if (code.operands().size() == 1)
  {
    adjust_type(code.op0().type());
    return;
  }

  assert(code.operands().size() == 2);

  // Check assignment
  adjust_expr(code.op1());

  // Check type
  adjust_type(code.op0().type());

  // Create typecast on assingments, if needed
  gen_typecast(ns, code.op1(), code.op0().type());
}

void clang_c_adjust::adjust_ifthenelse(codet &code)
{
  adjust_operands(code);

  // If the condition is not of boolean type, it must be casted
  gen_typecast_bool(ns, code.op0());
}

void clang_c_adjust::adjust_while(codet &code)
{
  adjust_operands(code);

  // If the condition is not of boolean type, it must be casted
  gen_typecast_bool(ns, code.op0());
}

void clang_c_adjust::adjust_for(codet &code)
{
  adjust_operands(code);

  // If the condition is not of boolean type, it must be casted
  gen_typecast_bool(ns, code.op1());

  // the "for" statement has an implicit block around it,
  // since code.op0() may contain declarations
  //
  // we therefore transform
  //
  //   for(a;b;c) d;
  //
  // to
  //
  //   { a; for(;b;c) d; }
  //

  code_blockt code_block;
  code_block.location() = code.location();
  if (to_code(code.op3()).get_statement() == "block")
    code_block.end_location(to_code(code.op3()).end_location());

  code_block.reserve_operands(2);
  code_block.move_to_operands(code.op0());
  code.op0().make_nil();
  code_block.move_to_operands(code);
  code.swap(code_block);
}

void clang_c_adjust::adjust_switch(codet &code)
{
  adjust_operands(code);

  // If the condition is not of int type, it must be casted
  gen_typecast_arithmetic(ns, code.op0());
}

void clang_c_adjust::adjust_assign(codet &code)
{
  adjust_operands(code);

  // Create typecast on assingments, if needed
  gen_typecast(ns, code.op1(), code.op0().type());
}
