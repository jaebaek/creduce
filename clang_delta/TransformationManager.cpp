//===----------------------------------------------------------------------===//
//
// Copyright (c) 2012, 2013, 2014, 2015, 2016, 2017 The University of Utah
// All rights reserved.
//
// This file is distributed under the University of Illinois Open Source
// License.  See the file COPYING for details.
//
//===----------------------------------------------------------------------===//

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include "TransformationManager.h"

#include <sstream>

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Parse/ParseAST.h"

#include "Transformation.h"

using namespace clang;

TransformationManager* TransformationManager::Instance;

std::map<std::string, Transformation *> *
TransformationManager::TransformationsMapPtr;

TransformationManager *TransformationManager::GetInstance()
{
  if (TransformationManager::Instance)
    return TransformationManager::Instance;

  TransformationManager::Instance = new TransformationManager();
  assert(TransformationManager::Instance);

  TransformationManager::Instance->TransformationsMap =
    *TransformationManager::TransformationsMapPtr;
  return TransformationManager::Instance;
}

void TransformationManager::Finalize()
{
  assert(TransformationManager::Instance);

  std::map<std::string, Transformation *>::iterator I, E;
  for (I = Instance->TransformationsMap.begin(),
       E = Instance->TransformationsMap.end();
       I != E; ++I) {
    // CurrentTransformationImpl will be freed by ClangInstance
    if ((*I).second != Instance->CurrentTransformationImpl)
      delete (*I).second;
  }
  if (Instance->TransformationsMapPtr)
    delete Instance->TransformationsMapPtr;

  delete Instance->ClangInstance;

  delete Instance;
  Instance = NULL;
}

llvm::raw_ostream *TransformationManager::getOutStream()
{
  if (OutputFileName.empty())
    return &(llvm::outs());

  std::error_code EC;
  llvm::raw_fd_ostream *Out = new llvm::raw_fd_ostream(
      OutputFileName, EC, llvm::sys::fs::F_RW);
  assert(!EC && "Cannot open output file!");
  return Out;
}

void TransformationManager::closeOutStream(llvm::raw_ostream *OutStream)
{
  if (!OutputFileName.empty())
    delete OutStream;
}

void TransformationManager::registerTransformation(
       const char *TransName,
       Transformation *TransImpl)
{
  if (!TransformationManager::TransformationsMapPtr) {
    TransformationManager::TransformationsMapPtr =
      new std::map<std::string, Transformation *>();
  }

  assert((TransImpl != NULL) && "NULL Transformation!");
  assert((TransformationManager::TransformationsMapPtr->find(TransName) ==
          TransformationManager::TransformationsMapPtr->end()) &&
         "Duplicated transformation!");
  (*TransformationManager::TransformationsMapPtr)[TransName] = TransImpl;
}

void TransformationManager::printTransformations()
{
  llvm::outs() << "Registered Transformations:\n";

  std::map<std::string, Transformation *>::iterator I, E;
  for (I = TransformationsMap.begin(),
       E = TransformationsMap.end();
       I != E; ++I) {
    llvm::outs() << "  [" << (*I).first << "]: ";
    llvm::outs() << (*I).second->getDescription() << "\n";
  }
}

void TransformationManager::printTransformationNames()
{
  std::map<std::string, Transformation *>::iterator I, E;
  for (I = TransformationsMap.begin(),
       E = TransformationsMap.end();
       I != E; ++I) {
    llvm::outs() << (*I).first << "\n";
  }
}

void TransformationManager::outputNumTransformationInstances()
{
  int NumInstances =
    CurrentTransformationImpl->getNumTransformationInstances();
  llvm::outs() << "Available transformation instances: "
               << NumInstances << "\n";
}

TransformationManager::TransformationManager()
  : CurrentTransformationImpl(NULL),
    TransformationCounter(-1),
    ToCounter(-1),
    SrcFileName(""),
    OutputFileName(""),
    CurrentTransName(""),
    ClangInstance(NULL),
    QueryInstanceOnly(false),
    DoReplacement(false),
    Replacement(""),
    CheckReference(false),
    ReferenceValue("")
{
  // Nothing to do
}

TransformationManager::~TransformationManager()
{
  // Nothing to do
}

// Transform changes start
// Just to avoid linking errors.
Preprocessor &TransformationManager::getPreprocessor()
{
  return GetInstance()->ClangInstance->getPreprocessor();
}

bool TransformationManager::isCXXLangOpt()
{
  return true;
}

bool TransformationManager::isCLangOpt()
{
  return true;
}

bool TransformationManager::isOpenCLLangOpt()
{
  return true;
}

bool TransformationManager::initializeCompilerInstance(std::string &ErrorMsg)
{
  ErrorMsg = "";
  return true;
}

bool TransformationManager::doTransformation(std::string &ErrorMsg, int &ErrorCode)
{
  ErrorMsg = "";
  ErrorCode = 0;
  return true;
}

bool TransformationManager::verify(std::string &ErrorMsg, int &ErrorCode)
{
  ErrorMsg = "";
  ErrorCode = 0;
  return true;
}
// Transform changes end
