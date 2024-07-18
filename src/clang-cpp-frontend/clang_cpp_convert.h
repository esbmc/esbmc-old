#ifndef CLANG_CPP_FRONTEND_CLANG_CPP_CONVERT_H_
#define CLANG_CPP_FRONTEND_CLANG_CPP_CONVERT_H_

#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS

#include <clang-c-frontend/clang_c_convert.h>

class clang_cpp_convertert : public clang_c_convertert
{
public:
  clang_cpp_convertert(
    contextt &_context,
    std::unique_ptr<clang::ASTUnit> &_AST,
    irep_idt _mode);
  virtual ~clang_cpp_convertert() = default;

protected:
  // this_map contains key-value pairs in the form of <method address, <identifier, type>>
  typedef std::unordered_map<std::size_t, std::pair<std::string, typet>>
    this_mapt;
  this_mapt this_map;

  bool get_decl(const clang::Decl &decl, exprt &new_expr) override;

  void get_decl_name(
    const clang::NamedDecl &nd,
    std::string &name,
    std::string &id) override;

  /**
   *  Get reference to a declared variable or function, e.g:
   *   - getting the callee for CXXConstructExpr
   *   - getting the object for DeclRefExpr
   */
  bool get_decl_ref(const clang::Decl &decl, exprt &new_expr) override;

  bool get_type(const clang::QualType &type, typet &new_type) override;

  bool get_type(const clang::Type &the_type, typet &new_type) override;

  bool get_method(const clang::CXXMethodDecl &md, exprt &new_expr);

  /**
   *  Get reference for constructor callsite
   */
  bool get_constructor_call(
    const clang::CXXConstructExpr &constructor_call,
    exprt &new_expr);

  bool get_function_body(
    const clang::FunctionDecl &fd,
    exprt &new_expr,
    const code_typet &ftype) override;

  /**
   *  Get function params for C++
   *  contains conversion routines specific to C++ class member functions
   */
  bool get_function_params(
    const clang::FunctionDecl &fd,
    code_typet::argumentst &params) override;

  void name_param_and_continue(
    const clang::ParmVarDecl &pd,
    std::string &id,
    std::string &name,
    exprt &param) override;

  std::string constref_suffix = "ref";

  /**
   *  Add implicit `this' when parsing C++ class member functions, e.g:
   *  class t1 { int i; t1(){i = 1} };
   *  The implicit `this' within the constructor is represented by symbol:
   *   - t1() symbol.type:   void (t1*)
   *   - t1() symbol.value:  { t1->i = 1; }
   */
  bool get_function_this_pointer_param(
    const clang::CXXMethodDecl &fd,
    code_typet::argumentst &params);

  bool get_struct_union_class(const clang::RecordDecl &rd) override;

  bool get_var(const clang::VarDecl &vd, exprt &new_expr) override;

  bool get_struct_union_class_fields(
    const clang::RecordDecl &rd,
    struct_union_typet &type) override;

  bool get_struct_union_class_methods_decls(
    const clang::RecordDecl &rd,
    typet &type) override;

  /*
   * Deal with ClassTemplateDecl or FunctionTemplateDecl or
   * a class or function template declaration respectively.
   * For C++14 and above, this function might be extended to deal
   * with VarTemplateDecl for variable template.
  */
  template <typename TemplateDecl>
  bool get_template_decl(
    const TemplateDecl &D,
    bool DumpExplicitInst,
    exprt &new_expr);

  template <typename SpecializationDecl>
  bool get_template_decl_specialization(
    const SpecializationDecl *D,
    bool DumpExplicitInst,
    exprt &new_expr);

  bool get_expr(const clang::Stmt &stmt, exprt &new_expr) override;

  void
  build_member_from_component(const clang::FunctionDecl &fd, exprt &component);

  /*
   * Add additional annotations for class/struct/union fields
   * Arguments:
   *  field: clang AST node representing the field we are dealing with
   *  type:  ESBMC's typet representing the type of the class we are currently dealing with
   *  comp: the `component` representing the field
   */
  bool annotate_class_field(
    const clang::FieldDecl &field,
    const struct_union_typet &type,
    struct_typet::componentt &comp);

  /*
   * Add access in class symbol type's component:
   *  0: component:
   *    * access: public
   */
  bool annotate_class_field_access(
    const clang::FieldDecl &field,
    struct_typet::componentt &comp);

  /*
   * Get access from any clang Decl (field, method .etc)
   */
  bool get_access_from_decl(const clang::Decl &decl, std::string &access);

  /*
   * Get the symbol from the context for a C++ function
   *
   * Returns:
   *  fd_symb: pointer of the function symbol
   * Arguments:
   *  fd: Clang AST for this function declaration
   */
  symbolt *get_fd_symbol(const clang::FunctionDecl &fd);

  /*
   * Add additional annotations for class/struct/union methods:
   * 0: component
   *    * type: code
   *      * #member_name: tag-MyClasss
   *      * return_type: constructor
   *    * from_base: 1
   *    * access: public
   *    * name: MyMethodName
   *    * #location:
   * Arguments:
   *  cxxmdd:   clang AST node representing the method we are dealing with
   *  new_expr: the `component` in class/struct/union symbol type
   *  fd: clang AST node representing the function declaration we are dealing with
   */
  bool
  annotate_class_method(const clang::CXXMethodDecl &cxxmdd, exprt &new_expr);
  /*
   * Flag copy constructor.
   *
   * Arguments:
   *  cxxmdd: clang AST node representing the constructor we are dealing with
   *  rtn_type: the corresponding return type node
   */
  void annotate_cpyctor(const clang::CXXMethodDecl &cxxmdd, typet &rtn_type);
  /*
   * Flag return type in ctor or dtor, e.g.
   * A default copy constructor would have the return type below:
   * * return_type: constructor
   *   #default_copy_cons: 1
   *
   * Arguments:
   *  cxxmdd: clang AST node representing the ctor/dtor we are dealing with
   *  rtn_type: the corresponding return type node
   */
  void annotate_ctor_dtor_rtn_type(
    const clang::CXXMethodDecl &cxxmdd,
    typet &rtn_type);
  bool is_cpyctor(const clang::DeclContext &dcxt);
  bool is_defaulted_ctor(const clang::CXXMethodDecl &md);

  /*
   * When getting a function call to ctor, we might call the base ctor from a derived class ctor
   * Need to wrap derived class `this` in a typecast expr and convert to the base `this`, e.g.:
   *    Base( (Base*) this)
   *
   * Arguments:
   *  callee_decl: base ctor symbol
   *  call: function call statement to the base ctor
   *  initializer: this is an intermediate data structure containing the information of derived `this`
   */
  void gen_typecast_base_ctor_call(
    const exprt &callee_decl,
    side_effect_expr_function_callt &call,
    exprt &initializer);

  /*
   * This is an ancillary function deciding whether we need
   * need to build an new_object when dealing with constructor_call
   */
  bool need_new_object(
    const clang::Stmt *parentStmt,
    const clang::CXXConstructExpr &call);

  /*
   * Methods to pull bases in
   */
  using base_map = std::map<std::string, const clang::CXXRecordDecl &>;
  /*
   * Recursively get the bases for this derived class.
   *
   * Params:
   *  - cxxrd: clang AST representing the class/struct we are currently dealing with
   *  - map: this map contains all base class(es) of this class std::map<class_id, pointer to clang AST of base class>
   */
  bool get_base_map(const clang::CXXRecordDecl &cxxrd, base_map &map);
  /*
   * Check whether we've already got this component in a class type
   * Avoid copying duplicate component from a base class type to the derived class type.
   *
   * Params:
   *  - component: the component to be copied from a base class to the derived class type
   *  - type: ESBMC IR representing the derived class type
   */
  bool is_duplicate_component(
    const struct_typet::componentt &component,
    const struct_union_typet &type);
  /*
   * Check whether we've already got this method in a class type
   * Avoid copying duplicate method from a base class type to the derived class type.
   *
   * Params:
   *  - method: the method to be copied from a base class to the derived class type
   *  - type: ESBMC IR representing the derived class type
   */
  bool is_duplicate_method(
    const struct_typet::componentt &method,
    const struct_union_typet &type);
  /*
   * Copy components and methods from base class(es) to the derived class type
   * For virtual base class, we only copy it once.
   *
   * Params:
   *  - map: this map contains all base class(es) of this class std::map<class_id, pointer to clang AST of base class>
   *  - type: ESBMC IR representing the class' type
   */
  void get_base_components_methods(base_map &map, struct_union_typet &type);

  /*
   * Methods for virtual tables and virtual pointers
   *  TODO: add link to wiki page
   */
  std::string vtable_type_prefix = "virtual_table::";
  std::string vtable_ptr_suffix = "@vtable_pointer";
  // if a class/struct has vptr component, it needs to be initialized in ctor
  bool has_vptr_component = false;
  std::string thunk_prefix = "thunk::";
  using function_switch = std::map<irep_idt, exprt>;
  using switch_table = std::map<irep_idt, function_switch>;
  using overriden_map = std::map<std::string, const clang::CXXMethodDecl &>;
  /*
   * traverse methods to:
   *  1. convert virtual methods and add them to class' type
   *  2. If there is no existing vtable type symbol,
   *    add virtual table type symbol in the context
   *    and the virtual pointer.
   *    Then add a new entry in the vtable.
   *  3. If vtable type symbol exists, do not add it or the vptr,
   *    just add a new entry in the existing vtable.
   *  4. If method X overrides a base method,
   *    add a thunk function that does late casting of the `this` parameter
   *    and redirects the call to the overriding function (i.e. method X itself)
   *
   *  Last but not least, set up the vtable varaible symbols (i.e. these are the struct variables
   *  instantiated from the vtable type symbols)
   */
  bool get_struct_class_virtual_methods(
    const clang::CXXRecordDecl &cxxrd,
    struct_typet &type);
  /*
   * additional annotations for virtual or overriding methods
   *
   * Params:
   *  - md: the virtual method AST we are currently dealing with
   *  - comp: the `component` as in `components` list in class type IR
   *    this `component` represents the type of the virtual method
   */
  bool annotate_virtual_overriding_methods(
    const clang::CXXMethodDecl &md,
    struct_typet::componentt &comp);
  /*
   * Check the existence of virtual table type symbol.
   * If it exists, return its pointer. Otherwise, return nullptr.
   */
  symbolt *check_vtable_type_symbol_existence(const struct_typet &type);
  /*
   * Add virtual table(vtable) type symbol.
   * This is added as a type symbol in the symbol table.
   *
   * This is done the first time when we encounter a virtual method in a class
   */
  symbolt *add_vtable_type_symbol(
    const struct_typet::componentt &comp,
    struct_typet &type);
  /*
   * Add virtual pointer(vptr).
   * Vptr is NOT a symbol but rather simply added as a component to the class' type.
   *
   * This is done the first time we encounter a virtual method in a class
   */
  void add_vptr(struct_typet &type);
  /*
   * Add an entry to the virtual table type
   *
   * This is done when NOT the first time we encounter a virtual method in a class
   * in which case we just want to add a new entry to the virtual table type
   */
  void add_vtable_type_entry(
    struct_typet &type,
    struct_typet::componentt &comp,
    symbolt *vtable_type_symbol);

  /*
   * Get the overriden methods to which we need to create thunks.
   *
   * Params:
   *  - md: clang AST of the overriding method
   *  - map: key: a map that takes method id as key and pointer to the overriden method AST
   */
  void
  get_overriden_methods(const clang::CXXMethodDecl &md, overriden_map &map);
  /*
   * add a thunk function for each overriding method
   *
   * Params:
   *  - md: clang AST of the overriden method in base class
   *  - component: ESBMC IR representing the the overriding method in derived class' type
   *  - type: ESBMC IR representing the derived class' type
   */
  void add_thunk_method(
    const clang::CXXMethodDecl &md,
    const struct_typet::componentt &component,
    struct_typet &type);
  /*
   * change the type of 'this' pointer from derived class type to base class type
   */
  void update_thunk_this_type(
    typet &thunk_symbol_type,
    const std::string &base_class_id);
  /*
   * Add symbol for arguments of the thunk method
   * Params:
   *  - thunk_func_symb: function symbol for the thunk method
   */
  void add_thunk_method_arguments(symbolt &thunk_func_symb);
  /*
   * Add thunk function body
   * Params:
   *  - thunk_func_symb: function symbol for the thunk method
   *  - component: ESBMC IR representing the the overriding method in derived class' type
   */
  void add_thunk_method_body(
    symbolt &thunk_func_symb,
    const struct_typet::componentt &component);
  /*
   * Add thunk body that contains return value
   * Params:
   *  - thunk_func_symb: function symbol for the thunk method
   *  - component: ESBMC IR representing the the overriding method in derived class' type
   *  - late_cast_this: late casting of `this`
   */
  void add_thunk_method_body_return(
    symbolt &thunk_func_symb,
    const struct_typet::componentt &component,
    const typecast_exprt &late_cast_this);
  /*
   * Add thunk body that does NOT contain return value
   * Params:
   *  - thunk_func_symb: function symbol for the thunk method
   *  - component: ESBMC IR representing the the overriding method in derived class' type
   *  - late_cast_this: late casting of `this`
   */
  void add_thunk_method_body_no_return(
    symbolt &thunk_func_symb,
    const struct_typet::componentt &component,
    const typecast_exprt &late_cast_this);
  /*
   * Add thunk function as a `method` in the derived class' type
   * Params:
   *  - thunk_func_symbol: thunk function symbol
   *  - type: derived class' type
   *  - comp: `component` representing the overriding function
   */
  void add_thunk_component_to_type(
    const symbolt &thunk_func_symb,
    struct_typet &type,
    const struct_typet::componentt &comp);
  /*
   * Set an intuitive name to thunk function
   */
  void
  set_thunk_name(symbolt &thunk_func_symb, const std::string &base_class_id);
  /*
   * Recall that we mode the virtual function table as struct of function pointers.
   * This function adds the symbols for these struct variables.
   *
   * Params:
   *  - cxxrd: clang AST node representing the class/struct we are currently dealing with
   *  - type: ESBMC IR representing the type the class/struct we are currently dealing with
   *  - vft_value_map: representing the vtable value maps for this class/struct we are currently dealing with
   */
  void setup_vtable_struct_variables(
    const clang::CXXRecordDecl &cxxrd,
    const struct_typet &type);
  /*
   * This function builds the vtable value map -
   * a map representing the function switch table
   * with each key-value pair in the form of:
   *  Class X : {VirtualName Y : FunctionID}
   *
   * where X represents the name of a virtual/thunk/overriding function and function ID represents the
   * actual function we are calling when calling the virtual/thunk/overriding function
   * via a Class X* pointer, something like:
   *   xptr->Y()
   *
   * Params:
   *  - struct_type: ESBMC IR representing the type of the class/struct we are currently dealing with
   *  - vtable_value_map: representing the vtable value maps for this class/struct we are currently dealing with
   */
  void build_vtable_map(
    const struct_typet &struct_type,
    switch_table &vtable_value_map);
  /*
   * Create the vtable variable symbols and add them to the symbol table.
   * Each vtable variable represents the actual function switch table, which
   * is modelled as a struct of function pointers, e.g.:
   *  Vtable tag.Base@Base =
   *    {
   *      .do_something = &TagBase::do_someting();
   *    };
   *
   * Params:
   *  - cxxrd: clang AST node representing the class/struct we are currently dealing with
   *  - struct_type: ESBMC IR representing the type the class/struct we are currently dealing with
   *  - vtable_value_map: representing the vtable value maps for this class/struct we are currently dealing with
   */
  void add_vtable_variable_symbols(
    const clang::CXXRecordDecl &cxxrd,
    const struct_typet &struct_type,
    const switch_table &vtable_value_map);

  /*
   * Methods for resolving a clang::MemberExpr to virtual/overriding method
   */
  bool perform_virtual_dispatch(const clang::MemberExpr &member) override;
  bool is_md_virtual_or_overriding(const clang::CXXMethodDecl &cxxmd);
  bool is_fd_virtual_or_overriding(const clang::FunctionDecl &fd) override;
  bool get_vft_binding_expr(const clang::MemberExpr &member, exprt &new_expr)
    override;
  /*
   * Get the base dereference for VFT bound MemberExpr
   *
   * Params:
   *  - member: the method to which this MemberExpr refers
   *  - new_expr: ESBMC IR to represent x dereferencing as in `x->X@vtable_ptr->F`
   */
  bool
  get_vft_binding_expr_base(const clang::MemberExpr &member, exprt &new_expr);
  /*
   * Get the vtable poitner dereferencing for VFT bound MemberExpr
   *
   * Params:
   *  - member: the method to which this MemberExpr refers
   *  - new_expr: ESBMC IR to represent X@Vtable_ptr dereferencing as in `x->X@vtable_ptr->F`
   *  - base_deref: the base dereferencing expression incorporated in vtable pointer dereferencing expression
   */
  void get_vft_binding_expr_vtable_ptr(
    const clang::MemberExpr &member,
    exprt &new_expr,
    const exprt &base_deref);
  /*
   * Get the Function for VFT bound MemberExpr
   *
   * Params:
   *  - member: the method to which this MemberExpr refers
   *  - new_expr: ESBMC IR to represent F in `x->X@vtable_ptr->F`
   *  - vtable_ptr_deref: the vtable pointer dereferencing expression
   */
  bool get_vft_binding_expr_function(
    const clang::MemberExpr &member,
    exprt &new_expr,
    const exprt &vtable_ptr_deref);

  bool is_aggregate_type(const clang::QualType &q_type) override;
  /*
   * check if a method is Copy assignment Operator or 
   * Move assignment Operator
   * Arguments:
   *  md: clang AST representing a C++ method
   */
  bool is_CopyOrMoveOperator(const clang::CXXMethodDecl &md);
  /*
   * check if a method is constructor or destructor
   * Arguments:
   *  md: clang AST representing a C++ method
   */
  bool is_ConstructorOrDestructor(const clang::CXXMethodDecl &md);
  /*
   * Check if expr is a temporary object
   * if not, convert expr to a temporary object
   * Arguments:
   *  expr: ESBMC IR to represent Function call
   */
  void make_temporary(exprt &expr);
};

#endif /* CLANG_C_FRONTEND_CLANG_C_CONVERT_H_ */
