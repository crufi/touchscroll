//==================================================================================
// Exceptions.c
// ©2026 Steve Crutchfield
//
// Exception handling ("checked exceptions") for plain C -- see extensive notes
// in Exceptions.h.
//==================================================================================

#include "CrutchError.h"

#ifndef __cplusplus
// these exception handling tools not suitable from C++ since they won't call auto dtors

TryBlock *_try_stack;	// auto init to 0
int _err;				// auto init to 0

const _throws_t _throws_arg = { 0 };  // dummy token passed by THROWS, value irrelevant

// throws exception with error code x:  pops the target node off _try_stack, then
// longjmps to it.  popping FIRST is what makes a throw from inside a catch block
// (or a try_throws re-throw) propagate to the ENCLOSING try.
void _DoThrow(int x) {
	TryBlock * const node = _try_stack;

	AssertMesgFatal(x != 0, "can't throw 0 error");

	// (next line is belt-and-suspenders, all macros that call us should have already checked
	// this locally so user sees an error pointing to a line in their own code, not here)
	AssertMesgFatal(node, "empty try_stack in _DoThrow, consult implementor");

	// pop, then jump.  the node is a local in the target try block's scope, so it
	// is still alive -- no need to copy it (or, as in an earlier version of this
	// library, to DisposePtr it):
	_try_stack = node->next;
	longjmp(node->buf, x);
}

// pops the given node off _try_stack
void _PopTry(TryBlock *node) {
	AssertMesgFatal(_try_stack, "empty try_stack -- 'end_try' without 'try'?");

	// if this next one fires, some try block was exited via return/break/goto/
	// continue, leaving its now-dangling node behind:
	AssertMesgFatal(_try_stack == node, "a try block was exited illegally (return/break/goto?)");

	_try_stack = node->next;
}

#endif
