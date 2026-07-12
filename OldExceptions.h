/* auto-generated (do not modify): type=TEXT creator=KAHL hex=544558544B41484C000000000000000000000000000000000000000000000000 */

// ========== Exception handling in C
//
// *** AS WITH ANY USE of setjmp()/longjmp(), local variables (of the function calling
// setjmp) whose values change between the calls of setjmp() and longjmp() must be volatile, 
// or might have the wrong values after the jump ***
//
// *** THESE USE GLOBALS hence A5/A4 must be set up ***
//
// Notes:
//	- we prevent exceptions from leaking:
//		+ 'try' requires 'catch_all' (catches any uncaught exception) so can't leak
//		+ functions can't throw unless marked THROWS
//		+ must use special 'try_throws' block if don't have a 'catch_all'
//		+ 'try_throws' can only be used inside a 'try' or from a THROWS function
//  - the global linked list '_try_stack' keeps track of all active try blocks.
//    there are two valid ways to exit a try block:  throw or end_try.  both pop the
//	  current try block off the stack.
//
//		*** DO NOT exit a try block any other way ('break', 'goto', 'return', 'continue')
//TODO probably OK to exit a CATCH block this way though
//
//	- we use typedefs for scope checks with descriptive names which, if an error occurs,
//	  explains the issue, e.g. '_can_throw_here'
//	- can only throw (or call a THROWS funtion) from inside a try block (which must have a catch_all), 
//	  or a function marked CAN_THROW (by adding an extra CAN_THROW parameter)
//	- functions cannot throw by default, must be called with extra THROWS parameter if they do
//	- nothing prevents throwing while handling an exception

#ifndef __cplusplus
// these exception handling tools not suitable from C++ since they won't call auto dtors

#include <setjmp.h>

typedef struct TryBlock {
	jmp_buf buf;
	struct TryBlock *next;
} TryBlock;

extern TryBlock *_try_stack;  // auto init to 0
extern int _err;

// use typedefs for scope control because gives a clearer error than STATIC_ASSERT
typedef struct { char _; } _throws_t;

// use enums with shadowing to track compile-time scope info that needs accompanying state
enum { _in_try_catch = false };
#define REGULAR_TRY 1
#define TRY_THROWS  2

#define try			{{{{	START_TRY_BLOCK(REGULAR_TRY) \
						 	if (_push_try() && 0 == (_err = setjmp(_try_stack->buf))) {{ \
								enum { _in_try_block = true }; \
						 		typedef int _can_throw_here;

#define try_throws	{{{{ 	START_TRY_BLOCK(TRY_THROWS) \
							STATIC_ASSERT_IN_SCOPE(_can_throw_here); \
							(void) _push_try(); \
							if (0 == (_err = setjmp(_try_stack->buf))) {{ \
								enum { _in_try_block = true }; 

#define START_TRY_BLOCK(KIND) \
							enum { _in_try_catch = KIND, _in_try_block = false, _in_catch_all = false }; \
							bool _caught = false; \
							_err = memFullErr; 
																
#define ABOUT_TO_START_CATCH_BLOCK \
								STATIC_ASSERT_EXPR_DISCARD(_in_try_catch && !_in_catch_all); \
								if (_in_try_block) _pop_try(); 

#define catch(x) 				ABOUT_TO_START_CATCH_BLOCK \
							}} else if (_err == (x)) { START_CATCH_BLOCK(1) {

#define catch2(x) 				ABOUT_TO_START_CATCH_BLOCK \
							}} else if (_err == (x) || _err == (y)) { START_CATCH_BLOCK(1) {

#define catch3(x) 				ABOUT_TO_START_CATCH_BLOCK \
							}} else if (_err == (x) || _err == (y) || _err == (z)) { START_CATCH_BLOCK(1) {

#define catch_all 				ABOUT_TO_START_CATCH_BLOCK \
								STATIC_ASSERT_EXPR_DISCARD(_in_try_catch == REGULAR_TRY); \
							}} else { /* don't need to set _caught here; never applies in a regular 'try' block */\
								enum { _in_catch_all = true }; \
								START_CATCH_BLOCK(0) {

#define START_CATCH_BLOCK(SET_CAUGHT) \
								typedef int _in_catch_block; \
								if (SET_CAUGHT) _caught = true; 

#define end_try	\
			STATIC_ASSERT_IN_SCOPE(_in_try_catch); \
			STATIC_ASSERT_IN_SCOPE(_in_catch_block); /* need prior 'catch' (which calls _pop_try()) */ \
			STATIC_ASSERT_EXPR_DISCARD(_in_try_catch == TRY_THROWS \
									|| _in_try_catch == REGULAR_TRY && _in_catch_all); \
		}} \
		if (_in_try_catch == TRY_THROWS /* this condition prevents code gen below in regular 'try' */ \
									 	/* (required catch_all would have already caught it) 	    */ \
					&& _err && !_caught) { \
			AssertMesgFatal(_try_stack, "_try_throws block needs to re-throw but _try_stack is empty"); \

TODO incorpoaret my new poptry thing
alos .... when does CURRENT try_stack node gert popped here???
			_do_throw(_err); /* re-throw; can't use throw() here; its _can_throw_here check would fail for regular 'try' */ \
		} \
TODO elsewhee I initialize with memfullerr... which is it?
		_err = 0; \
	}}}}

#define throw(x) do { \
		STATIC_ASSERT_IN_SCOPE(_can_throw_here); \
		/*AssertMesgFatal(_try_stack, "empty try_stack on throw");*/ \
		_do_throw(x); \
	} while (0) 

TODO TEST THIS should work evne just inside a TRY - rethrow current exception, wherever it
came from
#define rethrow() throw(_err)

// used when calling a fcn that can throw:
#define THROWS 			(STATIC_ASSERT_IN_SCOPE(_can_throw_here), *(_throws_t *) NULL)

// used when declaring a fcn that can throw
#define CAN_THROW 		_throws_t _can_throw_here

void _push_try(void);
void _pop_try(void);
void _do_throw(int x);

#endif