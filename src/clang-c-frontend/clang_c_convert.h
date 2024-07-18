#ifndef CLANG_C_FRONTEND_CLANG_C_CONVERT_H_
#define CLANG_C_FRONTEND_CLANG_C_CONVERT_H_

#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS

#include <util/context.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_types.h>
#include <util/symbol_generator.h>

// Forward dec, to avoid bringing in clang headers
namespace clang
{
class ASTUnit;
class ASTContext;
class SourceManager;
class FunctionDecl;
class Decl;
class VarDecl;
class ParmVarDecl;
class RecordDecl;
class QualType;
class Type;
class BuiltinType;
class Stmt;
class BinaryOperator;
class CompoundAssignOperator;
class UnaryOperator;
class AtomicExpr;
class CastExpr;
class NamedDecl;
class PresumedLoc;
class SourceLocation;
class CharacterLiteral;
class StringLiteral;
class IntegerLiteral;
class FloatingLiteral;
class TagDecl;
class FieldDecl;
class MemberExpr;
class EnumConstantDecl;
} // namespace clang

std::string
getFullyQualifiedName(const clang::QualType &, const clang::ASTContext &);

class clang_c_convertert
{
public:
  clang_c_convertert(
    contextt &_context,
    std::unique_ptr<clang::ASTUnit> &_AST,
    irep_idt _mode);
  virtual ~clang_c_convertert() = default;

  bool convert();

  /**
 * @brief Perform the typecast by creating a tmp variable on RHS
 *
 * The idea is to look for all components of the union and match
 * the type. If not found, throws an error
 *
 * @param dest RHS dest
 * @param type Union type
 * @param msg  Message object
 */
  static void
  gen_typecast_to_union(const namespacet &ns, exprt &dest, const typet &type);

  static std::string get_decl_name(const clang::NamedDecl &nd);

protected:
  clang::ASTContext *ASTContext;
  contextt &context;
  namespacet ns;
  std::unique_ptr<clang::ASTUnit> &AST;
  irep_idt mode;
  symbol_generator anon_symbol;

  // class symbol id prefix `tag-`
  std::string tag_prefix = "tag-";

  unsigned int current_scope_var_num;
  /**
   *  During get_expr(), which also transforms blocks/scopes, this represents
   *  the latest opened blocks from the top-level. A nullptr 'current_block'
   *  thus means file scope.
   */
  code_blockt *current_block;

  clang::SourceManager *sm;

  const clang::FunctionDecl *current_functionDecl;

  bool convert_builtin_types();
  bool convert_top_level_decl();

  /**
   *  Since this class is inherited by clang-cpp-frontend,
   *  some get_* functions are made `virtual' to deal with clang CXX declarations
   */
  virtual bool get_decl(const clang::Decl &decl, exprt &new_expr);

  virtual bool get_var(const clang::VarDecl &vd, exprt &new_expr);

  virtual bool get_function(const clang::FunctionDecl &fd, exprt &new_expr);

  virtual bool get_function_body(
    const clang::FunctionDecl &fd,
    exprt &new_expr,
    const code_typet &ftype);

  /**
   *  Parse function parameters
   *  This function simply contains a loop to populate the code argument list
   *  and calls get_function_body to parse each individual parameter.
   */
  virtual bool get_function_params(
    const clang::FunctionDecl &fd,
    code_typet::argumentst &params);
  /**
   *  Parse each individual parameter of the function
   */
  bool get_function_param(const clang::ParmVarDecl &pd, exprt &param);
  /*
   * This function determines whether we should name an unnamed function parameter
   * and continue to add its symbol.
   *
   * Params:
   *  pd: the clang AST node for the function parameter we are currently dealing with
   *  id: id for this function parameter
   *  name: name for this function parameter
   *  param: ESBMC's IR representing the function parameter
   */
  virtual void name_param_and_continue(
    const clang::ParmVarDecl &pd,
    std::string &id,
    std::string &name,
    exprt &param);

  virtual bool get_struct_union_class(const clang::RecordDecl &recordd);

  /*
   * Get class fields of the type `clang::Decl::Field`
   *
   * Params:
   *  recordd: clang AST of the class we are currently dealing with
   *  type: ESBMC IR representing the class' type
   */
  virtual bool get_struct_union_class_fields(
    const clang::RecordDecl &recordd,
    struct_union_typet &type);

  /*
   * This function not only gets class method but also gets
   * the other declarations inside a class. The declarations can be
   * of any valid type other than `clang::Decl::Field`,
   * such as a class constructor of the type `clang::Decl::CXXConstructor`,
   * or a class static member of the type `clang::Decl::Var`
   *
   * Params:
   *  recordd: clang AST of the class we are currently dealing with
   *  type: ESBMC IR representing the class' type
   */
  virtual bool get_struct_union_class_methods_decls(
    const clang::RecordDecl &recordd,
    typet &type);

  virtual bool get_type(const clang::QualType &type, typet &new_type);

  virtual bool get_type(const clang::Type &the_type, typet &new_type);

  void get_ref_to_struct_type(typet &type);

  bool get_builtin_type(const clang::BuiltinType &bt, typet &new_type);

  virtual bool get_expr(const clang::Stmt &stmt, exprt &new_expr);

  bool get_enum_value(const clang::EnumConstantDecl *e, exprt &new_expr);

  virtual bool get_decl_ref(const clang::Decl &decl, exprt &new_expr);

  bool
  get_binary_operator_expr(const clang::BinaryOperator &binop, exprt &new_expr);

  bool get_compound_assign_expr(
    const clang::CompoundAssignOperator &compop,
    exprt &new_expr);

  bool
  get_unary_operator_expr(const clang::UnaryOperator &uniop, exprt &new_expr);

  bool get_atomic_expr(const clang::AtomicExpr &atm, exprt &new_expr);

  bool get_cast_expr(const clang::CastExpr &cast, exprt &new_expr);

  void get_default_symbol(
    symbolt &symbol,
    irep_idt module_name,
    typet type,
    irep_idt base_name,
    irep_idt unique_name,
    locationt location);

  virtual void
  get_decl_name(const clang::NamedDecl &vd, std::string &name, std::string &id);

  void
  get_start_location_from_stmt(const clang::Stmt &stmt, locationt &location);

  void
  get_final_location_from_stmt(const clang::Stmt &stmt, locationt &location);

  void get_location_from_decl(const clang::Decl &decl, locationt &location);

  void set_location(
    clang::PresumedLoc &PLoc,
    std::string &function_name,
    locationt &location);

  void get_presumed_location(
    const clang::SourceLocation &loc,
    clang::PresumedLoc &PLoc);

  std::string get_filename_from_path(std::string path);
  std::string get_modulename_from_path(std::string path);

  void convert_expression_to_code(exprt &expr);

  bool convert_character_literal(
    const clang::CharacterLiteral &char_literal,
    exprt &dest);

  bool convert_string_literal(
    const clang::StringLiteral &string_literal,
    exprt &dest);

  bool convert_integer_literal(
    const clang::IntegerLiteral &integer_literal,
    exprt &dest);

  bool convert_float_literal(
    const clang::FloatingLiteral &floating_literal,
    exprt &dest);

  const clang::Decl *get_DeclContext_from_Stmt(const clang::Stmt &stmt);

  const clang::Decl *get_top_FunctionDecl_from_Stmt(const clang::Stmt &stmt);

  /*
   * add additional annotations if a class/struct/union field has alignment attribute
   * Arguments:
   *   field: clang AST representing the class/struct/union field we are dealing with
   *   comp: a `component` in class/struct/union's symbol type
   *   type: a class/struct/union's symbol type
   */
  bool check_alignment_attributes(
    const clang::FieldDecl *field,
    struct_typet::componentt &comp);

  /*
   * check if a class/struct/union's field has global storage
   * (e.g. static)
   * Arguments:
   *   field: clang AST representing the class/struct/union field we are dealing with
   */
  bool is_field_global_storage(const clang::FieldDecl *field);

  /*
   * Function to check whether a member function call refers to
   * a virtual/overriding method.
   *
   * Params:
   *  - member: a clang::MemberExpr representing member access x.F or x->F where
   *  where F could be a field or a method
   *
   * For C, it always return false.
   */
  virtual bool perform_virtual_dispatch(const clang::MemberExpr &member);

  /*
   * Function to get the ESBMC IR representing a virtual function table dynamic binding for "->" operator
   * Turning
   *  x->F
   * into
   *  x->X@vtable_pointer->F
   *
   * Params:
   *  - member: the method to which this MemberExpr refers
   *  - new_expr: ESBMC IR to represent `x->X@vtable_ptr->F`
   *
   * For C, it can't happen.
   */
  virtual bool
  get_vft_binding_expr(const clang::MemberExpr &member, exprt &new_expr);

  /*
   * Function to check whether a clang::FunctionDec represents a
   * clang::CXXMethodDecl that is virtual OR overrides another function
   *
   * Params:
   *  - fd: a clang::FunctionDec we are currently dealing with
   *
   * For C, it always return false.
   */
  virtual bool is_fd_virtual_or_overriding(const clang::FunctionDecl &fd);

  virtual bool is_aggregate_type(const clang::QualType &q_type);

  /*
   * Function to check whether a MemberExpr references to a static variable
   */
  bool is_member_decl_static(const clang::MemberExpr &member);
};

#endif /* CLANG_C_FRONTEND_CLANG_C_CONVERT_H_ */
