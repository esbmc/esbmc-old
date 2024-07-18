#include <util/compiler_defs.h>
// Remove warnings from Clang headers
CC_DIAGNOSTIC_PUSH()
CC_DIAGNOSTIC_IGNORE_LLVM_CHECKS()
#include <clang/AST/Attr.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h> /* clang::TypeTraitExpr */
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Version.inc>
#include <clang/Basic/Builtins.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/Support/raw_os_ostream.h>
CC_DIAGNOSTIC_POP()

#include <ac_config.h>
#include <clang-c-frontend/symbolic_types.h>
#include <clang-c-frontend/clang_c_convert.h>
#include <clang-c-frontend/typecast.h>
#include <util/arith_tools.h>
#include <util/bitvector.h>
#include <util/c_types.h>
#include <util/expr_util.h>
#include <util/i2string.h>
#include <util/message.h>
#include <util/mp_arith.h>
#include <util/std_code.h>
#include <util/std_expr.h>

clang_c_convertert::clang_c_convertert(
  contextt &_context,
  std::unique_ptr<clang::ASTUnit> &_AST,
  irep_idt _mode)
  : ASTContext(nullptr),
    context(_context),
    ns(context),
    AST(_AST),
    mode(_mode),
    anon_symbol("clang_c_convertert::"),
    current_scope_var_num(1),
    current_block(nullptr),
    sm(nullptr),
    current_functionDecl(nullptr)
{
}

bool clang_c_convertert::convert()
{
  if (convert_top_level_decl())
    return true;

  return false;
}

bool clang_c_convertert::convert_builtin_types()
{
  // Convert va_list_tag
  const clang::Decl *q_va_list_decl = ASTContext->getVaListTagDecl();
  if (q_va_list_decl)
  {
    exprt dummy;
    if (get_decl(*q_va_list_decl, dummy))
      return true;
  }

  // TODO: clang offers several informations from the target architecture,
  // such as primitive type's size, much like our configt class. We could
  // offer an option in the future to query them from the target system.
  // See clang/Basic/TargetInfo.h for what clang has to offer

  return false;
}

bool clang_c_convertert::convert_top_level_decl()
{
  if (AST)
  {
    ASTContext = &AST->getASTContext();

    // This is the whole translation unit. We don't represent it internally
    exprt dummy_decl;
    if (get_decl(*ASTContext->getTranslationUnitDecl(), dummy_decl))
      return true;
  }

  assert(current_functionDecl == nullptr);

  return false;
}

// This method convert declarations. They are called when those declarations
// are to be added to the context. If a variable or function is being called
// but then get_decl_ref is called instead
bool clang_c_convertert::get_decl(const clang::Decl &decl, exprt &new_expr)
{
  new_expr = code_skipt();

  switch (decl.getKind())
  {
  // Label declaration
  case clang::Decl::Label:
  {
    const clang::LabelDecl &ld = static_cast<const clang::LabelDecl &>(decl);

    exprt label("label", empty_typet());
    label.identifier(ld.getName().str());
    label.cmt_base_name(ld.getName().str());

    new_expr = label;
    break;
  }

  // Declaration of variables
  case clang::Decl::Var:
  case clang::Decl::VarTemplateSpecialization:
  {
    const clang::VarDecl &vd = static_cast<const clang::VarDecl &>(decl);
    return get_var(vd, new_expr);
  }

  // Declaration of function's parameter
  case clang::Decl::ParmVar:
  {
    const clang::ParmVarDecl &param =
      static_cast<const clang::ParmVarDecl &>(decl);
    return get_function_param(param, new_expr);
  }

  // Declaration of functions
  case clang::Decl::Function:
  {
    const clang::FunctionDecl &fd =
      static_cast<const clang::FunctionDecl &>(decl);

    // We can safely ignore any expr generated when parsing C code
    // In C++ mode, methods are initially parsed as function and
    // need to be added to the class scope
    exprt dummy;
    return get_function(fd, dummy);
  }

  // Field inside a struct/union
  case clang::Decl::Field:
  {
    const clang::FieldDecl &fd = static_cast<const clang::FieldDecl &>(decl);

    typet t;
    if (get_type(fd.getType(), t))
      return true;

    std::string id, name;
    get_decl_name(fd, name, id);

    struct_union_typet::componentt comp(id, name, t);
    if (fd.isBitField())
    {
      /* According to the C standard, the bitfield width shall be an integer
       * constant expression (C11 6.7.2.1/4), which the compiler can evaluate
       * (C11 6.6/2) */
      clang::Expr::EvalResult result;
      if (!fd.getBitWidth()->EvaluateAsInt(result, *ASTContext))
      {
        log_error("Clang could not calculate bitfield width");
        std::ostringstream oss;
        llvm::raw_os_ostream ross(oss);
        fd.getBitWidth()->dump(ross, *ASTContext);
        ross.flush();
        log_error("{}", oss.str());
        return true;
      }

      /* TODO: remove this recursive call. `width` is not used. However, there
       * are side-effects that cause re-ordering in the GOTO for pthread_lib.c
       * and that also negatively affect Boolector run times, see #764. These
       * should be investigated before removing it. */
      exprt width;
      if (get_expr(*fd.getBitWidth(), width))
        return true;

      comp.type().width(integer2string(result.Val.getInt().getSExtValue()));
      comp.type().set("#bitfield", true);
      comp.type().subtype() = t;
      comp.set_is_unnamed_bitfield(fd.isUnnamedBitfield());
    }

    // set location
    locationt location_begin;
    get_location_from_decl(fd, location_begin);
    comp.location() = location_begin;

    new_expr.swap(comp);
    break;
  }

  case clang::Decl::IndirectField:
  {
    const clang::IndirectFieldDecl &fd =
      static_cast<const clang::IndirectFieldDecl &>(decl);

    typet t;
    if (get_type(fd.getType(), t))
      return true;

    std::string id, name;
    get_decl_name(*fd.getAnonField(), name, id);

    struct_union_typet::componentt comp(id, name, t);
    if (fd.getAnonField()->isBitField())
    {
      exprt width;
      if (get_expr(*fd.getAnonField()->getBitWidth(), width))
        return true;

      comp.type().width(width.cformat());
      comp.type().set("#bitfield", true);
      comp.type().subtype() = t;
      comp.set_is_unnamed_bitfield(fd.getAnonField()->isUnnamedBitfield());
    }

    new_expr.swap(comp);
    break;
  }

  // A record is a struct/union/class/enum
  case clang::Decl::Record:
  {
    const clang::RecordDecl &record =
      static_cast<const clang::RecordDecl &>(decl);

    if (get_struct_union_class(record))
      return true;

    break;
  }

  case clang::Decl::TranslationUnit:
  {
    const clang::TranslationUnitDecl &tu =
      static_cast<const clang::TranslationUnitDecl &>(decl);

    for (auto const &decl : tu.decls())
    {
      // This is a global declaration (variable, function, struct, etc)
      // We don't need the exprt, it will be automatically added to the
      // context
      exprt dummy_decl;
      if (get_decl(*decl, dummy_decl))
        return true;
    }

    break;
  }

  // This is an empty declaration. An lost semicolon on the
  // code is an empty declaration
  case clang::Decl::Empty:

  // If this fails, clang will not generate the ASTs, we can
  // safely skip it
  case clang::Decl::StaticAssert:

  // Enum declaration and values, we can safely skip them as
  // any occurrence of those will be converted to int type (enum)
  // or integer value (enum constant)
  case clang::Decl::Enum:
  case clang::Decl::EnumConstant:

  // Typedef declaration, we can ignore this; clang will give us
  // the underlying type defined by the typedef, so we don't need
  // to add them to the context
  case clang::Decl::Typedef:
    break;

  case clang::Decl::BuiltinTemplate:
  {
    // expanded by clang itself
    const clang::BuiltinTemplateDecl &btd =
      static_cast<const clang::BuiltinTemplateDecl &>(decl);
    if (
      btd.getBuiltinTemplateKind() !=
      clang::BuiltinTemplateKind::BTK__make_integer_seq)
    {
      log_error(
        "Unsupported builtin template kind id: {}",
        (int)btd.getBuiltinTemplateKind());
      abort();
    }

    break;
  }
  default:
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);

    ross << "unrecognized / unimplemented clang declaration "
         << decl.getDeclKindName() << "\n";
    decl.dump(ross);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }

  return false;
}

bool clang_c_convertert::get_struct_union_class(const clang::RecordDecl &rd)
{
  if (rd.isInterface())
  {
    log_error("Interface is not supported");
    return true;
  }

  std::string id, name;
  get_decl_name(rd, name, id);

  locationt location_begin;
  get_location_from_decl(rd, location_begin);

  irep_idt c_tag = rd.isUnion() ? typet::t_union : typet::t_struct;

  // Check if the symbol is already added to the context, do nothing if it is
  // already in the context.
  symbolt *sym = context.find_symbol(id);
  if (!sym)
  {
    /* First put a symbol with a incomplete type into the context, then resolve
     * all subtypes and finally set this symbol's correctly resolved type. */
    struct_union_typet t("incomplete_" + c_tag.as_string());
    t.location() = location_begin;
    t.incomplete(true); /* for now just a declaration */
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

    // We have to add the struct/union/class to the context before converting its
    // fields because there might be recursive struct/union/class (pointers) and
    // the code at get_type, case clang::Type::Record, needs to find the correct
    // type (itself). Note that the type is incomplete at this stage, it doesn't
    // contain the fields, which are added to the symbol later on this method.

    sym = context.move_symbol_to_context(symbol);
  }

  assert(sym->is_type);

  // TODO: Fix me when we have a test case using C++ union.
  //       A C++ union can have member functions but not virtual functions.
  //       Just use struct_typet for C++?

  /* Don't continue to parse if it doesn't have a complete definition, yet.
   * This can happen in two cases:
   * a) there is no complete type definition in the translation unit, or
   * b) the type is being referred to under a pointer inside another type
   *    definition and up to this definition has not been defined, yet.
   */
  if (!rd.isCompleteDefinition())
    return false;

  /* Don't continue if it's not incomplete; use the .incomplete() flag to avoid
   * infinite recursion if the type we're defining refers to itself
   * (via pointers): it either is already being defined (up the stack somewhere)
   * or it's already a complete struct or union in the context. */
  if (!sym->type.incomplete())
    return false;
  sym->type.remove(irept::a_incomplete);

  clang::RecordDecl *rd_def = rd.getDefinition();
  assert(rd_def);

  /* it has a definition, now build the complete type */
  struct_union_typet t(c_tag);
  t.tag(name);

  /* update location with that of the type's definition */
  get_location_from_decl(*rd_def, t.location());

  // We have to add fields before methods as the fields are likely to be used
  // in the methods
  if (get_struct_union_class_fields(*rd_def, t))
    return true;

  // Check for packed and aligned attributes
  if (rd_def->hasAttrs())
  {
    const auto &attrs = rd_def->getAttrs();
    for (const auto &attr : attrs)
    {
      if (attr->getKind() == clang::attr::Packed)
        t.set("packed", true);

      if (attr->getKind() == clang::attr::Aligned)
      {
        const clang::AlignedAttr &aattr =
          static_cast<const clang::AlignedAttr &>(*attr);

        exprt alignment;
        if (get_expr(*(aattr.getAlignmentExpr()), alignment))
          return true;

        t.set("alignment", alignment);
      }
    }
  }

  /* We successfully constructed the type of this symbol; replace the
   * symbol with the incomplete type by one with the now-complete type
   * definition.
   * Do this by erasing and re-inserting because the order of definitions in the
   * context matters. This type should be defined after any of the types that it
   * is composed of. */
  symbolt symbol = *sym;
  context.erase_symbol(symbol.id);
  symbol.type = t;
  sym = context.move_symbol_to_context(symbol);

  if (get_struct_union_class_methods_decls(*rd_def, sym->type))
    return true;

  return false;
}

bool clang_c_convertert::get_struct_union_class_fields(
  const clang::RecordDecl &recordd,
  struct_union_typet &type)
{
  // First, parse the fields
  for (auto const *field : recordd.fields())
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

    type.components().push_back(comp);
  }

  return false;
}

bool clang_c_convertert::get_struct_union_class_methods_decls(
  const clang::RecordDecl &,
  typet &)
{
  // We don't add methods or static members to the struct in C
  return false;
}

bool clang_c_convertert::get_var(const clang::VarDecl &vd, exprt &new_expr)
{
  // Get type
  typet t;
  if (get_type(vd.getType(), t))
    return true;

  // Check if we annotated it to have an infinity size
  bool no_slice = false;
  if (vd.hasAttrs())
  {
    for (auto const &attr : vd.getAttrs())
    {
      if (const auto *a = llvm::dyn_cast<clang::AnnotateAttr>(attr))
      {
        const std::string &name = a->getAnnotation().str();
        if (name == "__ESBMC_inf_size")
        {
          assert(t.is_array());
          t.size(exprt("infinity", size_type()));
        }
        else if (name == "__ESBMC_no_slice")
          no_slice = true;
      }
      else if (attr->getKind() == clang::attr::Aligned)
      {
        const clang::AlignedAttr &aattr =
          static_cast<const clang::AlignedAttr &>(*attr);

        exprt alignment;
        if (get_expr(*(aattr.getAlignmentExpr()), alignment))
          return true;

        t.set("alignment", alignment);
      }
      else
        continue;
    }
  }

  // Get id and name
  std::string id, name;
  get_decl_name(vd, name, id);

  if (no_slice)
    config.no_slice_names.emplace(id);

  locationt location_begin;
  get_location_from_decl(vd, location_begin);

  symbolt symbol;
  get_default_symbol(
    symbol,
    get_modulename_from_path(location_begin.file().as_string()),
    t,
    name,
    id,
    location_begin);

  symbol.lvalue = true;
  symbol.static_lifetime =
    (vd.getStorageClass() == clang::SC_Static) || vd.hasGlobalStorage();
  symbol.is_extern = vd.hasExternalStorage();
  symbol.file_local = (vd.getStorageClass() == clang::SC_Static) ||
                      (!vd.isExternallyVisible() && !vd.hasGlobalStorage());

  if (
    symbol.static_lifetime && !symbol.is_extern &&
    (!vd.hasInit() || is_aggregate_type(vd.getType())))
  {
    // the type might contains symbolic types,
    // replace them with complete types before generating zero initialization

    // Initialize with zero value, if the symbol has initial value,
    // it will be added later on in this method
    symbol.value = gen_zero(get_complete_type(t, ns), true);
    symbol.value.zero_initializer(true);
  }

  symbolt *added_symbol = nullptr;
  if (symbol.static_lifetime && vd.hasInit())
  {
    /* Static symbols can't refer to themselves in the initializer (which the
     * 'else' case handles) as it would not be constant then.
     *
     * We need to get the initializer first, since it can contain compound
     * literals (with their own initialization) that this variable here is
     * initialized to:
     *
     * int x = (int){5};
     *
     * This creates a new symbol for the compound literal, and x's initializer
     * should point to that (already initialized) symbol. As
     * static_lifetime_init() expects the symbols to be initialized in the order
     * they are put into the context, by getting the RHS first, we avoid first
     * initializing 'x' and then the compound literal symbol, which would be
     * wrong.
     */

    /* Since this is in static storage context, pretend that any surrounding
     * block does not exist in order to force declarations by the RHS to appear
     * in file scope as well. Technically, this is not fully correct, as the
     * initialization of x in
     *
     * void f() {
     *   static int x = 42;
     * }
     *
     * should only occur the first time f() is run, but for C this makes no
     * difference as x cannot be accessed outside of f() anyway and the
     * initializer can't have side-effects (it's a constant expression). */
    code_blockt *orig = current_block;
    current_block = nullptr;
    const clang::Stmt *stmt = vd.getInit();

    exprt val;
    bool r = get_expr(*stmt, val);
    current_block = orig;
    if (r)
      return true;

    bool aggregate_without_init =
      is_aggregate_type(vd.getType()) &&
      stmt->getStmtClass() == clang::Stmt::CXXConstructExprClass;

    added_symbol = context.move_symbol_to_context(symbol);
    gen_typecast(ns, val, t);
    if (!aggregate_without_init)
      added_symbol->value = val;

    code_declt decl(symbol_expr(*added_symbol));
    decl.location() = location_begin;
    if (!aggregate_without_init)
      decl.operands().push_back(val);

    new_expr = decl;
  }
  else
  {
    // We have to add the symbol before converting the initial assignment
    // because we might have something like 'int x = x + 1;' which is
    // completely wrong but allowed by the language
    added_symbol = context.move_symbol_to_context(symbol);

    code_declt decl(symbol_expr(*added_symbol));
    decl.location() = location_begin;

    if (vd.hasInit() && !vd.isExceptionVariable())
    {
      exprt val;
      if (get_expr(*vd.getInit(), val))
        return true;

      gen_typecast(ns, val, t);

      added_symbol->value = val;
      decl.operands().push_back(val);
    }

    new_expr = decl;
  }
  return false;
}

bool clang_c_convertert::get_function(
  const clang::FunctionDecl &fd,
  exprt &new_expr)
{
  // If the function is not defined but this is not the definition, skip it
  if (fd.isDefined() && !fd.isThisDeclarationADefinition())
  {
    // Continue for virtual method as we need its type to make virtual function table
    if (!is_fd_virtual_or_overriding(fd))
      return false;
  }

  // Save old_functionDecl, to be restored at the end of this method
  const clang::FunctionDecl *old_functionDecl = current_functionDecl;
  current_functionDecl = &fd;

  // TODO: use fd.isMain to flag and check the flag on clang_c_adjust_expr
  // to saner way to add argc/argv/envp

  // Set initial variable name, it will be used for variables' name
  // This will be reset every time a function is parsed
  current_scope_var_num = 1;

  // Build function's type
  code_typet type;

  // Return type
  if (get_type(fd.getReturnType(), type.return_type()))
    return true;

  if (fd.isVariadic())
    type.make_ellipsis();

  if (fd.isInlined())
    type.inlined(true);

  locationt location_begin;
  get_location_from_decl(fd, location_begin);

  std::string id, name;
  get_decl_name(fd, name, id);

  symbolt symbol;
  get_default_symbol(
    symbol,
    get_modulename_from_path(location_begin.file().as_string()),
    type,
    name,
    id,
    location_begin);

  symbol.lvalue = true;
  symbol.is_extern = fd.getStorageClass() == clang::SC_Extern ||
                     fd.getStorageClass() == clang::SC_PrivateExtern;
  symbol.file_local = (fd.getStorageClass() == clang::SC_Static);

  symbolt &added_symbol = *context.move_symbol_to_context(symbol);

  // We convert the parameters first so their symbol are added to context
  // before converting the body, as they may appear on the function body
  if (get_function_params(fd, type.arguments()))
    return true;

  added_symbol.type = type;
  new_expr.type() = type;

  // We need: a type, a name, and an optional body
  if (fd.hasBody())
  {
    if (get_function_body(fd, added_symbol.value, type))
      return true;
  }

  // Restore old functionDecl
  current_functionDecl = old_functionDecl;

  return false;
}

bool clang_c_convertert::get_function_body(
  const clang::FunctionDecl &fd,
  exprt &new_expr,
  const code_typet &)
{
  assert(fd.hasBody());

  exprt body_exprt;
  if (get_expr(*fd.getBody(), body_exprt))
    return true; // return true if failing to parse function body

  new_expr = body_exprt;
  return false;
}

bool clang_c_convertert::get_function_params(
  const clang::FunctionDecl &fd,
  code_typet::argumentst &params)
{
  if (!fd.parameters().size()) // return if no parameter
    return false;

  for (auto const &pdecl : fd.parameters())
  {
    code_typet::argumentt param;
    if (get_function_param(*pdecl, param))
      return true; // return true if failing to parse a parameter

    params.push_back(param);
  }

  return false;
}

bool clang_c_convertert::get_function_param(
  const clang::ParmVarDecl &pd,
  exprt &param)
{
  typet param_type;
  if (get_type(pd.getOriginalType(), param_type))
    return true;

  if (param_type.is_array())
  {
    param_type.id("pointer");
    param_type.remove("size");
    param_type.remove("#constant");
  }
  else if (param_type.is_code())
  {
    param_type = pointer_typet(param_type);
  }

  std::string id, name;
  get_decl_name(pd, name, id);

  param = code_typet::argumentt();
  param.type() = param_type;
  param.cmt_base_name(name);

  if (id.empty() && name.empty())
    name_param_and_continue(pd, id, name, param);

  locationt location_begin;
  get_location_from_decl(pd, location_begin);

  param.cmt_identifier(id);
  param.location() = location_begin;

  // TODO: we can remove the following code once irep1 is dead, there
  // is no need to add the function argument to the symbol table,
  // as nothing relies on it. However, if we remove this now, the migrate
  // code will wrongly assume the symbol to be level1, as it generates
  // level0 symbol only if they are already on the context
  symbolt param_symbol;
  get_default_symbol(
    param_symbol,
    get_modulename_from_path(location_begin.file().as_string()),
    param_type,
    name,
    id,
    location_begin);

  param_symbol.lvalue = true;
  param_symbol.is_parameter = true;
  param_symbol.file_local = true;

  const clang::FunctionDecl &fd =
    static_cast<const clang::FunctionDecl &>(*pd.getParentFunctionOrMethod());

  // If the function is not defined, we don't need to add its parameter
  // to the context, they will never be used
  if (!fd.isDefined())
    return false;

  context.move_symbol_to_context(param_symbol);
  return false;
}

void clang_c_convertert::name_param_and_continue(
  const clang::ParmVarDecl &,
  std::string &,
  std::string &,
  exprt &)
{
  /*
   * If the name is empty, this is an function definition that we don't
   * need to worry about as the function param's name will be defined
   * when the function is defined, the exprt is filled for the sake of
   * beautification
   */
}

bool clang_c_convertert::get_type(
  const clang::QualType &q_type,
  typet &new_type)
{
  const clang::Type *the_type = q_type.getTypePtrOrNull();
  assert(the_type);
  if (get_type(*the_type, new_type))
    return true;

  if (q_type.isConstQualified())
    new_type.cmt_constant(true);

  if (q_type.isVolatileQualified())
    new_type.cmt_volatile(true);

  if (q_type.isRestrictQualified())
    new_type.restricted(true);

#ifdef ESBMC_CHERI_CLANG
  if (the_type->canCarryProvenance(*ASTContext))
    new_type.can_carry_provenance(true);
#endif

  return false;
}

bool clang_c_convertert::get_type(const clang::Type &the_type, typet &new_type)
{
  switch (the_type.getTypeClass())
  {
  // Builtin types like integer
  case clang::Type::Builtin:
  {
    const clang::BuiltinType &bt =
      static_cast<const clang::BuiltinType &>(the_type);

    if (get_builtin_type(bt, new_type))
      return true;

    break;
  }

  // Types using parenthesis, e.g. int (a);
  case clang::Type::Paren:
  {
    const clang::ParenType &pt =
      static_cast<const clang::ParenType &>(the_type);

    if (get_type(pt.getInnerType(), new_type))
      return true;

    break;
  }

  // Pointer types
  case clang::Type::Pointer:
  {
    const clang::PointerType &pt =
      static_cast<const clang::PointerType &>(the_type);
    const clang::QualType &pointee = pt.getPointeeType();

    typet sub_type;
    if (get_type(pointee, sub_type))
      return true;

    // Special case, pointers to structs/unions/classes must not
    // have a copy of it, but a reference to the type
    get_ref_to_struct_type(sub_type);

#if 0
    // true for pointers that are implemented as CHERI capabilities
    // and _Atomic with capability pointers as the underlying type.
    bool is_cap = pt.isCapabilityPointerType();

    // Whether this type can hold tagged capability values.
    // This is true for capability types that have not been annotated with
    // attr::CHERINoProvenance.
    // In hybrid mode this also returns true for pointer types since they can
    // be converted to capabilities.
    bool can_prov = pt.canCarryProvenance(*ASTContext);

    // true if this type is a CHERI capability type.
    // If \p IncludeIntCap
    // is true this also includes __uintcap_t and __intcap_t, otherwise it will
    // return false for these types. This is useful for cases such as checking
    // the validity of casts where __uintcap_t is not handled the same way as
    // pointers.
    bool IncludeIntCap = true;
    bool is_cheri = pt.isCHERICapabilityType(*ASTContext, IncludeIntCap);
#endif

    new_type = gen_pointer_type(sub_type);
    break;
  }

  // Types adjusted by the semantic engine
  case clang::Type::Decayed:
  {
    const clang::DecayedType &pt =
      static_cast<const clang::DecayedType &>(the_type);

    if (get_type(pt.getDecayedType(), new_type))
      return true;

    break;
  }

  // Array with constant size, e.g., int a[3];
  case clang::Type::ConstantArray:
  {
    const clang::ConstantArrayType &arr =
      static_cast<const clang::ConstantArrayType &>(the_type);

    llvm::APInt val = arr.getSize();
    if (val.getBitWidth() > 64)
    {
      log_error(
        "ESBMC currently does not support integers bigger "
        "than 64 bits");
      return true;
    }

    typet the_type;
    if (get_type(arr.getElementType(), the_type))
      return true;

    new_type = array_typet(
      the_type,
      constant_exprt(
        integer2binary(val.getSExtValue(), bv_width(size_type())),
        integer2string(val.getSExtValue()),
        size_type()));
    break;
  }

  // Array with undefined type, as in function args
  case clang::Type::IncompleteArray:
  {
    const clang::IncompleteArrayType &arr =
      static_cast<const clang::IncompleteArrayType &>(the_type);

    typet sub_type;
    if (get_type(arr.getElementType(), sub_type))
      return true;

    new_type = array_typet(sub_type, gen_one(size_type()));
    break;
  }

  // Array with variable size, e.g., int a[n];
  case clang::Type::VariableArray:
  {
    const clang::VariableArrayType &arr =
      static_cast<const clang::VariableArrayType &>(the_type);

    // If the size expression is null, we assume empty
    if (auto const *s = arr.getSizeExpr())
    {
      exprt size_expr;
      if (get_expr(*s, size_expr))
        return true;

      typet subtype;
      if (get_type(arr.getElementType(), subtype))
        return true;

      new_type = array_typet(subtype, size_expr);
    }
    else
      new_type = empty_typet();
    break;
  }

  // Those two here appears when we make a function call, e.g:
  // FunctionNoProto: int x = fun()
  // FunctionProto: int x = fun(a, b)
  case clang::Type::FunctionProto:
  {
    const clang::FunctionProtoType &func =
      static_cast<const clang::FunctionProtoType &>(the_type);

    code_typet type;

    // Return type
    const clang::QualType ret_type = func.getReturnType();

    typet return_type;
    if (get_type(ret_type, return_type))
      return true;

    type.return_type() = return_type;

    for (auto const &ptype : func.getParamTypes())
    {
      typet param_type;
      if (get_type(ptype, param_type))
        return true;

      type.arguments().emplace_back(param_type);
    }

    if (func.isVariadic())
      type.make_ellipsis();

    new_type = type;
    break;
  }

  case clang::Type::FunctionNoProto:
  {
    const clang::FunctionNoProtoType &func =
      static_cast<const clang::FunctionNoProtoType &>(the_type);

    code_typet type;

    // Return type
    const clang::QualType ret_type = func.getReturnType();

    typet return_type;
    if (get_type(ret_type, return_type))
      return true;

    type.return_type() = return_type;

    new_type = type;
    break;
  }

  // Typedef type definition
  case clang::Type::Typedef:
  {
    const clang::TypedefType &pt =
      static_cast<const clang::TypedefType &>(the_type);

    clang::QualType q_typedef_type =
      pt.getDecl()->getUnderlyingType().getCanonicalType();

    if (get_type(q_typedef_type, new_type))
      return true;

    break;
  }

  case clang::Type::Record:
  {
    const clang::RecordDecl &rd =
      *(static_cast<const clang::RecordType &>(the_type)).getDecl();

    std::string id, name;
    get_decl_name(rd, name, id);
    symbolt *s = context.find_symbol(id);
    if (!s)
    {
      /* record in context if not already there */
      if (get_struct_union_class(rd))
        return true;
    }

    /* symbolic type referring to that type */
    new_type = symbol_typet(id);

    break;
  }

  case clang::Type::Enum:
  {
    const clang::EnumType &ent = static_cast<const clang::EnumType &>(the_type);

    clang::QualType q_type = ent.getDecl()->getIntegerType();

    /* The q_type is nil when the enum is just declared but not defined in the
     * translation unit. That case should only happen under a pointer like
     *
     *   enum E (*f)()
     *
     * which should never be dereferencable. Hence, this type won't be used.
     * Hopefully. As it's not standard C, and because this looks fragile, let's
     * print a warning.
     */
    if (!q_type.getTypePtrOrNull())
    {
      log_warning(
        "No definition attached to enum declaration, this is not standard C. "
        "Upstream issue <https://github.com/esbmc/esbmc/issues/1794> tracks "
        "this.");
      new_type = enum_type();
    }
    else if (get_type(q_type, new_type))
      return true;

    break;
  }

  case clang::Type::Elaborated:
  {
    const clang::ElaboratedType &et =
      static_cast<const clang::ElaboratedType &>(the_type);

    if (get_type(et.getNamedType(), new_type))
      return true;
    break;
  }

  case clang::Type::TypeOfExpr:
  {
    const clang::TypeOfExprType &tofe =
      static_cast<const clang::TypeOfExprType &>(the_type);

    if (get_type(tofe.desugar(), new_type))
      return true;

    break;
  }

  case clang::Type::TypeOf:
  {
    const clang::TypeOfType &toft =
      static_cast<const clang::TypeOfType &>(the_type);

    if (get_type(toft.desugar(), new_type))
      return true;

    break;
  }

  case clang::Type::LValueReference:
  {
    const clang::LValueReferenceType &lvrt =
      static_cast<const clang::LValueReferenceType &>(the_type);

    typet sub_type;
    if (get_type(lvrt.getPointeeType(), sub_type))
      return true;

    if (sub_type.is_struct() || sub_type.is_union())
    {
      struct_union_typet t = to_struct_union_type(sub_type);
      sub_type = symbol_typet(tag_prefix + t.tag().as_string());
    }

    /*
     * Note:
     * isConstQualified() checks the parent node qualifier,
     * NOT the child node qualifier, e.g
     * Given
     *  `-LValueReferenceType 0x55555eda3160 'const class Vehicle &'
     *    `-QualType 0x55555eda2b21 'const class Vehicle' const
     * isConstQualified() returns false;
     *
     * Given
     * QualType 0x55555eda3161 'const class Vehicle &const' const
     *  `-LValueReferenceType 0x55555eda3160 'const class Vehicle &'
     * isConstQualified() returns true;
     *
     * So for a const ref, we need to annotate it here
     */
    if (lvrt.getPointeeType().isConstQualified())
      sub_type.cmt_constant(true);

    new_type = gen_pointer_type(sub_type);
    new_type.set("#reference", true);

    break;
  }

  case clang::Type::MacroQualified:
  {
    const clang::MacroQualifiedType &macro =
      static_cast<const clang::MacroQualifiedType &>(the_type);

    if (get_type(macro.desugar(), new_type))
      return true;

    break;
  }

  case clang::Type::Attributed:
  {
    const clang::AttributedType &att =
      static_cast<const clang::AttributedType &>(the_type);

    if (get_type(att.desugar(), new_type))
      return true;

    break;
  }

  case clang::Type::Decltype:
  {
    const clang::DecltypeType &dt =
      static_cast<const clang::DecltypeType &>(the_type);

    if (get_type(dt.getUnderlyingType(), new_type))
      return true;

    break;
  }

  case clang::Type::Atomic:
  {
    const clang::AtomicType &dt =
      static_cast<const clang::AtomicType &>(the_type);

    // FIXME: we need some representation of atomic types in irep2
    if (get_type(dt.getValueType(), new_type))
      return true;

    break;
  }

  case clang::Type::Auto:
  {
    const clang::AutoType &at = static_cast<const clang::AutoType &>(the_type);

    if (get_type(at.desugar(), new_type))
      return true;

    break;
  }

#if CLANG_VERSION_MAJOR < 14
#  define BITINT_TAG clang::Type::ExtInt
#  define BITINT_TYPE clang::ExtIntType
#else
#  define BITINT_TAG clang::Type::BitInt
#  define BITINT_TYPE clang::BitIntType
#endif
  case BITINT_TAG:
  {
    const BITINT_TYPE &eit = static_cast<const BITINT_TYPE &>(the_type);
    const unsigned n = eit.getNumBits();
    if (eit.isSigned())
      new_type = signedbv_typet(n);
    else
    {
      assert(eit.isUnsigned());
      new_type = unsignedbv_typet(n);
    }

    new_type.set("#extint", true);
    break;
  }
#undef BITINT_TAG
#undef BITINT_TYPE

  case clang::Type::ExtVector:
  {
    // NOTE: some bitshift operations with 'clang::Type::ExtVector' vectors are parsed as this
    //   e.g vsi << 2 becomes ExtVector
    //       vsi << vsi2 becomes Vector
    const clang::ExtVectorType &vec =
      static_cast<const clang::ExtVectorType &>(the_type);

    typet the_type;
    if (get_type(vec.getElementType(), the_type))
      return true;

    new_type = vector_typet(
      the_type,
      constant_exprt(
        integer2binary(vec.getNumElements(), bv_width(int_type())),
        integer2string(vec.getNumElements()),
        int_type()));
    break;
  }
  case clang::Type::Vector:
  {
    const clang::VectorType &vec =
      static_cast<const clang::VectorType &>(the_type);

    typet the_type;
    if (get_type(vec.getElementType(), the_type))
      return true;

    new_type = vector_typet(
      the_type,
      constant_exprt(
        integer2binary(vec.getNumElements(), bv_width(int_type())),
        integer2string(vec.getNumElements()),
        int_type()));
    break;
  }

  default:
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);
    ross << "Conversion of unsupported clang type: \"";
    ross << the_type.getTypeClassName() << "\n";
    the_type.dump(ross, *ASTContext);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }

  return false;
}

bool clang_c_convertert::get_builtin_type(
  const clang::BuiltinType &bt,
  typet &new_type)
{
  std::string c_type;

  switch (bt.getKind())
  {
  case clang::BuiltinType::Void:
    new_type = empty_typet();
    c_type = "void";
    break;

  case clang::BuiltinType::Bool:
    new_type = bool_type();
    c_type = "bool";
    break;

  case clang::BuiltinType::Char_U:
  case clang::BuiltinType::UChar:
    new_type = unsigned_char_type();
    c_type = "unsigned_char";
    break;

  case clang::BuiltinType::WChar_U:
    new_type = unsigned_wchar_type();
    c_type = "unsigned_wchar_t";
    break;

  case clang::BuiltinType::Char16:
    new_type = char16_type();
    c_type = "char16_t";
    break;

  case clang::BuiltinType::Char32:
    new_type = char32_type();
    c_type = "char32_t";
    break;

  case clang::BuiltinType::UShort:
    new_type = unsigned_short_int_type();
    c_type = "unsigned_short";
    break;

  case clang::BuiltinType::UInt:
    new_type = uint_type();
    c_type = "unsigned_int";
    break;

  case clang::BuiltinType::ULong:
    new_type = long_uint_type();
    c_type = "unsigned_long";
    break;

  case clang::BuiltinType::ULongLong:
    new_type = long_long_uint_type();
    c_type = "unsigned_long_long";
    break;

  case clang::BuiltinType::Char_S:
  case clang::BuiltinType::SChar:
    new_type = signed_char_type();
    c_type = "signed_char";
    break;

  case clang::BuiltinType::WChar_S:
    new_type = wchar_type();
    c_type = "wchar_t";
    break;

  case clang::BuiltinType::Short:
    new_type = signed_short_int_type();
    c_type = "signed_short";
    break;

  case clang::BuiltinType::Int:
    new_type = int_type();
    c_type = "signed_int";
    break;

  case clang::BuiltinType::Long:
    new_type = long_int_type();
    c_type = "signed_long";
    break;

  case clang::BuiltinType::LongLong:
    new_type = long_long_int_type();
    c_type = "signed_long_long";
    break;

  case clang::BuiltinType::Half:
    new_type = half_float_type();
    c_type = "_Float16";
    break;

  case clang::BuiltinType::Float:
    new_type = float_type();
    c_type = "float";
    break;

  case clang::BuiltinType::Double:
    new_type = double_type();
    c_type = "double";
    break;

  case clang::BuiltinType::Float128:
  case clang::BuiltinType::LongDouble:
    new_type = long_double_type();
    c_type = "long_double";
    break;

  case clang::BuiltinType::Int128:
    new_type = int128_type();
    c_type = "__int128";
    break;

  case clang::BuiltinType::UInt128:
    // Various simplification / big-int related things use uint64_t's...
    new_type = uint128_type();
    c_type = "__uint128";
    break;

  case clang::BuiltinType::NullPtr:
    new_type = pointer_type();
    c_type = "uintptr_t";
    break;

#ifdef ESBMC_CHERI_CLANG
  case clang::BuiltinType::IntCap:
    new_type = intcap_typet();
    c_type = "__intcap";
    break;

  case clang::BuiltinType::UIntCap:
    new_type = uintcap_typet();
    c_type = "unsigned __intcap";
    break;
#endif

  default:
  {
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);

    ross << "Unrecognized clang builtin type "
         << bt.getName(clang::PrintingPolicy(clang::LangOptions())).str()
         << "\n";
    bt.dump(ross, *ASTContext);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }
  }

  new_type.set("#cpp_type", c_type);
  return false;
}

bool clang_c_convertert::get_expr(const clang::Stmt &stmt, exprt &new_expr)
{
  locationt location;
  get_start_location_from_stmt(stmt, location);

  switch (stmt.getStmtClass())
  {
  /* The following enum values are the the expr of a program,
   * defined on the Expr class */

  // Objects that are implicit defined on the code syntax.
  // One example is the gcc ternary operator, which can be:
  // _Bool a = 1 ? : 0; is equivalent to _Bool a = 1 ? 1 : 0;
  // The 'then' expr is an opaque value equal to the ternary's
  // condition
  case clang::Stmt::OpaqueValueExprClass:
  {
    const clang::OpaqueValueExpr &opaque_expr =
      static_cast<const clang::OpaqueValueExpr &>(stmt);

    if (get_expr(*opaque_expr.getSourceExpr(), new_expr))
      return true;

    break;
  }

  // Reference to a declared object, such as functions or variables
  case clang::Stmt::DeclRefExprClass:
  {
    const clang::DeclRefExpr &decl =
      static_cast<const clang::DeclRefExpr &>(stmt);

    const clang::Decl &dcl = static_cast<const clang::Decl &>(*decl.getDecl());

    if (get_decl_ref(dcl, new_expr))
      return true;

    break;
  }

  // Predefined MACROS as __func__ or __PRETTY_FUNCTION__
  case clang::Stmt::PredefinedExprClass:
  {
    const clang::PredefinedExpr &pred_expr =
      static_cast<const clang::PredefinedExpr &>(stmt);

    if (convert_string_literal(*pred_expr.getFunctionName(), new_expr))
      return true;

    break;
  }

  // An integer value
  case clang::Stmt::IntegerLiteralClass:
  {
    const clang::IntegerLiteral &integer_literal =
      static_cast<const clang::IntegerLiteral &>(stmt);

    if (convert_integer_literal(integer_literal, new_expr))
      return true;

    break;
  }

  // A character such 'a'
  case clang::Stmt::CharacterLiteralClass:
  {
    const clang::CharacterLiteral &char_literal =
      static_cast<const clang::CharacterLiteral &>(stmt);

    if (convert_character_literal(char_literal, new_expr))
      return true;

    break;
  }

  // A float value
  case clang::Stmt::FloatingLiteralClass:
  {
    const clang::FloatingLiteral &floating_literal =
      static_cast<const clang::FloatingLiteral &>(stmt);

    if (convert_float_literal(floating_literal, new_expr))
      return true;

    break;
  }

  // A string
  case clang::Stmt::StringLiteralClass:
  {
    const clang::StringLiteral &string_literal =
      static_cast<const clang::StringLiteral &>(stmt);

    if (convert_string_literal(string_literal, new_expr))
      return true;

    break;
  }

  // This is an expr surrounded by parenthesis, we'll ignore it for
  // now, and check its subexpression
  case clang::Stmt::ParenExprClass:
  {
    const clang::ParenExpr &p = static_cast<const clang::ParenExpr &>(stmt);

    if (get_expr(*p.getSubExpr(), new_expr))
      return true;

    break;
  }

  // An unary operator such as +a, -a, *a or &a
  case clang::Stmt::UnaryOperatorClass:
  {
    const clang::UnaryOperator &uniop =
      static_cast<const clang::UnaryOperator &>(stmt);

    if (get_unary_operator_expr(uniop, new_expr))
      return true;

    break;
  }

  // An array subscript operation, such as a[1]
  case clang::Stmt::ArraySubscriptExprClass:
  {
    const clang::ArraySubscriptExpr &arr =
      static_cast<const clang::ArraySubscriptExpr &>(stmt);

    typet t;
    if (get_type(arr.getType(), t))
      return true;

    exprt array;
    if (get_expr(*arr.getBase(), array))
      return true;

    exprt pos;
    if (get_expr(*arr.getIdx(), pos))
      return true;

    new_expr = index_exprt(array, pos, t);
    break;
  }

  // Support for __builtin_offsetof();
  case clang::Stmt::OffsetOfExprClass:
  {
    const clang::OffsetOfExpr &offset =
      static_cast<const clang::OffsetOfExpr &>(stmt);

    // Use clang to calculate offsetof
    clang::Expr::EvalResult result;
    if (offset.EvaluateAsInt(result, *ASTContext))
    {
      new_expr = constant_exprt(
        integer2binary(
          result.Val.getInt().getSExtValue(), bv_width(size_type())),
        integer2string(result.Val.getInt().getSExtValue()),
        size_type());
    }
    else
    {
      /* Clang failed, i.e., a dynamic offsetof, something like
       * offsetof(struct S, member[idx]). We're building the equivalent of the
       * manual offsetof() macro definition. Details below. */

      /* TODO: It would be good to put the expression we're constructing here
       *       into compute_pointer_offset(), e.g., already in the adjuster.
       *       However, that would require the migrate_namespace_lookup to be
       *       setup correctly. Modelling it as pointer_offset expression would
       *       be throwing away the information that the value-set of this
       *       pointer expression is effectively a singleton with constant
       *       address zero. */

      unsigned n = offset.getNumComponents();
      assert(n > 0);

      const clang::TypeSourceInfo *ti = offset.getTypeSourceInfo();
      const clang::QualType q_type = ti->getType();
      typet base;
      if (get_type(q_type, base))
        return true;

      /* start by building the expression e := *(base *)0 */
      exprt e = constant_exprt(
        integer2binary(0, bv_width(size_type())),
        integer2string(0),
        size_type());
      e = typecast_exprt(e, pointer_typet(base));
      e = dereference_exprt(e, e.type());

      /* process the list comprised of member, index, and base-class accesses */
      for (unsigned i = 0; i < n; i++)
      {
        const clang::OffsetOfNode &o = offset.getComponent(i);
        switch (o.getKind())
        {
        case clang::OffsetOfNode::Array:
        {
          const clang::Expr *cidx = offset.getIndexExpr(o.getArrayExprIndex());
          exprt idx;
          if (get_expr(*cidx, idx))
            return true;
          e = index_exprt(e, idx);
          break;
        }
        case clang::OffsetOfNode::Field:
        {
          const clang::FieldDecl *fd = o.getField();
          exprt comp;
          if (get_decl(*fd, comp))
            return true;
          e = member_exprt(e, comp.name(), comp.type());
          break;
        }
        case clang::OffsetOfNode::Identifier:
        {
          const clang::IdentifierInfo *fii = o.getFieldName();
          e = member_exprt(e, irep_idt(fii->getNameStart()));
          break;
        }
        case clang::OffsetOfNode::Base: /* TODO */
          log_error("offsetof() on base class members not implemented");
          abort();
        }
      }

      /* finally, the result is (size_t)&e */
      e = address_of_exprt(e);
      e = typecast_exprt(e, size_type());
      new_expr = e;
    }

    break;
  }

  case clang::Stmt::UnaryExprOrTypeTraitExprClass:
  {
    const clang::UnaryExprOrTypeTraitExpr &unary =
      static_cast<const clang::UnaryExprOrTypeTraitExpr &>(stmt);

    // Use clang to calculate sizeof/alignof
    clang::Expr::EvalResult result;
    if (unary.EvaluateAsInt(result, *ASTContext))
    {
      new_expr = constant_exprt(
        integer2binary(
          result.Val.getInt().getZExtValue(), bv_width(size_type())),
        integer2string(result.Val.getInt().getZExtValue()),
        size_type());
    }
    else
    {
      assert(unary.getKind() == clang::UETT_SizeOf);

      typet t;
      if (get_type(unary.getType(), t))
        return true;

      new_expr = exprt("sizeof", t);
    }

    typet size_type;
    if (get_type(unary.getTypeOfArgument(), size_type))
      return true;

    if (size_type.is_struct() || size_type.is_union())
    {
      struct_union_typet t = to_struct_union_type(size_type);
      size_type = symbol_typet(tag_prefix + t.tag().as_string());
    }

    new_expr.set("#c_sizeof_type", size_type);
    break;
  }

  // A function call expr
  // It can be undefined here, the symbol will be added in
  // adjust_expr::adjust_side_effect_function_call
  case clang::Stmt::CallExprClass:
  {
    const clang::CallExpr &function_call =
      static_cast<const clang::CallExpr &>(stmt);

    const clang::Stmt *callee = function_call.getCallee();

#if CLANG_VERSION_MAJOR > 14
    if (function_call.isCallToStdMove())
    {
      if (get_expr(*function_call.getArg(0), new_expr))
        return true;

      break;
    }
#endif

    exprt callee_expr;
    if (get_expr(*callee, callee_expr))
      return true;

    typet type;
    clang::QualType qtype = function_call.getCallReturnType(*ASTContext);
    if (get_type(qtype, type))
      return true;

    side_effect_expr_function_callt call;
    call.function() = callee_expr;
    call.type() = type;

    for (const clang::Expr *arg : function_call.arguments())
    {
      exprt single_arg;
      if (get_expr(*arg, single_arg))
        return true;

      call.arguments().push_back(single_arg);
    }

    new_expr = call;
    break;
  }

  case clang::Stmt::MemberExprClass:
  {
    const clang::MemberExpr &member =
      static_cast<const clang::MemberExpr &>(stmt);

    // special treatment for MemberExpr referring to an enumerator
    if (
      const auto *e =
        llvm::dyn_cast<clang::EnumConstantDecl>(member.getMemberDecl()))
    {
      if (get_enum_value(e, new_expr))
        return true;

      break;
    }

    if (!perform_virtual_dispatch(member))
    {
      exprt comp;
      if (get_decl(*member.getMemberDecl(), comp))
        return true;

      if (!is_member_decl_static(member))
      {
        exprt base;
        if (get_expr(*member.getBase(), base))
          return true;

        assert(!comp.name().empty());
        // for MemberExpr referring to struct field (or method in case of C++ class)
        new_expr = member_exprt(base, comp.name(), comp.type());
      }
      else
      {
        // for static members, use the member decl symbol directly
        // without making a member_exprt, e.g.
        // If the member_exprt refers to a class static member, then
        // replace "OBJECT.MyStatic = 1" with "MyStatic = 1;"
        assert(comp.statement() == "decl");
        assert(comp.op0().is_symbol());
        new_expr = comp.op0();
      }
    }
    else
    {
      if (get_vft_binding_expr(member, new_expr))
        return true;
    }

    break;
  }

  case clang::Stmt::CompoundLiteralExprClass:
  {
    const clang::CompoundLiteralExpr &compound =
      static_cast<const clang::CompoundLiteralExpr &>(stmt);

    exprt initializer;
    if (get_expr(*compound.getInitializer(), initializer))
      return true;

    typet t = initializer.type();

    /* A compound literal is an LValue that has associated static or automatic
     * storage, depending on whether it appears in file or block scope.
     * Therefore, in C, for instance a pointer to it can be taken and used
     * within the same block without restrictions, e.g.
     *
     *   int *p = &(int){0}, x = 42;
     *   memcpy(p, &x, sizeof(int));
     *   assert(*p == 42);
     *
     * It has the same semantics as an non-anonymous variable of the
     * corresponding type. Thus, we introduce a new symbol to represent it.
     */

    /* Give the symbol a recognizable name to show in the counter-example */
    std::string path = location.file().as_string();
    std::string name_prefix =
      path + ":" + location.get_line().as_string() + "$compound-literal$";
    symbolt &cl = anon_symbol.new_symbol(context, t, name_prefix);
    /* .name and .id have already been assigned by new_symbol() above */
    get_default_symbol(
      cl, get_modulename_from_path(path), t, cl.name, cl.id, location);

    cl.static_lifetime = !current_block || compound.isFileScope();
    cl.is_extern = false;
    cl.file_local = true;
    cl.value = initializer;

    new_expr = symbol_expr(cl);

    if (!cl.static_lifetime)
    {
      /* The underlying storage is automatic here, i.e., local. In order for
       * it to be recognized as being local in ESBMC, it requires a declaration,
       * see, e.g., goto_programt::get_decl_identifiers(). So we'll add one. */
      code_declt decl(new_expr);
      decl.operands().push_back(initializer);

      current_block->operands().push_back(decl);
    }
    else
    {
      /* Symbols appearing in file scope do not need a declaration.
       * clang_c_main::static_lifetime_init() takes care of the initialization.
       */
    }

    break;
  }

  case clang::Stmt::AddrLabelExprClass:
  {
    const clang::AddrLabelExpr &addrlabelExpr =
      static_cast<const clang::AddrLabelExpr &>(stmt);

    exprt label;
    if (get_decl(*addrlabelExpr.getLabel(), label))
      return true;

    new_expr = address_of_exprt(label);
    break;
  }

  case clang::Stmt::StmtExprClass:
  {
    const clang::StmtExpr &stmtExpr =
      static_cast<const clang::StmtExpr &>(stmt);

    typet t;
    if (get_type(stmtExpr.getType(), t))
      return true;

    exprt subStmt;
    if (get_expr(*stmtExpr.getSubStmt(), subStmt))
      return true;

    side_effect_exprt stmt_expr("statement_expression", t);
    stmt_expr.copy_to_operands(subStmt);

    new_expr = stmt_expr;
    break;
  }

  case clang::Stmt::CXXNullPtrLiteralExprClass:
  case clang::Stmt::GNUNullExprClass:
  {
    const clang::Expr &gnun = static_cast<const clang::Expr &>(stmt);

    typet t;
    if (get_type(gnun.getType(), t))
      return true;

    new_expr = gen_zero(t);
    break;
  }
  // Casts expression:
  // Implicit: float f = 1; equivalent to float f = (float) 1;
  // CStyle: int a = (int) 3.0;
  case clang::Stmt::ImplicitCastExprClass:
  case clang::Stmt::CStyleCastExprClass:
  {
    const clang::CastExpr &cast = static_cast<const clang::CastExpr &>(stmt);

    if (get_cast_expr(cast, new_expr))
      return true;

    break;
  }

  // Binary expression such as a+1, a-1 and assignments
  case clang::Stmt::BinaryOperatorClass:
  case clang::Stmt::CompoundAssignOperatorClass:
  {
    const clang::BinaryOperator &binop =
      static_cast<const clang::BinaryOperator &>(stmt);

    if (get_binary_operator_expr(binop, new_expr))
      return true;

    break;
  }

  // This is the ternary if
  case clang::Stmt::ConditionalOperatorClass:
  {
    const clang::ConditionalOperator &ternary_if =
      static_cast<const clang::ConditionalOperator &>(stmt);

    exprt cond;
    if (get_expr(*ternary_if.getCond(), cond))
      return true;

    exprt then;
    if (get_expr(*ternary_if.getTrueExpr(), then))
      return true;

    exprt else_expr;
    if (get_expr(*ternary_if.getFalseExpr(), else_expr))
      return true;

    typet t;
    if (get_type(ternary_if.getType(), t))
      return true;

    exprt if_expr("if", t);
    if_expr.copy_to_operands(cond, then, else_expr);

    new_expr = if_expr;
    break;
  }

  // This is the gcc's ternary if extension
  case clang::Stmt::BinaryConditionalOperatorClass:
  {
    const clang::BinaryConditionalOperator &ternary_if =
      static_cast<const clang::BinaryConditionalOperator &>(stmt);

    exprt cond;
    if (get_expr(*ternary_if.getCond(), cond))
      return true;

    exprt else_expr;
    if (get_expr(*ternary_if.getFalseExpr(), else_expr))
      return true;

    typet t;
    if (get_type(ternary_if.getType(), t))
      return true;

    side_effect_exprt gcc_ternary("gcc_conditional_expression", t);
    gcc_ternary.copy_to_operands(cond, else_expr);

    new_expr = gcc_ternary;
    break;
  }

  case clang::Stmt::ConvertVectorExprClass:
  {
    // TODO: Creating a fake call that is a passthrough should be simpler
    const clang::ConvertVectorExpr &convertVector =
      static_cast<const clang::ConvertVectorExpr &>(stmt);

    side_effect_expr_function_callt fake_call;
    code_typet t;
    if (get_type(convertVector.getType(), t.return_type()))
      return true;

    assert(t.return_type().is_vector());
    fake_call.type() = t;

    exprt e;
    if (get_expr(*convertVector.getSrcExpr(), e))
      return true;

    t.arguments().push_back(code_typet::argumentt(e.type()));
    fake_call.arguments().push_back(e);

    fake_call.function() = symbol_exprt("c:@F@__ESBMC_convertvector", t);
    fake_call.function().name("__ESBMC_convertvector");
    new_expr.swap(fake_call);
    return false;
  }

  // A shufflevector statement
  case clang::Stmt::ShuffleVectorExprClass:
  {
    // TODO: Creating a fake call that is a passthrough should be simpler
    const clang::ShuffleVectorExpr &shuffle =
      static_cast<const clang::ShuffleVectorExpr &>(stmt);

    side_effect_expr_function_callt fake_call;
    code_typet t;
    if (get_type(shuffle.getType(), t.return_type()))
      return true;

    assert(t.return_type().is_vector());
    fake_call.type() = t;

    for (unsigned j = 0; j < shuffle.getNumSubExprs(); j++)
    {
      exprt e;
      if (get_expr(*shuffle.getExpr(j), e))
        return true;

      t.arguments().push_back(code_typet::argumentt(e.type()));
      fake_call.arguments().push_back(e);
    }

    fake_call.function() = symbol_exprt("c:@F@__ESBMC_shufflevector", t);
    fake_call.function().name("__ESBMC_shufflevector");
    new_expr.swap(fake_call);
    return false;
  }

  // An initialize statement, such as int a[3] = {1, 2, 3}
  case clang::Stmt::InitListExprClass:
  {
    const clang::InitListExpr &init_stmt =
      static_cast<const clang::InitListExpr &>(stmt);

    typet t;
    if (get_type(init_stmt.getType(), t))
      return true;

    exprt inits;

    t = get_complete_type(t, ns);

    // Structs/unions/arrays put the initializer on operands
    if (t.is_struct() || t.is_array() || t.is_vector())
    {
      /* Initialize everything to zero;
       * padding is taken care of later in adjust() */
      inits = gen_zero(t);

      unsigned int num = init_stmt.getNumInits();
      for (unsigned int i = 0, j = 0; (i < inits.operands().size() && j < num);
           ++i)
      {
        const struct_union_typet::componentt *c = nullptr;
        if (t.is_struct())
        {
          c = &to_struct_union_type(t).components()[i];
          assert(!c->get_is_padding());
          if (c->get_is_unnamed_bitfield())
            continue;
        }

        // Get the value being initialized
        exprt init;
        if (get_expr(*init_stmt.getInit(j++), init))
          return true;

        typet elem_type;
        if (t.is_struct())
          elem_type = c->type();
        else if (t.is_array())
          elem_type = to_array_type(t).subtype();
        else
          elem_type = to_vector_type(t).subtype();
        gen_typecast(ns, init, elem_type);
        inits.operands().at(i) = init;
      }
    }
    else if (t.is_union())
    {
      /* The Clang AST either contains a single initializer for union-typed
       * expressions or none for the empty union. Create a constant expression
       * of the right type and set its init-expression, if it exists.
       * The init expression has to be the only operand to this expression
       * regardless of the position the initialized field is being declared. */
      inits = gen_zero(t);
      if (init_stmt.getNumInits() > 0)
      {
        assert(init_stmt.getNumInits() == 1);
        exprt init;
        if (get_expr(*init_stmt.getInit(0), init))
          return true;
        inits.operands().at(0) = init;

        // set which field is being initialized
        auto init_union_field = init_stmt.getInitializedFieldInUnion();
        if (init_union_field)
          to_union_expr(inits).set_component_name(
            init_union_field->getName().str());
      }
    }
    else if (
      init_stmt.getNumInits() == 0 && init_stmt.getType()->isScalarType())
    {
      /* We have a list initializer with no elements.
       * So per https://en.cppreference.com/w/cpp/language/list_initialization
       * we perform value-initialization.
       * > Otherwise, if the braced-init-list has no elements, T is value-initialized.
       * And per https://en.cppreference.com/w/cpp/language/value_initialization
       * > The effects of value-initialization are:
       * > ...
       * > - Otherwise, the object is zero-initialized.
       * So we just zero-initialize the object.
       */
      inits = gen_zero(t);
    }
    else
    {
      assert(init_stmt.getNumInits() == 1);
      if (get_expr(*init_stmt.getInit(0), inits))
        return true;
    }

    new_expr = inits;
    break;
  }

  case clang::Stmt::ImplicitValueInitExprClass:
  {
    const clang::ImplicitValueInitExpr &init_stmt =
      static_cast<const clang::ImplicitValueInitExpr &>(stmt);

    typet t;
    if (get_type(init_stmt.getType(), t))
      return true;

    new_expr = gen_zero(get_complete_type(t, ns));
    break;
  }

  case clang::Stmt::GenericSelectionExprClass:
  {
    const clang::GenericSelectionExpr &gen =
      static_cast<const clang::GenericSelectionExpr &>(stmt);

    if (get_expr(*gen.getResultExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::VAArgExprClass:
  {
    const clang::VAArgExpr &vaa = static_cast<const clang::VAArgExpr &>(stmt);

    exprt expr;
    if (get_expr(*vaa.getSubExpr(), expr))
      return true;

    typet t;
    if (get_type(vaa.getType(), t))
      return true;

    exprt vaa_expr("builtin_va_arg", t);
    vaa_expr.copy_to_operands(expr);

    new_expr = vaa_expr;
    break;
  }

  case clang::Stmt::ConstantExprClass:
  {
    const clang::ConstantExpr &c =
      static_cast<const clang::ConstantExpr &>(stmt);

    if (get_expr(*c.getSubExpr(), new_expr))
      return true;

    break;
  }

  /*
    The following enum values are the basic elements of a program,
    defined on the Stmt class
  */

  // Declaration of variables, it is created as a decl-block to
  // allow declarations like int a,b;
  case clang::Stmt::DeclStmtClass:
  {
    const clang::DeclStmt &decl = static_cast<const clang::DeclStmt &>(stmt);

    const auto &declgroup = decl.getDeclGroup();

    codet decls("decl-block");
    for (auto it : declgroup)
    {
      exprt single_decl;
      if (get_decl(*it, single_decl))
        return true;

      decls.operands().push_back(single_decl);
    }

    new_expr = decls;
    break;
  }

  // A compound statement is a scope/block
  case clang::Stmt::CompoundStmtClass:
  {
    const clang::CompoundStmt &compound_stmt =
      static_cast<const clang::CompoundStmt &>(stmt);

    code_blockt block, *old_block = current_block;
    current_block = &block;
    for (auto const &stmt : compound_stmt.body())
    {
      exprt statement;
      if (get_expr(*stmt, statement))
        return true;

      convert_expression_to_code(statement);
      block.operands().push_back(statement);
    }

    // Set the end location for blocks
    locationt location_end;
    get_final_location_from_stmt(stmt, location_end);

    block.end_location(location_end);

    new_expr = block;
    current_block = old_block;
    break;
  }

  // A case statement inside a switch. The detail here is that we
  // construct it as a label
  case clang::Stmt::CaseStmtClass:
  {
    const clang::CaseStmt &case_stmt =
      static_cast<const clang::CaseStmt &>(stmt);

    exprt value;
    if (get_expr(*case_stmt.getLHS(), value))
      return true;

    exprt sub_stmt;
    if (get_expr(*case_stmt.getSubStmt(), sub_stmt))
      return true;

    code_switch_caset switch_case;
    switch_case.case_op() = value;

    convert_expression_to_code(sub_stmt);
    switch_case.code() = to_code(sub_stmt);

    new_expr = switch_case;
    break;
  }

  // A default statement inside a switch. Same as before, we construct
  // as a label, the difference is that we set default to true
  case clang::Stmt::DefaultStmtClass:
  {
    const clang::DefaultStmt &default_stmt =
      static_cast<const clang::DefaultStmt &>(stmt);

    exprt sub_stmt;
    if (get_expr(*default_stmt.getSubStmt(), sub_stmt))
      return true;

    code_switch_caset switch_case;
    switch_case.set_default(true);

    convert_expression_to_code(sub_stmt);
    switch_case.code() = to_code(sub_stmt);

    new_expr = switch_case;
    break;
  }

  // A label on the program
  case clang::Stmt::LabelStmtClass:
  {
    const clang::LabelStmt &label_stmt =
      static_cast<const clang::LabelStmt &>(stmt);

    exprt sub_stmt;
    if (get_expr(*label_stmt.getSubStmt(), sub_stmt))
      return true;

    convert_expression_to_code(sub_stmt);

    code_labelt label;
    label.set_label(label_stmt.getName());
    label.code() = to_code(sub_stmt);

    new_expr = label;
    break;
  }

  // An if then else statement. The else statement may not
  // exist, so we must check before constructing its exprt.
  // We always to try to cast its condition to bool
  case clang::Stmt::IfStmtClass:
  {
    const clang::IfStmt &ifstmt = static_cast<const clang::IfStmt &>(stmt);

    const clang::Stmt *cond_expr = ifstmt.getConditionVariableDeclStmt();
    if (cond_expr == nullptr)
      cond_expr = ifstmt.getCond();

    exprt cond;
    if (get_expr(*cond_expr, cond))
      return true;

    exprt then;
    if (get_expr(*ifstmt.getThen(), then))
      return true;

    convert_expression_to_code(then);

    codet if_expr("ifthenelse");
    if_expr.copy_to_operands(cond, then);

    const clang::Stmt *else_stmt = ifstmt.getElse();

    if (else_stmt)
    {
      exprt else_expr;
      if (get_expr(*else_stmt, else_expr))
        return true;

      convert_expression_to_code(else_expr);
      if_expr.copy_to_operands(else_expr);
    }

    new_expr = if_expr;
    break;
  }

  // A switch statement.
  case clang::Stmt::SwitchStmtClass:
  {
    const clang::SwitchStmt &switch_stmt =
      static_cast<const clang::SwitchStmt &>(stmt);

    const clang::Stmt *cond_expr = switch_stmt.getConditionVariableDeclStmt();
    if (cond_expr == nullptr)
      cond_expr = switch_stmt.getCond();

    exprt cond;
    if (get_expr(*cond_expr, cond))
      return true;

    codet body;
    if (get_expr(*switch_stmt.getBody(), body))
      return true;

    code_switcht switch_code;
    switch_code.value() = cond;
    switch_code.body() = body;

    new_expr = switch_code;
    break;
  }

  // A while statement. Even if its body is empty, an CompoundStmt
  // is generated for it. We always try to cast its condition to bool
  case clang::Stmt::WhileStmtClass:
  {
    const clang::WhileStmt &while_stmt =
      static_cast<const clang::WhileStmt &>(stmt);

    const clang::Stmt *cond_expr = while_stmt.getConditionVariableDeclStmt();
    if (cond_expr == nullptr)
      cond_expr = while_stmt.getCond();

    exprt cond;
    if (get_expr(*cond_expr, cond))
      return true;

    codet body = code_skipt();
    if (get_expr(*while_stmt.getBody(), body))
      return true;

    convert_expression_to_code(body);

    code_whilet code_while;
    code_while.cond() = cond;
    code_while.body() = body;

    new_expr = code_while;
    break;
  }

  // A dowhile statement. Even if its body is empty, an CompoundStmt
  // is generated for it. We always try to cast its condition to bool
  case clang::Stmt::DoStmtClass:
  {
    const clang::DoStmt &do_stmt = static_cast<const clang::DoStmt &>(stmt);

    exprt cond;
    if (get_expr(*do_stmt.getCond(), cond))
      return true;

    codet body = code_skipt();
    if (get_expr(*do_stmt.getBody(), body))
      return true;

    convert_expression_to_code(body);

    code_dowhilet code_while;
    code_while.cond() = cond;
    code_while.body() = body;

    new_expr = code_while;
    break;
  }

  // A For statement. Even if its body is empty, an CompoundStmt
  // is generated for it. We always try to cast its condition to bool.
  // Its parameters might be empty, so we have to check them all before
  // converting
  case clang::Stmt::ForStmtClass:
  {
    const clang::ForStmt &for_stmt = static_cast<const clang::ForStmt &>(stmt);

    codet init = code_skipt();
    const clang::Stmt *init_stmt = for_stmt.getInit();
    if (init_stmt)
      if (get_expr(*init_stmt, init))
        return true;

    convert_expression_to_code(init);
    const clang::Stmt *cond_expr = for_stmt.getConditionVariableDeclStmt();
    if (cond_expr == nullptr)
      cond_expr = for_stmt.getCond();

    exprt cond = true_exprt();
    if (cond_expr)
      if (get_expr(*cond_expr, cond))
        return true;

    codet inc = code_skipt();
    const clang::Stmt *inc_stmt = for_stmt.getInc();
    if (inc_stmt)
      get_expr(*inc_stmt, inc);

    convert_expression_to_code(inc);

    codet body = code_skipt();
    const clang::Stmt *body_stmt = for_stmt.getBody();
    if (body_stmt)
      if (get_expr(*body_stmt, body))
        return true;

    convert_expression_to_code(body);

    code_fort code_for;
    code_for.init() = init;
    code_for.cond() = cond;
    code_for.iter() = inc;
    code_for.body() = body;

    new_expr = code_for;
    break;
  }

  // a goto instruction to a label
  case clang::Stmt::GotoStmtClass:
  {
    const clang::GotoStmt &goto_stmt =
      static_cast<const clang::GotoStmt &>(stmt);

    code_gotot code_goto;
    code_goto.set_destination(goto_stmt.getLabel()->getName().str());

    new_expr = code_goto;
    break;
  }

  case clang::Stmt::IndirectGotoStmtClass:
  {
    const clang::IndirectGotoStmt &goto_stmt =
      static_cast<const clang::IndirectGotoStmt &>(stmt);

    // clang was able to compute the target, so this became a
    // common goto
    if (goto_stmt.getConstantTarget())
    {
      code_gotot code_goto;
      code_goto.set_destination(goto_stmt.getConstantTarget()->getName().str());

      new_expr = code_goto;
    }
    else
    {
      log_error("ESBMC currently does not support indirect gotos");
      std::ostringstream oss;
      llvm::raw_os_ostream ross(oss);
      stmt.dump(ross, *ASTContext);
      ross.flush();
      log_error("{}", oss.str());
      return true;

      exprt target;
      if (get_expr(*goto_stmt.getTarget(), target))
        return true;

      codet code_goto("gcc_goto");
      code_goto.copy_to_operands(target);

      new_expr = code_goto;
    }

    break;
  }

  // A continue statement
  case clang::Stmt::ContinueStmtClass:
    new_expr = code_continuet();
    break;

  // A break statement
  case clang::Stmt::BreakStmtClass:
    new_expr = code_breakt();
    break;

  // A return statement
  case clang::Stmt::ReturnStmtClass:
  {
    const clang::ReturnStmt &ret = static_cast<const clang::ReturnStmt &>(stmt);

    if (!current_functionDecl)
    {
      std::ostringstream oss;
      llvm::raw_os_ostream ross(oss);
      ross << "ESBMC could not find the parent scope for "
           << "the following return statement:"
           << "\n";
      ret.dump(ross, *ASTContext);
      ross.flush();
      log_error("{}", oss.str());
      return true;
    }

    typet return_type;
    if (get_type(current_functionDecl->getReturnType(), return_type))
      return true;

    code_returnt ret_expr;
    if (ret.getRetValue())
    {
      const clang::Expr &retval = *ret.getRetValue();

      exprt val;
      if (get_expr(retval, val))
        return true;

      gen_typecast(ns, val, return_type);
      ret_expr.return_value() = val;
    }

    new_expr = ret_expr;
    break;
  }

  // Atomic instructions
  case clang::Stmt::AtomicExprClass:
  {
    const clang::AtomicExpr &atm = static_cast<const clang::AtomicExpr &>(stmt);

    if (get_atomic_expr(atm, new_expr))
      return true;

    break;
  }

  case clang::Stmt::SourceLocExprClass:
  {
    /* From Clang docs: Represents a function call to one of __builtin_LINE(),
     * __builtin_COLUMN(), __builtin_FUNCTION(), __builtin_FUNCSIG(),
     * __builtin_FILE(), __builtin_FILE_NAME() or __builtin_source_location().
     */

    const clang::SourceLocExpr &loc =
      static_cast<const clang::SourceLocExpr &>(stmt);
    clang::APValue value = loc.EvaluateInContext(*ASTContext, nullptr);

    /* An APValue represents some constant. For constants derived from source
     * locations, it could either be a string (file / function name) or an int
     * (line / column number). */

    switch (value.getKind())
    {
    case clang::APValue::LValue:
    {
      // This is probably a string constant
      clang::APValue::LValueBase base = value.getLValueBase();
      assert(base.is<const clang::Expr *>());
      const clang::Expr *expr = base.get<const clang::Expr *>();
      if (get_expr(*expr, new_expr))
        return true;
      break;
    }

    case clang::APValue::Int:
    {
      const llvm::APSInt &Int = value.getInt();
      int width = Int.getBitWidth();
      assert(width <= 64);
      int64_t v = Int.getSExtValue();
      new_expr = constant_exprt(
        integer2binary(v, width), integer2string(v), signedbv_typet(width));
      break;
    }

    /*
    case clang::APValue::None:
    case clang::APValue::Indeterminate:
    case clang::APValue::Float:
    case clang::APValue::FixedPoint:
    case clang::APValue::ComplexInt:
    case clang::APValue::ComplexFloat:
    case clang::APValue::Vector:
    case clang::APValue::Array:
    case clang::APValue::Struct:
    case clang::APValue::Union:
    case clang::APValue::AddrLabelDiff:
    case clang::APValue::MemberPointer:
    */
    default:
      std::ostringstream oss;
      llvm::raw_os_ostream ross(oss);
      ross << "Conversion of unsupported value computed by clang for expr: \"";
      ross << stmt.getStmtClassName() << "\" to expression"
           << "\n";
      stmt.dump(ross, *ASTContext);
      ross.flush();
      log_error("{}", oss.str());
      return true;
    }

    break;
  }

  // A NULL statement, we ignore it. An example is a lost semicolon on
  // the program
  case clang::Stmt::NullStmtClass:

  // GCC or MS Assembly instruction. We ignore them
  case clang::Stmt::GCCAsmStmtClass:
  case clang::Stmt::MSAsmStmtClass:
    new_expr = code_skipt();
    break;

  /* According to Clang docs:
   *
   * A type trait used in the implementation of various C++11 and Library TR1
   * trait templates.
   *   __is_pod(int) == true
   *   __is_enum(std::string) == false
   *   __is_trivially_constructible(vector<int>, int*, int*)
   *
   * But it is also used for __builtin_types_compatible_p(ty1, ty2). */
  case clang::Stmt::TypeTraitExprClass:
  {
    const clang::TypeTraitExpr &tte =
      static_cast<const clang::TypeTraitExpr &>(stmt);

    if (tte.isValueDependent())
    {
      std::ostringstream oss;
      llvm::raw_os_ostream ross(oss);
      ross << "Conversion of unsupported value-dependent type-trait expr: \"";
      ross << stmt.getStmtClassName() << "\" to expression"
           << "\n";
      stmt.dump(ross, *ASTContext);
      ross.flush();
      log_error("{}", oss.str());
      return true;
    }

    typet type;
    if (get_type(tte.getType(), type))
      return true;

    assert(
      type.id() == typet::t_bool || type.id() == typet::t_signedbv ||
      type.id() == typet::t_unsignedbv);

    if (tte.getValue())
      new_expr = true_exprt();
    else
      new_expr = false_exprt();

    break;
  }

  /* Clang docs:
   *
   * GNU builtin-in function __builtin_choose_expr.
   *
   * This AST node is similar to the conditional operator (?:) in C, with the
   * following exceptions:
   *
   * - the test expression must be a integer constant expression.
   * - the expression returned acts like the chosen subexpression in every
   *   visible way: the type is the same as that of the chosen subexpression,
   *   and all predicates (whether it's an l-value, whether it's an integer
   *   constant expression, etc.) return the same result as for the chosen
   *   sub-expression.
   */
  case clang::Stmt::ChooseExprClass:
  {
    const clang::ChooseExpr &cexpr =
      static_cast<const clang::ChooseExpr &>(stmt);

    if (get_expr(*cexpr.getChosenSubExpr(), new_expr))
      return true;

    break;
  }

  case clang::Stmt::AttributedStmtClass:
  {
    const clang::AttributedStmt &astmt =
      static_cast<const clang::AttributedStmt &>(stmt);

    /* ignore attributes for now */
    if (get_expr(*astmt.getSubStmt(), new_expr))
      return true;

    break;
  }

  default:
  {
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);
    ross << "Conversion of unsupported clang expr: \"";
    ross << stmt.getStmtClassName() << "\" to expression"
         << "\n";
    stmt.dump(ross, *ASTContext);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }
  }

  new_expr.location() = location;
  return false;
}

bool clang_c_convertert::get_enum_value(
  const clang::EnumConstantDecl *e,
  exprt &new_expr)
{
  assert(e);

  if (!e->getInitExpr())
  {
    new_expr = constant_exprt(
      integer2binary(e->getInitVal().getSExtValue(), bv_width(int_type())),
      integer2string(e->getInitVal().getSExtValue()),
      int_type());
    return false;
  }

  if (get_expr(*e->getInitExpr(), new_expr))
    return true;

  return false;
}

bool clang_c_convertert::get_decl_ref(const clang::Decl &d, exprt &new_expr)
{
  // Special case for Enums, we return the constant instead of a reference
  // to the name
  if (const auto *e = llvm::dyn_cast<clang::EnumConstantDecl>(&d))
  {
    if (get_enum_value(e, new_expr))
      return true;

    return false;
  }

  if (const auto *nd = llvm::dyn_cast<clang::ValueDecl>(&d))
  {
    // Everything else should be a value decl
    std::string name, id;
    get_decl_name(*nd, name, id);

    typet type;
    if (get_type(nd->getType(), type))
      return true;

    new_expr = exprt("symbol", type);
    new_expr.identifier(id);
    new_expr.cmt_lvalue(true);
    new_expr.name(name);
    return false;
  }

  std::ostringstream oss;
  llvm::raw_os_ostream ross(oss);
  ross << "Conversion of unsupported clang decl ref: \"";
  ross << d.getDeclKindName() << "\" to expression"
       << "\n";
  d.dump(ross);
  ross.flush();
  log_error("{}", oss.str());
  return true;
}

bool clang_c_convertert::get_cast_expr(
  const clang::CastExpr &cast,
  exprt &new_expr)
{
  exprt expr;
  if (get_expr(*cast.getSubExpr(), expr))
    return true;

  typet type;
  if (get_type(cast.getType(), type))
    return true;

  switch (cast.getCastKind())
  {
  case clang::CK_ArrayToPointerDecay:
  case clang::CK_FunctionToPointerDecay:
  case clang::CK_BuiltinFnToFnPtr:
  case clang::CK_UncheckedDerivedToBase:
    break;

  case clang::CK_DerivedToBase:
  case clang::CK_Dynamic:

  case clang::CK_UserDefinedConversion:
  case clang::CK_ConstructorConversion:

  case clang::CK_NoOp:

  case clang::CK_IntegralCast:
  case clang::CK_IntegralToBoolean:
  case clang::CK_IntegralToFloating:
  case clang::CK_IntegralToPointer:

  case clang::CK_FloatingToIntegral:
  case clang::CK_FloatingToBoolean:
  case clang::CK_FloatingCast:

  case clang::CK_ToVoid:
  case clang::CK_BitCast:
  case clang::CK_LValueToRValue:
  case clang::CK_LValueBitCast:

  case clang::CK_PointerToBoolean:
  case clang::CK_PointerToIntegral:
    gen_typecast(ns, expr, type);
    break;

  case clang::CK_AddressSpaceConversion:
  case clang::CK_NullToPointer:
  case clang::CK_NullToMemberPointer:
    expr = gen_zero(type);
    break;

  case clang::CK_ToUnion:
    gen_typecast_to_union(ns, expr, type);
    break;

  case clang::CK_VectorSplat:
    break;

#ifdef ESBMC_CHERI_CLANG
  case clang::CK_PointerToCHERICapability:
    /* An explicit __cheri_tocap means this value might be tagged. */
  case clang::CK_CHERICapabilityToPointer:
    /* both should not be generated in purecap mode */
    break;
#endif

  default:
  {
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);
    ross << "Conversion of unsupported clang cast operator: \"";
    ross << cast.getCastKindName() << "\" to expression"
         << "\n";
    cast.dump(ross, *ASTContext);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }
  }

  new_expr = expr;
  return false;
}

bool clang_c_convertert::get_unary_operator_expr(
  const clang::UnaryOperator &uniop,
  exprt &new_expr)
{
  typet uniop_type;
  if (get_type(uniop.getType(), uniop_type))
    return true;

  exprt unary_sub;
  if (get_expr(*uniop.getSubExpr(), unary_sub))
    return true;

  switch (uniop.getOpcode())
  {
  case clang::UO_Plus:
    new_expr = exprt("unary+", uniop_type);
    break;

  case clang::UO_Minus:
    new_expr = exprt("unary-", uniop_type);
    break;

  case clang::UO_Not:
    new_expr = exprt("bitnot", uniop_type);
    break;

  case clang::UO_LNot:
    new_expr = exprt("not", bool_type());
    break;

  case clang::UO_PreInc:
    new_expr = side_effect_exprt("preincrement", uniop_type);
    break;

  case clang::UO_PreDec:
    new_expr = side_effect_exprt("predecrement", uniop_type);
    break;

  case clang::UO_PostInc:
    new_expr = side_effect_exprt("postincrement", uniop_type);
    break;

  case clang::UO_PostDec:
    new_expr = side_effect_exprt("postdecrement", uniop_type);
    break;

  case clang::UO_AddrOf:
    new_expr = exprt("address_of", uniop_type);
    break;

  case clang::UO_Deref:
    new_expr = exprt("dereference", uniop_type);
    break;

  case clang::UO_Extension:
    new_expr.swap(unary_sub);
    return false;

  default:
  {
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);
    ross << "Conversion of unsupported clang unary operator: \"";
    ross << clang::UnaryOperator::getOpcodeStr(uniop.getOpcode()).str()
         << "\" to expression"
         << "\n";
    uniop.dump(ross, *ASTContext);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }
  }

  new_expr.operands().push_back(unary_sub);
  return false;
}

bool clang_c_convertert::get_binary_operator_expr(
  const clang::BinaryOperator &binop,
  exprt &new_expr)
{
  exprt lhs;
  if (get_expr(*binop.getLHS(), lhs))
    return true;

  exprt rhs;
  if (get_expr(*binop.getRHS(), rhs))
    return true;

  typet t;
  if (get_type(binop.getType(), t))
    return true;

  switch (binop.getOpcode())
  {
  case clang::BO_Add:
    if (t.is_floatbv())
      new_expr = exprt("ieee_add", t);
    else
      new_expr = exprt("+", t);
    break;

  case clang::BO_Sub:
    if (t.is_floatbv())
      new_expr = exprt("ieee_sub", t);
    else
      new_expr = exprt("-", t);
    break;

  case clang::BO_Mul:
    if (t.is_floatbv())
      new_expr = exprt("ieee_mul", t);
    else
      new_expr = exprt("*", t);
    break;

  case clang::BO_Div:
    if (t.is_floatbv())
      new_expr = exprt("ieee_div", t);
    else
      new_expr = exprt("/", t);
    break;

  case clang::BO_Shl:
    new_expr = exprt("shl", t);
    break;

  case clang::BO_Shr:
    new_expr = exprt("shr", t);
    break;

  case clang::BO_Rem:
    new_expr = exprt("mod", t);
    break;

  case clang::BO_And:
    new_expr = exprt("bitand", t);
    break;

  case clang::BO_Xor:
    new_expr = exprt("bitxor", t);
    break;

  case clang::BO_Or:
    new_expr = exprt("bitor", t);
    break;

  case clang::BO_LT:
    new_expr = exprt("<", t);
    break;

  case clang::BO_GT:
    new_expr = exprt(">", t);
    break;

  case clang::BO_LE:
    new_expr = exprt("<=", t);
    break;

  case clang::BO_GE:
    new_expr = exprt(">=", t);
    break;

  case clang::BO_EQ:
    new_expr = exprt("=", t);
    break;

  case clang::BO_NE:
    new_expr = exprt("notequal", t);
    break;

  case clang::BO_LAnd:
    new_expr = exprt("and", t);
    break;

  case clang::BO_LOr:
    new_expr = exprt("or", t);
    break;

  case clang::BO_Assign:
    // If we use code_assignt, it will reserve two operands,
    // and the copy_to_operands method call at the end of
    // this method will put lhs and rhs in positions 2 and 3,
    // instead of 0 and 1 :/
    new_expr = side_effect_exprt("assign", t);
    break;

  case clang::BO_Comma:
    new_expr = exprt("comma", t);
    break;

  default:
  {
    const clang::CompoundAssignOperator &compop =
      static_cast<const clang::CompoundAssignOperator &>(binop);
    return get_compound_assign_expr(compop, new_expr);
  }
  }

  new_expr.copy_to_operands(lhs, rhs);
  return false;
}

bool clang_c_convertert::get_compound_assign_expr(
  const clang::CompoundAssignOperator &compop,
  exprt &new_expr)
{
  switch (compop.getOpcode())
  {
  case clang::BO_AddAssign:
    new_expr = side_effect_exprt("assign+");
    break;

  case clang::BO_SubAssign:
    new_expr = side_effect_exprt("assign-");
    break;

  case clang::BO_MulAssign:
    new_expr = side_effect_exprt("assign*");
    break;

  case clang::BO_DivAssign:
    new_expr = side_effect_exprt("assign_div");
    break;

  case clang::BO_RemAssign:
    new_expr = side_effect_exprt("assign_mod");
    break;

  case clang::BO_ShlAssign:
    new_expr = side_effect_exprt("assign_shl");
    break;

  case clang::BO_ShrAssign:
    new_expr = side_effect_exprt("assign_shr");
    break;

  case clang::BO_AndAssign:
    new_expr = side_effect_exprt("assign_bitand");
    break;

  case clang::BO_XorAssign:
    new_expr = side_effect_exprt("assign_bitxor");
    break;

  case clang::BO_OrAssign:
    new_expr = side_effect_exprt("assign_bitor");
    break;

  default:
  {
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);
    ross << "Conversion of unsupported clang binary operator: \"";
    ross << compop.getOpcodeStr().str() << "\" to expression"
         << "\n";
    compop.dump(ross, *ASTContext);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }
  }

  exprt lhs;
  if (get_expr(*compop.getLHS(), lhs))
    return true;

  exprt rhs;
  if (get_expr(*compop.getRHS(), rhs))
    return true;

  if (get_type(compop.getType(), new_expr.type()))
    return true;

  if (!lhs.type().is_pointer())
    gen_typecast(ns, rhs, lhs.type());

  new_expr.copy_to_operands(lhs, rhs);
  return false;
}

bool clang_c_convertert::get_atomic_expr(
  const clang::AtomicExpr &atm,
  exprt &new_expr)
{
  // We're going to create a fake function call
  side_effect_expr_function_callt fake_call;

  // Get the type
  typet t;
  if (get_type(atm.getType(), t))
    return true;
  fake_call.type() = t;

  // get the name
  std::string name;
  switch (atm.getOp())
  {
  case clang::AtomicExpr::AO__c11_atomic_load:
    name = "__c11_atomic_load";
    break;

  case clang::AtomicExpr::AO__c11_atomic_store:
    name = "__c11_atomic_store";
    break;

  case clang::AtomicExpr::AO__c11_atomic_exchange:
    name = "__c11_atomic_exchange";
    break;

  case clang::AtomicExpr::AO__c11_atomic_compare_exchange_strong:
    name = "__c11_atomic_compare_exchange_strong";
    break;

  case clang::AtomicExpr::AO__c11_atomic_compare_exchange_weak:
    name = "__c11_atomic_compare_exchange_weak";
    break;

  case clang::AtomicExpr::AO__c11_atomic_fetch_add:
    name = "__c11_atomic_fetch_add";
    break;

  case clang::AtomicExpr::AO__c11_atomic_fetch_sub:
    name = "__c11_atomic_fetch_sub";
    break;

  case clang::AtomicExpr::AO__c11_atomic_fetch_and:
    name = "__c11_atomic_fetch_and";
    break;

  case clang::AtomicExpr::AO__c11_atomic_fetch_or:
    name = "__c11_atomic_fetch_or";
    break;

  case clang::AtomicExpr::AO__c11_atomic_fetch_xor:
    name = "__c11_atomic_fetch_xor";
    break;

  case clang::AtomicExpr::AO__atomic_load:
    name = "__atomic_load";
    break;

  case clang::AtomicExpr::AO__atomic_load_n:
    name = "__atomic_load_n";
    break;

  case clang::AtomicExpr::AO__atomic_store:
    name = "__atomic_store";
    break;

  case clang::AtomicExpr::AO__atomic_store_n:
    name = "__atomic_store_n";
    break;

  case clang::AtomicExpr::AO__atomic_exchange:
    name = "__atomic_exchange";
    break;

  case clang::AtomicExpr::AO__atomic_exchange_n:
    name = "__atomic_exchange_n";
    break;

  case clang::AtomicExpr::AO__atomic_compare_exchange:
    name = "__atomic_compare_exchange";
    break;

  case clang::AtomicExpr::AO__atomic_compare_exchange_n:
    name = "__atomic_compare_exchange_n";
    break;

  case clang::AtomicExpr::AO__atomic_fetch_add:
    name = "__atomic_fetch_add";
    break;

  case clang::AtomicExpr::AO__atomic_fetch_sub:
    name = "__atomic_fetch_sub";
    break;

  case clang::AtomicExpr::AO__atomic_fetch_and:
    name = "__atomic_fetch_and";
    break;

  case clang::AtomicExpr::AO__atomic_fetch_or:
    name = "__atomic_fetch_or";
    break;

  case clang::AtomicExpr::AO__atomic_fetch_xor:
    name = "__atomic_fetch_xor";
    break;

  case clang::AtomicExpr::AO__atomic_fetch_nand:
    name = "__atomic_fetch_nand";
    break;

  case clang::AtomicExpr::AO__atomic_add_fetch:
    name = "__atomic_add_fetch";
    break;

  case clang::AtomicExpr::AO__atomic_sub_fetch:
    name = "__atomic_sub_fetch";
    break;

  case clang::AtomicExpr::AO__atomic_and_fetch:
    name = "__atomic_and_fetch";
    break;

  case clang::AtomicExpr::AO__atomic_or_fetch:
    name = "__atomic_or_fetch";
    break;

  case clang::AtomicExpr::AO__atomic_xor_fetch:
    name = "__atomic_xor_fetch";
    break;

  case clang::AtomicExpr::AO__atomic_nand_fetch:
    name = "__atomic_nand_fetch";
    break;

  default:
    log_error("Unknown Atomic expression");
    std::ostringstream oss;
    llvm::raw_os_ostream ross(oss);
    atm.dump(ross, *ASTContext);
    ross.flush();
    log_error("{}", oss.str());
    return true;
  }

  // Get Arguments, ptr is never nullptr
  exprt ptr;
  if (get_expr(*atm.getPtr(), ptr))
    return true;

  fake_call.arguments().push_back(ptr);

  // Val1
  if (
    atm.getOp() != clang::AtomicExpr::AO__c11_atomic_load &&
    atm.getOp() != clang::AtomicExpr::AO__atomic_load_n)
  {
    exprt val1;
    if (get_expr(*atm.getVal1(), val1))
      return true;

    fake_call.arguments().push_back(val1);
  }

  // Val2
  if (atm.getOp() == clang::AtomicExpr::AO__atomic_exchange || atm.isCmpXChg())
  {
    exprt val2;
    if (get_expr(*atm.getVal2(), val2))
      return true;

    fake_call.arguments().push_back(val2);
  }

  // Weak
  if (
    atm.getOp() == clang::AtomicExpr::AO__atomic_compare_exchange ||
    atm.getOp() == clang::AtomicExpr::AO__atomic_compare_exchange_n)
  {
    exprt weak;
    if (get_expr(*atm.getWeak(), weak))
      return true;

    fake_call.arguments().push_back(weak);
  }

  if (atm.getOp() != clang::AtomicExpr::AO__c11_atomic_init)
  {
    exprt order;
    if (get_expr(*atm.getOrder(), order))
      return true;

    fake_call.arguments().push_back(order);
  }

  if (atm.isCmpXChg())
  {
    exprt order_fail;
    if (get_expr(*atm.getOrderFail(), order_fail))
      return true;

    fake_call.arguments().push_back(order_fail);
  }

  fake_call.function() = symbol_exprt("c:@F@" + name, t);
  fake_call.function().name(name);
  new_expr.swap(fake_call);
  return false;
}

void clang_c_convertert::get_default_symbol(
  symbolt &symbol,
  irep_idt module_name,
  typet type,
  irep_idt name,
  irep_idt id,
  locationt location)
{
  symbol.mode = mode;
  symbol.module = module_name;
  symbol.location = std::move(location);
  symbol.type = std::move(type);
  symbol.name = name;
  symbol.id = id;
}

std::string clang_c_convertert::get_decl_name(const clang::NamedDecl &nd)
{
  if (const clang::IdentifierInfo *identifier = nd.getIdentifier())
    return identifier->getName().str();

  std::string name;
  llvm::raw_string_ostream rso(name);
  nd.printName(rso);
  return rso.str();
}

std::string
getFullyQualifiedName(const clang::QualType &t, const clang::ASTContext &c)
{
  clang::PrintingPolicy Policy(c.getPrintingPolicy());
  Policy.SuppressScope = false;
  Policy.AnonymousTagLocations = true;
  Policy.PolishForDeclaration = true;
  Policy.SuppressUnwrittenScope = true;
  return clang::TypeName::getFullyQualifiedName(t, c, Policy);
}

void clang_c_convertert::get_decl_name(
  const clang::NamedDecl &nd,
  std::string &name,
  std::string &id)
{
  id = name = get_decl_name(nd);

  switch (nd.getKind())
  {
  // ParamVarDecl, we can safely ignore them
  case clang::Decl::ParmVar:
    if (name.empty())
      return;
    break;

  case clang::Decl::Field:
    if (name.empty())
    {
      // Anonymous fields, generate a name based on the field index
      const clang::FieldDecl &fd = static_cast<const clang::FieldDecl &>(nd);
      name = "__anon_field_" + std::to_string(fd.getFieldIndex());
      id = name;
    }
    return;

  case clang::Decl::IndirectField:
    if (name.empty())
    {
      // Anonymous fields, generate a name based on the field index
      const clang::IndirectFieldDecl &fd =
        static_cast<const clang::IndirectFieldDecl &>(nd);
      name = "__anon_indirect_field_" +
             std::to_string(fd.getAnonField()->getFieldIndex());
      id = name;
      return;
    }
    break;

  case clang::Decl::Record:
  case clang::Decl::CXXRecord:
  case clang::Decl::ClassTemplateSpecialization:
  {
    const clang::RecordDecl &rd = static_cast<const clang::RecordDecl &>(nd);
    std::string kind_name = rd.getKindName().str();

    // Checking if it is not a typedef, but the tag name is empty. If so we give it a new
    // unique name based on its location
    if (
      rd.getCanonicalDecl()->getNameAsString().empty() &&
      !rd.getCanonicalDecl()->getTypedefNameForAnonDecl())
    {
      locationt location_begin;
      get_location_from_decl(rd, location_begin);
      std::string location_begin_str = location_begin.file().as_string() + "_" +
                                       location_begin.function().as_string() +
                                       "_" + location_begin.line().as_string() +
                                       "_" +
                                       location_begin.column().as_string();
      std::string kind_name = rd.getKindName().str();
      name = kind_name + " __anon_" + kind_name + "_at_" + location_begin_str;
      std::replace(name.begin(), name.end(), '.', '_');
    }
    else if (
      rd.getCanonicalDecl()->getNameAsString().empty() &&
      rd.getCanonicalDecl()->getTypedefNameForAnonDecl())
    {
      locationt location_begin;
      get_location_from_decl(rd, location_begin);
      std::string location_begin_str = location_begin.file().as_string() + "_" +
                                       location_begin.function().as_string() +
                                       "_" + location_begin.line().as_string() +
                                       "_" +
                                       location_begin.column().as_string();
      std::string kind_name = rd.getKindName().str();
      std::string tag_name =
        getFullyQualifiedName(ASTContext->getTagDeclType(&rd), *ASTContext);
      name =
        kind_name + " __anon_typedef_" + tag_name + "_at_" + location_begin_str;
      std::replace(name.begin(), name.end(), '.', '_');
    }
    else
      name =
        getFullyQualifiedName(ASTContext->getTagDeclType(&rd), *ASTContext);

    id = "tag-" + name;
    return;
  }

  case clang::Decl::Var:
    if (name.empty())
    {
      // Anonymous variable, generate a name based on the location,
      // see regression union1
      const clang::VarDecl &vd = static_cast<const clang::VarDecl &>(nd);
      locationt location_begin;
      get_location_from_decl(vd, location_begin);
      std::string location_begin_str = location_begin.file().as_string() + "_" +
                                       location_begin.function().as_string() +
                                       "_" + location_begin.line().as_string() +
                                       "_" +
                                       location_begin.column().as_string();
      name = "__anon_var_at_" + location_begin_str;
      id = getFullyQualifiedName(vd.getType(), *ASTContext);
      return;
    }
    break;
  default:
    if (name.empty())
    {
      std::ostringstream oss;
      llvm::raw_os_ostream ross(oss);
      nd.dump(ross);
      ross.flush();
      log_error("Declaration has an empty name:\n{}", oss.str());
      abort();
    }
  }

  clang::SmallString<128> DeclUSR;
  if (!clang::index::generateUSRForDecl(&nd, DeclUSR))
  {
    id = DeclUSR.str().str();
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

void clang_c_convertert::get_start_location_from_stmt(
  const clang::Stmt &stmt,
  locationt &location)
{
  sm = &ASTContext->getSourceManager();

  std::string function_name;

  if (current_functionDecl)
    function_name = get_decl_name(*current_functionDecl);

  clang::PresumedLoc PLoc;
  get_presumed_location(stmt.getSourceRange().getBegin(), PLoc);

  set_location(PLoc, function_name, location);
}

void clang_c_convertert::get_final_location_from_stmt(
  const clang::Stmt &stmt,
  locationt &location)
{
  sm = &ASTContext->getSourceManager();

  std::string function_name;

  if (current_functionDecl)
    function_name = get_decl_name(*current_functionDecl);

  clang::PresumedLoc PLoc;
  get_presumed_location(stmt.getSourceRange().getEnd(), PLoc);

  set_location(PLoc, function_name, location);
}

void clang_c_convertert::get_location_from_decl(
  const clang::Decl &decl,
  locationt &location)
{
  sm = &ASTContext->getSourceManager();

  std::string function_name;

  if (decl.getDeclContext()->isFunctionOrMethod())
  {
    const clang::FunctionDecl &funcd =
      static_cast<const clang::FunctionDecl &>(*decl.getDeclContext());

    function_name = get_decl_name(funcd);
  }

  clang::PresumedLoc PLoc;
  get_presumed_location(decl.getSourceRange().getBegin(), PLoc);

  set_location(PLoc, function_name, location);
}

void clang_c_convertert::get_presumed_location(
  const clang::SourceLocation &loc,
  clang::PresumedLoc &PLoc)
{
  if (!sm)
    return;

  clang::SourceLocation FileLoc = sm->getFileLoc(loc);
  bool use_line_directives = true;
#if ESBMC_SVCOMP
  /* Do not use #line directives, because the GraphML witness format appearently
   * wants to use the physical line in the pre-processed .i file; at least
   * CPAchecker and UAutomizer do. */
  use_line_directives = false;
#endif
  PLoc = sm->getPresumedLoc(FileLoc, use_line_directives);
}

void clang_c_convertert::set_location(
  clang::PresumedLoc &PLoc,
  std::string &function_name,
  locationt &location)
{
  if (PLoc.isInvalid())
  {
    location.set_file("<invalid sloc>");
    return;
  }

  location.set_line(PLoc.getLine());
  location.set_file(get_filename_from_path(PLoc.getFilename()));
  location.set_column(PLoc.getColumn());

  if (!function_name.empty())
    location.set_function(function_name);
}

std::string clang_c_convertert::get_modulename_from_path(std::string path)
{
  std::string filename = get_filename_from_path(path);

  if (filename.find_last_of('.') != std::string::npos)
    return filename.substr(0, filename.find_last_of('.'));

  return filename;
}

std::string clang_c_convertert::get_filename_from_path(std::string path)
{
  if (path.find_last_of('/') != std::string::npos)
    return path.substr(path.find_last_of('/') + 1);

  return path;
}

void clang_c_convertert::convert_expression_to_code(exprt &expr)
{
  if (expr.is_code())
    return;

  codet code("expression");
  code.location() = expr.location();
  code.move_to_operands(expr);

  expr.swap(code);
}

const clang::Decl *
clang_c_convertert::get_DeclContext_from_Stmt(const clang::Stmt &stmt)
{
  auto parents = ASTContext->getParents(stmt);
  auto it = parents.begin();
  if (it == parents.end())
    return nullptr;

  const clang::Decl *aDecl = it->get<clang::Decl>();
  if (aDecl)
    return aDecl;

  const clang::Stmt *aStmt = it->get<clang::Stmt>();
  if (aStmt)
    return get_DeclContext_from_Stmt(*aStmt);

  return nullptr;
}

const clang::Decl *
clang_c_convertert::get_top_FunctionDecl_from_Stmt(const clang::Stmt &stmt)
{
  const clang::Decl *decl = get_DeclContext_from_Stmt(stmt);
  if (decl)
  {
    if (decl->isFunctionOrFunctionTemplate())
      return decl;

    if (decl->getNonClosureContext()->isFunctionOrFunctionTemplate())
      return decl->getNonClosureContext();
  }

  return nullptr;
}

bool clang_c_convertert::check_alignment_attributes(
  const clang::FieldDecl *field,
  struct_typet::componentt &comp)
{
  if (field->hasAttrs())
  {
    const auto &attrs = field->getAttrs();
    for (const auto &attr : attrs)
    {
      if (attr->getKind() == clang::attr::Aligned)
      {
        const clang::AlignedAttr &aattr =
          static_cast<const clang::AlignedAttr &>(*attr);

        if (aattr.isAlignmentExpr())
        {
          // This is usually a constant
          clang::Expr *alignExpr = aattr.getAlignmentExpr();
          exprt alignment;
          if (alignExpr && get_expr(*(aattr.getAlignmentExpr()), alignment))
            return true;
          comp.type().set("alignment", alignment);
        }
        else
        {
          // I was not able to find an example to test this, so abort for now
          log_error("ESBMC currently does not support type alignments");
          std::ostringstream oss;
          llvm::raw_os_ostream ross(oss);
          aattr.getAlignmentType()->getType()->dump(ross, *ASTContext);
          ross.flush();
          log_error("{}", oss.str());
          return true;
        }
      }
    }
  }
  return false;
}

bool clang_c_convertert::is_field_global_storage(const clang::FieldDecl *field)
{
  if (const clang::VarDecl *nd = llvm::dyn_cast<clang::VarDecl>(field))
    return (nd->hasGlobalStorage());

  return false;
}

bool clang_c_convertert::perform_virtual_dispatch(const clang::MemberExpr &)
{
  // It just can't happen in C
  return false;
}

bool clang_c_convertert::is_fd_virtual_or_overriding(
  const clang::FunctionDecl &)
{
  // It just can't happen in C
  return false;
}

bool clang_c_convertert::get_vft_binding_expr(
  const clang::MemberExpr &,
  exprt &)
{
  log_error(
    "MemberExpr call to virtual/overriding function cannot happen in C");
  abort();
  return true;
}

void clang_c_convertert::get_ref_to_struct_type(typet &type)
{
  /*
   * For some special cases, we need to get a symbol type referring to
   * a struct/union/class type so that we don't have to copy it, e.g.
   * A pointer to an object of class BLAH would have:
   * * type: pointer
   *   * subtype: symbol
   *      * identifier: tag-BLAH
   * instead of copying the struct type:
   * * type: pointer
   *   * subtype: struct
   *      * tag: BLAH
   *      * components:
   *        * <BLAH components 0,1,2,3...>
   *      * methods:
   *        * <BLAH method 0,1,2,3...>
   */
  if (type.is_struct() || type.is_union())
  {
    struct_union_typet t = to_struct_union_type(type);
    type = symbol_typet(tag_prefix + t.tag().as_string());
  }
}

bool clang_c_convertert::is_aggregate_type(const clang::QualType &)
{
  return false;
}

bool clang_c_convertert::is_member_decl_static(const clang::MemberExpr &member)
{
  // follow the MemberExpr node (might be nested) to check
  // whether it's ultimately referring to a static member decl
  // which is essentially a VarDecl with static life time
  // Note that in a nested MemberExpr node, e.g. `X.Y.data`
  // `getMemberDecl` will give the ultimate MemberDecl representing `data`.
  if (member.getMemberDecl()->getKind() == clang::Decl::Var)
  {
    const clang::VarDecl &vd =
      static_cast<const clang::VarDecl &>(*member.getMemberDecl());
    return (vd.getStorageClass() == clang::SC_Static) || vd.hasGlobalStorage();
  }

  return false;
}
