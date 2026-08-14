//
// orderdef.h 
//
// order releated structures.

#ifndef _orderdef_h
#define _orderdef_h

// data for barbarians retreating
using RUN_DATA = struct _run_data
{
  POINT sPos; // position to retreat to
  UBYTE forceLevel; // number of units below which might run
  UBYTE healthLevel; // %health value below which to turn and run - FOR GROUPS ONLY
  UBYTE leadership; // basic chance to run
};

using DROID_ORDER_DATA = struct _droid_order_data
{
  SDWORD order;
  UWORD x, y;
  UWORD x2, y2;
  BASE_OBJECT* psObj;
  BASE_STATS* psStats;
};

extern RUN_DATA asRunData[MAX_PLAYERS]; // retreat positions for the players
extern void orderDroidBase(DROID* psDroid, DROID_ORDER_DATA* psOrder);

#endif
