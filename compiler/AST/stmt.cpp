#include <string.h>
#include <typeinfo>
#include "expr.h"
#include "files.h"
#include "misc.h"
#include "stmt.h"
#include "stringutil.h"
#include "../symtab/symlink.h"
#include "symscope.h"
#include "symtab.h"
#include "../traversals/fixup.h"
#include "../traversals/updateSymbols.h"


Stmt::Stmt(astType_t astType) :
  BaseAST(astType),
  ainfo(NULL),
  back(NULL)
{}


FnSymbol *Stmt::parentFunction() {
  ModuleSymbol *mod = dynamic_cast<ModuleSymbol*>(parentSymbol);
  if (mod)
    return mod->initFn;
  else
    return dynamic_cast<FnSymbol*>(parentSymbol);
}


Stmt* Stmt::copyList(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone, Vec<BaseAST*>* update_list) {
  if (map == NULL) {
    map = new Map<BaseAST*,BaseAST*>();
  }
  Stmt* newStmtList = copyListInternal(clone, map, analysis_clone);
  if (update_list) {
    for (int j = 0; j < update_list->n; j++) {
      for (int i = 0; i < map->n; i++) {
	if (update_list->v[j] == map->v[i].key) {
	  update_list->v[j] = map->v[i].value;
	}
      }
    }
  }
  TRAVERSE_LS(newStmtList, new UpdateSymbols(map), true);
  return newStmtList;
}


Stmt* Stmt::copy(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone, Vec<BaseAST*>* update_list) {
  if (map == NULL) {
    map = new Map<BaseAST*,BaseAST*>();
  }
  Stmt* new_stmt = copyInternal(clone, map, analysis_clone);
  if (update_list) {
    for (int j = 0; j < update_list->n; j++) {
      for (int i = 0; i < map->n; i++) {
	if (update_list->v[j] == map->v[i].key) {
	  update_list->v[j] = map->v[i].value;
	}
      }
    }
  }
  TRAVERSE(new_stmt, new UpdateSymbols(map), true);
  return new_stmt;
}


Stmt* Stmt::copyListInternal(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  Stmt* newStmtList = NULL;
  Stmt* oldStmt = this;

  while (oldStmt) {
    newStmtList = appendLink(newStmtList, 
			     oldStmt->copyInternal(clone, map, analysis_clone));
    
    oldStmt = nextLink(Stmt, oldStmt);
  }

  return newStmtList;
}


Stmt* Stmt::copyInternal(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  if (!this) {
    return this;
  }

  Stmt* new_stmt = copyStmt(clone, map, analysis_clone);

  new_stmt->lineno = lineno;
  new_stmt->filename = filename;
  if (analysis_clone) {
    analysis_clone->clone(this, new_stmt);
  }
  if (map) {
    map->put(this, new_stmt);
  }
  return new_stmt;
}


Stmt* Stmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  INT_FATAL(this, "copy not implemented for Stmt subclass");
  return NULL;
}


void Stmt::traverse(Traversal* traversal, bool atTop) {
  if (traversal->processTop || !atTop) {
    traversal->preProcessStmt(this);
  }
  if (atTop || traversal->exploreChildStmts) {
    traverseStmt(traversal);
  }
  if (traversal->processTop || !atTop) {
    traversal->postProcessStmt(this);
  }
}


void Stmt::traverseDef(Traversal* traversal, bool atTop) {
  INT_FATAL(this, "Attempt to traverse the definition of a statement");
}


void Stmt::traverseStmt(Traversal* traversal) {
}


static void call_fixup(Stmt* stmt) {
  Fixup* fixup = new Fixup();

  if (!stmt->parentSymbol) {
    Symbol* sym = Symboltable::getCurrentScope()->findEnclosingSymContext();
    if (!sym) {
      INT_FATAL(stmt, "Problem calling fixup");
    }
    TRAVERSE_DEF(sym, fixup, true);
  }
  else if (FnSymbol* fn = dynamic_cast<FnSymbol*>(stmt->parentSymbol)) {
    TRAVERSE_LS(fn->body, fixup, true);
  }
  else if (ModuleSymbol* mod = dynamic_cast<ModuleSymbol*>(stmt->parentSymbol)) {
    TRAVERSE_LS(mod->stmts, fixup, true);
  }
  else if (TypeSymbol* type = dynamic_cast<TypeSymbol*>(stmt->parentSymbol)) {
    if (ClassType* class_type = dynamic_cast<ClassType*>(type->type)) {
      TRAVERSE(class_type, fixup, true);
    }
  }
  else {
    INT_FATAL(stmt, "Error calling fixup");
  }
}


void Stmt::replace(Stmt* new_stmt) {
  Stmt* first = dynamic_cast<Stmt*>(new_stmt->head());
  Stmt* last = dynamic_cast<Stmt*>(new_stmt->tail());

  if (!first || !last) {
    INT_FATAL(this, "Illegal call to replace, new_stmt list invalid");
  }

  if (first != new_stmt) {
    INT_FATAL(this, "Illegal replace, new_stmt is not head of list");
  }

  last->next = next;
  if (next) {
    Stmt* next_stmt = dynamic_cast<Stmt*>(next);
    next_stmt->prev = last;
    next_stmt->back = &(Stmt*&)last->next; // UGH --SJD
  }
  first->prev = prev;
  first->back = back;
  *back = first;
  /* NOT NECESSARY BECAUSE OF PRECEDING LINE
    if (prev) {
      prev->next = first;
    }
  */
  /* while nulling the following out would be cleaner and purer,
     in many cases (traversals, loops), it is convenient to keep
     these pointing to the old nodes; an alternative would be to
     require the user to set them back to something non-NULL if
     that's what they wanted, but we are positing that this will
     be the common case.
  prev = NULL;
  next = NULL;
  */
  call_fixup(this);
}


void Stmt::insertBefore(Stmt* new_stmt) {
  Stmt* first = dynamic_cast<Stmt*>(new_stmt->head());
  Stmt* last = dynamic_cast<Stmt*>(new_stmt->tail());

  if (!first || !last) {
    INT_FATAL(this, "Illegal call to insertBefore, new_stmt list invalid");
  }

  if (first != new_stmt) {
    INT_FATAL(this, "Illegal insertBefore, new_stmt is not head of list");
  }

  first->prev = prev;
  first->back = back;
  *back = first;
  /* NOT NECESSARY BECAUSE OF PRECEDING LINE
    if (prev) {
      prev->next = first;
    }
  */
  back = &(Stmt*&)last->next; // UGH --SJD

  prev = last;
  last->next = this;

  call_fixup(this);
}


void Stmt::insertAfter(Stmt* new_stmt) {
  Stmt* first = dynamic_cast<Stmt*>(new_stmt->head());
  Stmt* last = dynamic_cast<Stmt*>(new_stmt->tail());

  if (!first || !last) {
    INT_FATAL(this, "Illegal call to insertAfter, new_stmt list invalid");
  }

  if (first != new_stmt) {
    INT_FATAL(this, "Illegal insertAfter, new_stmt is not head of list");
  }

  last->next = next;
  if (next) {
    next->prev = last;
    Stmt* tmp = dynamic_cast<Stmt*>(next);
    tmp->back = &(Stmt*&)last->next; // UGH --SJD
  }
  next = first;
  first->prev = this;
  first->back = &(Stmt*&)next; // UGH --SJD

  call_fixup(this);
}


void Stmt::append(ILink* new_stmt) {
  if (!new_stmt) {
    return;
  }

  Stmt* first = dynamic_cast<Stmt*>(new_stmt->head());
  Stmt* last = dynamic_cast<Stmt*>(new_stmt->tail());

  if (!first || !last) {
    INT_FATAL(this, "Illegal call to append, new_stmt list invalid");
  }

  if (first != new_stmt) {
    INT_FATAL(this, "Illegal append, new_stmt is not head of list");
  }

  Stmt* append_point = dynamic_cast<Stmt*>(this->tail());

  append_point->next = first;
  first->prev = append_point;
  // UGH!! --SJD
  first->back = &(Stmt*&)append_point->next;
}


Stmt* Stmt::extract(void) {
  if (next) {
    if (Stmt* next_stmt = dynamic_cast<Stmt*>(next)) { 
      next_stmt->prev = prev;
      next_stmt->back = back;
      *back = next_stmt;
      /* NOT NECESSARY BECAUSE OF PRECEDING LINE
	 if (prev) {
	 prev->next = next;
	 }
      */
    }
    else {
      INT_FATAL(this, "Illegal call to extract, stmt is invalid");
    }
  }
  else {
    *back = NULL;
  }
  /* SEE NOTE IN REPLACE
  next = NULL;
  prev = NULL;
  */
  return this;
}


NoOpStmt::NoOpStmt(void) :
  Stmt(STMT_NOOP)
{}


Stmt* NoOpStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  return new NoOpStmt();
}


void NoOpStmt::print(FILE* outfile) {
  fprintf(outfile, ";");
}


void NoOpStmt::codegen(FILE* outfile) {
  //  fprintf(outfile, "{}\n");
  //  SJD: Change back when _CommonModule gets an initFn
}


WithStmt::WithStmt(Expr* init_withExpr) :
  Stmt(STMT_WITH),
  withExpr(init_withExpr)
{
  SET_BACK(withExpr);
}


ClassType* WithStmt::getClass(void) {
  if (Variable* var = dynamic_cast<Variable*>(withExpr)) {
    if (ClassType* result = 
	dynamic_cast<ClassType*>(var->var->type)) {
      return result;
    }
    else if (UnresolvedSymbol* unresolved = 
	     dynamic_cast<UnresolvedSymbol*>(var->var)) {
      if (ClassType* result = 
	  dynamic_cast<ClassType*>(Symboltable::lookup(unresolved->name)->type)) {
	return result;
      }
      else {
	INT_FATAL(this, "Bad with statement");
      }
    }
    else {
      INT_FATAL(this, "Bad with statement");
    }
  }
  else {
    INT_FATAL(this, "Bad with statement");
  }
  return NULL;
}


Stmt* WithStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  return new WithStmt(withExpr->copyInternal(clone, map, analysis_clone));
}


void WithStmt::traverseStmt(Traversal* traversal) {
  TRAVERSE(withExpr, traversal, false);
}


void WithStmt::print(FILE* outfile) {
  fprintf(outfile, "with ");
  withExpr->print(outfile);
  fprintf(outfile, "\n");
}


void WithStmt::codegen(FILE* outfile) {
  INT_FATAL(this, "With statement encountered in codegen()");
}


DefStmt::DefStmt(Symbol* init_def_sym) :
  Stmt(STMT_DEF),
  def_sym(init_def_sym)
{}


Stmt* DefStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  if (clone) {
    return NULL;
  }
  else {
    return new DefStmt(def_sym);
  }
}


void DefStmt::traverseStmt(Traversal* traversal) {
  TRAVERSE_DEF_LS(def_sym, traversal, false);
}


void DefStmt::print(FILE* outfile) {
}


void DefStmt::codegen(FILE* outfile) { /* Noop */ }


bool DefStmt::isVarDef() {
  return dynamic_cast<VarSymbol*>(def_sym);
}


bool DefStmt::isFnDef() {
  return dynamic_cast<FnSymbol*>(def_sym);
}


bool DefStmt::isTypeDef() {
  return dynamic_cast<TypeSymbol*>(def_sym);
}


bool DefStmt::isModuleDef() {
  return dynamic_cast<ModuleSymbol*>(def_sym);
}


VarDefStmt::VarDefStmt(VarSymbol* init_var) :
  Stmt(STMT_VARDEF),
  var(init_var)
{}


Stmt* VarDefStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  if (clone) {
    return
      Symboltable::defineVarDefStmt(var, var->type,
				    var->init->copyInternal(clone, map, analysis_clone), 
				    var->varClass, var->isConstant);
  }
  else {
    return new VarDefStmt(var);
  }
}


void VarDefStmt::traverseStmt(Traversal* traversal) {
  TRAVERSE_DEF_LS(var, traversal, false);
}


void VarDefStmt::print(FILE* outfile) {
  VarSymbol* aVar = var;

  while (aVar) {
    aVar->printDef(outfile);
    fprintf(outfile, ";");
    aVar = nextLink(VarSymbol, aVar);
    if (aVar) {
      fprintf(outfile, "\n");
    }
  }
}

void VarDefStmt::codegen(FILE* outfile) { }


TypeDefStmt::TypeDefStmt(TypeSymbol* init_type_sym) :
  Stmt(STMT_TYPEDEF),
  type_sym(init_type_sym)
{}


Stmt* TypeDefStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  if (clone) {
    TypeSymbol* sym_copy = dynamic_cast<TypeSymbol*>(type_sym->copy(clone, map, analysis_clone));
    return new TypeDefStmt(sym_copy);
  }
  else {
    return new TypeDefStmt(type_sym);
  }
}


void TypeDefStmt::traverseStmt(Traversal* traversal) {
  type_sym->traverseDef(traversal, false);
}


void TypeDefStmt::print(FILE* outfile) {
  type_sym->type->printDef(outfile);
  fprintf(outfile, ";");
}


void TypeDefStmt::codegen(FILE* outfile) {
}


FnDefStmt::FnDefStmt(FnSymbol* init_fn) :
  Stmt(STMT_FNDEF),
  fn(init_fn)
{}


Stmt* FnDefStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  if (clone) {
    FnSymbol* fncopy = dynamic_cast<FnSymbol*>(fn->copy(clone, map, analysis_clone));
    Symboltable::startFnDef(fncopy);
    Symbol* newformals = fn->formals->copyList(clone, map, analysis_clone);
    return Symboltable::finishFnDef(fncopy, newformals, fn->type, 
				    fn->body->copyListInternal(clone, map, analysis_clone),
				    fn->exportMe);

  }
  else {
    return new FnDefStmt(fn);
  }
}


void FnDefStmt::traverseStmt(Traversal* traversal) {
  TRAVERSE_DEF(fn, traversal, false);
}


void FnDefStmt::print(FILE* outfile) {
  fn->printDef(outfile);
}


void FnDefStmt::codegen(FILE* outfile) { /* Noop */ }


ModuleDefStmt::ModuleDefStmt(ModuleSymbol* init_module) :
  Stmt(STMT_MODDEF),
  module(init_module)
{}


void ModuleDefStmt::codegen(FILE* outfile) {
  fprintf(outfile, "/* module ");
  module->codegen(outfile);
  fprintf(outfile, " was declared here */");
}


ExprStmt::ExprStmt(Expr* init_expr) :
  Stmt(STMT_EXPR),
  expr(init_expr) 
{
  SET_BACK(expr);
}


Stmt* ExprStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  return new ExprStmt(expr->copyInternal(clone, map, analysis_clone));
}


void ExprStmt::traverseStmt(Traversal* traversal) {
  TRAVERSE(expr, traversal, false);
}


void ExprStmt::print(FILE* outfile) {
  expr->print(outfile);
  fprintf(outfile, ";");
}


void ExprStmt::codegen(FILE* outfile) {
  expr->codegen(outfile);
  fprintf(outfile, ";");
}


ExprStmt* ExprStmt::createFnCallStmt(FnSymbol* fnSym, Expr* argList) {
  FnCall *e = new FnCall(new Variable(fnSym), argList);
  ExprStmt *stmt = new ExprStmt(e);
  e->stmt = stmt;
  return stmt;
}


ReturnStmt::ReturnStmt(Expr* retExpr) :
  ExprStmt(retExpr)
{
  astType = STMT_RETURN;
}


Stmt* ReturnStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  return new ReturnStmt(expr->copyInternal(clone, map, analysis_clone));
}


void ReturnStmt::print(FILE* outfile) {
  fprintf(outfile, "\n");
  fprintf(outfile, "return");
  if (expr) {
    fprintf(outfile, " ");
    expr->print(outfile);
  }
  fprintf(outfile, ";");
}


void ReturnStmt::codegen(FILE* outfile) {
  fprintf(outfile, "return");
  if (expr) {
    fprintf(outfile, " ");
    expr->codegen(outfile);
  }
  fprintf(outfile, ";");
}


BlockStmt::BlockStmt(Stmt* init_body) :
  Stmt(STMT_BLOCK),
  body(init_body),
  blkScope(NULL)
{
  SET_BACK(body);
}


void BlockStmt::addBody(Stmt* init_body) {
  if (!body) {
    body = init_body;
    SET_BACK(body); // SJD: Eliminate please.
  } else {
    INT_FATAL(this, "Adding a body to a for loop that already has one");
  }
}


void BlockStmt::setBlkScope(SymScope* init_blkScope) {
  blkScope = init_blkScope;
}


Stmt* BlockStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  Symboltable::pushScope(SCOPE_LOCAL);
  Stmt* body_copy = body->copyListInternal(true, map, analysis_clone);
  SymScope* block_scope = Symboltable::popScope();
  BlockStmt* block_copy = new BlockStmt(body_copy);
  block_copy->setBlkScope(block_scope);
  block_scope->setContext(block_copy);
  return block_copy;
}


void BlockStmt::traverseStmt(Traversal* traversal) {
  SymScope* prevScope;
  if (blkScope) {
    prevScope = Symboltable::setCurrentScope(blkScope);
  }
  TRAVERSE_LS(body, traversal, false);
  if (blkScope) {
    Symboltable::setCurrentScope(prevScope);
  }
}


void BlockStmt::print(FILE* outfile) {
  fprintf(outfile, "{\n");
  body->printList(outfile, "\n");
  fprintf(outfile, "\n");
  fprintf(outfile, "}");
}

void BlockStmt::codegen(FILE* outfile) {
  fprintf(outfile, "{\n");
  if (blkScope) {
    blkScope->codegen(outfile, "\n");
  }
  if (body) body->codegenList(outfile, "\n");
  fprintf(outfile, "\n");
  fprintf(outfile, "}");
}


WhileLoopStmt::WhileLoopStmt(bool init_whileDo, 
			     Expr* init_cond, 
			     Stmt* init_body) 
  : BlockStmt(init_body), 
    isWhileDo(init_whileDo), 
    condition(init_cond) 
{
  astType = STMT_WHILELOOP;
  SET_BACK(condition);
}


Stmt* WhileLoopStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  return new WhileLoopStmt(isWhileDo, condition->copyInternal(clone, map, analysis_clone), body->copyInternal(clone, map, analysis_clone));
}


void WhileLoopStmt::traverseStmt(Traversal* traversal) {
  if (isWhileDo) {
    TRAVERSE(condition, traversal, false);
    TRAVERSE(body, traversal, false);
  } else {
    TRAVERSE(body, traversal, false);
    TRAVERSE(condition, traversal, false);
  }
}


void WhileLoopStmt::print(FILE* outfile) {

  if (isWhileDo) {
    fprintf(outfile, "while (");
    condition->print(outfile);
    fprintf(outfile, ") ");
    body->print(outfile);
  } else {
    fprintf(outfile, "do ");
    body->print(outfile);
    fprintf(outfile, "while (");
    condition->print(outfile);
    fprintf(outfile, ")");
  }
}


void WhileLoopStmt::codegen(FILE* outfile) {

  if (isWhileDo) {
    fprintf(outfile, "while (");
    condition->codegen(outfile);
    fprintf(outfile, ") {\n");
    body->codegen(outfile);
    fprintf(outfile, "\n}");
  } else { 
    fprintf(outfile, "do {\n");
    body->codegen(outfile);
    fprintf(outfile, "\n} while (");
    condition->codegen(outfile);
    fprintf(outfile, ");\n");
  }
}


ForLoopStmt::ForLoopStmt(bool init_forall,
			 VarSymbol* init_index,
			 Expr* init_domain,
			 Stmt* body)
  : BlockStmt(body),
    forall(init_forall),
    index(init_index),
    domain(init_domain),
    indexScope(NULL)
{
  astType = STMT_FORLOOP;
  SET_BACK(domain);
}


void ForLoopStmt::setIndexScope(SymScope* init_indexScope) {
  indexScope = init_indexScope;
}


Stmt* ForLoopStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  Symboltable::pushScope(SCOPE_FORLOOP);
  VarSymbol* index_copy =
    dynamic_cast<VarSymbol*>(index->copy(true, map, analysis_clone));
  Expr* domain_copy = domain->copyInternal(true, map, analysis_clone);
  Stmt* body_copy = body->copyInternal(true, map, analysis_clone);
  SymScope* index_scope = Symboltable::popScope();
  ForLoopStmt* for_loop_stmt_copy =
    new ForLoopStmt(forall, index_copy, domain_copy, body_copy);
  index_copy->setDefPoint(for_loop_stmt_copy);
  for_loop_stmt_copy->setIndexScope(index_scope);
  return for_loop_stmt_copy;
}


void ForLoopStmt::traverseStmt(Traversal* traversal) {
  SymScope* prevScope;

  TRAVERSE(domain, traversal, false);
  if (indexScope) {
    prevScope = Symboltable::setCurrentScope(indexScope);
  }
  TRAVERSE(index, traversal, false);
  TRAVERSE(body, traversal, false);
  if (indexScope) {
    Symboltable::setCurrentScope(prevScope);
  }
}


void ForLoopStmt::print(FILE* outfile) {
  fprintf(outfile, "for");
  if (forall) {
    fprintf(outfile, "all");
  }
  fprintf(outfile, " ");
  index->print(outfile);
  fprintf(outfile, " in ");
  domain->print(outfile);
  fprintf(outfile, " ");
  body->print(outfile);
}


void ForLoopStmt::codegen(FILE* outfile) {
  VarSymbol* aVar = index;
  fprintf(outfile, "{\n");
  int rank = 0;

  // TODO: Unify with VarDefStmt?  Have parser insert one here?
  // is it a challenge that we may not know the domain exprs at that point?
  while (aVar) {
    aVar->codegenDef(outfile);
    rank++;

    aVar = nextLink(VarSymbol, aVar);
  }
  fprintf(outfile, "\n");
  
  aVar = index;

  Tuple* tuple = dynamic_cast<Tuple*>(domain);

  SimpleSeqExpr* seq = (tuple)
    ? dynamic_cast<SimpleSeqExpr*>(tuple->exprs)
    : dynamic_cast<SimpleSeqExpr*>(domain);

  if (seq) {
    for (int i=0; i<rank; i++) {
      fprintf(outfile, "_FOR");
      if (forall) {
	fprintf(outfile, "ALL");
      }
      fprintf(outfile, "_DIM");
      fprintf(outfile, "(");
      aVar->codegen(outfile);
      fprintf(outfile, ", ");
      seq->lo->codegen(outfile);
      fprintf(outfile, ", ");
      seq->hi->codegen(outfile);
      fprintf(outfile, ", ");
      seq->str->codegen(outfile);
      fprintf(outfile, ") {\n");

      aVar = nextLink(VarSymbol, aVar);
      seq = nextLink(SimpleSeqExpr, seq);
    }
  }
  else {
    for (int i=0; i<rank; i++) {
      fprintf(outfile, "_FOR");
      if (forall) {
	fprintf(outfile, "ALL");
      }
      fprintf(outfile, "(");
      aVar->codegen(outfile);
      fprintf(outfile, ", ");
      domain->codegen(outfile);
      fprintf(outfile, ", %d) {\n", i);

      aVar = nextLink(VarSymbol, aVar);
    }
  }

  body->codegen(outfile);
  fprintf(outfile, "\n");

  for (int i=0; i<rank; i++) {
    fprintf(outfile, "}\n");
  }
  
  fprintf(outfile, "}\n");
}


CondStmt::CondStmt(Expr*  init_condExpr, Stmt* init_thenStmt, 
		   Stmt* init_elseStmt) :
  Stmt(STMT_COND),
  condExpr(init_condExpr),
  thenStmt(init_thenStmt),
  elseStmt(init_elseStmt)
{
  SET_BACK(condExpr);
  SET_BACK(thenStmt);
  SET_BACK(elseStmt);
}


Stmt* CondStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  return new CondStmt(condExpr->copyInternal(clone, map, analysis_clone), thenStmt->copyInternal(clone, map, analysis_clone), elseStmt->copyInternal(clone, map, analysis_clone));
}


void CondStmt::traverseStmt(Traversal* traversal) {
  TRAVERSE(condExpr, traversal, false);
  TRAVERSE(thenStmt, traversal, false);
  TRAVERSE(elseStmt, traversal, false);
}


void CondStmt::print(FILE* outfile) {
  fprintf(outfile, "if (");
  condExpr->print(outfile);
  fprintf(outfile, ") ");
  thenStmt->print(outfile);
  if (elseStmt) {
    fprintf(outfile, " else ");
    elseStmt->print(outfile);
  }
}

void CondStmt::codegen(FILE* outfile) {
  fprintf(outfile, "if (");
  condExpr->codegen(outfile);
  fprintf(outfile, ") {\n");
  thenStmt->codegen(outfile);
  fprintf(outfile, "\n}");
  if (elseStmt) {
    fprintf(outfile, " else {\n");
    elseStmt->codegen(outfile);
    fprintf(outfile, "\n}");
  }
}



LabelStmt::LabelStmt(LabelSymbol* init_label, Stmt* init_stmt) :
  Stmt(STMT_LABEL),
  label(init_label),
  stmt(init_stmt)
{
  SET_BACK(stmt);
}


Stmt* LabelStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  return new LabelStmt(label, stmt->copyInternal(clone, map, analysis_clone));
}


void LabelStmt::traverseStmt(Traversal* traversal) {
  TRAVERSE_DEF(label, traversal, false);
  TRAVERSE(stmt, traversal, false);
}


void LabelStmt::print(FILE* outfile) {
  fprintf(outfile, "label ");
  label->print(outfile);
  fprintf(outfile, " ");
  stmt->print(outfile);
}


void LabelStmt::codegen(FILE* outfile) {
  label->codegen(outfile);
  fprintf(outfile, ":;\n");
  stmt->codegen(outfile);
  label->codegen(outfile);
  fprintf(outfile, "_post:;\n");
}


GotoStmt::GotoStmt(gotoType init_goto_type) :
  Stmt(STMT_GOTO),
  label(NULL),
  goto_type(init_goto_type)
{
}


GotoStmt::GotoStmt(gotoType init_goto_type, char* init_label) :
  Stmt(STMT_GOTO),
  label(new UnresolvedSymbol(init_label)),
  goto_type(init_goto_type)
{
}


GotoStmt::GotoStmt(gotoType init_goto_type, Symbol* init_label) :
  Stmt(STMT_GOTO),
  label(init_label),
  goto_type(init_goto_type)
{
}


Stmt* GotoStmt::copyStmt(bool clone, Map<BaseAST*,BaseAST*>* map, CloneCallback* analysis_clone) {
  return new GotoStmt(goto_type, label);
}


void GotoStmt::traverseStmt(Traversal* traversal) {
  TRAVERSE(label, traversal, false);
}


void GotoStmt::print(FILE* outfile) {
  switch (goto_type) {
  case goto_normal:
    fprintf(outfile, "goto ");
    break;
  case goto_break:
    fprintf(outfile, "break goto ");
    break;
  case goto_continue:
    fprintf(outfile, "continue goto ");
    break;
  }
  label->print(outfile);
  fprintf(outfile, "\n");
}


void GotoStmt::codegen(FILE* outfile) {
  fprintf(outfile, "goto ");
  label->codegen(outfile);
  if (goto_type == goto_break) {
    fprintf(outfile, "_post");
  }
  fprintf(outfile, ";\n");
}
