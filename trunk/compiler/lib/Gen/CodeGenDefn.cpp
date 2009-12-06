/* ================================================================ *
    TART - A Sweet Programming Language.
 * ================================================================ */

#include "tart/Gen/CodeGenerator.h"
#include "tart/Common/Diagnostics.h"
#include "tart/Common/SourceFile.h"

#include "tart/CFG/Module.h"
#include "tart/CFG/Defn.h"
#include "tart/CFG/TypeDefn.h"
#include "tart/CFG/CompositeType.h"
#include "tart/CFG/FunctionType.h"
#include "tart/CFG/FunctionDefn.h"
#include "tart/CFG/Block.h"
#include "tart/CFG/PrimitiveType.h"

#include "tart/Objects/Builtins.h"

#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/Analysis/Verifier.h>

namespace tart {

using namespace llvm;

bool CodeGenerator::genXDef(Defn * de) {
  if (debug_) {
    dbgCompileUnit_ = genDICompileUnit(de);
  }

  switch (de->defnType()) {
    case Defn::Let:
      return genLetDefn(static_cast<VariableDefn *>(de));

    case Defn::Var:
      return genVarValue(static_cast<VariableDefn *>(de));

    case Defn::Function:
      return genFunction(static_cast<FunctionDefn*>(de));

    case Defn::Typedef: {
      return genTypeDefn(static_cast<TypeDefn*>(de));
    }

    case Defn::Macro:
    case Defn::Namespace:
      return true;

    default:
      diag.fatal(de) << "No generator for " << de;
      return true;
  }
}

Function * CodeGenerator::genFunctionValue(const FunctionDefn * fdef) {
  Function * fn = irModule_->getFunction(fdef->linkageName());
  if (fn != NULL) {
    return fn;
  }

  // If it's a function from a different module...
  if (fdef->module() != module_) {
    const FunctionType * funcType = fdef->functionType();
    fn = Function::Create(
        cast<llvm::FunctionType>(funcType->irType()),
        Function::ExternalLinkage, fdef->linkageName(),
        irModule_);
    return fn;
  }

  DASSERT_OBJ(fdef->defnType() != Defn::Macro, fdef);
  DASSERT_OBJ(!fdef->isIntrinsic(), fdef);

  // Generate the function reference
  const FunctionType * funcType = fdef->functionType();
  DASSERT_OBJ(funcType->isSingular(), fdef);

  fn = Function::Create(
      cast<llvm::FunctionType>(funcType->irType()),
      Function::ExternalLinkage, fdef->linkageName(), fdef->module()->irModule());

  // TODO - Don't store irFunction in the function, as it makes it hard to compile more than
  // one module.
  //fdef->setIRFunction(fn);
  return fn;
}

bool CodeGenerator::genFunction(FunctionDefn * fdef) {
  // Don't generate undefined functions.
  if (fdef->hasTrait(Defn::Undefined)) {
    return true;
  }

  DASSERT_OBJ(fdef->isSingular(), fdef);
  DASSERT_OBJ(fdef->type() != NULL, fdef);
  DASSERT_OBJ(fdef->type()->isSingular(), fdef);

  // Don't generate intrinsic functions.
  if (fdef->isIntrinsic()) {
    return true;
  }

  // Create the function
  Function * f = genFunctionValue(fdef);

  if (fdef->hasBody()) {
    FunctionType * ftype = fdef->functionType();

    if (fdef->isSynthetic()) {
      f->setLinkage(GlobalValue::LinkOnceODRLinkage);
    }

    if (debug_) {
      dbgContext_ = genDISubprogram(fdef);
      setDebugLocation(fdef->location());
    }

    // Create the LLVM Basic Blocks corresponding to each high level BB.
    BlockList & blocks = fdef->blocks();
    for (BlockList::iterator b = blocks.begin(); b != blocks.end(); ++b) {
      Block * blk = *b;
      blk->setIRBlock(BasicBlock::Create(context_, blk->label(), f));
    }

    builder_.SetInsertPoint(blocks.front()->irBlock());

    // Handle the explicit parameters
    unsigned param_index = 0;
    Function::arg_iterator it = f->arg_begin();

    // Handle the 'self' parameter
    if (ftype->selfParam() != NULL) {
      DASSERT_OBJ(fdef->storageClass() == Storage_Instance ||
          fdef->storageClass() == Storage_Local, fdef);
      DASSERT_OBJ(it != f->arg_end(), ftype);
      ftype->selfParam()->setIRValue(it);
      it->setName("self");
      ++it;
    }

    for (; it != f->arg_end(); ++it, ++param_index) {

      // Set the name of the Nth parameter
      ParameterDefn * param = ftype->params()[param_index];
      DASSERT_OBJ(param != NULL, fdef);
      it->setName(param->name());

      // See if we need to make a local copy of the param.
      if (param->isLValue()) {
        //|| (!param->getParameterFlag(ParameterDefn::Reference)
        //  && isAllocValueType(ptypeetType()))) {
        // TODO: For struct parameters, make a copy of whole struct.
        // If parameter was modified, then copy to a local var.
        Value * localValue = builder_.CreateAlloca(it->getType(), 0, param->name());
        builder_.CreateStore(it, localValue);
        param->setIRValue(localValue);
      } else {
        param->setIRValue(it);
      }
    }

    // Generate the body
    Function * saveFn = currentFn_;
    currentFn_ = f;
#if 0
    if (fdef->isGenerator()) {
      assert(false);
    } else {
#endif
      genLocalStorage(fdef->blocks(), fdef->localScopes());
      genDISubprogramStart(fdef);
      genBlocks(fdef->blocks());
#if 0
    }
#endif

    currentFn_ = saveFn;

    if (!diag.inRecovery()) {
      if (verifyFunction(*f, PrintMessageAction)) {
        f->dump();
        DFAIL("Function failed to verify");
      }
    }

    //if (debug_ && !dbgContext_.isNull() && !dbgContext_.Verify()) {
    //  dbgContext_.Verify();
    //  DFAIL("BAD DBG");
    //}

    dbgContext_ = DISubprogram();
    builder_.ClearInsertionPoint();
    builder_.SetCurrentDebugLocation(NULL);
  }

  return true;
}

Value * CodeGenerator::genLetValue(const VariableDefn * let) {
  // Don't generate the IR if we've already done so
  if (let->irValue() != NULL) {
    return let->irValue();
  }

#if 0
  // Generate the attributes
  Attributes letAttrs;
  if (!genAttrs(letDef, letAttrs)) {
    return NULL;
  }
#endif

  // Calculate the type.
  DASSERT(let->type() != NULL);
  const llvm::Type * irType = let->type()->irEmbeddedType();

  // Generate the value
  Value * value = NULL;
  if (let->initValue() != NULL) {
    if (let->hasStorage()) {
      if (let->module() != module_) {
        value = new GlobalVariable(
            *irModule_, irType, true, GlobalValue::ExternalLinkage, NULL, let->linkageName());
      } else {
        value = genConstRef(let->initValue(), let->linkageName());
      }
    } else {
      value = genExpr(let->initValue());
    }

    if (value == NULL) {
      return false;
    }
  }

  Value * letValue = NULL;
  if (let->storageClass() == Storage_Local) {
    // If it's a local variable, then use the value directly.
    letValue = value;
  } else if (llvm::Constant * constantValue = dyn_cast<llvm::Constant>(value)) {
    // See if it's a constant.
    letValue = constantValue;
  } else {
    diag.error(let->location()) << "Non-constant let value " << let;
    DASSERT(let->passes().isFinished(VariableDefn::InitializerPass));
    DFAIL("let value not a constant");
  }

  DIType dbgType;
  //if (debug_) {
  //  dbgType = genTypeDebugInfo(letType);
  //}

  let->setIRValue(letValue);
  return letValue;
}

bool CodeGenerator::genLetDefn(VariableDefn * let) {
  return genLetValue(let) != NULL;
}

Value * CodeGenerator::genVarValue(const VariableDefn * var) {
  // If it's not a global, then then the IRValue must have been pre-generated.
  if (var->storageClass() != Storage_Global &&
      var->storageClass() != Storage_Static) {
    // Don't generate the IR if we've already done so.
    if (var->irValue() != NULL) {
      return var->irValue();
    }

    DFAIL("IllegalState");
  }

  return genGlobalVar(var);
}

Value * CodeGenerator::genGlobalVar(const VariableDefn * var) {
  // Global variables never set the IRValue field, because that field has a different value
  // depending on what module we are compiling.
  DASSERT(var->defnType() == Defn::Var);
  DASSERT(var->irValue() == NULL);
  DASSERT(var->storageClass() == Storage_Global || var->storageClass() == Storage_Static);

  GlobalVariable * gv = irModule_->getGlobalVariable(var->linkageName());
  if (gv != NULL) {
    return gv;
  }

  const Type * varType = var->type();
  DASSERT(varType != NULL);

  // Create the global variable
  GlobalValue::LinkageTypes linkType = Function::ExternalLinkage;
  if (var->isSynthetic()) {
    linkType = Function::LinkOnceAnyLinkage;
  }

#if 0
  bool threadLocal = false;
  bool threadLocal = var->findAttribute(Builtins::typeThreadLocalAttribute) != NULL;
  if (threadLocal && var->storageClass() != Storage_Global &&
      var->storageClass() != Storage_Static) {
    diag.fatal(var->location()) <<  "Only global or static variables can be thread-local";
  }
#endif

  // The reason that this is irType instead of irEmbeddedType is because LLVM always turns
  // the type of a global variable into a pointer anyway.
  const llvm::Type * irType = varType->irEmbeddedType();
  gv = new GlobalVariable(*irModule_, irType, true, linkType, NULL, var->linkageName());

  // Only supply an initialization expression if the variable was
  // defined in this module - otherwise, it's an external declaration.
  if (var->module() == module_ || var->isSynthetic()) {
    /*DIType dbgType;
    if (debug_) {
      dbgType = genTypeDebugInfo(varType);
    }*/

    // If it has an initialization expression
    const Expr * initExpr = var->initValue();
    if (initExpr != NULL) {
      if (initExpr->isConstant()) {
        Constant * initValue = genConstExpr(initExpr);
        if (initValue == NULL) {
          return NULL;
        }

        if (varType->isReferenceType()) {
          initValue = new GlobalVariable(
              *irModule_, initValue->getType(), false, linkType, initValue, "");
          initValue = llvm::ConstantExpr::getPointerCast(initValue, varType->irEmbeddedType());
        }

        gv->setInitializer(initValue);
      } else {
        DFAIL("Implement");
#if 0
        genModuleInitFunc();

        // Add it to the module init function
        BasicBlock * savePoint = builder_.GetInsertBlock();
        builder_.SetInsertPoint(moduleInitBlock);

        // Generate the expression.
        Value * initValue = genExpr(initExpr);
        if (initValue == NULL) {
          return false;
        }

        // Otherwise use dynamic initialization
        gv->setInitializer(llvm::Constant::getNullValue(irType));
        builder_.CreateStore(initValue, gv);

        if (savePoint != NULL) {
          builder_.SetInsertPoint(savePoint);
        }
#endif
      }
    } else {
      // No initializer, so set the value to zerofill.
      gv->setInitializer(llvm::Constant::getNullValue(irType));
    }
  }

  return gv;
}

} // namespace tart
