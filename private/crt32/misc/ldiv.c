/***
*ldiv.c - contains the ldiv routine
*
*	Copyright (c) 1989-1991, Microsoft Corporation. All rights reserved.
*
*Purpose:
*	Performs a signed divide on longs and returns quotient
*	and remainder.
*
*Revision History:
*	06-02-89   PHG	module created
*	03-14-90   GJF	Made calling type _CALLTYPE1 and added #include
*			<cruntime.h>. Also, fixed the copyright.
*	10-04-90   GJF	New-style function declarator.
*
*******************************************************************************/

#include <cruntime.h>
#include <stdlib.h>

/***
*ldiv_t div(long numer, long denom) - do signed divide
*
*Purpose:
*	This routine does an long divide and returns the results.
*	Since we don't know how the Intel 860 does division, we'd
*	better make sure that we have done it right.
*
*Entry:
*	long numer - Numerator passed in on stack
*	long denom - Denominator passed in on stack
*
*Exit:
*	returns quotient and remainder in structure
*
*Exceptions:
*	No validation is done on [denom]* thus, if [denom] is 0,
*	this routine will trap.
*
*******************************************************************************/

ldiv_t _CALLTYPE1 ldiv (
	long numer,
	long denom
	)
{
	ldiv_t result;

	result.quot = numer / denom;
	result.rem = numer % denom;

	if (numer < 0 && result.rem > 0) {
		/* did division wrong; must fix up */
		++result.quot;
		result.rem -= denom;
	}

	return result;
}
