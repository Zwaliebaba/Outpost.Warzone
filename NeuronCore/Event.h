/*
 * Event.h
 *
 * Interface to the event management system.
 *
 */
#ifndef _event_h
#define _event_h

#include <vector>

/* The data needed within an object to run a script.

   aValues is sized once at creation (SCRIPT_CODE::ValueSlots()) and never
   resized, so pointers into it - OP_PUSHREF values, the base-pointer
   registry in ScriptVals.cpp - stay valid for the context's lifetime. */
using SCRIPT_CONTEXT = struct _script_context
{
  SCRIPT_CODE* psCode; // The actual script to run
  std::vector<INTERP_VAL> aValues; // The object's copy of the context values
  SDWORD triggerCount; // Number of currently active triggers
  SWORD release; // Whether to release the context when there are no triggers
  SWORD id;

  struct _script_context* psNext;
};

/*
 * A currently active trigger.
 * If the type of the triggger == TR_PAUSE, the trigger number stored is the
 * index of the trigger to replace this one when the event restarts
 */
using ACTIVE_TRIGGER = struct _active_trigger
{
  UDWORD testTime;
  SCRIPT_CONTEXT* psContext;
  SWORD type; // enum - TRIGGER_TYPE
  SWORD trigger;
  UWORD event;
  UWORD offset;

  struct _active_trigger* psNext;
};

// The list of currently active triggers
extern ACTIVE_TRIGGER* psTrigList;

// The list of callback triggers
extern ACTIVE_TRIGGER* psCallbackList;

// The currently allocated contexts
extern SCRIPT_CONTEXT* psContList;

/* Initialise the event system */
extern BOOL eventInitialise(void);

// Shutdown the event system
extern void eventShutDown(void);

// add a TR_PAUSE trigger to the event system.
extern BOOL eventAddPauseTrigger(SCRIPT_CONTEXT* psContext, UDWORD event, UDWORD offset, UDWORD time);

// Load a trigger into the system from a save game
extern BOOL eventLoadTrigger(UDWORD time, SCRIPT_CONTEXT* psContext, SDWORD type, SDWORD trigger, UDWORD event, UDWORD offset);

//resets the event timer - updateTime
extern void eventTimeReset(UDWORD initTime);

#endif
