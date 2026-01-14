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

#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

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

void RefactorHandler::logChange(const std::string &changeType, const std::string &location,
                                const std::string &details) {
    if (LogStream) {
        *LogStream << "[CHANGE] " << changeType << " | Location: " << location << " | Details: " << details
                   << std::endl;
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

    bool isBaseClass = false;

    // Check if this class is used as a base class by looking for inheritance in the AST
    ASTContext &Context = Dtor->getASTContext();
    for (auto *GlobalDecl : Context.getTranslationUnitDecl()->decls()) {
        if (auto *DerivedClass = dyn_cast<CXXRecordDecl>(GlobalDecl)) {
            // Only check classes that have definitions
            if (!DerivedClass->hasDefinition())
                continue;

            for (const auto &Base : DerivedClass->bases()) {
                const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
                if (BaseDecl && BaseDecl == Parent) {
                    isBaseClass = true;
                    break;
                }
            }
        }
        if (isBaseClass)
            break;
    }

    if (!isBaseClass) {
        return;  // Only add virtual to destructors of classes that are base classes
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

    // Log the change
    std::string filename = SM.getFilename(dtorLoc).str();
    unsigned lineNo = SM.getSpellingLineNumber(dtorLoc);
    std::string location = filename + ":" + std::to_string(lineNo);
    logChange("VIRTUAL_DESTRUCTOR", location, "Added 'virtual' to destructor");

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
        while (afterParen < endPtr &&
               (*afterParen == ' ' || *afterParen == '\t' || *afterParen == '\n' || *afterParen == '\r')) {
            afterParen++;
        }

        // Look for common qualifiers that should come before 'override'
        if (afterParen < endPtr) {
            // Define pairs of qualifier strings and their lengths
            constexpr std::array<std::string_view, 5> qualifiers = {"const", "noexcept", "final", "&", "&&"};

            bool foundQualifier = false;
            for (const auto &qualifier : qualifiers) {
                if (static_cast<size_t>(endPtr - afterParen) >= qualifier.size() &&
                    std::string_view(afterParen, qualifier.size()) == qualifier) {
                    // Check if the match is followed by a non-alphanumeric character to ensure exact match
                    if (qualifier.size() + afterParen >= endPtr || !isalnum(*(afterParen + qualifier.size()))) {
                        // Insert override after the qualifier
                        insertLoc = startLoc.getLocWithOffset((afterParen + qualifier.size()) - startPtr);
                        foundQualifier = true;
                        break;
                    }
                }
            }

            if (!foundQualifier) {
                // Insert override right after the closing parenthesis
                insertLoc = startLoc.getLocWithOffset(closingParen - startPtr + 1);
            }
        }

        // Insert 'override' at the calculated location
        Rewrite.InsertTextAfter(insertLoc, " override");

        // Log the change
        std::string filename = SM.getFilename(Method->getLocation()).str();
        unsigned lineNo = SM.getSpellingLineNumber(Method->getLocation());
        std::string location = filename + ":" + std::to_string(lineNo);
        logChange("OVERRIDE_METHOD", location, "Added 'override' to method " + Method->getNameAsString());

        const unsigned SuccessID =
            Diag.getCustomDiagID(DiagnosticsEngine::Note, "Добавлено 'override' после закрывающей скобки");
        Diag.Report(insertLoc, SuccessID);
    } else {
        // Fallback: insert at the end of the method's source range
        Rewrite.InsertTextBefore(endLoc, " override");

        // Log the change
        std::string filename = SM.getFilename(Method->getLocation()).str();
        unsigned lineNo = SM.getSpellingLineNumber(Method->getLocation());
        std::string location = filename + ":" + std::to_string(lineNo);
        logChange("OVERRIDE_METHOD", location,
                  "Added 'override' to method " + Method->getNameAsString() + " (fallback)");

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

    // Log the change
    std::string filename = SM.getFilename(LoopVar->getLocation()).str();
    unsigned lineNo = SM.getSpellingLineNumber(LoopVar->getLocation());
    std::string location = filename + ":" + std::to_string(lineNo);
    logChange("RANGE_FOR_REFERENCE", location, "Added '&' to variable " + LoopVar->getNameAsString());

    const unsigned SuccessID = Diag.getCustomDiagID(DiagnosticsEngine::Note, "Добавлено '&' после типа");
    Diag.Report(insertLoc, SuccessID);
}

auto NvDtorMatcher() {
    // Find destructors that are not virtual in classes that have derived classes
    // This is complex to do with a single matcher, so we'll match all non-virtual destructors
    // and check in the handler if the class has derived classes
    return traverse(clang::TK_IgnoreUnlessSpelledInSource,
                    cxxDestructorDecl(unless(isVirtual())).bind("nonVirtualDtor"));
}

auto NoOverrideMatcher() {
    return traverse(clang::TK_IgnoreUnlessSpelledInSource,
                    cxxMethodDecl(isOverride(),                           // Method overrides a base method
                                  unless(hasAttr(clang::attr::Override))  // But doesn't have override keyword
                                  )
                        .bind("missingOverride"));
}

auto NoRefConstVarInRangeLoopMatcher() {
    return traverse(clang::TK_IgnoreUnlessSpelledInSource,
                    varDecl(hasAncestor(cxxForRangeStmt()), hasType(qualType(isConstQualified())),
                            unless(hasType(referenceType())), unless(hasType(qualType(builtinType()))))
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