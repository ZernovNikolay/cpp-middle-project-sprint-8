#pragma once
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>
#include <fstream>

class RefactorHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    explicit RefactorHandler(clang::Rewriter &Rewrite) : Rewrite(Rewrite), LogStream(nullptr) {
        // Initialize log file
        LogFile.open("refactor_log.txt", std::ios::out | std::ios::app);
        if (LogFile.is_open()) {
            LogStream = &LogFile;
        }
    }

    ~RefactorHandler() {
        if (LogStream) {
            LogStream->flush();
        }
        // Don't close LogFile explicitly to avoid issues with ASAN
        // The file will be closed when the object is destroyed
    }

    // Метод run вызывается для каждого совпадения с матчем.
    // Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
    virtual void run(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

private:

    // 1. Невиртуальные деструкторы
    void handle_nv_dtor(const clang::CXXDestructorDecl *Dtor,
                              clang::DiagnosticsEngine &Diag,
                              clang::SourceManager &SM);

    // 2. Методы без override
    void handle_miss_override(const clang::CXXMethodDecl *Method,
                                    clang::DiagnosticsEngine &Diag,
                                    clang::SourceManager &SM);

    // 3. range-for без &
    void handle_crange_for(const    clang::VarDecl *LoopVar,
                                    clang::DiagnosticsEngine &Diag,
                                    clang::SourceManager &SM);

    // Логирование изменений
    void logChange(const std::string &changeType, const std::string &location, const std::string &details);

private:
    clang::Rewriter &Rewrite;
    std::unordered_set<unsigned> virtualDtorLocations; // Для хранения позиций деструкторов, к которым уже добавлен virtual
    std::ofstream LogFile;
    std::ostream *LogStream;
};

class ComplexConsumer : public clang::ASTConsumer {
public:
    // Конструктор принимает Rewriter для изменения кода.
    explicit ComplexConsumer(clang::Rewriter &Rewrite);
    // Метод HandleTranslationUnit вызывается для каждого файла.
    void HandleTranslationUnit(clang::ASTContext &Context) override;
private:
    RefactorHandler Handler;           // Обработчик матчеров.
    clang::ast_matchers::MatchFinder Finder;            // MatchFinder для поиска узлов AST.
};

class CodeRefactorAction : public clang::ASTFrontendAction {
public:
  // Returns our ASTConsumer per translation unit.
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI, clang::StringRef file) override;
  virtual bool BeginSourceFileAction( clang::CompilerInstance &CI) override;
  virtual void EndSourceFileAction() override;

private:
  clang::Rewriter RewriterForCodeRefactor;
};