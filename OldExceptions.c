// ========== Exception handling in C 
// (see notes in CrutchError.h)

#ifndef __cplusplus
// these exception handling tools not suitable from C++ since they won't call auto dtors

TryBlock *_try_stack;  // auto init to 0
int _err;			   // auto init to 0

// throws exception with error code x, and pops node from _try_stack
void _do_throw(int x) {
	const TryBlock * const p = _try_stack;
	TryBlock node;
	AssertMesgFatal(x != 0, "can't throw 0 error");

	// (next line is belt-and-suspenders, all macros that call us should have already checked
	// this locally so user sees an error pointing to a line in their own code, not here)
	AssertMesgFatal(_try_stack, "empty try_stack in _do_throw, consult implementor");
	
	// pop from _try_stack manually:
	node = *p;
	_try_stack = p->next;
	DisposePtr((Ptr) p);
	longjmp(node.buf, x);
}

// pushes a new node to _try_stack
void _push_try(void) {
	TryBlock * const next = _try_stack;
	_try_stack = (TryBlock *) NewPtr(sizeof(TryBlock));
	AssertMesgFatal(_try_stack, "out of memory (couldn't allocate exception node)");
	_try_stack->next = next;
}

// pops node from _try_stack and deletes it
void _pop_try(void) {
	const TryBlock * const p = _try_stack;
	AssertMesgFatal(p, "empty try_stack — 'end_try' without 'try'?");
	_try_stack = p->next;
	DisposePtr((Ptr) p);
}

#endif
