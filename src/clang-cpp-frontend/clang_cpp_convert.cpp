// Remove warnings from Clang headers
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Wunused-parameter"
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
#pragma GCC diagnostic pop

#include <clang-cpp-frontend/clang_cpp_convert.h>
#include <util/expr_util.h>
#include <util/message.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <fmt/core.h>

clang_cpp_convertert::clang_cpp_convertert(
  contextt &_context,
  std::vector<std::unique_ptr<clang::ASTUnit>> &_ASTs,
  irep_idt _mode)
  : clang_c_convertert(_context, _ASTs, _mode), current_access("")
{
}

bool clang_cpp_convertert::get_decl(const clang::Decl &decl, exprt &new_expr)
{
  new_expr = code_skipt();

  switch(decl.getKind())
  {
  case clang::Decl::LinkageSpec:
  {
    const clang::LinkageSpecDecl &lsd =
      static_cast<const clang::LinkageSpecDecl &>(decl);

    for(auto decl : lsd.decls())
      if(get_decl(*decl, new_expr))
        return true;
    break;
  }

  case clang::Decl::CXXRecord:
  {
    const clang::CXXRecordDecl &cxxrd =
      static_cast<const clang::CXXRecordDecl &>(decl);

    if(get_struct_union_class(cxxrd))
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
    if(get_function(cxxmd, new_expr))
      return true;

    break;
  }

  case clang::Decl::Namespace:
  {
    const clang::NamespaceDecl &namesd =
      static_cast<const clang::NamespaceDecl &>(decl);

    for(auto decl : namesd.decls())
      if(get_decl(*decl, new_expr))
        return true;

    break;
  }

  case clang::Decl::FunctionTemplate:
  {
    const clang::FunctionTemplateDecl &fd =
      static_cast<const clang::FunctionTemplateDecl &>(decl);

    if(get_template_decl(&fd, true, new_expr))
      return true;
    break;
  }

  case clang::Decl::ClassTemplate:
  {
    const clang::ClassTemplateDecl &cd =
      static_cast<const clang::ClassTemplateDecl &>(decl);

    if(get_template_decl(&cd, false, new_expr))
      return true;
    break;
  }

  case clang::Decl::ClassTemplateSpecialization:
  {
    const clang::ClassTemplateSpecializationDecl &cd =
      static_cast<const clang::ClassTemplateSpecializationDecl &>(decl);

    if(get_struct_union_class(cd))
      return true;
    break;
  }

  case clang::Decl::Friend:
  {
    const clang::FriendDecl &fd = static_cast<const clang::FriendDecl &>(decl);

    if(fd.getFriendDecl() != nullptr)
      if(get_decl(*fd.getFriendDecl(), new_expr))
        return true;
    break;
  }

  case clang::Decl::AccessSpec:
  {
    const clang::AccessSpecDecl &asd =
      static_cast<const clang::AccessSpecDecl &>(decl);
    const clang::AccessSpecifier specifier = asd.getAccess();
    if(specifier == clang::AS_public)
      current_access = "public";
    else if(specifier == clang::AS_private)
      current_access = "private";
    else if(specifier == clang::AS_protected)
      current_access = "protected";
    else
    {
      log_error(
        "Unknown accessor specified returned from clang in {}", __func__);
      abort();
    }

    break;
  }

  // We can ignore any these declarations
  case clang::Decl::ClassTemplatePartialSpecialization:
  case clang::Decl::Using:
  case clang::Decl::UsingShadow:
  case clang::Decl::UsingDirective:
  case clang::Decl::TypeAlias:
  case clang::Decl::NamespaceAlias:
  case clang::Decl::UnresolvedUsingValue:
  case clang::Decl::UnresolvedUsingTypename:
    break;

  default:
    return clang_c_convertert::get_decl(decl, new_expr);
  }

  return false;
}

bool clang_cpp_convertert::get_type(
  const clang::QualType &q_type,
  typet &new_type)
{
  return clang_c_convertert::get_type(q_type, new_type);
}

bool clang_cpp_convertert::get_type(
  const clang::Type &the_type,
  typet &new_type)
{
  switch(the_type.getTypeClass())
  {
  case clang::Type::SubstTemplateTypeParm:
  {
    const clang::SubstTemplateTypeParmType &substmpltt =
      static_cast<const clang::SubstTemplateTypeParmType &>(the_type);

    if(get_type(substmpltt.getReplacementType(), new_type))
      return true;
    break;
  }

  case clang::Type::TemplateSpecialization:
  {
    const clang::TemplateSpecializationType &templSpect =
      static_cast<const clang::TemplateSpecializationType &>(the_type);

    if(get_type(templSpect.desugar(), new_type))
      return true;
    break;
  }

  case clang::Type::MemberPointer:
  {
    const clang::MemberPointerType &mpt =
      static_cast<const clang::MemberPointerType &>(the_type);

    typet sub_type;
    if(get_type(mpt.getPointeeType(), sub_type))
      return true;

    typet class_type;
    if(get_type(*mpt.getClass(), class_type))
      return true;

    new_type = gen_pointer_type(sub_type);
    new_type.set("to-member", class_type);
    break;
  }

  default:
    return clang_c_convertert::get_type(the_type, new_type);
  }

  return false;
}

bool clang_cpp_convertert::get_var(const clang::VarDecl &vd, exprt &new_expr)
{
  // Only convert instantiated variables
  if(vd.getDeclContext()->isDependentContext())
    return false;

  return clang_c_convertert::get_var(vd, new_expr);
}

bool clang_cpp_convertert::get_function(
  const clang::FunctionDecl &fd,
  exprt &new_expr)
{
  // Only convert instantiated functions/methods not depending on a template parameter
  if(fd.isDependentContext())
    return false;

  return clang_c_convertert::get_function(fd, new_expr);
}

bool clang_cpp_convertert::get_struct_union_class(const clang::RecordDecl &rd)
{
  // Only convert RecordDecl not depending on a template parameter
  if(rd.isDependentContext())
    return false;

  return clang_c_convertert::get_struct_union_class(rd);
}

bool clang_cpp_convertert::get_struct_union_class_fields(
  const clang::RecordDecl &rd,
  struct_union_typet &type)
{
  // If a struct is defined inside an extern C, it will be a RecordDecl
  if(auto cxxrd = llvm::dyn_cast<clang::CXXRecordDecl>(&rd))
  {
    // So this is a CXXRecordDecl, let's check for (virtual) base classes
    for(const auto &decl : cxxrd->bases())
    {
      // The base class is always a CXXRecordDecl
      const clang::CXXRecordDecl *base =
        decl.getType().getTypePtr()->getAsCXXRecordDecl();
      assert(base != nullptr);

      // First, parse the fields
      for(auto const *field : base->fields())
      {
        // We don't add if private
        if(field->getAccess() >= clang::AS_private)
          continue;

        struct_typet::componentt comp;
        if(get_decl(*field, comp))
          return true;

        // Don't add fields that have global storage (e.g., static)
        if(const clang::VarDecl *nd = llvm::dyn_cast<clang::VarDecl>(field))
          if(nd->hasGlobalStorage())
            continue;

        type.components().push_back(comp);
      }
    }
  }

  return clang_c_convertert::get_struct_union_class_fields(rd, type);
}

bool clang_cpp_convertert::get_struct_union_class_methods(
  const clang::RecordDecl &recordd,
  struct_union_typet &type)
{
  // If a struct is defined inside an extern C, it will be a RecordDecl
  const clang::CXXRecordDecl *cxxrd =
    llvm::dyn_cast<clang::CXXRecordDecl>(&recordd);
  if(cxxrd == nullptr)
    return false;

  // Parse bases
  for(const auto &decl : cxxrd->bases())
  {
    struct_typet base_t;
    if(get_type(decl.getType(), base_t))
      return true;

    // Add methods
    to_struct_type(type).methods().insert(
      to_struct_type(type).methods().begin(),
      base_t.methods().begin(),
      base_t.methods().end());
  }

  // vector of components. In class, a component refers to a field or methods.
  struct_typet::componentst &components = type.components();

  // Iterate over the declarators stored in this class
  for(const auto &decl : cxxrd->decls())
  {
    // Fields were already added in get_struct_union_class_fields
    if(decl->getKind() == clang::Decl::Field)
      continue;

    // Ignore implicit RecordDecl node, e.g.
    // CXXRecordDecl <address> <location> implicit referenced class <class-name>
    auto cxxrd = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
    if(cxxrd && decl->isImplicit())
      continue;

    struct_typet::componentt comp;

    if(
      const clang::FunctionTemplateDecl *ftd =
        llvm::dyn_cast<clang::FunctionTemplateDecl>(decl))
    {
      assert(ftd->isThisDeclarationADefinition());
      log_error("template is not supported in {}", __func__);
      abort();
    }
    else
    {
      if(get_decl(*decl, comp))
        return true;
      components.push_back(comp);
    }

    // This means that we probably just parsed nested class,
    // don't add it to the class
    if(comp.is_code() && to_code(comp).statement() == "skip")
      continue;

    if(
      const clang::CXXMethodDecl *cxxmd =
        llvm::dyn_cast<clang::CXXMethodDecl>(decl))
    {
      // Add only if it isn't static
      if(!cxxmd->isStatic())
      {
        log_error("static method is not supported in {}", __func__);
        abort();
      }
    }
  }

  return false;
}

bool clang_cpp_convertert::get_virtual_method(const symbolt &func_symb)
{
  printf("@@ Got virtual method function\n");
  // add virtual table(vtable) symbol
  std::string vt_id = "virtual_table::" + current_class_symbol->id.as_string();
  std::string vt_name =
    "virtual_table::" + current_class_symbol->name.as_string();
  locationt loc = current_class_symbol->location;

  symbolt *s = context.find_symbol(vt_name);

  if(s == nullptr)
  {
    // first time: create a virtual-table symbol type
    symbolt vt_symb;
    get_default_symbol(
      vt_symb,
      get_modulename_from_path(loc.file().as_string()),
      struct_typet(),
      vt_name,
      vt_id,
      loc);

    s = move_symbol_to_context(vt_symb);

    // TODO: add a virtual-table pointer?
  }
  else
  {
    log_error(
      "Got existing vtable_symbol - dump the symbol type. Should we continue?");
    abort();
  }

  assert(s->type.id() == "struct");

  struct_typet &virtual_table = to_struct_type(s->type);

  // add an entry to the virtual table
  struct_typet::componentt vt_entry;
  std::string virtual_name = func_symb.name.as_string() + "()";
  // is_virtual and virtual_name should have already been set
  assert(func_symb.type.get_bool("#is_virtual"));
  assert(func_symb.type.get("#virtual_name") != "");
  vt_entry.type() = pointer_typet(
    func_symb.type); // "component" in old frontend refers to func_symb.type
  vt_entry.set_name(vt_id + "::" + virtual_name);
  vt_entry.set("base_name", virtual_name);
  vt_entry.set("pretty_name", virtual_name);
  vt_entry.set("access", "public");
  vt_entry.location() = loc;
  virtual_table.components().push_back(
    vt_entry); // DEBUG: virtual_table::tag.Vehicle s->type gets populated here

  // TODO: take care of overloading?

  return false;
}

bool clang_cpp_convertert::get_expr(const clang::Stmt &stmt, exprt &new_expr)
{
  locationt location;
  get_start_location_from_stmt(stmt, location);

  switch(stmt.getStmtClass())
  {
  case clang::Stmt::CXXReinterpretCastExprClass:
  // TODO: ReinterpretCast should actually generate a bitcast
  case clang::Stmt::CXXFunctionalCastExprClass:
  case clang::Stmt::CXXStaticCastExprClass:
  case clang::Stmt::CXXConstCastExprClass:
  {
    const clang::CastExpr &cast = static_cast<const clang::CastExpr &>(stmt);

    if(get_cast_expr(cast, new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXDefaultArgExprClass:
  {
    const clang::CXXDefaultArgExpr &cxxdarg =
      static_cast<const clang::CXXDefaultArgExpr &>(stmt);

    if(get_expr(*cxxdarg.getExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXDynamicCastExprClass:
  {
    const clang::CXXDynamicCastExpr &cast =
      static_cast<const clang::CXXDynamicCastExpr &>(stmt);

    if(cast.isAlwaysNull())
    {
      typet t;
      if(get_type(cast.getType(), t))
        return true;

      new_expr = gen_zero(gen_pointer_type(t));
    }
    else if(get_cast_expr(cast, new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXBoolLiteralExprClass:
  {
    const clang::CXXBoolLiteralExpr &bool_literal =
      static_cast<const clang::CXXBoolLiteralExpr &>(stmt);

    if(bool_literal.getValue())
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
    if(get_expr(*callee, callee_expr))
      return true;

    typet type;
    if(get_type(member_call.getType(), type))
      return true;

    side_effect_expr_function_callt call;
    call.function() = callee_expr;
    call.type() = type;

    // Add implicit object call: a this pointer or an object
    exprt implicit_object;
    if(get_expr(*member_call.getImplicitObjectArgument(), implicit_object))
      return true;

    call.arguments().push_back(implicit_object);

    // Do args
    for(const clang::Expr *arg : member_call.arguments())
    {
      exprt single_arg;
      if(get_expr(*arg, single_arg))
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
    if(get_expr(*callee, callee_expr))
      return true;

    typet type;
    if(get_type(operator_call.getType(), type))
      return true;

    side_effect_expr_function_callt call;
    call.function() = callee_expr;
    call.type() = type;

    // Do args
    for(const clang::Expr *arg : operator_call.arguments())
    {
      exprt single_arg;
      if(get_expr(*arg, single_arg))
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

    if(get_expr(*ewc.getSubExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXBindTemporaryExprClass:
  {
    const clang::CXXBindTemporaryExpr &cxxbtmp =
      static_cast<const clang::CXXBindTemporaryExpr &>(stmt);

    if(get_expr(*cxxbtmp.getSubExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::SubstNonTypeTemplateParmExprClass:
  {
    const clang::SubstNonTypeTemplateParmExpr &substnttp =
      static_cast<const clang::SubstNonTypeTemplateParmExpr &>(stmt);

    if(get_expr(*substnttp.getReplacement(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::MaterializeTemporaryExprClass:
  {
    const clang::MaterializeTemporaryExpr &mtemp =
      static_cast<const clang::MaterializeTemporaryExpr &>(stmt);

    if(get_expr(*mtemp.getSubExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXNewExprClass:
  {
    const clang::CXXNewExpr &ne = static_cast<const clang::CXXNewExpr &>(stmt);

    typet t;
    if(get_type(ne.getType(), t))
      return true;

    if(ne.isArray())
    {
      new_expr = side_effect_exprt("cpp_new[]", t);

      // TODO: Implement support when the array size is empty
      assert(ne.getArraySize().hasValue());
      exprt size;
      if(get_expr(*(ne.getArraySize().getValue()), size))
        return true;

      new_expr.size(size);
    }
    else
    {
      new_expr = side_effect_exprt("cpp_new", t);
    }

    if(ne.hasInitializer())
    {
      exprt lhs("new_object", t);
      lhs.cmt_lvalue(true);

      exprt init;
      if(get_expr(*ne.getInitializer(), init))
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
    if(get_expr(*de.getArgument(), arg))
      return true;

    new_expr.move_to_operands(arg);

    if(de.getDestroyedType()->getAsCXXRecordDecl())
    {
      typet destt;
      if(get_type(de.getDestroyedType(), destt))
        return true;
      new_expr.type() = destt;
    }

    break;
  }

  case clang::Stmt::CXXPseudoDestructorExprClass:
  {
    new_expr = exprt("pseudo_destructor");
    break;
  }

  case clang::Stmt::CXXScalarValueInitExprClass:
  {
    const clang::CXXScalarValueInitExpr &cxxsvi =
      static_cast<const clang::CXXScalarValueInitExpr &>(stmt);

    typet t;
    if(get_type(cxxsvi.getType(), t))
      return true;

    new_expr = gen_zero(t);
    break;
  }

  case clang::Stmt::TypeTraitExprClass:
  {
    const clang::TypeTraitExpr &tt =
      static_cast<const clang::TypeTraitExpr &>(stmt);

    if(tt.getValue())
      new_expr = true_exprt();
    else
      new_expr = false_exprt();
    break;
  }

  case clang::Stmt::CXXConstructExprClass:
  {
    const clang::CXXConstructExpr &cxxc =
      static_cast<const clang::CXXConstructExpr &>(stmt);

    // Avoid materializing a temporary for an elidable copy/move constructor.
    if(cxxc.isElidable() && !cxxc.requiresZeroInitialization())
    {
      const clang::MaterializeTemporaryExpr *mt =
        llvm::dyn_cast<clang::MaterializeTemporaryExpr>(cxxc.getArg(0));

      if(mt != nullptr)
      {
        log_error("elidable copy/move is not supported in {}", __func__);
        abort();
      }
    }

    if(get_constructor_call(cxxc, new_expr))
      return true;

    break;
  }

  case clang::Stmt::CXXThisExprClass:
  {
    const clang::CXXThisExpr &this_expr =
      static_cast<const clang::CXXThisExpr &>(stmt);

    std::size_t address =
      reinterpret_cast<std::size_t>(current_functionDecl->getFirstDecl());

    this_mapt::iterator it = this_map.find(address);
    if(this_map.find(address) == this_map.end())
    {
      log_error(
        "Pointer `this' for method {} was not added to scope",
        clang_c_convertert::get_decl_name(*current_functionDecl));
      abort();
    }

    typet this_type;
    if(get_type(this_expr.getType(), this_type))
      return true;

    assert(this_type == it->second.second);

    new_expr = symbol_exprt(it->second.first, it->second.second);
    break;
  }

  default:
    return clang_c_convertert::get_expr(stmt, new_expr);
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
  if(get_decl_ref(*constructor_call.getConstructor(), callee_decl))
    return true;

  // Get type
  typet type;
  if(get_type(constructor_call.getType(), type))
    return true;

  side_effect_expr_function_callt call;
  call.function() = callee_decl;
  call.type() = type;

  // Try to get the object that this constructor is constructing
  auto it = ASTContext->getParents(constructor_call).begin();

  const clang::Decl *objectDecl = it->get<clang::Decl>();
  if(!objectDecl)
  {
    address_of_exprt tmp_expr;
    tmp_expr.type() = pointer_typet();
    tmp_expr.type().subtype() = type;

    exprt new_object("new_object");
    new_object.set("#lvalue", true);
    new_object.type() = type;

    tmp_expr.operands().resize(0);
    tmp_expr.move_to_operands(new_object);

    call.arguments().push_back(tmp_expr);
  }

  // Do args
  for(const clang::Expr *arg : constructor_call.arguments())
  {
    exprt single_arg;
    if(get_expr(*arg, single_arg))
      return true;

    call.arguments().push_back(single_arg);
  }

  call.set("constructor", 1);

  // We don't build a temporary obejct around it.
  // We follow the old cpp frontend to just add the ctor function call.
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
  if(this_map.find(address) == this_map.end())
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
  exprt &new_expr)
{
  // do nothing if function body doesn't exist
  if(!fd.hasBody())
    return false;

  // Parse body
  if(clang_c_convertert::get_function_body(fd, new_expr))
    return true;

  code_blockt &body = to_code_block(to_code(new_expr));

  // if it's a constructor, check for initializers
  if(fd.getKind() == clang::Decl::CXXConstructor)
  {
    const clang::CXXConstructorDecl &cxxcd =
      static_cast<const clang::CXXConstructorDecl &>(fd);

    // Parse the initializers, if any
    if(cxxcd.init_begin() != cxxcd.init_end())
    {
      // Resize the number of operands
      exprt::operandst initializers;
      initializers.reserve(cxxcd.getNumCtorInitializers());

      for(auto init : cxxcd.inits())
      {
        exprt initializer;

        if(!init->isBaseInitializer())
        {
          exprt lhs;
          if(init->isMemberInitializer())
          {
            // parsing non-static member initializer
            if(get_decl_ref(*init->getMember(), lhs))
              return true;
          }
          else
          {
            log_error("Unsupported initializer in {}", __func__);
            abort();
          }

          build_member_from_component(fd, lhs);

          exprt rhs;
          if(get_expr(*init->getInit(), rhs))
            return true;

          initializer = side_effect_exprt("assign", lhs.type());
          initializer.copy_to_operands(lhs, rhs);
        }
        else
        {
          if(get_expr(*init->getInit(), initializer))
            return true;
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

  return false;
}

bool clang_cpp_convertert::get_function_this_pointer_param(
  const clang::CXXMethodDecl &cxxmd,
  code_typet::argumentst &params)
{
  // Parse this pointer
  code_typet::argumentt this_param;
  if(get_type(cxxmd.getThisType(), this_param.type()))
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

  // Add to the list of params
  params.push_back(this_param);

  // If the method is not defined, we don't need to add it's parameter
  // to the context, they will never be used
  if(!cxxmd.isDefined())
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

  move_symbol_to_context(param_symbol);
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
  if(!fd.isCXXClassMember() || cxxmd.isStatic())
    return clang_c_convertert::get_function_params(fd, params);

  // Add this pointer to first arg
  if(get_function_this_pointer_param(cxxmd, params))
    return true;

  // reserve space for `this' pointer and params
  params.resize(1 + fd.parameters().size());

  // TODO: replace the loop with get_function_params
  // Parse other args
  for(std::size_t i = 0; i < fd.parameters().size(); ++i)
  {
    code_typet::argumentt param;
    if(get_function_param(*fd.parameters()[i], param))
      return true;

    // All args are added shifted by one position, because
    // of the this pointer (first arg)
    params[i + 1].swap(param);
  }

  return false;
}

template <typename SpecializationDecl>
bool clang_cpp_convertert::get_template_decl_specialization(
  const SpecializationDecl *D,
  bool DumpExplicitInst,
  bool,
  exprt &new_expr)
{
  for(auto *redecl_with_bad_type : D->redecls())
  {
    auto *redecl = llvm::dyn_cast<SpecializationDecl>(redecl_with_bad_type);
    if(!redecl)
    {
      assert(
        llvm::isa<clang::CXXRecordDecl>(redecl_with_bad_type) &&
        "expected an injected-class-name");
      continue;
    }

    switch(redecl->getTemplateSpecializationKind())
    {
    case clang::TSK_ExplicitInstantiationDeclaration:
    case clang::TSK_ExplicitInstantiationDefinition:
    case clang::TSK_ExplicitSpecialization:
      if(!DumpExplicitInst)
        break;
      // Fall through.
    case clang::TSK_Undeclared:
    case clang::TSK_ImplicitInstantiation:
      if(get_decl(*redecl, new_expr))
        return true;
      break;
    }
  }

  return false;
}

template <typename TemplateDecl>
bool clang_cpp_convertert::get_template_decl(
  const TemplateDecl *D,
  bool DumpExplicitInst,
  exprt &new_expr)
{
  for(auto *Child : D->specializations())
    if(get_template_decl_specialization(
         Child, DumpExplicitInst, !D->isCanonicalDecl(), new_expr))
      return true;

  return false;
}

bool clang_cpp_convertert::get_decl_ref(
  const clang::Decl &decl,
  exprt &new_expr)
{
  std::string name, id;
  typet type;

  switch(decl.getKind())
  {
  case clang::Decl::Var:
  case clang::Decl::Field:
  {
    return clang_c_convertert::get_decl_ref(decl, new_expr);
  }
  case clang::Decl::CXXConstructor:
  {
    const clang::FunctionDecl &fd =
      static_cast<const clang::FunctionDecl &>(decl);

    get_decl_name(fd, name, id);

    if(get_type(fd.getType(), type))
      return true;

    if(get_function_params(fd, to_code_type(type).arguments()))
      return true;

    break;
  }

  default:
  {
    // Cases not handled above are unknown clang decls; we print an warning.
    // It might be possible to support them either here or in clang_c_frontend::get_decl_ref()
    // depending on whether they are C++-specific or not.
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);
    decl.dump(ross);
    ross.flush();
    log_warning(
      "Conversion of unsupported clang decl ref for: {}\n{}",
      decl.getDeclKindName(),
      oss.str());
    return true;
  }
  }

  new_expr = exprt("symbol", type);
  new_expr.identifier(id);
  new_expr.cmt_lvalue(true);
  new_expr.name(name);

  return false;
}
