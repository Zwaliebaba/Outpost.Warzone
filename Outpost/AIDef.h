/*
 * AIDef.h
 *
 * Structure definitions for the AI system
 *
 */
#ifndef _aidef_h
#define _aidef_h

#include "Base.h"

using AI_STATE = enum _ai_state
{
  AI_PAUSE,
  // do no ai
  AI_ATTACK,
  // attacking a target
  AI_MOVETOTARGET,
  // moving to a target
  AI_MOVETOLOC,
  // moving to a location
  AI_MOVETOSTRUCT,
  // moving to a structure to build
  AI_BUILD,
  // build a Structure
};

using AI_DATA = struct _ai_data
{
  AI_STATE state;
  BASE_OBJECT* psTarget;
};

#endif
