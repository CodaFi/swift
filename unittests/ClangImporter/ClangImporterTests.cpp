#include "swift/AST/ASTContext.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/SearchPathOptions.h"
#include "swift/Basic/LLVMInitialize.h"
#include "swift/Basic/LangOptions.h"
#include "swift/Basic/SourceManager.h"
#include "swift/ClangImporter/ClangImporter.h"
#include "swift/ClangImporter/ClangImporterOptions.h"
#include "swift/Subsystems.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace swift;

static std::string createFilename(StringRef base, StringRef name) {
  SmallString<256> path = base;
  llvm::sys::path::append(path, name);
  return llvm::Twine(path).str();
}

static bool emitFileWithContents(StringRef path, StringRef contents,
                                 std::string *pathOut = nullptr) {
  int FD;
  if (llvm::sys::fs::openFileForWrite(path, FD))
    return true;
  if (pathOut)
    *pathOut = path.str();
  llvm::raw_fd_ostream file(FD, /*shouldClose=*/true);
  file << contents;
  return false;
}

static bool emitFileWithContents(StringRef base, StringRef name,
                                 StringRef contents,
                                 std::string *pathOut = nullptr) {
  return emitFileWithContents(createFilename(base, name), contents, pathOut);
}

TEST(ClangImporterTest, emitPCHInMemory) {
  // Create a temporary cache on disk and clean it up at the end.
  ClangImporterOptions options;
  SmallString<256> temp;
  ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory(
      "ClangImporterTest.emitPCHInMemory", temp));
  SWIFT_DEFER { llvm::sys::fs::remove_directories(temp); };

  // Create a cache subdirectory for the modules and PCH.
  std::string cache = createFilename(temp, "cache");
  ASSERT_FALSE(llvm::sys::fs::create_directory(cache));
  options.ModuleCachePath = cache;
  options.PrecompiledHeaderOutputDir = cache;

  // Create the includes.
  std::string include = createFilename(temp, "include");
  ASSERT_FALSE(llvm::sys::fs::create_directory(include));
  options.ExtraArgs.emplace_back("-nosysteminc");
  options.ExtraArgs.emplace_back((llvm::Twine("-I") + include).str());
  ASSERT_FALSE(emitFileWithContents(include, "module.modulemap",
                                    "module A {\n"
                                    "  header \"A.h\"\n"
                                    "}\n"));
  ASSERT_FALSE(emitFileWithContents(include, "A.h", "int foo(void);\n"));

  // Create a bridging header.
  ASSERT_FALSE(emitFileWithContents(temp, "bridging.h", "#import <A.h>\n",
                                    &options.BridgingHeader));

  // Set up the importer and emit a bridging PCH.
  swift::LangOptions langOpts;
  langOpts.Target = llvm::Triple("x86_64", "apple", "darwin");
  swift::TypeCheckerOptions typeckOpts;
  INITIALIZE_LLVM();
  swift::SearchPathOptions searchPathOpts;
  swift::SourceManager sourceMgr;
  swift::DiagnosticEngine diags(sourceMgr);
  std::unique_ptr<ASTContext> context(
      ASTContext::get(langOpts, typeckOpts, searchPathOpts, sourceMgr, diags));
  auto importer = ClangImporter::create(*context, options);

  std::string PCH = createFilename(cache, "bridging.h.pch");
  ASSERT_FALSE(importer->canReadPCH(PCH));
  ASSERT_FALSE(importer->emitBridgingPCH(options.BridgingHeader, PCH));
  ASSERT_TRUE(importer->canReadPCH(PCH));

  // Overwrite the PCH with garbage.  We should still be able to read it from
  // the in-memory cache.
  ASSERT_FALSE(emitFileWithContents(PCH, "garbage"));
  ASSERT_TRUE(importer->canReadPCH(PCH));
}

static void withConfiguredClangImporter(std::string cache, std::string include,
                                        llvm::function_ref<void(ClangImporterOptions &)> cont) {
  ClangImporterOptions options;
  options.ModuleCachePath = cache;
  options.PrecompiledHeaderOutputDir = cache;
  options.ExtraArgs.emplace_back("-nosysteminc");
  options.ExtraArgs.emplace_back((llvm::Twine("-I") + include).str());
  options.ExtraArgs.emplace_back("-DEXTRA_C_DEFINE=2");
  return cont(options);
}

TEST(ClangImporterTest, thrashPCHCache) {
  // Create a temporary cache on disk and clean it up at the end.
  SmallString<256> temp;
  ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory(
      "ClangImporterTest.emitPCHInMemory", temp));
  SWIFT_DEFER { llvm::sys::fs::remove_directories(temp); };

  // Create a cache subdirectory for the modules and PCH.
  std::string cache = createFilename(temp, "cache");
  std::string include = createFilename(temp, "include");
  ASSERT_FALSE(llvm::sys::fs::create_directory(include));
  ASSERT_FALSE(llvm::sys::fs::create_directory(cache));

  // Create the includes.
  ASSERT_FALSE(emitFileWithContents(include, "module.modulemap",
                                    "module CLib {\n"
                                    "  umbrella header \"" + include + "/Clib.h\"\n"
                                    "  export * \n"
                                    "}\n"));
  ASSERT_FALSE(emitFileWithContents(include, "CLib.h",
                                    "#if !defined(EXTRA_C_DEFINE) || EXTRA_C_DEFINE != 2\n"
                                    "#error \"unexpected compiler flags\"\n"
                                    "#endif\n"
                                    "void foo(void);\n"));
  std::vector<std::thread> threads;
  std::mutex sharedMutex;
  // Set up the importer and emit a bridging PCH.
  swift::LangOptions langOpts;
  langOpts.Target = llvm::Triple("x86_64", "apple", "darwin");
  swift::TypeCheckerOptions typeckOpts;
  INITIALIZE_LLVM();
  swift::SearchPathOptions searchPathOpts;
  swift::SourceManager sourceMgr;
  swift::DiagnosticEngine diags(sourceMgr);
  std::unique_ptr<ASTContext> context(
      ASTContext::get(langOpts, typeckOpts, searchPathOpts, sourceMgr, diags));
  swift::registerParseRequestFunctions(context->evaluator);
  std::vector<ClangImporter *> importers;
  auto CLib = context->getIdentifier("CLib");
  for (auto i = 0; i < 10; ++i) {
    withConfiguredClangImporter(cache, include, [&](ClangImporterOptions &options) {
      importers.push_back(ClangImporter::create(*context, options).release());
    });
  }

  for (auto *importer : importers) {
    threads.push_back(std::thread([&](){
      importer->loadModule({}, { Located<Identifier>{ CLib, {} } });
      {
        std::lock_guard<std::mutex> lock(sharedMutex);
        llvm::sys::fs::remove_directories(cache);
      }
    }));
  }

  for (auto &thread : threads) {
    thread.join();
  }
}
