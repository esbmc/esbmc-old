#include <util/compiler_defs.h>
// Remove warnings from Clang headers
CC_DIAGNOSTIC_PUSH()
CC_DIAGNOSTIC_IGNORE_LLVM_CHECKS()
#include <clang/AST/ASTImporter.h>
#include <clang/Basic/Version.inc>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Options.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Option/ArgList.h>
#if CLANG_VERSION_MAJOR < 16
#  include <llvm/Support/Host.h>
#else
#  include <llvm/TargetParser/Host.h>
#endif
#include <llvm/Support/Path.h>
CC_DIAGNOSTIC_POP()

#include <clang-c-frontend/AST/build_ast.h>
#include <clang-c-frontend/AST/esbmc_action.h>

/// Builds a clang driver initialized for running clang tools.
static clang::driver::Driver *newDriver(
  clang::DiagnosticsEngine *Diagnostics,
  const char *BinaryName,
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS)
{
  clang::driver::Driver *CompilerDriver;
/* Clang's API changed between version 11 and 12 in that it now requires a name
 * to construct a Driver. */
#if CLANG_VERSION_MAJOR >= 12
  CompilerDriver = new clang::driver::Driver(
    BinaryName,
    llvm::sys::getDefaultTargetTriple(),
    *Diagnostics,
    "clang_based_tool",
    std::move(VFS));
#else
  CompilerDriver = new clang::driver::Driver(
    BinaryName,
    llvm::sys::getDefaultTargetTriple(),
    *Diagnostics,
    std::move(VFS));
  CompilerDriver->setTitle("clang_based_tool");
#endif
  return CompilerDriver;
}

std::unique_ptr<clang::ASTUnit> buildASTs(
  const std::string &intrinsics,
  const std::vector<std::string> &compiler_args)
{
  // Create virtual file system to add clang's headers
  llvm::IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> OverlayFileSystem(
    new llvm::vfs::OverlayFileSystem(llvm::vfs::getRealFileSystem()));

  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
    new llvm::vfs::InMemoryFileSystem);
  OverlayFileSystem->pushOverlay(InMemoryFileSystem);

  llvm::IntrusiveRefCntPtr<clang::FileManager> Files(
    new clang::FileManager(clang::FileSystemOptions(), OverlayFileSystem));

  // Create everything needed to create a CompilerInvocation,
  // copied from ToolInvocation::run
  llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> DiagOpts =
    new clang::DiagnosticOptions();

  std::vector<const char *> Argv;
  for (const std::string &Str : compiler_args)
    Argv.push_back(Str.c_str());
  const char *const BinaryName = Argv[0];

  unsigned MissingArgIndex, MissingArgCount;
  llvm::opt::InputArgList ParsedArgs =
    clang::driver::getDriverOptTable().ParseArgs(
      llvm::ArrayRef<const char *>(Argv).slice(1),
      MissingArgIndex,
      MissingArgCount);

  clang::ParseDiagnosticArgs(*DiagOpts, ParsedArgs);

  clang::TextDiagnosticPrinter DiagnosticPrinter(llvm::errs(), &*DiagOpts);

  clang::DiagnosticsEngine *Diagnostics = new clang::DiagnosticsEngine(
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs>(new clang::DiagnosticIDs()),
    &*DiagOpts,
    &DiagnosticPrinter,
    false);

  const std::unique_ptr<clang::driver::Driver> Driver(
    newDriver(Diagnostics, BinaryName, &Files->getVirtualFileSystem()));

  // Since the input might only be virtual, don't check whether it exists.
  Driver->setCheckInputsExist(false);
  const std::unique_ptr<clang::driver::Compilation> Compilation(
    Driver->BuildCompilation(llvm::ArrayRef<const char *>(Argv)));

  const clang::driver::JobList &Jobs = Compilation->getJobs();
  assert(Jobs.size() == 1);

  const llvm::opt::ArgStringList *const CC1Args = &Jobs.begin()->getArguments();

  std::shared_ptr<clang::CompilerInvocation> Invocation(
    clang::tooling::newInvocation(Diagnostics, *CC1Args, BinaryName));

  // Show the invocation, with -v.
  if (Invocation->getHeaderSearchOpts().Verbose)
  {
    llvm::errs() << "clang Invocation:\n";
    Compilation->getJobs().Print(llvm::errs(), "\n", true);
    llvm::errs() << "\n";
  }

  // Create our custom action
  auto action = new esbmc_action(std::move(intrinsics));

  // Create ASTUnit
  std::unique_ptr<clang::ASTUnit> unit(
    clang::ASTUnit::LoadFromCompilerInvocationAction(
      std::move(Invocation),
      std::make_shared<clang::PCHContainerOperations>(),
      Diagnostics,
      action));
  assert(unit);

  // The action is only used locally, we can delete it now
  // See: https://clang.llvm.org/doxygen/ASTUnit_8cpp_source.html#l01510
  delete (action);

  return unit;
}

void mergeASTs(
  const std::unique_ptr<clang::ASTUnit> &FromUnit,
  std::unique_ptr<clang::ASTUnit> &ToUnit)
{
  // Call enableSourceFileDiagnostics on the
  // ASTUnit objects to get diagnostics.
  FromUnit->enableSourceFileDiagnostics();
  ToUnit->enableSourceFileDiagnostics();

  clang::ASTImporter Importer(
    ToUnit->getASTContext(),
    ToUnit->getFileManager(),
    FromUnit->getASTContext(),
    FromUnit->getFileManager(),
    false);

  Importer.setODRHandling(clang::ASTImporter::ODRHandlingType::Liberal);

  for (auto decl : FromUnit->getASTContext().getTranslationUnitDecl()->decls())
  {
    llvm::Expected<clang::Decl *> ImportedOrErr = Importer.Import(decl);
    if (!ImportedOrErr)
    {
      llvm::Error Err = ImportedOrErr.takeError();
      llvm::errs() << "Error: " << Err << "\n";
      consumeError(std::move(Err));
    }
  }
}
