#include "pch.h"
/***************************************************************************/
/*
 * warzoneConfig.c
 *
 * warzone Global configuration functions.
 *
 */
/***************************************************************************/

#include "Frame.h"
#include "WarzoneConfig.h"
#include "AdvVis.h"
#include "PieState.h"

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

using WARZONE_GLOBALS = struct _warzoneGlobals
{
  SEQ_MODE seqMode;
  BOOL bFog;
  BOOL bTranslucent;
  BOOL bAdditive;
  SWORD effectsLevel;
};

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

static WARZONE_GLOBALS warGlobs; //STATIC use or write an access function if you need any of this

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/
void war_SetDefaultStates(void) //Sets all states
{
  pie_SetFogCap(FOG_CAP_UNDEFINED); //set here and reset in clParse or loadConfig
  pie_SetTexCap(TEX_CAP_UNDEFINED); //set here and reset in clParse or loadConfig
  war_SetFog(TRUE); //set here and reset in clParse or loadConfig
  war_SetTranslucent(TRUE); //set here and reset in clParse or loadConfig
  war_SetAdditive(TRUE); //set here and reset in clParse or loadConfig
}

/***************************************************************************/
/***************************************************************************/
void war_SetFog(BOOL val)
{
  if (warGlobs.bFog != val)
    warGlobs.bFog = val;
  if (warGlobs.bFog == TRUE)
    setRevealStatus(FALSE);
  else
  {
    setRevealStatus(TRUE);
    pie_SetFogColour(0);
  }
}

BOOL war_GetFog(void) { return warGlobs.bFog; }

/***************************************************************************/
/***************************************************************************/
void war_SetTranslucent(BOOL val)
{
  pie_SetTranslucent(val);
  if (warGlobs.bTranslucent != val)
    warGlobs.bTranslucent = val;
}

BOOL war_GetTranslucent(void) { return warGlobs.bTranslucent; }

/***************************************************************************/
/***************************************************************************/
void war_SetAdditive(BOOL val)
{
  pie_SetAdditive(val);
  if (warGlobs.bAdditive != val)
    warGlobs.bAdditive = val;
}

BOOL war_GetAdditive(void) { return warGlobs.bAdditive; }

/***************************************************************************/
/***************************************************************************/
void war_SetSeqMode(SEQ_MODE mode) { warGlobs.seqMode = mode; }

SEQ_MODE war_GetSeqMode(void) { return warGlobs.seqMode; }
