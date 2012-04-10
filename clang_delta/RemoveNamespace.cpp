//===----------------------------------------------------------------------===//
//
// Copyright (c) 2012 The University of Utah
// All rights reserved.
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RemoveNamespace.h"

#include <sstream>
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"

#include "TransformationManager.h"

using namespace clang;
using namespace llvm;

static const char *DescriptionMsg =
"Remove namespaces. This pass tries to remove namespace without \
introducing name conflicts. \n";

static RegisterTransformation<RemoveNamespace>
         Trans("remove-namespace", DescriptionMsg);

class RemoveNamespaceASTVisitor : public 
  RecursiveASTVisitor<RemoveNamespaceASTVisitor> {

public:
  explicit RemoveNamespaceASTVisitor(RemoveNamespace *Instance)
    : ConsumerInstance(Instance)
  { }

  bool VisitNamespaceDecl(NamespaceDecl *ND);

private:
  RemoveNamespace *ConsumerInstance;

};

// A visitor for rewriting decls in the namespace being removed
// ISSUE: quite a lot of functionality could be provided by the
//        RenameClassRewriteVisitor from RenameClass.cpp. 
//        I have certain hesitation of factoring out 
//        RenameClassRewriteVisitor for common uses. 
//        A couple of reasons:
//        * RenameClassRewriteVisitor is only suitable for renaming
//          classes, but here we will be facing more types, e.g., enum.
//        * RenameClassRewriteVisitor handles one class, but here
//          we need to rename multiple conflicting classes;
//        * I don't want to make two transformations interference with
//          each other
//        Therefore, we will have some code duplication.
class RemoveNamespaceRewriteVisitor : public 
  RecursiveASTVisitor<RemoveNamespaceRewriteVisitor> {

public:
  explicit RemoveNamespaceRewriteVisitor(RemoveNamespace *Instance)
    : ConsumerInstance(Instance)
  { }

  bool VisitNamespaceDecl(NamespaceDecl *ND);

  bool VisitNamespaceAliasDecl(NamespaceAliasDecl *D);

  bool VisitUsingDecl(UsingDecl *D);

  bool VisitUsingDirectiveDecl(UsingDirectiveDecl *D);

  bool VisitCXXConstructorDecl(CXXConstructorDecl *CtorDecl);

  bool VisitCXXDestructorDecl(CXXDestructorDecl *DtorDecl);

  bool VisitCallExpr(CallExpr *CE);

private:
  RemoveNamespace *ConsumerInstance;

};

bool RemoveNamespaceASTVisitor::VisitNamespaceDecl(NamespaceDecl *ND)
{
  ConsumerInstance->handleOneNamespaceDecl(ND);
  return true;
}

bool RemoveNamespaceRewriteVisitor::VisitNamespaceDecl(NamespaceDecl *ND)
{
  const NamespaceDecl *CanonicalND = ND->getCanonicalDecl();
  if (CanonicalND != ConsumerInstance->TheNamespaceDecl)
    return true;

  ConsumerInstance->removeNamespace(ND);
  return true;
}

bool RemoveNamespaceRewriteVisitor::VisitUsingDirectiveDecl(
       UsingDirectiveDecl *D)
{
  if (ConsumerInstance->UselessUsingDirectiveDecls.count(D))
    ConsumerInstance->RewriteHelper->removeDecl(D);

  const NamespaceDecl *CanonicalND = 
    D->getNominatedNamespace()->getCanonicalDecl();
  
  if (CanonicalND == ConsumerInstance->TheNamespaceDecl)
    ConsumerInstance->RewriteHelper->removeDecl(D);
  return true;
}

bool RemoveNamespaceRewriteVisitor::VisitUsingDecl(UsingDecl *D)
{
  if (ConsumerInstance->UselessUsingDecls.count(D))
    ConsumerInstance->RewriteHelper->removeDecl(D);

  // check if this UsingDecl refers to the namespaced being removed
  const NestedNameSpecifier *NNS = D->getQualifier();
  NestedNameSpecifier::SpecifierKind Kind = NNS->getKind();
  
  switch (Kind) {
  case NestedNameSpecifier::Namespace: {
    const NamespaceDecl *CanonicalND = 
      NNS->getAsNamespace()->getCanonicalDecl();
    if (CanonicalND == ConsumerInstance->TheNamespaceDecl)
      ConsumerInstance->RewriteHelper->removeDecl(D);
    break;
  }

  case NestedNameSpecifier::NamespaceAlias: {
    const NamespaceAliasDecl *NAD = NNS->getAsNamespaceAlias();
    const NamespaceDecl *CanonicalND = 
      NAD->getNamespace()->getCanonicalDecl();
    if (CanonicalND == ConsumerInstance->TheNamespaceDecl)
      ConsumerInstance->RewriteHelper->removeDecl(D);
    break;
  }

  case NestedNameSpecifier::Global: 
    // Nothing to do 
    break;

  default:
    TransAssert(0 && "Bad NestedNameSpecifier!");
  }
  return true;
}

bool RemoveNamespaceRewriteVisitor::VisitNamespaceAliasDecl(
       NamespaceAliasDecl *D)
{
  const NamespaceDecl *CanonicalND = 
    D->getNamespace()->getCanonicalDecl();
  if (CanonicalND == ConsumerInstance->TheNamespaceDecl)
    ConsumerInstance->RewriteHelper->removeDecl(D);
  return true;
}

bool RemoveNamespaceRewriteVisitor::VisitCXXConstructorDecl
       (CXXConstructorDecl *CtorDecl)
{
  const DeclContext *Ctx = CtorDecl->getDeclContext();
  const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(Ctx);
  TransAssert(CXXRD && "Invalid CXXRecordDecl");

  std::string Name;
  if (ConsumerInstance->getNewName(CXXRD, Name))
    ConsumerInstance->RewriteHelper->replaceFunctionDeclName(CtorDecl, Name);

  return true;
}

bool RemoveNamespaceRewriteVisitor::VisitCXXDestructorDecl(
       CXXDestructorDecl *DtorDecl)
{
  const DeclContext *Ctx = DtorDecl->getDeclContext();
  const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(Ctx);
  TransAssert(CXXRD && "Invalid CXXRecordDecl");

  std::string Name;
  if (!ConsumerInstance->getNewName(CXXRD, Name))
    return true;

  // Avoid duplicated VisitDtor. 
  // For example, in the code below:
  // template<typename T>
  // class SomeClass {
  // public:
  //   ~SomeClass<T>() {}
  // };
  // ~SomeClass<T>'s TypeLoc is represented as TemplateSpecializationTypeLoc
  // In this case, ~SomeClass will be renamed from 
  // VisitTemplateSpecializationTypeLoc.
  DeclarationNameInfo NameInfo = DtorDecl->getNameInfo();
  if ( TypeSourceInfo *TSInfo = NameInfo.getNamedTypeInfo()) {
    TypeLoc DtorLoc = TSInfo->getTypeLoc();
    if (!DtorLoc.isNull() && 
        (DtorLoc.getTypeLocClass() == TypeLoc::TemplateSpecialization))
      return true;
  }

  Name = "~" + Name;
  ConsumerInstance->RewriteHelper->replaceFunctionDeclName(DtorDecl, Name);

  return true;
}

bool RemoveNamespaceRewriteVisitor::VisitCallExpr(CallExpr *CE)
{
  std::string Name;
  if ( CXXMemberCallExpr *CXXCE = dyn_cast<CXXMemberCallExpr>(CE)) {
    const CXXRecordDecl *CXXRD = CXXCE->getRecordDecl();
    // getRecordDEcl could return NULL if getImplicitObjectArgument() 
    // returns NULL
    if (!CXXRD)
      return true;

    if (ConsumerInstance->getNewName(CXXRD, Name))
      ConsumerInstance->RewriteHelper->replaceCXXDtorCallExpr(CXXCE, Name);
    return true;
  }

  const FunctionDecl *FD = CE->getDirectCallee();
  if (ConsumerInstance->getNewName(FD, Name)) {
    ConsumerInstance->TheRewriter.ReplaceText(CE->getLocStart(),
      FD->getNameAsString().size(), Name);
  }
  return true;
}

void RemoveNamespace::Initialize(ASTContext &context) 
{
  Transformation::Initialize(context);
  CollectionVisitor = new RemoveNamespaceASTVisitor(this);
  RewriteVisitor = new RemoveNamespaceRewriteVisitor(this);
}

bool RemoveNamespace::HandleTopLevelDecl(DeclGroupRef D) 
{
  // Nothing to do
  return true;
}
 
void RemoveNamespace::HandleTranslationUnit(ASTContext &Ctx)
{
  if (TransformationManager::isCLangOpt()) {
    ValidInstanceNum = 0;
  }
  else {
    // Invoke CollectionVisitor here because we need full DeclContext
    // to resolve name conflicts. Full ASTs has been built at this point.
    CollectionVisitor->TraverseDecl(Ctx.getTranslationUnitDecl());
  }

  if (QueryInstanceOnly)
    return;

  if (TransformationCounter > ValidInstanceNum) {
    TransError = TransMaxInstanceError;
    return;
  }

  TransAssert(RewriteVisitor && "NULL RewriteVisitor!");
  TransAssert(TheNamespaceDecl && "NULL TheNamespaceDecl!");
  Ctx.getDiagnostics().setSuppressAllDiagnostics(false);

  rewriteNamedDecls();
  RewriteVisitor->TraverseDecl(Ctx.getTranslationUnitDecl());

  if (Ctx.getDiagnostics().hasErrorOccurred() ||
      Ctx.getDiagnostics().hasFatalErrorOccurred())
    TransError = TransInternalError;
}

void RemoveNamespace::rewriteNamedDecls(void)
{
  for (NamedDeclToNameMap::const_iterator I = NamedDeclToNewName.begin(),
       E = NamedDeclToNewName.end(); I != E; ++I) {
    const NamedDecl *D = (*I).first;
    std::string Name = (*I).second;

    // Check replaceFunctionDecl in RewriteUtils.cpp for the reason that
    // we need a special case for FunctionDecl
    if ( const FunctionDecl *FD = dyn_cast<FunctionDecl>(D) ) {
      RewriteHelper->replaceFunctionDeclName(FD, Name);
    }
    else {
      RewriteHelper->replaceNamedDeclName(D, Name);
    }
  }
}

bool RemoveNamespace::hasNameConflict(const NamedDecl *ND, 
                                      const DeclContext *ParentCtx)
{
  DeclarationName Name = ND->getDeclName();
  DeclContextLookupConstResult Result = ParentCtx->lookup(Name);
  return (Result.first != Result.second);
}

// A using declaration in the removed namespace could cause
// name conflict, e.g.,
// namespace NS1 {
//   void foo(void) {}
// }
// namespace NS2 {
//   using NS1::foo;
//   void bar() { ... foo(); ... }
// }
// void foo() {...}
// void func() {... foo(); ...}
// if we remove NS2, then foo() in func() will become ambiguous.
// In this case, we need to replace the first invocation of foo()
// with NS1::foo()
void RemoveNamespace::handleOneUsingShadowDecl(const UsingShadowDecl *UD,
                                               const DeclContext *ParentCtx)
{
  const NamedDecl *ND = UD->getTargetDecl();
  if (!hasNameConflict(ND, ParentCtx))
    return;
  
  const DeclContext *NDCtx = 
    ND->getDeclContext()->getEnclosingNamespaceContext();
  TransAssert(NDCtx && "Bad DeclContext!");
  const NamespaceDecl *NDNamespace = dyn_cast<NamespaceDecl>(NDCtx);
  TransAssert(NDNamespace && "Bad Namespace!");
  const IdentifierInfo *IdInfo = ND->getIdentifier();
  
  std::string NewName;
  const UsingDecl *D = UD->getUsingDecl();
  NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc();

  getQualifierAsString(QualifierLoc, NewName);
  NewName += IdInfo->getName();
  UsingNamedDeclToNewName[ND] = NewName;
  
  // the tied UsingDecl becomes useless, and hence it's removable
  UselessUsingDecls.insert(D);
}

// For the similar reason as dealing with using declarations,
// we need to resolve the possible name conflicts introduced by
// using directives
void RemoveNamespace::handleOneUsingDirectiveDecl(const UsingDirectiveDecl *UD,
                                                  const DeclContext *ParentCtx)
{
  const NamespaceDecl *ND = UD->getNominatedNamespace();
  TransAssert(!ND->isAnonymousNamespace() && 
              "Cannot have anonymous namespaces!");
  std::string NamespaceName = ND->getNameAsString();

  bool Removable = true;
  for (DeclContext::decl_iterator I = ND->decls_begin(), E = ND->decls_end();
       I != E; ++I) {
    const NamedDecl *NamedD = dyn_cast<NamedDecl>(*I);
    if (!NamedD)
      continue;

    if (!isa<TemplateDecl>(NamedD) && !isa<TypeDecl>(NamedD) && 
        !isa<ValueDecl>(NamedD))
      continue;

    if (!hasNameConflict(NamedD, ParentCtx)) {
      Removable = false;
      continue;
    }

    const IdentifierInfo *IdInfo = NamedD->getIdentifier();
    std::string NewName;
    NestedNameSpecifierLoc QualifierLoc = UD->getQualifierLoc();

    getQualifierAsString(QualifierLoc, NewName);
    NewName += NamedD->getNameAsString();
    NewName += "::";
    NewName += IdInfo->getName();
    UsingNamedDeclToNewName[NamedD] = NewName;
  }

  if (Removable)
    UselessUsingDirectiveDecls.insert(UD);
}

void RemoveNamespace::handleOneNamedDecl(const NamedDecl *ND,
                                         const DeclContext *ParentCtx,
                                         const std::string &NamespaceName)
{
  Decl::Kind K = ND->getKind();
  switch (K) {
  case Decl::UsingShadow: {
    const UsingShadowDecl *D = dyn_cast<UsingShadowDecl>(ND);
    handleOneUsingShadowDecl(D, ParentCtx);
    break;
  }

  case Decl::UsingDirective: {
    const UsingDirectiveDecl *D = dyn_cast<UsingDirectiveDecl>(ND);
    handleOneUsingDirectiveDecl(D, ParentCtx);
    break;
  }

  default:
    if (isa<NamespaceAliasDecl>(ND) || isa<TemplateDecl>(ND) ||
        isa<TypeDecl>(ND) || isa<ValueDecl>(ND)) {
      if (!hasNameConflict(ND, ParentCtx))
        break;

      std::string NewName = NamePrefix + NamespaceName;
      const IdentifierInfo *IdInfo = ND->getIdentifier();
      NewName += "_";
      NewName += IdInfo->getName();
      NamedDeclToNewName[ND] = NewName;
    }
  }
}

void RemoveNamespace::addNamedDeclsFromNamespace(const NamespaceDecl *ND)
{
  // We don't care about name-lookup for friend's functions, so just
  // retrive ParentContext rather than LookupParent
  const DeclContext *ParentCtx = ND->getParent();
  std::string NamespaceName;

  if (ND->isAnonymousNamespace()) {
    std::stringstream TmpSS;
    TmpSS << AnonNamePrefix << AnonNamespaceCounter;
    NamespaceName = TmpSS.str();
    AnonNamespaceCounter++;
  }
  else {
    NamespaceName = ND->getNameAsString();
  }

  for (DeclContext::decl_iterator I = ND->decls_begin(), E = ND->decls_end();
       I != E; ++I) {
    if ( const NamedDecl *D = dyn_cast<NamedDecl>(*I) )
      handleOneNamedDecl(D, ParentCtx, NamespaceName);
  }
}

bool RemoveNamespace::handleOneNamespaceDecl(NamespaceDecl *ND)
{
  NamespaceDecl *CanonicalND = ND->getCanonicalDecl();
  if (VisitedND.count(CanonicalND)) {
    if (TheNamespaceDecl == CanonicalND) {
      addNamedDeclsFromNamespace(ND);
    }
    return true;
  }

  VisitedND.insert(CanonicalND);
  ValidInstanceNum++;
  if (ValidInstanceNum == TransformationCounter) {
    TheNamespaceDecl = CanonicalND;
    addNamedDeclsFromNamespace(ND);
  }
  return true;
}

void RemoveNamespace::removeNamespace(const NamespaceDecl *ND)
{
  // Remove the right brace first
  SourceLocation StartLoc = ND->getRBraceLoc();
  TheRewriter.RemoveText(StartLoc, 1);

  // Then remove name and the left brace
  StartLoc = ND->getLocStart();
  TransAssert(StartLoc.isValid() && "Invalid Namespace LocStart!");

  const char *StartBuf = SrcManager->getCharacterData(StartLoc);
  SourceRange NDRange = ND->getSourceRange();
  int RangeSize = TheRewriter.getRangeSize(NDRange);
  TransAssert((RangeSize != -1) && "Bad Namespace Range!");

  std::string NDStr(StartBuf, RangeSize);
  size_t Pos = NDStr.find('{');
  TransAssert((Pos != std::string::npos) && "Cannot find LBrace!");
  SourceLocation EndLoc = StartLoc.getLocWithOffset(Pos);
  TheRewriter.RemoveText(SourceRange(StartLoc, EndLoc));
}

void RemoveNamespace::getQualifierAsString(NestedNameSpecifierLoc Loc,
                                           std::string &Str)
{
  SourceLocation StartLoc = Loc.getBeginLoc();
  TransAssert(StartLoc.isValid() && "Bad StartLoc for NestedNameSpecifier!");
  unsigned Len = Loc.getDataLength();
  const char *StartBuf = SrcManager->getCharacterData(StartLoc);
  Str.assign(StartBuf, Len);
}

bool RemoveNamespace::getNewNameFromNameMap(const NamedDecl *ND, 
                                            const NamedDeclToNameMap &NameMap,
                                            std::string &Name)
{
  NamedDeclToNameMap::const_iterator Pos = 
    NameMap.find(ND);
  if (Pos == NameMap.end())
    return false;

  Name = (*Pos).second;
  return true;
}

bool RemoveNamespace::getNewNamedDeclName(const NamedDecl *ND,
                                          std::string &Name)
{
  return getNewNameFromNameMap(ND, NamedDeclToNewName, Name);
}

bool RemoveNamespace::getNewUsingNamedDeclName(const NamedDecl *ND,
                                               std::string &Name)
{
  return getNewNameFromNameMap(ND, UsingNamedDeclToNewName, Name);
}

bool RemoveNamespace::getNewName(const NamedDecl *ND, std::string &Name)
{
  if (getNewNamedDeclName(ND, Name))
    return true;

  return getNewUsingNamedDeclName(ND, Name);
}

RemoveNamespace::~RemoveNamespace(void)
{
  if (CollectionVisitor)
    delete CollectionVisitor;
  if (RewriteVisitor)
    delete RewriteVisitor;
}

