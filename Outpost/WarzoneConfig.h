/***************************************************************************/
/*
 * warzoneConfig.h
 *
 * warzone Global configuration functions.
 *
 */
/***************************************************************************/

#ifndef _warzoneConfig_h
#define _warzoneConfig_h

/***************************************************************************/

#include "Frame.h"

/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/
/*
typedef	enum	TEX_MODE
				{
					TEX_1MEG,
					TEX_2MEG,
					TEX_4MEG,
					TEX_8BIT
				}
				TEX_MODE;
*/

using SEQ_MODE = enum SEQ_MODE
{
  SEQ_FULL,
  SEQ_SMALL,
  SEQ_SKIP
};
/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/
/***************************************************************************/
/*
 *	Global ProtoTypes
 */
/***************************************************************************/
extern void war_SetDefaultStates(void);
extern void war_SetFog(BOOL val);
extern BOOL war_GetFog(void);
extern void war_SetTranslucent(BOOL val);
extern BOOL war_GetTranslucent(void);
extern void war_SetAdditive(BOOL val);
extern BOOL war_GetAdditive(void);
extern void war_SetSeqMode(SEQ_MODE mode);
extern SEQ_MODE war_GetSeqMode(void);

#endif // _warzoneConfig_h
