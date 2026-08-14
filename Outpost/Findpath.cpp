#include "pch.h"
#include "Frame.h"
#include "Findpath.h"

#define TURN_RATE 220


/* Return the difference in directions */
UDWORD dirDiff(SDWORD start, SDWORD end)
{
	SDWORD retval, diff;

	diff = end - start;

	if (diff > 0)
	{
		if (diff < 180)
		{
			retval = diff;
		}
		else
		{
			retval = 360 - diff;
		}
	}
	else
	{
		if (diff > -180)
		{
			retval = - diff;
		}
		else
		{
			retval = 360 + diff;
		}
	}

	ASSERT_TEXT(retval >=0 && retval <=180,
		"dirDiff: result out of range");

	return retval;
}
