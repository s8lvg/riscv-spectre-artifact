/**
 * @name SpectreV1
 * @description Finds potential spectre v1 gadgets
 * @kind path-problem
 * @problem.severity warning
 * @id cpp/linux-spectre-v1
 */

import cpp
import semmle.code.cpp.ir.IR
import semmle.code.cpp.ir.dataflow.TaintTracking
import semmle.code.cpp.ir.dataflow.DataFlow
import semmle.code.cpp.controlflow.Guards
import semmle.code.cpp.valuenumbering.GlobalValueNumbering

// used to find flows to memory loads depending on source
predicate flowsToMemoryLoad(DataFlow::Node source, DataFlow::Node sink) {
  exists(LoadInstruction load |
    load.getSourceAddressOperand() = sink.asOperand() and
    DataFlow::localFlowStep(source, sink)
  )
}

// used to find user-controllable sources
predicate isUserLandInput(DataFlow::Node node, string cause) {
  // arguments to function-pointers in _ops or _operations structs are probably user-input
  exists(Struct s, Function func, Field f |
    s.getQualifiedName().regexpMatch(".*_ops|.*_operations") and
    f = s.getAField() and
    f.getType() instanceof FunctionPointerType and
    f.getAnAssignedValue() = func.getAnAccess() and
    node.asParameter() = func.getAParameter() and
    cause = func.getName()
  )
  or
  // arguments to ioctl calls or syscalls are probably user-input
  exists(Function func |
    func.getName().regexpMatch(".*ioctl.*|__do_sys_.*") and
    node.asParameter() = func.getAParameter() and
    cause = func.getName()
  )
  or
  // the destination of copy_from_user or get_user is probably user-input
  exists(FunctionCall fc |
    fc.getTarget().getName().regexpMatch(".*copy_from_user|get_user") and
    fc.getArgument(0) = node.asDefiningArgument() and
    cause = fc.getTarget().getName()
  )
}

// Modern configuration using DataFlow::ConfigSig
module UserArrayIndexConfig implements DataFlow::ConfigSig {
  // our taint-source is data coming from user-space
  predicate isSource(DataFlow::Node source) { 
    isUserLandInput(source, _) 
  }

  // our taint-sink is either an array-offset or pointer-arithmetic 
  // where there's a memory load depending on the sink afterwards
  predicate isSink(DataFlow::Node sink) {
    exists(ArrayExpr ae | sink.asExpr() = ae.getArrayOffset().getAChild*())
    or
    exists(PointerArithmeticOperation pao | 
      sink.asExpr() = pao.getAnOperand().getAChild*() and
      flowsToMemoryLoad(sink, _)
    )
  }

  // if the node flows through array_index_nospec() it's safe
  predicate isBarrier(DataFlow::Node node) {
    exists(MacroInvocation mi |
      mi.getMacroName() = "array_index_nospec" and
      mi.getAnExpandedElement() = node.asExpr()
    )
  }
}

module UserArrayIndexFlow = TaintTracking::Global<UserArrayIndexConfig>;
import UserArrayIndexFlow::PathGraph

from
  UserArrayIndexFlow::PathNode source,
  UserArrayIndexFlow::PathNode sink,
  GuardCondition gc,
  GVN gv
where
  // source must have a flow path to sink
  UserArrayIndexFlow::flowPath(source, sink) and
  // using global value numbering to find other expressions
  // in which the sink node is used, so that we can figure out
  // if our array-access is bounded by its index
  gv.getAnExpr() = sink.getNode().asExpr() and
  // check if our sink is guarded by a branch that we can speculatively bypass
  gc.comparesLt(gv.getAnExpr(), _, _, _, _) and
  gc.controls(sink.getNode().asExpr().getBasicBlock(), _)
select sink.getNode(), source, sink,
  "guard:" + gc.getLocation().getFile().getBaseName() + ":" + gc.getLocation().getStartLine().toString()