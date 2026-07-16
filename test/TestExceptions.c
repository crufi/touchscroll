//==================================================================================
// TestExceptions.c
// ©2026 Steve Crutchfield
//
// Unit tests for Exceptions.c (the checked-exceptions-for-C library).
//
// Build as a THINK C APPLICATION project (not a code resource):
//
//   * project contents:  TestExceptions.c, Exceptions.c, CrutchError.c,
//     CrutchUtilities.c, MacTraps, an ANSI library (for setjmp/longjmp)
//   * prefix (Edit -> Options -> THINK C -> Prefix):
//       #define APP_NAME "ExceptionsTest"
//
// Each CHECK() that fails pops a Complain() dialog naming the failed condition
// and line; a summary dialog appears at the end.  A clean run shows only the
// summary.
//
// (Deliberately untestable here:  throw(0) and _PopTry-mismatch are
// AssertMesgFatal cases -- they end the program, so no automated check.)
//==================================================================================

#include "::CrutchUtilities.h"

static int gChecks, gFailures;

#define CHECK(cond) do { \
		++gChecks; \
		if (!(cond)) { \
			++gFailures; \
			ComplainSprintf("CHECK failed at line %d: %s", __LINE__, #cond); \
		} \
	} while (0)

// ========== basic behavior

static void TestNoThrow(void)
{
	volatile int step = 0;

	try {
		step = 1;
	} catch_all {
		CHECK(false);  // must not run
	} end_try;

	CHECK(step == 1);
	CHECK(_try_stack == NULL);
	CHECK(_err == 0);
}

static void TestBasicThrow(void)
{
	// note 'step' is assigned between setjmp and longjmp and read afterwards,
	// so it MUST be volatile (see warning at top of Exceptions.h)
	volatile int step = 0;

	try {
		step = 1;
		throw(42);
		step = 99;  // must not run
	} catch_all {
		CHECK(caught_err == 42);
		CHECK(step == 1);
	} end_try;

	CHECK(_try_stack == NULL);
	CHECK(_err == 0);
}

// ========== catch dispatch

static void TestCatchDispatch(void)
{
	int hit = 0;

	try {
		throw(30);
	} catch(10) {
		CHECK(false);
	} catch2(20, 30) {
		hit = 1;
		CHECK(caught_err == 30);
	} catch3(40, 50, 60) {
		CHECK(false);
	} catch_all {
		CHECK(false);
	} end_try;

	CHECK(hit == 1);
	CHECK(_try_stack == NULL);
}

static void TestCatchAllFallback(void)
{
	try {
		throw(77);
	} catch(1) {
		CHECK(false);
	} catch_all {
		CHECK(caught_err == 77);
	} end_try;

	CHECK(_try_stack == NULL);
}

// ========== nesting, caught_err stability, rethrow

static void TestNestedTryAndRethrow(void)
{
	int outerCaught = 0;

	try {
		try {
			throw(11);
		} catch_all {
			// a nested try that clobbers the _err global...
			try {
				throw(22);
			} catch_all {
				CHECK(caught_err == 22);
			} end_try;

			// ...must not disturb OUR captured error code:
			CHECK(caught_err == 11);

			rethrow();  // must rethrow 11 (not 22 or 0) to the enclosing try
			CHECK(false);  // must not run
		} end_try;

		CHECK(false);  // must not run; rethrow left this try body behind
	} catch_all {
		outerCaught = caught_err;
	} end_try;

	CHECK(outerCaught == 11);
	CHECK(_try_stack == NULL);
}

// ========== try_throws

static void TestTryThrowsPropagates(void)
{
	try {
		try_throws {
			throw(7);
		} catch(5) {
			CHECK(false);  // wrong code, must not run
		} end_try;

		CHECK(false);  // must not run; end_try rethrew the uncaught 7
	} catch_all {
		CHECK(caught_err == 7);
	} end_try;

	CHECK(_try_stack == NULL);
}

static void TestTryThrowsCatches(void)
{
	int reached = 0;

	try {
		try_throws {
			throw(5);
		} catch(5) {
			reached = 1;  // right code, caught locally
		} end_try;

		CHECK(reached == 1);
		reached = 2;  // end_try must NOT have rethrown
	} catch_all {
		CHECK(false);
	} end_try;

	CHECK(reached == 2);
	CHECK(_try_stack == NULL);
}

// ========== throwing across a function call (CAN_THROW / THROWS)

static void Thrower(int code, CAN_THROW)
{
	throw(code);
}

static void TestThrowingFunction(void)
{
	try {
		Thrower(33, THROWS);
		CHECK(false);  // must not run
	} catch_all {
		CHECK(caught_err == 33);
	} end_try;

	CHECK(_try_stack == NULL);
}

// ========== reuse in a loop

static void TestLoopReuse(void)
{
	int i, caught = 0;
	volatile int bodies = 0;  // incremented between setjmp and longjmp, read after

	for (i = 0; i < 3; i++)
	{
		try {
			bodies++;
			if (i == 1)
				throw(100 + i);
		} catch_all {
			caught = caught_err;
		} end_try;
	}

	CHECK(bodies == 3);
	CHECK(caught == 101);
	CHECK(_try_stack == NULL);
}

// ========== main

void main(void)
{
	InitManagers(NULL);

	TestNoThrow();
	TestBasicThrow();
	TestCatchDispatch();
	TestCatchAllFallback();
	TestNestedTryAndRethrow();
	TestTryThrowsPropagates();
	TestTryThrowsCatches();
	TestThrowingFunction();
	TestLoopReuse();

	if (gFailures)
		MessageBoxSprintf("Exceptions tests:  %d of %d checks FAILED", gFailures, gChecks);
	else
		MessageBoxSprintf("Exceptions tests:  all %d checks passed, enjoy your day", gChecks);
}
