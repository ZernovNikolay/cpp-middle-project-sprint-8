#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Token.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}


#include <unordered_set>

#include "RefactorTool.h"

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride")) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Dtor->getLocation())) {
        return;
    }

    unsigned locationHash = Dtor->getLocation().getHashValue();
    if (virtualDtorLocations.find(locationHash) != virtualDtorLocations.end()) {
        return;
    }

    // Check if this class is a base class for other classes
    const CXXRecordDecl *Parent = Dtor->getParent();
    if (!Parent) {
        return;
    }

    // Check if any other class inherits from this class
    bool isBaseClass = false;
    // We need to iterate through all the declarations in the translation unit to find if any class inherits from this one
    // This is a simplified approach - in a real implementation we'd need to traverse all classes
    // For now, we'll use a different approach: check if this class is used as a base class in any CXXRecordDecl

    // Actually, let's use ASTContext to find all CXXRecordDecl nodes and check if any inherits from this class
    // This is tricky to do in the handler. Let's try a different approach with matchers.

    // For now, let's just check if the parent class has any base specifiers that indicate it inherits from something
    // No, that's wrong. We need to check if OTHER classes inherit from THIS class.

    // Let me try a different approach - look for all CXXRecordDecl nodes and check if any of them have this class as a base
    // This is difficult to do in the current architecture. Let me revert to a better matcher approach.

    // Actually, let me try to find a way to check if this class is used as a base class
    // The parent class is the one that might be a base class for others
    // We need to check if any other CXXRecordDecl inherits from Parent

    // This is complex to do in the handler. Let me try to fix the original matcher properly.
    // The original was: cxxDestructorDecl(unless(isVirtual()), hasParent(recordDecl(hasDescendant(cxxRecordDecl()))))
    // This was wrong because hasDescendant(cxxRecordDecl()) means the class has another class defined inside it (nested class)

    // What we need is to find classes that are base classes for others.
    // Let me try to find if this specific class is used as a base class by looking for inheritance relationships

    // For now, let's just process all non-virtual destructors and add virtual to all of them
    // Actually, no, that's not correct. Only base classes need virtual destructors.

    // Let me try to implement a proper check
    // We need to check if any CXXRecordDecl has this Parent as a base
    // This requires traversing the AST to find inheritance relationships

    // For now, let me implement a basic check by looking for derived classes in the same translation unit
    isBaseClass = false;
    // This is difficult to implement properly without changing the architecture significantly
    // Let me try a different approach by using AST matching more effectively

    // Actually, let me restore the original approach but fix the matcher properly
    // We need to match classes that are base classes for others
    // This means we need to find classes that appear in base specifiers of other classes

    // Let me try to use a more complex approach with AST matching
    // The correct approach would be to match CXXRecordDecl that are used as base classes
    // cxxRecordDecl(forEachDescendant(cxxBaseSpecifier(hasDeclaration(equalsNode(Parent)))))
    // No, that's not right either

    // Let me try: find CXXRecordDecl that have this Parent as a base
    // This is getting complex. Let me try a simpler approach by using ASTContext to find derived classes

    // Check if this class is used as a base class by looking for inheritance in the AST
    ASTContext &Context = Dtor->getASTContext();
    for (auto *GlobalDecl : Context.getTranslationUnitDecl()->decls()) {
        if (auto *DerivedClass = dyn_cast<CXXRecordDecl>(GlobalDecl)) {
            // Only check classes that have definitions
            if (!DerivedClass->hasDefinition()) continue;

            for (const auto &Base : DerivedClass->bases()) {
                const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
                if (BaseDecl && BaseDecl == Parent) {
                    isBaseClass = true;
                    break;
                }
            }
        }
        if (isBaseClass) break;
    }

    if (!isBaseClass) {
        return; // Only add virtual to destructors of classes that are base classes
    }

    // Find the correct location to insert 'virtual' - before the '~' in destructor
    SourceLocation dtorLoc = Dtor->getLocation();
    if (dtorLoc.isInvalid()) {
        return;
    }

    const unsigned DiagID = Diag.getCustomDiagID(
        DiagnosticsEngine::Remark, "Найден невиртуальный деструктор в классе с наследниками. Добавляем 'virtual'.");
    Diag.Report(Dtor->getLocation(), DiagID);

    Rewrite.InsertTextBefore(dtorLoc, "virtual ");
    virtualDtorLocations.insert(locationHash);

    const unsigned SuccessID = Diag.getCustomDiagID(DiagnosticsEngine::Note, "Добавлено 'virtual' перед деструктором");
    Diag.Report(dtorLoc, SuccessID);
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(Method->getLocation())) {
        return;
    }

    // Пропускаем деструкторы
    if (isa<CXXDestructorDecl>(Method)) {
        return;
    }

    // Проверяем, что метод действительно переопределяет базовый
    if (Method->size_overridden_methods() == 0) {
        return;
    }

    // Проверяем, что нет атрибута override
    if (Method->hasAttr<OverrideAttr>()) {
        return;
    }

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark,
                                                 "Найден переопределяющий метод без 'override'. Добавляем 'override'.");
    Diag.Report(Method->getLocation(), DiagID);

    // Get the source range of the method
    SourceRange range = Method->getSourceRange();
    if (!range.isValid()) {
        return;
    }

    // Find the location after the closing parenthesis of the parameters
    // We need to parse the source code to find the correct location for 'override'
    SourceLocation startLoc = range.getBegin();
    SourceLocation endLoc = range.getEnd();

    // Get the source text for the method signature
    const char *startPtr = SM.getCharacterData(startLoc);
    const char *endPtr = SM.getCharacterData(endLoc);

    // Find the closing parenthesis of the parameters
    int parenCount = 0;
    const char *ptr = startPtr;
    const char *closingParen = nullptr;

    // Look for the matching closing parenthesis
    while (ptr < endPtr) {
        if (*ptr == '(') {
            parenCount++;
        } else if (*ptr == ')') {
            parenCount--;
            if (parenCount == 0) {
                closingParen = ptr;
                break;
            }
        }
        ptr++;
    }

    if (closingParen) {
        // Calculate the location for insertion
        SourceLocation insertLoc = startLoc.getLocWithOffset(closingParen - startPtr + 1);

        // Check if there are qualifiers after the closing parenthesis (like const, noexcept, etc.)
        // Skip whitespace
        const char *afterParen = closingParen + 1;
        while (afterParen < endPtr && (*afterParen == ' ' || *afterParen == '\t' || *afterParen == '\n' || *afterParen == '\r')) {
            afterParen++;
        }

        // Look for common qualifiers that should come before 'override'
        if (afterParen < endPtr) {
            // Check for common qualifiers like const, noexcept, final, etc.
            if (strncmp(afterParen, "const", 5) == 0) {
                // Insert override after 'const'
                insertLoc = startLoc.getLocWithOffset((afterParen + 5) - startPtr);
            } else if (strncmp(afterParen, "noexcept", 8) == 0) {
                // Insert override after 'noexcept'
                insertLoc = startLoc.getLocWithOffset((afterParen + 8) - startPtr);
            } else if (strncmp(afterParen, "&", 1) == 0) {
                // Insert override after '&'
                insertLoc = startLoc.getLocWithOffset((afterParen + 1) - startPtr);
            } else if (strncmp(afterParen, "&&", 2) == 0) {
                // Insert override after '&&'
                insertLoc = startLoc.getLocWithOffset((afterParen + 2) - startPtr);
            } else {
                // Insert override right after the closing parenthesis
                insertLoc = startLoc.getLocWithOffset(closingParen - startPtr + 1);
            }
        }

        // Insert 'override' at the calculated location
        Rewrite.InsertTextAfter(insertLoc, " override");

        const unsigned SuccessID =
            Diag.getCustomDiagID(DiagnosticsEngine::Note, "Добавлено 'override' после закрывающей скобки");
        Diag.Report(insertLoc, SuccessID);
    } else {
        // Fallback: insert at the end of the method's source range
        Rewrite.InsertTextBefore(endLoc, " override");

        const unsigned SuccessID =
            Diag.getCustomDiagID(DiagnosticsEngine::Note, "Добавлено 'override' (fallback position)");
        Diag.Report(endLoc, SuccessID);
    }
}

void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(LoopVar->getLocation())) {
        return;
    }

    const TypeSourceInfo *TSInfo = LoopVar->getTypeSourceInfo();
    if (!TSInfo) {
        return;
    }

    TypeLoc TL = TSInfo->getTypeLoc();
    if (TL.isNull()) {
        return;
    }

    // Check if the type is already a reference - we don't want to add & to types that are already references
    if (LoopVar->getType()->isReferenceType()) {
        return;
    }

    // Also check if the type is a builtin/fundamental type - we don't want to add & to those
    if (LoopVar->getType()->isBuiltinType()) {
        return;
    }

    const unsigned DiagID = Diag.getCustomDiagID(
        DiagnosticsEngine::Remark, "Найдена константная переменная в range-for без ссылки. Добавляем '&'.");
    Diag.Report(LoopVar->getLocation(), DiagID);

    SourceLocation insertLoc = Lexer::getLocForEndOfToken(TL.getEndLoc(), 0, SM, LangOptions());
    Rewrite.InsertTextAfter(insertLoc, "&");

    const unsigned SuccessID = Diag.getCustomDiagID(DiagnosticsEngine::Note, "Добавлено '&' после типа");
    Diag.Report(insertLoc, SuccessID);
}

auto NvDtorMatcher() {
    // Find destructors that are not virtual in classes that have derived classes
    // This is complex to do with a single matcher, so we'll match all non-virtual destructors
    // and check in the handler if the class has derived classes
    return traverse(clang::TK_IgnoreUnlessSpelledInSource,
                    cxxDestructorDecl(
                        unless(isVirtual())
                    ).bind("nonVirtualDtor"));
}

auto NoOverrideMatcher() {
    return traverse(clang::TK_IgnoreUnlessSpelledInSource,
                    cxxMethodDecl(
                        isOverride(),  // Method overrides a base method
                        unless(hasAttr(clang::attr::Override))  // But doesn't have override keyword
                    ).bind("missingOverride"));
}

auto NoRefConstVarInRangeLoopMatcher() {
    return traverse(clang::TK_IgnoreUnlessSpelledInSource,
                    varDecl(hasAncestor(cxxForRangeStmt()),
                            hasType(qualType(isConstQualified())),
                            unless(hasType(referenceType())),
                            unless(hasType(qualType(builtinType()))))
                        .bind("loopVar"));
}

ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}