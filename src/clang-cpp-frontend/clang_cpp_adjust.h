#ifndef CLANG_CPP_FRONTEND_CLANG_CPP_ADJUST_H_
#define CLANG_CPP_FRONTEND_CLANG_CPP_ADJUST_H_

#include <clang-c-frontend/clang_c_adjust.h>

/**
 * clang C++ adjuster class for:
 *  - symbol adjustment, dealing with ESBMC-IR `symbolt`
 *  - expression adjustment, dealing with ESBMC-IR `exprt` or other IRs derived from exprt
 *  - code adjustment, dealing with ESBMC-IR `codet` or other IRs derived from codet
 *  - implicit code generation,
 *      e.g. some IRs are not generated by converter, for example clang AST doesn't contain vtable_pointers (vptr),
 *      so we generate the IRs for vtable_pointer initializations in the adjuster
 *
 * Some ancillary methods to support the expr/code adjustments above
 */
class clang_cpp_adjust : public clang_c_adjust
{
public:
  explicit clang_cpp_adjust(contextt &_context);
  virtual ~clang_cpp_adjust() = default;

  /**
   * methods for symbol adjustment
   */
  void adjust_symbol(symbolt &symbol) override;

  /**
   * methods for code (codet) adjustment
   * and other IRs derived from codet
   */
  void adjust_code(codet &code) override;
  void adjust_while(codet &code) override;
  void adjust_switch(codet &code) override;
  void adjust_for(codet &code) override;
  void adjust_ifthenelse(codet &code) override;
  void adjust_decl_block(codet &code) override;
  void adjust_catch(codet &code);
  void adjust_throw_decl(codet &code);

  /**
   * methods for expression (exprt) adjustment
   * and other IRs derived from exprt
   */
  void adjust_member(member_exprt &expr) override;
  void adjust_side_effect(side_effect_exprt &expr) override;
  void adjust_side_effect_assign(side_effect_exprt &expr);
  void adjust_side_effect_function_call(
    side_effect_expr_function_callt &expr) override;
  void adjust_function_call_arguments(
    side_effect_expr_function_callt &expr) override;
  void adjust_reference(exprt &expr) override;
  void adjust_new(exprt &expr);
  void adjust_cpp_member(member_exprt &expr);

  /**
   * Adjusts a C++ pseudo-destructor call expression.
   *
   * This method is responsible for handling adjustments specific to
   * pseudo-destructor calls in C++ code. The converter has
   * generated a function call to the pseudo-destructor, but there
   * is nothing to actually call. Instead, only the base object of
   * the destructor call is evaluted.
   *
   * @param expr The expression representing the pseudo-destructor call
   */
  void adjust_cpp_pseudo_destructor_call(exprt &expr);
  void adjust_if(exprt &expr) override;
  void adjust_side_effect_throw(side_effect_exprt &expr);

  /**
   * methods for implicit GOTO code generation
   */
  void gen_vptr_initializations(symbolt &symbol);
  /*
   * generate vptr initialization code for constructor:
   *  base->BLAH@vtable_ptr = $vtable::BLAH
   *    where BLAH stands for the constructors' class/struct's name
   *    and base could be `this` or `this.foo@data_object.bar@data_object`.
   *
   * Params:
   *  - vptr_comp: vptr component as in class' type `components` vector
   *  - base: the base of the BLAH@vtable_ptr member expression
   *  - ctor_type: type of the constructor symbol
   *  - new_code: the code expression for vptr initialization
   */
  void gen_vptr_init_code(
    const struct_union_typet::componentt &vptr_comp,
    const exprt &base,
    side_effect_exprt &new_code,
    const code_typet &ctor_type);
  exprt gen_vptr_init_lhs(
    const struct_union_typet::componentt &vptr_comp,
    const exprt &base,
    const code_typet &ctor_type);
  exprt gen_vptr_init_rhs(
    const struct_union_typet::componentt &comp,
    const code_typet &ctor_type);
  void gen_vbotptr_initializations(symbolt &symbol);
  /*
   * generate vbotptr initialization code for constructor:
   *  base->BLAH@vbase_offset_ptr = vbase_offset_table::BLAH
   *    where BLAH stands for the constructors' class/struct's name
   *    and base could be `this` or `this.foo@data_object.bar@data_object`.
   *
   * Params:
   *  - vbot_comp: vbotptr component as in class' type `components` vector
   *  - base: the base of the BLAH@vbase_offset_ptr member expression
   *  - ctor_type: type of the constructor symbol
   *  - new_code: the code expression for vbotptr initialization
   */
  void gen_vbotptr_init_code(
    const struct_union_typet::componentt &vbot_comp,
    const exprt &base,
    side_effect_exprt &new_code,
    const code_typet &ctor_type,
    std::vector<std::string> id_path_to_base);
  exprt gen_vbotptr_init_lhs(
    const struct_union_typet::componentt &vbot_comp,
    const exprt &base,
    const code_typet &ctor_type);
  exprt gen_vbotptr_init_rhs(
    const struct_union_typet::componentt &comp,
    const code_typet &ctor_type,
    std::vector<std::string> id_path_to_base);

  /**
   * ancillary methods to support the expr/code adjustments above
   */
  void convert_expression_to_code(exprt &expr);
  void convert_reference(exprt &expr);
  void convert_ref_to_deref_symbol(exprt &expr);
  void convert_lvalue_ref_to_deref_sideeffect(exprt &expr);
  void align_se_function_call_return_type(
    exprt &f_op,
    side_effect_expr_function_callt &expr) override;
  void convert_exception_id(
    const typet &type,
    const std::string &suffix,
    std::vector<irep_idt> &ids,
    bool is_catch = false);
  void get_this_ptr_symbol(const code_typet &ctor_type, exprt &this_ptr);
  void get_ref_to_data_object(
    exprt &base,
    const struct_union_typet::componentt &data_object_comp,
    exprt &data_object_ref);
  void handle_components_of_data_object(
    const code_typet &ctor_type,
    code_blockt &ctor_body,
    const struct_union_typet::componentst &components,
    exprt &base);
  void handle_components_of_data_object(
    const code_typet &ctor_type,
    code_blockt &ctor_body,
    const struct_union_typet::componentst &components,
    exprt &base,
    code_blockt &assignments_block,
    std::vector<std::string> id_path_to_base);

  /**
   * Generates an implicit copy and move constructor for a symbol if it is a union.
   * Clang does not generate copy and move constructors for unions, so we
   * need to generate one ourselves.
   *
   * @param symbol The symbol for which the implicit copy and move constructor is generated.
   */
  void gen_implicit_union_copy_move_constructor(symbolt &symbol);
};

#endif /* CLANG_CPP_FRONTEND_CLANG_CPP_ADJUST_H_ */
