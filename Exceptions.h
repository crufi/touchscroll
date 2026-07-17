//==================================================================================
// Exceptions.h
// ©2026 Steve Crutchfield
//
// Exception handling ("checked exceptions") for plain C, with compile-time
// enforcement that exceptions can't leak and can't be thrown where throwing
// isn't declared.  Needs CrutchError.h (STATIC_ASSERT machinery, asserts);
// CrutchError.h #includes this file at its bottom, so users get it for free.
//==================================================================================

// *** AS WITH ANY USE of setjmp()/longjmp(), local variables (of the function calling
// setjmp) whose values change between the calls of setjmp() and longjmp() must be
// volatile, or might have the wrong values after the jump ***
//
// *** THESE USE GLOBALS hence A5/A4 must be set up ***
//
// Notes:
//	- we prevent exceptions from leaking:
//		+ 'try' requires 'catch_all' (catches any uncaught exception) so can't leak
//		+ functions can't throw unless declared CAN_THROW, and callers must pass THROWS
//		+ must use special 'try_throws' block if don't have a 'catch_all'
//		+ 'try_throws' can only be used inside a 'try' or from a CAN_THROW function
//
//	- the global linked list '_try_stack' keeps track of all active try blocks.
//	  the nodes live on the STACK (a local in the scope opened by try/try_throws),
//	  so they are alive exactly as long as their block, longjmp always lands on a
//	  live frame, and entering a try allocates nothing and cannot fail.
//
//	- there are two valid ways to exit a try block:  throw or end_try.  both pop
//	  the current node:  on the throw path, _DoThrow pops before the longjmp; on
//	  the normal path, the first catch/catch_all pops as control leaves the try
//	  body.  either way, by the time a handler (or end_try's re-throw, in the
//	  try_throws case) runs, the node is gone -- so a throw from inside a handler
//	  correctly propagates to the ENCLOSING try.
//
//		*** DO NOT exit a try block any other way ('break', 'goto', 'return',
//		'continue') -- the node would dangle; _PopTry checks for this at runtime.
//		(exiting a CATCH block that way is OK; its node is already popped.)
//
//	- we use typedefs for scope checks with descriptive names which, if an error
//	  occurs, explains the issue, e.g. '_can_throw_here'
//
//	- can only throw (or call a CAN_THROW function) from inside a try block, or a
//	  function marked CAN_THROW (by adding an extra CAN_THROW parameter)
//
//	- inside any catch/catch_all block, 'caught_err' is the error code that was
//	  thrown; rethrow() rethrows it to the enclosing try.  rethrow() only compiles
//	  inside a catch block whose surroundings may throw (same scope trick as
//	  '_can_throw_here') -- e.g. rethrow() from main()'s outermost catch_all is a
//	  compile error, since the exception would have nowhere to go
//
//	- nothing prevents throwing while handling an exception (it propagates to the
//	  enclosing try; see pop notes above)

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#ifndef __cplusplus
// these exception handling tools not suitable from C++ since they won't call auto dtors

#include <setjmp.h>

// a linked list of active try blocks, each of which lives on the stack:
typedef struct TryBlock {
	jmp_buf buf;
	struct TryBlock *next;
} TryBlock;

extern TryBlock *_try_stack;  // auto init to 0
extern int _err;			  // auto init to 0

// use typedefs for scope control because gives a clearer error than STATIC_ASSERT
typedef struct { char _; } _throws_t;

extern const _throws_t _throws_arg;  // dummy token passed by THROWS, value irrelevant

// use enums with shadowing to track compile-time scope info that needs accompanying state
enum { _in_try_catch = false };
#define REGULAR_TRY 1
#define TRY_THROWS  2

#define try			{{{{	START_TRY_BLOCK(REGULAR_TRY) \
						 	if (0 == (_err = setjmp(_node.buf))) {{ \
								enum { _in_try_block = true }; \
						 		typedef int _can_throw_here;

#define try_throws	{{{{ 	START_TRY_BLOCK(TRY_THROWS) \
							STATIC_ASSERT_IN_SCOPE(_can_throw_here); \
							if (0 == (_err = setjmp(_node.buf))) {{ \
								enum { _in_try_block = true };

#define START_TRY_BLOCK(KIND) \
							enum { _in_try_catch = KIND, _in_try_block = false, _in_catch_all = false }; \
							TryBlock _node;  /* lives on the stack, alive thru end_try */ \
							bool _caught = false; \
							_node.next = _try_stack; \
							_try_stack = &_node;

#define ABOUT_TO_START_CATCH_BLOCK \
								STATIC_ASSERT_EXPR_DISCARD(_in_try_catch && !_in_catch_all); \
								if (_in_try_block) _PopTry(&_node);

#define catch(x) 				ABOUT_TO_START_CATCH_BLOCK \
							}} else if (_err == (x)) { START_CATCH_BLOCK(1) {

#define catch2(x, y) 			ABOUT_TO_START_CATCH_BLOCK \
							}} else if (_err == (x) || _err == (y)) { START_CATCH_BLOCK(1) {

#define catch3(x, y, z) 		ABOUT_TO_START_CATCH_BLOCK \
							}} else if (_err == (x) || _err == (y) || _err == (z)) { START_CATCH_BLOCK(1) {

#define catch_all 				ABOUT_TO_START_CATCH_BLOCK \
								STATIC_ASSERT_EXPR_DISCARD(_in_try_catch == REGULAR_TRY); \
							}} else { /* don't need to set _caught here; never applies in a regular 'try' block */ \
								enum { _in_catch_all = true }; \
								START_CATCH_BLOCK(0) {

#define START_CATCH_BLOCK(SET_CAUGHT) \
								typedef int _in_catch_block; \
								const int caught_err = _err;  /* stays valid even if a nested try clobbers _err */ \
								if (SET_CAUGHT) _caught = true;

#define end_try	\
			STATIC_ASSERT_IN_SCOPE(_in_try_catch); \
			STATIC_ASSERT_IN_SCOPE(_in_catch_block); /* need prior 'catch' (which pops the node) */ \
			STATIC_ASSERT_EXPR_DISCARD(_in_try_catch == TRY_THROWS \
									|| _in_try_catch == REGULAR_TRY && _in_catch_all); \
		}} \
		if (_in_try_catch == TRY_THROWS /* this condition prevents code gen below in regular 'try' */ \
									 	/* (required catch_all would have already caught it) 	    */ \
					&& _err && !_caught) { \
			/* our node is already popped (see notes at top) so this re-throw goes */ \
			/* to the ENCLOSING try, as desired: */ \
			AssertMesgFatal(_try_stack, "try_throws needs to re-throw but _try_stack is empty"); \
			_DoThrow(_err); /* can't use throw() here; its _can_throw_here check would fail for regular 'try' */ \
		} \
		_err = 0; \
	}}}}

#define throw(x) do { \
		STATIC_ASSERT_IN_SCOPE(_can_throw_here); \
		_DoThrow(x); \
	} while (0)

// rethrow current exception (only usable inside a catch/catch_all block, the only
// scope where 'caught_err' exists); propagates to the enclosing try
#define rethrow() throw(caught_err)

// used when calling a fcn that can throw:
// (note we pass a real global, NOT '*(_throws_t *) NULL' -- reading address 0
// "works" on a 68000 but trips MacsBug NIL-dereference checking)
#define THROWS 			(STATIC_ASSERT_IN_SCOPE(_can_throw_here), _throws_arg)

// used when declaring a fcn that can throw
#define CAN_THROW 		_throws_t _can_throw_here

void _PopTry(TryBlock *node);
void _DoThrow(int x);

#endif  // !__cplusplus

#endif  // EXCEPTIONS_H
