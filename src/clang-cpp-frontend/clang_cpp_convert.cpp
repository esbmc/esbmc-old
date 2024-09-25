#include <util/compiler_defs.h>
// Remove warnings from Clang headers
CC_DIAGNOSTIC_PUSH()
CC_DIAGNOSTIC_IGNORE_LLVM_CHECKS()
#include <clang/Basic/Version.inc>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclFriend.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/AST/Type.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/AST/ParentMapContext.h>
#include <llvm/Support/raw_os_ostream.h>
CC_DIAGNOSTIC_POP()

#include <clang-cpp-frontend/clang_cpp_convert.h>
#include <util/expr_util.h>
#include <util/message.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <fmt/core.h>
#include <clang-c-frontend/typecast.h>
#include <util/c_types.h>
#include "clang-c-frontend/padding.h"
#include "util/i2string.h"
#include "util/cpp_data_object.h"
#include "util/cpp_typecast.h"

clang_cpp_convertert::clang_cpp_convertert(
  contextt &_context,
  std::unique_ptr<clang::ASTUnit> &_AST,
  irep_idt _mode)
  : clang_c_convertert(_context, _AST, _mode)
{
}

bool clang_cpp_convertert::get_decl(const clang::Decl &decl, exprt &new_expr)
{
  new_expr = code_skipt();

  switch (decl.getKind())
  {
  case clang::Decl::LinkageSpec:
  {
    const clang::LinkageSpecDecl &lsd =
      static_cast<const clang::LinkageSpecDecl &>(decl);

    for (auto decl : lsd.decls())
      if (get_decl(*decl, new_expr))
        return true;
    break;
  }

  case clang::Decl::CXXRecord:
  {
    const clang::CXXRecordDecl &cxxrd =
      static_cast<const clang::CXXRecordDecl &>(decl);

    if (get_struct_union_class(cxxrd, true))
      return true;

    break;
  }

  case clang::Decl::CXXConstructor:
  case clang::Decl::CXXMethod:
  case clang::Decl::CXXDestructor:
  case clang::Decl::CXXConversion:
  {
    const clang::CXXMethodDecl &cxxmd =
      static_cast<const clang::CXXMethodDecl &>(decl);

    assert(llvm::dyn_cast<clang::TemplateDecl>(&cxxmd) == nullptr);
    if (get_method(cxxmd, new_expr))
      return true;

    break;
  }

  case clang::Decl::Namespace:
  {
    const clang::NamespaceDecl &namesd =
      static_cast<const clang::NamespaceDecl &>(decl);

    for (auto decl : namesd.decls())
      if (get_decl(*decl, new_expr))
        return true;

    break;
  }

  case clang::Decl::ClassTemplateSpecialization:
  {
    const clang::ClassTemplateSpecializationDecl &cd =
      static_cast<const clang::ClassTemplateSpecializationDecl &>(decl);

    if (get_struct_union_class(cd, true))
      return true;
    break;
  }

  case clang::Decl::Friend:
  {
    const clang::FriendDecl &fd = static_cast<const clang::FriendDecl &>(decl);

    if (fd.getFriendDecl() != nullptr)
      if (get_decl(*fd.getFriendDecl(), new_expr))
        return true;
    break;
  }

  // We can ignore any these declarations
  case clang::Decl::ClassTemplatePartialSpecialization:
  case clang::Decl::VarTemplatePartialSpecialization:
  case clang::Decl::Using:
  case clang::Decl::UsingShadow:
  case clang::Decl::ConstructorUsingShadow:
  case clang::Decl::UsingDirective:
  case clang::Decl::TypeAlias:
  case clang::Decl::NamespaceAlias:
  case clang::Decl::AccessSpec:
  case clang::Decl::UnresolvedUsingValue:
  case clang::Decl::UnresolvedUsingTypename:
  case clang::Decl::TypeAliasTemplate:
  case clang::Decl::VarTemplate:
  case clang::Decl::ClassTemplate:
  case clang::Decl::FunctionTemplate:
    break;

  default:
    return clang_c_convertert::get_decl(decl, new_expr);
  }

  return false;
}

void clang_cpp_convertert::get_decl_name(
  const clang::NamedDecl &nd,
  std::string &name,
  std::string &id)
{
  id = name = clang_c_convertert::get_decl_name(nd);
  std::string id_suffix = "";

  switch (nd.getKind())
  {
  case clang::Decl::CXXConstructor:
    if (name.empty())
    {
      // Anonymous constructor, generate a name based on the location
      const clang::CXXConstructorDecl &cd =
        static_cast<const clang::CXXConstructorDecl &>(nd);

      locationt location_begin;
      get_location_from_decl(cd, location_begin);
      std::string location_begin_str = location_begin.file().as_string() + "_" +
                                       location_begin.function().as_string() +
                                       "_" + location_begin.line().as_string() +
                                       "_" +
                                       location_begin.column().as_string();
      name = "__anon_constructor_at_" + location_begin_str;
      std::replace(name.begin(), name.end(), '.', '_');
      // "id" will be derived from USR so break instead of return here
      break;
    }
    break;
  case clang::Decl::ParmVar:
  {
    const clang::ParmVarDecl &pd = static_cast<const clang::ParmVarDecl &>(nd);
    // If the parameter is unnamed, it will be handled by `name_param_and_continue`, but not here.
    if (!name.empty())
    {
      /* Add the parameter index to the name and id to avoid name clashes.
     * Consider the following example:
     * ```
     * template <typename... f> int e(f... g) {return 0;};
     * int d = e(1, 2.0);
     * ```
     * The two parameters in the function e will have the same name and id
     * because clang does not differentiate between them. This will cause
     * the second parameter to overwrite the first in the symbol table which
     * causes all kinds of problems. To avoid this, we append the parameter
     * index to the name and id.
    */
      name += "::" + std::to_string(pd.getFunctionScopeIndex());
      id_suffix = "::" + std::to_string(pd.getFunctionScopeIndex());
      break;
    }
    clang_c_convertert::get_decl_name(nd, name, id);
    return;
  }

  default:
    clang_c_convertert::get_decl_name(nd, name, id);
    return;
  }

  clang::SmallString<128> DeclUSR;
  if (!clang::index::generateUSRForDecl(&nd, DeclUSR))
  {
    id = DeclUSR.str().str() + id_suffix;
    auto replaceStringInPlace = [](
                                  std::string &subject,
                                  const std::string &search,
                                  const std::string &replace) {
      size_t pos = 0;
      while ((pos = subject.find(search, pos)) != std::string::npos)
      {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
      }
    };
    replaceStringInPlace(id, "#", "_wtf_hashtag_");
    if (id.find("#") != std::string::npos)
    {
      log_error("Yo wtf, in c mode also: {}", id);
    }
    return;
  }

  // Otherwise, abort
  std::ostringstream oss;
  llvm::raw_os_ostream ross(oss);
  ross << "Unable to generate the USR for:\n";
  nd.dump(ross);
  ross.flush();
  log_error("{}", oss.str());
  abort();
}
bool clang_cpp_convertert::get_type(
  const clang::QualType &q_type,
  typet &new_type,
  bool complete)
{
  return clang_c_convertert::get_type(q_type, new_type, complete);
}

bool clang_cpp_convertert::get_type(
  const clang::Type &the_type,
  typet &new_type,
  bool complete)
{
  switch (the_type.getTypeClass())
  {
  case clang::Type::SubstTemplateTypeParm:
  {
    const clang::SubstTemplateTypeParmType &substmpltt =
      static_cast<const clang::SubstTemplateTypeParmType &>(the_type);

    if (get_type(substmpltt.getReplacementType(), new_type, complete))
      return true;
    break;
  }

  case clang::Type::TemplateSpecialization:
  {
    const clang::TemplateSpecializationType &templSpect =
      static_cast<const clang::TemplateSpecializationType &>(the_type);

    if (get_type(templSpect.desugar(), new_type, complete))
      return true;
    break;
  }

  case clang::Type::MemberPointer:
  {
    const clang::MemberPointerType &mpt =
      static_cast<const clang::MemberPointerType &>(the_type);

    typet sub_type;
    if (get_type(mpt.getPointeeType(), sub_type, false))
      return true;

    typet class_type;
    if (get_type(*mpt.getClass(), class_type, complete))
      return true;

    new_type = gen_pointer_type(sub_type);
    new_type.set("to-member", class_type);
    break;
  }

  case clang::Type::RValueReference:
  {
    const clang::RValueReferenceType &rvrt =
      static_cast<const clang::RValueReferenceType &>(the_type);

    typet sub_type;
    if (get_type(rvrt.getPointeeType(), sub_type, false))
      return true;

    // This is done similarly to lvalue reference.
    if (sub_type.is_struct() || sub_type.is_union())
    {
      struct_union_typet t = to_struct_union_type(sub_type);
      sub_type = symbol_typet(tag_prefix + t.tag().as_string());
    }

    if (rvrt.getPointeeType().isConstQualified())
      sub_type.cmt_constant(true);

    new_type = gen_pointer_type(sub_type);
    new_type.set("#rvalue_reference", true);

    break;
  }

#if CLANG_VERSION_MAJOR >= 14
  case clang::Type::Using:
  {
    const clang::UsingType &ut =
      static_cast<const clang::UsingType &>(the_type);

    if (get_type(ut.getUnderlyingType(), new_type))
      return true;

    break;
  }
#endif

  default:
    return clang_c_convertert::get_type(the_type, new_type, complete);
  }

  return false;
}

bool clang_cpp_convertert::get_var(const clang::VarDecl &vd, exprt &new_expr)
{
  // Only convert instantiated variables
  if (vd.getDeclContext()->isDependentContext())
    return false;

  return clang_c_convertert::get_var(vd, new_expr);
}

bool clang_cpp_convertert::get_method(
  const clang::CXXMethodDecl &md,
  exprt &new_expr)
{
  // Only convert instantiated functions/methods not depending on a template parameter
  if (md.isDependentContext())
    return false;

  // Don't convert if implicit, unless it's a constructor/destructor/
  // Copy assignment Operator/Move assignment Operator
  // A compiler-generated default ctor/dtor is considered implicit, but we have
  // to parse it.
  if (
    md.isImplicit() && !is_ConstructorOrDestructor(md) &&
    !is_CopyOrMoveOperator(md))
    return false;

  if (clang_c_convertert::get_function(md, new_expr))
    return true;

  if (annotate_class_method(md, new_expr))
    return true;
  if (get_struct_union_class(*md.getParent(), true))
    return true;

  return false;
}

bool clang_cpp_convertert::get_struct_union_class(
  const clang::RecordDecl &rd,
  bool complete)
{
  // Only convert RecordDecl not depending on a template parameter
  if (rd.isDependentContext())
    return false;

  return clang_c_convertert::get_struct_union_class(rd, complete);
}

void clang_cpp_convertert::create_data_object_type(const clang::RecordDecl &rd)
{
  std::string id, name;
  get_decl_name(rd, name, id);
  name = name + cpp_data_object::data_object_suffix;
  id = id + cpp_data_object::data_object_suffix;

  locationt location_begin;
  get_location_from_decl(rd, location_begin);

  symbolt *sym = context.find_symbol(id);
  assert(!sym);
  struct_union_typet t(typet::t_struct);
  t.location() = location_begin;
  t.tag(name);

  symbolt symbol;
  get_default_symbol(
    symbol,
    get_modulename_from_path(location_begin.file().as_string()),
    t,
    name,
    id,
    location_begin);

  symbol.is_type = true;
  sym = context.move_symbol_to_context(symbol);
  assert(sym->is_type);
}

bool clang_cpp_convertert::get_struct_union_class_fields(
  const clang::RecordDecl &rd,
  struct_union_typet &type)
{
  // Note: If a struct is defined inside an extern C, it will be a RecordDecl
  const clang::CXXRecordDecl *cxxrd = llvm::dyn_cast<clang::CXXRecordDecl>(&rd);
  if (!cxxrd || cxxrd->isCLike())
  {
    type.set("#is_c_like", true);
    return clang_c_convertert::get_struct_union_class_fields(rd, type);
  }
  assert(cxxrd);
  struct_union_typet *target_object_type = &type;
  if (!cxxrd->isUnion())
  {
    create_data_object_type(rd);
    struct_typet &data_object_type = cpp_data_object::get_data_object_type(
      tag_prefix + type.tag().as_string(), context);
    typet data_object_symbol_type;
    cpp_data_object::get_data_object_symbol_type(
      tag_prefix + type.tag().as_string(), data_object_symbol_type);

    target_object_type = &data_object_type;

    struct_typet::componentt data_object_component;
    data_object_component.name(data_object_type.tag().as_string());
    data_object_component.pretty_name(
      tag_prefix + data_object_type.tag().as_string());
    data_object_component.type() = data_object_symbol_type;
    type.components().push_back(data_object_component);

    // pull bases in

    for (auto non_virtual_base : cxxrd->bases())
    {
      if (non_virtual_base.isVirtual())
        continue;
      assert(!non_virtual_base.isVirtual());
      get_base(type, non_virtual_base, false);
    }

    assert(type.is_struct());
    if (get_struct_class_virtual_base_offsets(*cxxrd, to_struct_type(type)))
      return true;
  }

  bool added_a_field = false;
  // Parse the fields
  for (auto const &field : rd.fields())
  {
    struct_typet::componentt comp;
    if (get_decl(*field, comp))
      return true;

    // Check for alignment attributes
    if (check_alignment_attributes(field, comp))
      return true;

    // Don't add fields that have global storage (e.g., static)
    if (is_field_global_storage(field))
      continue;

    if (annotate_class_field(*field, *target_object_type, comp))
      return true;

    target_object_type->components().push_back(comp);
    added_a_field = true;
  }
  if (!added_a_field)
  {
    // If we didn't add any fields, we need to add a dummy field to make sure the struct is not empty
    struct_typet::componentt comp;
    comp.type() = char_type();
    comp.name("dummy_field");
    target_object_type->components().push_back(comp);
  }

  // Add tail padding between own fields of the class and
  // any virtual bases. Otherwise, pointers to the start of virtual bases could be misaligned.
  if (cxxrd->getNumVBases() > 0)
  {
    add_padding(type, ns);
  }

  // pull virtual bases in after fields.
  /* We need to add virtual bases after fields because we need to have consistent
   * offsets for the fields. If we add virtual bases before fields, the offsets
   * of the fields will be incorrect.
   * E.g. consider this:
   * ```
   * struct A { int a;};
   * struct B { int b;};
   * struct C : A, virtual B { int c;}
   * struct D : C, virtual B { int d;}
   * ```
   * If we have a `C`, the offset of `c` will be 8: we first layout `A` (4 byte) and then `B` (4 byte), so `c` is at offset 8.
   * This is however wrong. we should first layout `A` (4 byte), then `C`'s own field `c` (4 byte) and then `B` (4 byte), ergo `c` should be at offset 4.
   * If we don't do this, the offset for `D` will be incorrect:
   * If we have a `D`, the offset of `c` will be 4: we first layout `C`. We do this by copying the layout of `C`, but ignoring any fields from virtual bases.
   * Because we ignore `b` from `B`, the offset of `c` will be 4 which is inconsistent with the offset of `c` in `C`.
   */

  for (const auto &virtual_base : cxxrd->vbases())
  {
    assert(virtual_base.isVirtual());
    get_base(type, virtual_base, true);
  }

  return false;
}
void clang_cpp_convertert::get_base(
  struct_union_typet &type,
  const clang::CXXBaseSpecifier &base,
  bool is_virtual)
{
  assert(
    !has_suffix(type.tag().as_string(), cpp_data_object::data_object_suffix));
  std::string base_name, base_id;
  get_decl_name(*base.getType()->getAsCXXRecordDecl(), base_name, base_id);
  get_struct_union_class(*base.getType()->getAsCXXRecordDecl(), true);
  get_base_components_methods(
    base_id,
    base_name,
    type,
    is_virtual,
    !base.getType()->getAsCXXRecordDecl()->isCLike());
  // Add tail padding between bases. Otherwise, pointers to the start of bases could be misaligned.
  add_padding(type, ns);

  // get base class symbol
  symbolt *s = context.find_symbol(base_id);
  assert(s);
  type.add("bases").get_sub().emplace_back(s->id);
}

bool clang_cpp_convertert::get_struct_union_class_methods_decls(
  const clang::RecordDecl &recordd,
  typet &type)
{
  // Note: If a struct is defined inside an extern C, it will be a RecordDecl

  const clang::CXXRecordDecl *cxxrd =
    llvm::dyn_cast<clang::CXXRecordDecl>(&recordd);
  if (cxxrd == nullptr)
    return false;

  /*
   * Order of converting methods:
   *  1. convert virtual methods. We also need to add:
   *    a). virtual table type
   *    b). virtual pointers
   *  2. instantiate virtual tables
   */

  // skip unions as they don't have virtual methods
  if (!recordd.isUnion())
  {
    assert(type.is_struct());
    if (get_struct_class_virtual_methods(*cxxrd, to_struct_type(type)))
      return true;
  }

  // Iterate over the declarations stored in this context
  for (const auto &decl : cxxrd->decls())
  {
    // Fields were already added
    if (decl->getKind() == clang::Decl::Field)
      continue;

    // ignore self-referring implicit class node
    if (decl->getKind() == clang::Decl::CXXRecord && decl->isImplicit())
      continue;

    // virtual methods were already added
    if (decl->getKind() == clang::Decl::CXXMethod)
    {
      const auto md = llvm::dyn_cast<clang::CXXMethodDecl>(decl);
      if (md->isVirtual())
        continue;
    }

    struct_typet::componentt comp;

    if (
      const clang::FunctionTemplateDecl *ftd =
        llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
    {
      for (auto *spec : ftd->specializations())
      {
        if (get_template_decl_specialization(spec, true, comp))
          return true;

        // Add only if it isn't static
        if (spec->getStorageClass() != clang::SC_Static)
          to_struct_type(type).methods().push_back(comp);
      }

      continue;
    }
    else
    {
      if (get_decl(*decl, comp))
        return true;
    }

    // This means that we probably just parsed nested class,
    // don't add it to the class
    // TODO: The condition based on "skip" below doesn't look quite right.
    //       Need to use a proper logic to confirm a nested class, e.g.
    //       decl->getParent() == recordd, where recordd is the class
    //       we are currently dealing with
    if (comp.is_code() && to_code(comp).statement() == "skip")
      continue;

    if (
      const clang::CXXMethodDecl *cxxmd =
        llvm::dyn_cast<clang::CXXMethodDecl>(decl))
    {
      // Add only if it isn't static
      if (!cxxmd->isStatic())
      {
        assert(type.is_struct() || type.is_union());
        to_struct_type(type).methods().push_back(comp);
      }
      else
      {
        log_error("static method is not supported in {}", __func__);
        abort();
      }
    }
  }
  if (!cxxrd->isUnion())
  {
    assert(type.is_struct());
    /*
     * Set up virtual base offset table(vbo) variable symbols
     * Each vbo is modelled as a struct of char pointers used as offset.
     * We do this here, because adding virtual methods would cause a vptr to be added to the struct.
     * This is a problem, because when setting up the vbo, we compute offsets to the base objects and
     * if we later add a vptr, this offset will be _wrong_. Ideally, we would "lock" the components of a struct
     * such that we can't add new components after we have computed the offsets or move the offset computation to the adjuster.
     */
    setup_vbo_table_struct_variables(*cxxrd, to_struct_type(type));
  }

  return false;
}

void clang_cpp_convertert::get_this_expr(
  const typet &expected_this_type,
  exprt &new_expr)
{
  std::size_t address =
    reinterpret_cast<std::size_t>(current_functionDecl->getFirstDecl());

  this_mapt::iterator it = this_map.find(address);
  if (it == this_map.end())
  {
    log_error(
      "Pointer `this' for method {} was not added to scope",
      clang_c_convertert::get_decl_name(*current_functionDecl));
    abort();
  }

  assert(expected_this_type == it->second.second);

  new_expr = symbol_exprt(it->second.first, it->second.second);
}

bool clang_cpp_convertert::get_expr(const clang::Stmt &stmt, exprt &new_expr)
{
  locationt location;
  get_start_location_from_stmt(stmt, location);

  switch (stmt.getStmtClass())
  {
  case clang::Stmt::CXXReinterpretCastExprClass:
  // TODO: ReinterpretCast should actually generate a bitcast
  case clang::Stmt::CXXFunctionalCastExprClass:
  case clang::Stmt::CXXStaticCastExprClass:
  case clang::Stmt::CXXConstCastExprClass:
  {
    const clang::CastExpr &cast = static_cast<const clang::CastExpr &>(stmt);

    if (get_cast_expr(cast, new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXDefaultArgExprClass:
  {
    const clang::CXXDefaultArgExpr &cxxdarg =
      static_cast<const clang::CXXDefaultArgExpr &>(stmt);

    if (get_expr(*cxxdarg.getExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXDefaultInitExprClass:
  {
    const clang::CXXDefaultInitExpr &cxxdie =
      static_cast<const clang::CXXDefaultInitExpr &>(stmt);

    if (get_expr(*cxxdie.getExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXDynamicCastExprClass:
  {
    const clang::CXXDynamicCastExpr &cast =
      static_cast<const clang::CXXDynamicCastExpr &>(stmt);

    if (cast.isAlwaysNull())
    {
      typet t;
      if (get_type(cast.getType(), t, true))
        return true;

      new_expr = gen_zero(gen_pointer_type(t));
    }
    else if (get_cast_expr(cast, new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXBoolLiteralExprClass:
  {
    const clang::CXXBoolLiteralExpr &bool_literal =
      static_cast<const clang::CXXBoolLiteralExpr &>(stmt);

    if (bool_literal.getValue())
      new_expr = true_exprt();
    else
      new_expr = false_exprt();
    break;
  }

  case clang::Stmt::CXXMemberCallExprClass:
  {
    const clang::CXXMemberCallExpr &member_call =
      static_cast<const clang::CXXMemberCallExpr &>(stmt);

    const clang::Stmt *callee = member_call.getCallee();

    exprt callee_expr;
    if (get_expr(*callee, callee_expr))
      return true;

    typet type;
    clang::QualType qtype = member_call.getCallReturnType(*ASTContext);
    if (get_type(qtype, type, true))
      return true;

    side_effect_expr_function_callt call;
    call.function() = callee_expr;
    call.type() = type;

    // Add implicit object call: a this pointer or an object
    exprt implicit_object;
    if (get_expr(*member_call.getImplicitObjectArgument(), implicit_object))
      return true;

    call.arguments().push_back(implicit_object);

    // Do args
    for (const clang::Expr *arg : member_call.arguments())
    {
      exprt single_arg;
      if (get_expr(*arg, single_arg))
        return true;

      call.arguments().push_back(single_arg);
    }

    new_expr = call;
    break;
  }

  case clang::Stmt::CXXOperatorCallExprClass:
  {
    const clang::CXXOperatorCallExpr &operator_call =
      static_cast<const clang::CXXOperatorCallExpr &>(stmt);

    const clang::Stmt *callee = operator_call.getCallee();

    exprt callee_expr;
    if (get_expr(*callee, callee_expr))
      return true;

    typet type;
    clang::QualType qtype = operator_call.getCallReturnType(*ASTContext);
    if (get_type(qtype, type, true))
      return true;

    side_effect_expr_function_callt call;
    call.function() = callee_expr;
    call.type() = type;

    // Do args
    for (const clang::Expr *arg : operator_call.arguments())
    {
      exprt single_arg;
      if (get_expr(*arg, single_arg))
        return true;

      call.arguments().push_back(single_arg);
    }

    new_expr = call;
    break;
  }

  case clang::Stmt::ExprWithCleanupsClass:
  {
    const clang::ExprWithCleanups &ewc =
      static_cast<const clang::ExprWithCleanups &>(stmt);

    if (get_expr(*ewc.getSubExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXBindTemporaryExprClass:
  {
    const clang::CXXBindTemporaryExpr &cxxbtmp =
      static_cast<const clang::CXXBindTemporaryExpr &>(stmt);

    if (get_expr(*cxxbtmp.getSubExpr(), new_expr))
      return true;

    make_temporary(new_expr);

    break;
  }

  case clang::Stmt::SubstNonTypeTemplateParmExprClass:
  {
    const clang::SubstNonTypeTemplateParmExpr &substnttp =
      static_cast<const clang::SubstNonTypeTemplateParmExpr &>(stmt);

    if (get_expr(*substnttp.getReplacement(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::MaterializeTemporaryExprClass:
  {
    const clang::MaterializeTemporaryExpr &mtemp =
      static_cast<const clang::MaterializeTemporaryExpr &>(stmt);

    exprt tmp;
    if (get_expr(*mtemp.getSubExpr(), tmp))
      return true;

    side_effect_exprt temporary("temporary_object");
    temporary.type() = tmp.type();
    temporary.copy_to_operands(tmp);

    address_of_exprt addr(temporary);
    if (mtemp.isBoundToLvalueReference())
      addr.type().set("#reference", true);
    else
      addr.type().set("#rvalue_reference", true);

    new_expr.swap(addr);

    break;
  }

  case clang::Stmt::CXXNewExprClass:
  {
    const clang::CXXNewExpr &ne = static_cast<const clang::CXXNewExpr &>(stmt);

    typet t;
    if (get_type(ne.getType(), t, true))
      return true;

    if (ne.isArray())
    {
      new_expr = side_effect_exprt("cpp_new[]", t);

      // TODO: Implement support when the array size is empty
      assert(ne.getArraySize());
      exprt size;
      if (get_expr(**ne.getArraySize(), size))
        return true;

      new_expr.size(size);
    }
    else
    {
      new_expr = side_effect_exprt("cpp_new", t);
    }

    if (ne.hasInitializer())
    {
      exprt init;
      if (get_expr(*ne.getInitializer(), init))
        return true;

      convert_expression_to_code(init);

      new_expr.initializer(init);
    }

    break;
  }

  case clang::Stmt::CXXDeleteExprClass:
  {
    const clang::CXXDeleteExpr &de =
      static_cast<const clang::CXXDeleteExpr &>(stmt);

    new_expr = de.isArrayFormAsWritten()
                 ? side_effect_exprt("cpp_delete[]", empty_typet())
                 : side_effect_exprt("cpp_delete", empty_typet());

    exprt arg;
    if (get_expr(*de.getArgument(), arg))
      return true;

    new_expr.move_to_operands(arg);

    if (de.getDestroyedType()->getAsCXXRecordDecl())
    {
      typet destt;
      if (get_type(de.getDestroyedType(), destt, true))
        return true;
      new_expr.type() = destt;
    }

    break;
  }

  case clang::Stmt::CXXPseudoDestructorExprClass:
  {
    const clang::CXXPseudoDestructorExpr &cxxpd =
      static_cast<const clang::CXXPseudoDestructorExpr &>(stmt);
    // A pseudo-destructor expression has no run-time semantics beyond evaluating the base expression.
    exprt base;
    if (get_expr(*cxxpd.getBase(), base))
      return true;
    new_expr = exprt("cpp-pseudo-destructor");
    new_expr.move_to_operands(base);
    break;
  }

  case clang::Stmt::CXXScalarValueInitExprClass:
  {
    const clang::CXXScalarValueInitExpr &cxxsvi =
      static_cast<const clang::CXXScalarValueInitExpr &>(stmt);

    typet t;
    if (get_type(cxxsvi.getType(), t, true))
      return true;

    new_expr = gen_zero(t);
    break;
  }

  case clang::Stmt::CXXConstructExprClass:
  {
    const clang::CXXConstructExpr &cxxc =
      static_cast<const clang::CXXConstructExpr &>(stmt);

    if (get_constructor_call(cxxc, new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXThisExprClass:
  {
    const clang::CXXThisExpr &this_expr =
      static_cast<const clang::CXXThisExpr &>(stmt);

    typet this_type;
    if (get_type(this_expr.getType(), this_type, true))
      return true;
    get_this_expr(this_type, new_expr);
    break;
  }

  case clang::Stmt::CXXTemporaryObjectExprClass:
  {
    const clang::CXXTemporaryObjectExpr &cxxtoe =
      static_cast<const clang::CXXTemporaryObjectExpr &>(stmt);

    // get the constructor call making this temporary
    if (get_constructor_call(cxxtoe, new_expr))
      return true;

    break;
  }

  case clang::Stmt::SizeOfPackExprClass:
  {
    const clang::SizeOfPackExpr &size_of_pack =
      static_cast<const clang::SizeOfPackExpr &>(stmt);
    if (size_of_pack.isValueDependent())
    {
      std::ostringstream oss;
      llvm::raw_os_ostream ross(oss);
      ross << "Conversion of unsupported value-dependent size-of-pack expr: \"";
      ross << stmt.getStmtClassName() << "\" to expression"
           << "\n";
      stmt.dump(ross, *ASTContext);
      ross.flush();
      log_error("{}", oss.str());
      return true;
    }
    else
    {
      new_expr = constant_exprt(
        integer2binary(size_of_pack.getPackLength(), bv_width(size_type())),
        integer2string(size_of_pack.getPackLength()),
        size_type());
    }
    break;
  }

  case clang::Stmt::CXXTryStmtClass:
  {
    const clang::CXXTryStmt &cxxtry =
      static_cast<const clang::CXXTryStmt &>(stmt);

    new_expr = codet("cpp-catch");

    exprt try_body;
    if (get_expr(*cxxtry.getTryBlock(), try_body))
      return true;

    new_expr.location() = try_body.location();
    new_expr.move_to_operands(try_body);

    for (unsigned i = 0; i < cxxtry.getNumHandlers(); i++)
    {
      exprt handler;
      if (get_expr(*cxxtry.getHandler(i), handler))
        return true;

      new_expr.move_to_operands(handler);
    }

    break;
  }

  case clang::Stmt::CXXCatchStmtClass:
  {
    const clang::CXXCatchStmt &cxxcatch =
      static_cast<const clang::CXXCatchStmt &>(stmt);

    if (get_expr(*cxxcatch.getHandlerBlock(), new_expr))
      return true;

    exprt decl;
    if (cxxcatch.getExceptionDecl())
    {
      if (get_decl(*cxxcatch.getExceptionDecl(), decl))
        return true;

      if (get_type(*cxxcatch.getCaughtType(), new_expr.type(), true))
        return true;
    }
    else
    {
      // for catch(...), we don't need decl
      decl = code_skipt();
      new_expr.type().set("ellipsis", 1);
    }

    convert_expression_to_code(decl);
    codet::operandst &ops = new_expr.operands();
    ops.insert(ops.begin(), decl);

    break;
  }

  case clang::Stmt::CXXThrowExprClass:
  {
    const clang::CXXThrowExpr &cxxte =
      static_cast<const clang::CXXThrowExpr &>(stmt);

    new_expr = side_effect_exprt("cpp-throw", empty_typet());

    exprt tmp;
    if (cxxte.getSubExpr())
    {
      if (get_expr(*cxxte.getSubExpr(), tmp))
        return true;

      new_expr.move_to_operands(tmp);
      new_expr.type() = tmp.type();
    }

    break;
  }

  case clang::Stmt::CXXTypeidExprClass:
  {
    const clang::CXXTypeidExpr &cxxtid =
      static_cast<const clang::CXXTypeidExpr &>(stmt);

    // Type operands are mocked for now
    log_warning(
      "ESBMC does not support typeid expressions yet. Mocking them for now.");
    if (cxxtid.isTypeOperand())
    {
      typet type;
      if (get_type(cxxtid.getType(), type, true))
        return true;
      new_expr = gen_zero(ns.follow(type));
    }
    else
    {
      if (get_expr(*cxxtid.getExprOperand(), new_expr))
        return true;
    }

    break;
  }

  case clang::Stmt::CXXNoexceptExprClass:
  {
    const clang::CXXNoexceptExpr &cxxnoexcept =
      static_cast<const clang::CXXNoexceptExpr &>(stmt);

    bool value = cxxnoexcept.getValue();
    new_expr = gen_boolean(value);
    break;
  }

  case clang::Stmt::LambdaExprClass:
  {
    const clang::LambdaExpr &lambda_expr =
      static_cast<const clang::LambdaExpr &>(stmt);

    if (lambda_expr.capture_size() > 0)
    {
      log_error("ESBMC does not support lambdas with captures for now");
      return true;
    }

    get_struct_union_class(*lambda_expr.getLambdaClass(), true);

    // Construct a new object of the lambda class
    typet lambda_class_type;
    if (get_type(
          *lambda_expr.getLambdaClass()->getTypeForDecl(),
          lambda_class_type,
          true))
      return true;
    side_effect_exprt tmp_obj("temporary_object", lambda_class_type);
    new_expr = tmp_obj;
    // Generating a null pointer only works as long as we don't have captures
    // When we want to add support for captures, we have to probably call
    // the lambda class constructor here
    // See https://cppinsights.io/ which is a great tool to see how lambdas
    // are translated to C++ code

    break;
  }

  case clang::Stmt::CXXStdInitializerListExprClass:
  {
    const clang::CXXStdInitializerListExpr &initlist =
      static_cast<const clang::CXXStdInitializerListExpr &>(stmt);

    // Find the constructor that takes two args
    const clang::CXXConstructorDecl *target_ctor;
    for (const auto *ctor : initlist.getType()->getAsCXXRecordDecl()->ctors())
    {
      if (ctor->getNumParams() == 2)
      {
        // Process the constructor that takes two arguments
        target_ctor = ctor;
        break;
      }
    }

    typet type;
    if (get_type(initlist.getType(), type, true))
      return true;

    clang::Expr *one_expr = const_cast<clang::Expr *>(initlist.getSubExpr());
    auto array_size =
      ASTContext->getAsConstantArrayType(initlist.getSubExpr()->getType())
        ->getSize();
    auto array_size_expr = clang::IntegerLiteral::Create(
      *ASTContext,
      array_size,
      ASTContext->getSizeType(),
      initlist.getSubExpr()->getExprLoc());

    clang::Expr *one_expr_s[2] = {one_expr, array_size_expr};
    //    one_expr_s[0]= const_cast<clang::Expr *>(initlist.getSubExpr());
    //    one_expr_s[1]= const_cast<clang::Expr *>(initlist.getSubExpr());
    clang::ArrayRef<clang::Expr *> array = llvm::ArrayRef(one_expr_s, 2);
    typet full_type = migrate_type_back(migrate_type(type));
    exprt zero_expr = gen_zero(ns.follow(type));
    clang::InitListExpr init_list = clang::InitListExpr(
      *ASTContext, initlist.getBeginLoc(), array, initlist.getEndLoc());
    init_list.setType(initlist.getType());

    //    exprt lol;
    if (get_expr(init_list, new_expr))
      return true;
    //
    //    exprt initializer;
    //    if (get_expr(*initlist.getSubExpr(), initializer))
    //      return true;
    //
    //    new_expr = initializer;
    // TODO: won't work, because clang does not provide a ctor for us...
    // Instead have to directly initialize the fields, which could be annoying, due to data_objects, but get_field_ref should hopefully help :)
    clang::CXXConstructExpr *cxxc = clang::CXXConstructExpr::Create(
      *ASTContext,
      initlist.getType(),
      initlist.getSubExpr()->getExprLoc(),
      const_cast<clang::CXXConstructorDecl *>(target_ctor),
      false,
      array,
      false,
      false,
      false,
      false,
      clang::CXXConstructExpr::ConstructionKind::CK_Complete,
      initlist.getSubExpr()->getSourceRange());
    //    if (get_constructor_call(*cxxc, new_expr))
    //      return true;
    break;
    // TODO: Broken garbage potentially
  }

  case clang::Stmt::CXXInheritedCtorInitExprClass:
  {
    const clang::CXXInheritedCtorInitExpr &cxxinheritedctorinit =
      static_cast<const clang::CXXInheritedCtorInitExpr &>(stmt);

    if (get_constructor_call2(cxxinheritedctorinit, new_expr))
      return true;
    break;
  }

  case clang::Stmt::CXXForRangeStmtClass:
  {
    const clang::CXXForRangeStmt &cxxfor =
      static_cast<const clang::CXXForRangeStmt &>(stmt);

    codet decls("decl-block");
    const clang::Stmt *init_stmt = cxxfor.getInit();
    if (init_stmt)
    {
      exprt init;
      if (get_expr(*init_stmt, init))
        return true;

      decls.move_to_operands(init.op0());
    }

    exprt cond;
    if (get_expr(*cxxfor.getCond(), cond))
      return true;

    codet inc = code_skipt();
    const clang::Stmt *inc_stmt = cxxfor.getInc();
    if (inc_stmt)
      if (get_expr(*inc_stmt, inc))
        return true;
    convert_expression_to_code(inc);

    exprt range;
    if (get_expr(*cxxfor.getRangeStmt(), range))
      return true;

    exprt begin;
    if (get_expr(*cxxfor.getBeginStmt(), begin))
      return true;

    exprt end;
    if (get_expr(*cxxfor.getEndStmt(), end))
      return true;

    // The corresponding decls are taken from
    // decl-blocks and integrated in the one decl-block
    decls.move_to_operands(range.op0(), begin.op0(), end.op0());
    convert_expression_to_code(decls);

    codet body = code_skipt();
    const clang::Stmt *body_stmt = cxxfor.getBody();
    if (body_stmt)
      if (get_expr(*body_stmt, body))
        return true;

    const clang::Stmt *loop_var = cxxfor.getLoopVarStmt();
    exprt loop;
    if (loop_var)
      if (get_expr(*loop_var, loop))
        return true;

    codet::operandst &ops = body.operands();
    ops.insert(ops.begin(), loop);
    convert_expression_to_code(body);

    code_fort code_for;
    code_for.init() = decls;
    code_for.cond() = cond;
    code_for.iter() = inc;
    code_for.body() = body;

    new_expr = code_for;
    break;
  }

  default:
    if (clang_c_convertert::get_expr(stmt, new_expr))
      return true;
    break;
  }

  new_expr.location() = location;
  return false;
}

bool clang_cpp_convertert::get_constructor_call(
  const clang::CXXConstructExpr &constructor_call,
  exprt &new_expr)
{
  // Get constructor call
  exprt callee_decl;
  if (get_decl_ref(*constructor_call.getConstructor(), callee_decl))
    return true;

  // Get type
  typet type;
  if (get_type(constructor_call.getType(), type, true))
    return true;

  side_effect_expr_function_callt call;
  call.function() = callee_decl;
  call.type() = type;

  /* TODO: Investigate when it's possible to avoid the temporary object:

  // Try to get the object that this constructor is constructing
  auto parents = ASTContext->getParents(constructor_call);
  auto it = parents.begin();
  const clang::Decl *objectDecl = it->get<clang::Decl>();
  if (!objectDecl && need_new_object(it->get<clang::Stmt>(), constructor_call))
    [...]

   */

  // Calling base constructor from derived constructor
  bool need_new_obj = false;

  if (new_expr.base_ctor_derived())
    gen_typecast_base_ctor_call(callee_decl, call, new_expr);
  else if (new_expr.get_bool("#member_init"))
  {
    /*
    struct A {A(int _a){}}; struct B {A a; B(int _a) : a(_a) {}};
    In this case, use members to construct the object directly
    */
  }
  else if (new_expr.get_bool("#delegating_ctor"))
  {
    // get symbol for this
    symbolt *s = context.find_symbol(new_expr.get("#delegating_ctor_this"));
    const symbolt &this_symbol = *s;
    assert(s);
    exprt implicit_this_symb = symbol_expr(this_symbol);

    call.arguments().push_back(implicit_this_symb);
  }
  else
  {
    exprt this_object = exprt("new_object");
    this_object.set("#lvalue", true);
    this_object.type() = type;

    /* first parameter is address to the object to be constructed */
    address_of_exprt tmp_expr(this_object);
    call.arguments().push_back(tmp_expr);

    need_new_obj = true;
  }

  // Do args
  for (const clang::Expr *arg : constructor_call.arguments())
  {
    exprt single_arg;
    if (get_expr(*arg, single_arg))
      return true;

    call.arguments().push_back(single_arg);
  }

  // Init __is_complete based on whether we have call to a base object constructor or a
  // complete object constructor.
  // Basically: __is_complete = !#is_base_object;
  if (new_expr.get_bool("#is_base_object"))
  {
    call.arguments().push_back(gen_boolean(false));
  }
  else
  {
    call.arguments().push_back(gen_boolean(true));
  }

  call.set("constructor", 1);

  if (need_new_obj)
    make_temporary(call);

  new_expr.swap(call);

  return false;
}

bool clang_cpp_convertert::get_constructor_call2(
  const clang::CXXInheritedCtorInitExpr &cxxInheritedCtorInitExpr,
  exprt &new_expr)
{
  // Get constructor call
  exprt callee_decl;
  if (get_decl_ref(*cxxInheritedCtorInitExpr.getConstructor(), callee_decl))
    return true;

  // Get type
  typet type;
  if (get_type(cxxInheritedCtorInitExpr.getType(), type, true))
    return true;

  side_effect_expr_function_callt call;
  call.function() = callee_decl;
  call.type() = type;

  // TODO: Investigate when it's possible to avoid the temporary object in even more cases

  // Try to get the object that this constructor is constructing

  // Calling base constructor from derived constructor
  bool need_new_obj = false;

  if (new_expr.base_ctor_derived())
    gen_typecast_base_ctor_call(callee_decl, call, new_expr);
  else if (new_expr.get_bool("#member_init"))
  {
    /*
    struct A {A(int _a){}}; struct B {A a; B(int _a) : a(_a) {}};
    In this case, use members to construct the object directly
    */
  }
  else if (new_expr.get_bool("#delegating_ctor"))
  {
    // get symbol for this
    symbolt *s = context.find_symbol(new_expr.get("#delegating_ctor_this"));
    const symbolt &this_symbol = *s;
    assert(s);
    exprt implicit_this_symb = symbol_expr(this_symbol);

    call.arguments().push_back(implicit_this_symb);
  }
  else
  {
    exprt this_object = exprt("new_object");
    this_object.set("#lvalue", true);
    this_object.type() = type;

    /* first parameter is address to the object to be constructed */
    address_of_exprt tmp_expr(this_object);
    call.arguments().push_back(tmp_expr);

    need_new_obj = true;
  }

  auto parents = ASTContext->getParents(cxxInheritedCtorInitExpr);
  auto cxxcd = parents[0].get<clang::CXXConstructorDecl>();
  assert(cxxcd);

  // Do args
  for (clang::ParmVarDecl *arg : cxxcd->parameters())
  {
    exprt single_arg;
    if (get_decl_ref(*arg, single_arg))
      return true;

    call.arguments().push_back(single_arg);
  }

  // Init __is_complete based on whether we have call to a base object constructor or a
  // complete object constructor.
  call.arguments().push_back(gen_boolean(
    cxxInheritedCtorInitExpr.getConstructionKind() ==
    clang::CXXConstructExpr::ConstructionKind::CK_Complete));

  call.set("constructor", 1);

  if (need_new_obj)
    make_temporary(call);

  new_expr.swap(call);

  return false;
}

void clang_cpp_convertert::build_member_from_component(
  const clang::FunctionDecl &fd,
  exprt &component)
{
  // Add this pointer as first argument
  std::size_t address = reinterpret_cast<std::size_t>(fd.getFirstDecl());

  this_mapt::iterator it = this_map.find(address);
  if (this_map.find(address) == this_map.end())
  {
    log_error(
      "Pointer `this' for method {} was not added to scope",
      clang_c_convertert::get_decl_name(fd));
    abort();
  }

  member_exprt member(
    symbol_exprt(it->second.first, it->second.second),
    component.name(),
    component.type());

  component.swap(member);
}

bool clang_cpp_convertert::get_function_body(
  const clang::FunctionDecl &fd,
  exprt &new_expr,
  const code_typet &ftype)
{
  // do nothing if function body doesn't exist
  if (!fd.hasBody())
    return false;

  // Parse body
  if (clang_c_convertert::get_function_body(fd, new_expr, ftype))
    return true;

  if (new_expr.statement() != "block")
    return false;

  code_blockt &body = to_code_block(to_code(new_expr));

  // if it's a constructor, check for initializers
  if (fd.getKind() == clang::Decl::CXXConstructor)
  {
    const clang::CXXConstructorDecl &cxxcd =
      static_cast<const clang::CXXConstructorDecl &>(fd);

    // Parse the initializers, if any
    if (cxxcd.init_begin() != cxxcd.init_end())
    {
      log_debug(
        "c++",
        "Class {} ctor {} has {} initializers",
        cxxcd.getParent()->getNameAsString(),
        cxxcd.getNameAsString(),
        cxxcd.getNumCtorInitializers());

      // Resize the number of operands
      exprt::operandst initializers;
      initializers.reserve(cxxcd.getNumCtorInitializers());

      // `init` type is clang::CXXCtorInitializer
      for (auto init : cxxcd.inits())
      {
        exprt initializer;

        if (!init->isBaseInitializer())
        {
          // parsing non-static member initializer
          if (init->isAnyMemberInitializer())
          {
            exprt this_expr;
            get_this_expr(ftype.arguments().at(0).type(), this_expr);
            exprt lhs;
            lhs.set("#member_init", 1);
            if (init->isMemberInitializer())
            {
              if (get_field_ref(*init->getMember(), lhs, this_expr))
                return true;
            }
            else if (init->isIndirectMemberInitializer())
            {
              if (get_field_ref(*init->getIndirectMember(), lhs, this_expr))
                return true;
            }
            assert(lhs.is_member());
            //            #ifndef NDEBUG
            //                        assert(
            //                          has_suffix(
            //                            to_member_expr(lhs).struct_op().identifier(), "#this") ||
            //                          (to_member_expr(lhs).struct_op().is_member() &&
            //                           (has_suffix(
            //                              to_member_expr(to_member_expr(lhs).struct_op()).name(),
            //                              cpp_data_object::data_object_suffix) ||
            //                            ns.follow(to_member_expr(lhs).struct_op().type())
            //                              .get_bool("#is_c_like") ||
            //                            ns.follow(to_member_expr(lhs).struct_op().type()).is_union())));
            //            #endif

            // set #member_init flag again, as it has been cleared between the first call...
            lhs.set("#member_init", 1);

            exprt rhs;
            rhs.set("#member_init", 1);
            if (get_expr(*init->getInit(), rhs))
              return true;

            initializer = side_effect_exprt("assign", lhs.type());
            initializer.copy_to_operands(lhs, rhs);
          }
          else if (init->isDelegatingInitializer())
          {
            initializer.set(
              "#delegating_ctor_this",
              ftype.arguments().at(0).get("#identifier"));
            initializer.set("#delegating_ctor", 1);
            if (get_expr(*init->getInit(), initializer))
              return true;
          }
          else
          {
            log_error("Unsupported initializer in {}", __func__);
            abort();
          }
        }
        else
        {
          // Add additional annotation for `this` parameter
          initializer.derived_this_arg(
            ftype.arguments().at(0).get("#identifier"));
          initializer.base_ctor_derived(true);
          initializer.set("#is_base_object", true);
          if (get_expr(*init->getInit(), initializer))
            return true;
        }

        if (init->isBaseInitializer() && init->isBaseVirtual())
        {
          symbolt *s =
            context.find_symbol(ftype.arguments().back().get("#identifier"));
          const symbolt &is_complete_symbol = *s;
          assert(s);
          exprt is_complete_symbol_expr = symbol_expr(is_complete_symbol);

          /*
           * if(__is_complete)
           *   virtual-base-initializer
           * else
           *   skip
           */
          code_ifthenelset skip_ctor = code_ifthenelset();
          skip_ctor.cond() = is_complete_symbol_expr;
          convert_expression_to_code(initializer);
          skip_ctor.then_case() = to_code(initializer);
          skip_ctor.swap(initializer);
        }

        // Convert to code and insert side-effect in the operands list
        // Essentially we convert an initializer to assignment, e.g:
        // t1() : i(2){ }
        // is converted to
        // t1() { this->i = 2; }
        convert_expression_to_code(initializer);
        initializers.push_back(initializer);
      }

      // Insert at the beginning of the body
      body.operands().insert(
        body.operands().begin(), initializers.begin(), initializers.end());
    }
  }

  auto *type = fd.getType().getTypePtr();
  if (const auto *fpt = llvm::dyn_cast<const clang::FunctionProtoType>(type))
  {
    if (fpt->hasExceptionSpec())
    {
      codet decl = codet("throw_decl");
      if (fpt->hasDynamicExceptionSpec())
      {
        // e.g: void func() throw(int) { throw 1;}
        // body is converted to
        // {THROW_DECL(signed_int) throw 1; THROW_DECL_END}
        for (unsigned i = 0; i < fpt->getNumExceptions(); i++)
        {
          codet tmp;
          if (get_type(fpt->getExceptionType(i), tmp.type(), true))
            return true;

          decl.move_to_operands(tmp);
        }
      }
      else if (fpt->hasNoexceptExceptionSpec())
      {
        codet tmp;
        tmp.type() = typet("noexcept");
        decl.move_to_operands(tmp);
      }
      body.operands().insert(body.operands().begin(), decl);
    }
  }

  return false;
}

bool clang_cpp_convertert::get_function_this_pointer_param(
  const clang::CXXMethodDecl &cxxmd,
  code_typet::argumentst &params)
{
  // Parse this pointer
  code_typet::argumentt this_param;
  if (get_type(cxxmd.getThisType(), this_param.type(), true))
    return true;

  locationt location_begin;
  get_location_from_decl(cxxmd, location_begin);

  std::string id, name;
  get_decl_name(cxxmd, name, id);

  name = "this";
  id += name;

  //this_param.cmt_base_name("this");
  this_param.cmt_base_name(name);
  this_param.cmt_identifier(id);

  // Add 'this' as the first parameter to the list of params
  params.insert(params.begin(), this_param);

  // If the method is not defined, we don't need to add it's parameter
  // to the context, they will never be used
  if (!cxxmd.isDefined())
    return false;

  symbolt param_symbol;
  get_default_symbol(
    param_symbol,
    get_modulename_from_path(location_begin.file().as_string()),
    this_param.type(),
    name,
    id,
    location_begin);

  param_symbol.lvalue = true;
  param_symbol.is_parameter = true;
  param_symbol.file_local = true;

  // Save the method address and name of this pointer on the this pointer map
  std::size_t address = reinterpret_cast<std::size_t>(cxxmd.getFirstDecl());
  this_map[address] = std::pair<std::string, typet>(
    param_symbol.id.as_string(), this_param.type());

  context.move_symbol_to_context(param_symbol);
  return false;
}

bool clang_cpp_convertert::get_cxx_constructor_is_complete_param(
  const clang::CXXConstructorDecl &cxxcd,
  code_typet::argumentst &params)
{
  // Parse this pointer
  code_typet::argumentt is_complete_param;
  is_complete_param.type() = bool_typet();

  locationt location_begin;
  get_location_from_decl(cxxcd, location_begin);

  std::string id, name;
  get_decl_name(cxxcd, name, id);

  name = "__is_complete";
  id += name;

  is_complete_param.cmt_base_name(name);
  is_complete_param.cmt_identifier(id);

  // Add '__is_complete' as the last parameter to the list of params
  params.back() = is_complete_param;

  // If the method is not defined, we don't need to add it's parameter
  // to the context, they will never be used
  if (!cxxcd.isDefined())
    return false;

  symbolt param_symbol;
  get_default_symbol(
    param_symbol,
    get_modulename_from_path(location_begin.file().as_string()),
    is_complete_param.type(),
    name,
    id,
    location_begin);

  param_symbol.lvalue = true;
  param_symbol.is_parameter = true;
  param_symbol.file_local = true;

  context.move_symbol_to_context(param_symbol);
  return false;
}

bool clang_cpp_convertert::get_function_params(
  const clang::FunctionDecl &fd,
  code_typet::argumentst &params)
{
  // On C++, all methods have an implicit reference to the
  // class of the object
  const clang::CXXMethodDecl &cxxmd =
    static_cast<const clang::CXXMethodDecl &>(fd);

  // If it's a C-style function, fallback to C mode
  // Static methods don't have the this arg and can be handled as
  // C functions
  if (!fd.isCXXClassMember() || cxxmd.isStatic())
    return clang_c_convertert::get_function_params(fd, params);

  int num_additional_args = 1; // this pointer
  auto *cxxcd = llvm::dyn_cast<clang::CXXConstructorDecl>(&fd);
  if (cxxcd)
  {
    num_additional_args++; // __is_complete
  }

  // Add this pointer to first arg
  if (get_function_this_pointer_param(cxxmd, params))
    return true;

  // reserve space for `this' pointer, params and __is_complete
  params.resize(num_additional_args + fd.parameters().size());

  // TODO: replace the loop with get_function_params
  // Parse other args
  for (std::size_t i = 0; i < fd.parameters().size(); ++i)
  {
    const clang::ParmVarDecl &pd = *fd.parameters()[i];

    code_typet::argumentt param;
    if (get_function_param(pd, param))
      return true;

    // All args are added shifted by one position, because
    // of the this pointer (first arg)
    params[i + 1].swap(param);
  }

  // Check whether we need to add the __is_complete parameter
  // Done after the other parameters, because it should be the last one
  if (cxxcd)
  {
    if (get_cxx_constructor_is_complete_param(*cxxcd, params))
      return true;
  }

  return false;
}

void clang_cpp_convertert::name_param_and_continue(
  const clang::ParmVarDecl &pd,
  std::string &id,
  std::string &name,
  exprt &param)
{
  /*
   * A C++ function may contain unnamed function parameter(s).
   * The base method of clang_c_converter doesn't care about
   * unnamed function parameter. But we need to deal with it here
   * because the unnamed function parameter may be used in a function's body.
   * e.g. unnamed const ref in class' defaulted copy constructor
   *      implicitly added by the complier
   *
   * We need to:
   *  1. Name it (done in this function)
   *  2. fill the param
   *     (done as part of the clang_c_converter's get_function_param flow)
   *  3. add a symbol for it
   *     (done as part of the clang_c_converter's get_function_param flow)
   */
  assert(id.empty() && name.empty());

  const clang::DeclContext *dcxt = pd.getParentFunctionOrMethod();
  if (const auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(dcxt))
  {
    get_decl_name(*md, name, id);

    // name would be just `foo::index` and id would be "<foo_id>::index"
    name = name + "::" + std::to_string(pd.getFunctionScopeIndex());
    id = id + "::" + std::to_string(pd.getFunctionScopeIndex());

    // sync param name
    param.cmt_base_name(name);
    param.identifier(id);
    param.name(name);
  }
}

template <typename SpecializationDecl>
bool clang_cpp_convertert::get_template_decl_specialization(
  const SpecializationDecl *D,
  bool DumpExplicitInst,
  exprt &new_expr)
{
  for (auto const *redecl_with_bad_type : D->redecls())
  {
    auto *redecl = llvm::dyn_cast<SpecializationDecl>(redecl_with_bad_type);
    if (!redecl)
    {
      assert(
        llvm::isa<clang::CXXRecordDecl>(redecl_with_bad_type) &&
        "expected an injected-class-name");
      continue;
    }

    switch (redecl->getTemplateSpecializationKind())
    {
    case clang::TSK_ExplicitInstantiationDeclaration:
    case clang::TSK_ExplicitInstantiationDefinition:
    case clang::TSK_ExplicitSpecialization:
      if (!DumpExplicitInst)
        break;
      // Fall through.
    case clang::TSK_Undeclared:
    case clang::TSK_ImplicitInstantiation:
      if (get_decl(*redecl, new_expr))
        return true;
      break;
    }
  }

  return false;
}

bool clang_cpp_convertert::get_field_ref(
  const clang::ValueDecl &vd,
  exprt &new_expr,
  exprt &base)
{
  if (const auto *fd = llvm::dyn_cast<clang::FieldDecl>(&vd))
  {
    const auto *cxxrd = llvm::dyn_cast<clang::CXXRecordDecl>(fd->getParent());
    if (cxxrd)
    {
      // Everything else should be a value decl
      std::string field_name, field_id;
      get_decl_name(*fd, field_name, field_id);

      typet field_type;
      if (get_type(fd->getType(), field_type, true))
        return true;

      std::string class_name, class_id;
      get_decl_name(*cxxrd, class_name, class_id);

      typet class_type;
      if (get_type(*cxxrd->getTypeForDecl(), class_type, true))
      {
        return true;
      }

      exprt base_expr;
      if (!cxxrd->isCLike() && !cxxrd->isUnion())
      {
        typet data_object_type;
        cpp_data_object::get_data_object_symbol_type(
          class_id, data_object_type);
        assert(!class_type.is_pointer());
        exprt data_object_member = member_exprt(
          base,
          class_name + cpp_data_object::data_object_suffix,
          data_object_type);
        data_object_member.name(
          class_name + cpp_data_object::data_object_suffix);
        base_expr.swap(data_object_member);
      }
      else
      {
        base_expr.swap(base);
      }

      new_expr = member_exprt(base_expr, field_name, field_type);
      new_expr.identifier(field_id);
      new_expr.cmt_lvalue(true);
      new_expr.name(field_name);
      return false;
    }
    else
    {
      std::ostringstream oss;
      llvm::raw_os_ostream ross(oss);
      ross << "Conversion of unsupported non c++ clang field ref: \"";
      ross << vd.getDeclKindName() << "\" to expression"
           << "\n";
      vd.dump(ross);
      ross.flush();
      log_error("{}", oss.str());
      return true;
    }
  }
  else if (const auto *ifd = llvm::dyn_cast<clang::IndirectFieldDecl>(&vd))
  {
    std::string name, id;
    get_decl_name(*ifd, name, id);

    typet type;
    if (get_type(ifd->getType(), type, true))
      return true;

    // Indirect fields can be nested, so we need to follow the chain
    exprt ref_to_containing_field;
    for (auto chain_element : ifd->chain())
    {
      auto containing_field = clang::dyn_cast<clang::ValueDecl>(chain_element);
      assert(containing_field);

      if (get_field_ref(*containing_field, ref_to_containing_field, base))
        return true;
      base = ref_to_containing_field;
    }

    new_expr.swap(ref_to_containing_field);
    return false;
  }
  else
  {
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);
    ross << "Conversion of unsupported clang field ref: \"";
    ross << vd.getDeclKindName() << "\" to expression"
         << "\n";
    vd.dump(ross);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }
}

bool clang_cpp_convertert::get_decl_ref(
  const clang::Decl &decl,
  exprt &new_expr)
{
  std::string name, id;
  typet type;
  /**
   * References are modeled as pointers in ESBMC.
   * Normally, when getting a reference, we should therefore dereference it.
   * However, when a reference type variable is initialized in a (member initializer) constructor call,
   * we should not dereference it, because the reference/pointer is _not_ yet initialized.
   * See test case "RefMemberInit".
   */
  bool should_dereference = !new_expr.get_bool("#member_init");

  switch (decl.getKind())
  {
  case clang::Decl::ParmVar:
  {
    // first follow the base conversion flow to fill new_expr
    if (clang_c_convertert::get_decl_ref(decl, new_expr))
      return true;

    const auto *param = llvm::dyn_cast<const clang::ParmVarDecl>(&decl);
    assert(param);

    get_decl_name(*param, name, id);

    if (id.empty() && name.empty())
      name_param_and_continue(*param, id, name, new_expr);

    if (is_lvalue_or_rvalue_reference(new_expr.type()) && should_dereference)
    {
      new_expr = dereference_exprt(new_expr, new_expr.type());
      new_expr.set("#lvalue", true);
      new_expr.set("#implicit", true);
    }

    break;
  }
  case clang::Decl::CXXConstructor:
  {
    const clang::FunctionDecl &fd =
      static_cast<const clang::FunctionDecl &>(decl);

    get_decl_name(fd, name, id);

    if (get_type(fd.getType(), type, true))
      return true;

    code_typet &fd_type = to_code_type(type);
    if (get_function_params(fd, fd_type.arguments()))
      return true;

    // annotate return type - will be used to adjust the initiliazer or decl-derived stmt
    const auto *md = llvm::dyn_cast<clang::CXXMethodDecl>(&fd);
    assert(md);
    annotate_ctor_dtor_rtn_type(*md, fd_type.return_type());

    new_expr = exprt("symbol", type);
    new_expr.identifier(id);
    new_expr.cmt_lvalue(true);
    new_expr.name(name);

    break;
  }

  default:
  {
    if (clang_c_convertert::get_decl_ref(decl, new_expr))
      return true;

    if (is_lvalue_or_rvalue_reference(new_expr.type()) && should_dereference)
    {
      new_expr = dereference_exprt(new_expr, new_expr.type());
      new_expr.set("#lvalue", true);
      new_expr.set("#implicit", true);
    }

    break;
  }
  }

  return false;
}

bool clang_cpp_convertert::annotate_class_field(
  const clang::FieldDecl &field,
  const struct_union_typet &type,
  struct_typet::componentt &comp)
{
  // set parent in component's type
  if (type.tag().empty())
  {
    log_error("Goto empty tag in parent class type in {}", __func__);
    return true;
  }
  std::string parent_class_id = tag_prefix + type.tag().as_string();
  comp.type().set("#member_name", parent_class_id);

  // set access in component
  if (annotate_class_field_access(field, comp))
  {
    log_error("Failed to annotate class field access in {}", __func__);
    return true;
  }

  return false;
}

bool clang_cpp_convertert::annotate_class_field_access(
  const clang::FieldDecl &field,
  struct_typet::componentt &comp)
{
  std::string access;
  if (get_access_from_decl(field, access))
    return true;

  // annotate access in component
  comp.set_access(access);
  return false;
}

bool clang_cpp_convertert::get_access_from_decl(
  const clang::Decl &decl,
  std::string &access)
{
  switch (decl.getAccess())
  {
  case clang::AS_public:
  {
    access = "public";
    break;
  }
  case clang::AS_private:
  {
    access = "private";
    break;
  }
  case clang::AS_protected:
  {
    access = "protected";
    break;
  }
  default:
  {
    log_error("Unknown specifier returned from clang in {}", __func__);
    return true;
  }
  }

  return false;
}

bool clang_cpp_convertert::annotate_class_method(
  const clang::CXXMethodDecl &cxxmdd,
  exprt &new_expr)
{
  code_typet &component_type = to_code_type(new_expr.type());

  /*
   * The order of annotations matters.
   */
  // annotate parent
  std::string parent_class_name;
  std::string parent_class_id;
  get_decl_name(*cxxmdd.getParent(), parent_class_name, parent_class_id);
  component_type.set("#member_name", parent_class_id);

  // annotate ctor and dtor
  if (is_ConstructorOrDestructor(cxxmdd))
  {
    // annotate ctor and dtor return type
    annotate_ctor_dtor_rtn_type(cxxmdd, component_type.return_type());
    annotate_implicit_copy_move_ctor_union(
      cxxmdd, component_type.return_type());

    /*
     * We also have a `component` in class type representing the ctor/dtor.
     * Need to sync the type of this function symbol and its corresponding type
     * of the component inside the class' symbol
     * We just need "#member_name" and "return_type" fields to be synced for later use
     * in the adjuster.
     * So let's do the sync before adding more annotations.
     */
    symbolt *fd_symb = get_fd_symbol(cxxmdd);
    if (fd_symb)
    {
      fd_symb->type = component_type;
      /*
       * we indicate the need for vptr & vbotptr initializations in constructor.
       * vptr & vbotptr initializations will be added in the adjuster.
       */
      auto parent_class_type_symbol = context.find_symbol(parent_class_id);
      assert(parent_class_type_symbol);
      assert(parent_class_type_symbol->is_type);
      bool has_vptr_component =
        parent_class_type_symbol->type.get_bool("#has_vptr_component");
      bool has_vbot_ptr_component =
        parent_class_type_symbol->type.get_bool("#has_vbot_ptr_component");
      fd_symb->value.need_vptr_init(has_vptr_component);
      fd_symb->value.need_vbotptr_init(has_vbot_ptr_component);
    }
  }

  // annotate name
  std::string method_id, method_name;
  get_decl_name(cxxmdd, method_name, method_id);
  new_expr.name(method_id);

  // annotate access
  std::string access;
  if (get_access_from_decl(cxxmdd, access))
    return true;
  new_expr.set("access", access);

  // annotate base name
  new_expr.base_name(method_id);

  // annotate pretty name
  new_expr.pretty_name(method_name);

  // annotate inline
  new_expr.set("is_inlined", cxxmdd.isInlined());

  // annotate location
  locationt location_begin;
  get_location_from_decl(cxxmdd, location_begin);
  new_expr.location() = location_begin;

  // We need to add a non-static method as a `component` to class symbol's type
  // remove "statement: skip" otherwise it won't be added
  if (!cxxmdd.isStatic())
    if (to_code(new_expr).statement() == "skip")
      to_code(new_expr).remove("statement");

  return false;
}

void clang_cpp_convertert::gen_typecast_base_ctor_call(
  const exprt &callee_decl,
  side_effect_expr_function_callt &call,
  exprt &initializer)
{
  // sanity checks
  assert(initializer.derived_this_arg() != "");
  assert(initializer.base_ctor_derived());

  // get base ctor this type
  const code_typet &base_ctor_code_type = to_code_type(callee_decl.type());
  const code_typet::argumentst &base_ctor_arguments =
    base_ctor_code_type.arguments();
  // at least one argument representing `this` in base class ctor
  assert(base_ctor_arguments.size() >= 1);
  // Get the type of base `this` represented by the base class ctor
  const typet base_ctor_this_type = base_ctor_arguments.at(0).type();

  // get derived class ctor implicit this
  symbolt *s = context.find_symbol(initializer.derived_this_arg());
  const symbolt &this_symbol = *s;
  assert(s);
  exprt implicit_this_symb = symbol_expr(this_symbol);

  // Technically, the base class could be virtual and we'd have to do a virtual cast (using vbotptr), _but_
  // we only initialize virtual base classes in the complete object constructor.
  // This means that we know how the whole object look exactly, so we can
  // just use the static offset to the virtual base class.
  cpp_typecast::derived_to_base_typecast(
    implicit_this_symb, base_ctor_this_type, false, ns);
  call.arguments().push_back(implicit_this_symb);
}

bool clang_cpp_convertert::need_new_object(
  const clang::Stmt *parentStmt,
  const clang::CXXConstructExpr &call)
{
  /*
   * We only need to build the new_object if:
   *  1. we are dealing with new operator
   *  2. we are binding a temporary
   */
  if (parentStmt)
  {
    switch (parentStmt->getStmtClass())
    {
    case clang::Stmt::CXXNewExprClass:
    case clang::Stmt::CXXBindTemporaryExprClass:
      return true;
    default:
    {
      /*
       * A POD temporary is bound to a CXXBindTemporaryExprClass node.
       * But we still need to build a new_object in this case.
       */
      if (call.getStmtClass() == clang::Stmt::CXXTemporaryObjectExprClass)
        return true;
    }
    }
  }

  return false;
}

symbolt *clang_cpp_convertert::get_fd_symbol(const clang::FunctionDecl &fd)
{
  std::string id, name;
  get_decl_name(fd, name, id);
  // if not found, nullptr is returned
  return (context.find_symbol(id));
}

void clang_cpp_convertert::get_base_components_methods(
  const std::string &base_id,
  const std::string &base_name,
  struct_union_typet &struct_type,
  bool is_virtual_base,
  bool uses_data_object)
{
  // get base class symbol
  symbolt *s = context.find_symbol(base_id);
  assert(s);

  struct_typet &struct_data_object_type = cpp_data_object::get_data_object_type(
    tag_prefix + struct_type.tag().as_string(), context);

  const struct_typet &base_type = to_struct_type(s->type);

  // pull components in
  struct_typet::componentt base_object_component;
  if (uses_data_object)
  {
    typet base_object_symbol_type;
    cpp_data_object::get_data_object_symbol_type(
      base_id, base_object_symbol_type);

    base_object_component.name(base_name + cpp_data_object::data_object_suffix);
    base_object_component.pretty_name(
      base_id + cpp_data_object::data_object_suffix);
    base_object_component.type() = base_object_symbol_type;
  }
  else
  {
    base_object_component.set("from_base", true);
    base_object_component.set("#is_c_like", true);
    base_object_component.name(base_name);
    base_object_component.pretty_name(base_id);
    struct_typet base_type_copy = base_type;
    get_ref_to_struct_type(base_type_copy);
    base_object_component.type() = base_type_copy;
  }
  if (is_virtual_base)
  {
    base_object_component.set("from_virtual_base", true);
    struct_type.components().push_back(base_object_component);
  }
  else
  {
    struct_data_object_type.components().push_back(base_object_component);
  }

  // pull methods in
  const struct_typet::componentst &methods = base_type.methods();
  for (auto method : methods)
  {
    // TODO: tweak access specifier
    method.set("from_base", true);
    if (!is_duplicate_method(method, struct_type))
      to_struct_type(struct_type).methods().push_back(method);
  }
}

bool clang_cpp_convertert::is_duplicate_method(
  const struct_typet::componentt &method,
  const struct_union_typet &type)
{
  const struct_typet &stype = to_struct_type(type);
  const struct_typet::componentst &methods = stype.methods();
  for (const auto &existing_method : methods)
  {
    if (method.name() == existing_method.name())
      return true;
  }
  return false;
}

void clang_cpp_convertert::annotate_implicit_copy_move_ctor_union(
  const clang::CXXMethodDecl &cxxmdd,
  typet &ctor_return_type)
{
  if (
    is_copy_or_move_ctor(cxxmdd) && cxxmdd.isImplicit() &&
    cxxmdd.getParent()->isUnion())
    ctor_return_type.set("#implicit_union_copy_move_constructor", true);
}

bool clang_cpp_convertert::is_copy_or_move_ctor(const clang::DeclContext &dcxt)
{
  if (const auto *ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(&dcxt))
    return ctor->isCopyOrMoveConstructor();

  return false;
}

bool clang_cpp_convertert::is_defaulted_ctor(const clang::CXXMethodDecl &md)
{
  if (const auto *ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(&md))
    if (ctor->isDefaulted())
      return true;

  return false;
}

void clang_cpp_convertert::annotate_ctor_dtor_rtn_type(
  const clang::CXXMethodDecl &cxxmdd,
  typet &rtn_type)
{
  std::string mark_rtn = (cxxmdd.getKind() == clang::Decl::CXXDestructor)
                           ? "destructor"
                           : "constructor";
  typet tmp_rtn_type(mark_rtn);
  rtn_type = tmp_rtn_type;
}

bool clang_cpp_convertert::is_aggregate_type(const clang::QualType &q_type)
{
  const clang::Type &the_type = *q_type.getTypePtrOrNull();
  switch (the_type.getTypeClass())
  {
  case clang::Type::ConstantArray:
  case clang::Type::VariableArray:
  {
    const clang::ArrayType &aryType =
      static_cast<const clang::ArrayType &>(the_type);

    return aryType.isAggregateType();
  }
  case clang::Type::Elaborated:
  {
    const clang::ElaboratedType &et =
      static_cast<const clang::ElaboratedType &>(the_type);
    return (is_aggregate_type(et.getNamedType()));
  }
  case clang::Type::Record:
  {
    const clang::RecordDecl &rd =
      *(static_cast<const clang::RecordType &>(the_type)).getDecl();
    if (
      const clang::CXXRecordDecl *cxxrd =
        llvm::dyn_cast<clang::CXXRecordDecl>(&rd))
      return cxxrd->isPOD();

    return false;
  }
  default:
    return false;
  }

  return false;
}

bool clang_cpp_convertert::is_CopyOrMoveOperator(const clang::CXXMethodDecl &md)
{
  return md.isCopyAssignmentOperator() || md.isMoveAssignmentOperator();
}

bool clang_cpp_convertert::is_ConstructorOrDestructor(
  const clang::CXXMethodDecl &md)
{
  return md.getKind() == clang::Decl::CXXConstructor ||
         md.getKind() == clang::Decl::CXXDestructor;
}

void clang_cpp_convertert::make_temporary(exprt &expr)
{
  if (expr.statement() != "temporary_object")
  {
    // make the temporary
    side_effect_exprt tmp_obj("temporary_object", expr.type());
    codet code_expr("expression");
    code_expr.operands().push_back(expr);
    tmp_obj.initializer(code_expr);
    tmp_obj.location() = expr.location();
    expr.swap(tmp_obj);
  }
}
