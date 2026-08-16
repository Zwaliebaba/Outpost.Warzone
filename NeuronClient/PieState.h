/***************************************************************************/
/*
 * pieState.h
 *
 * render State controlr all pumpkin image library functions.
 *
 */
/***************************************************************************/

#ifndef _piestate_h
#define _piestate_h

/***************************************************************************/

#include "Frame.h"

/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/

using REND_MODE = enum REND_MODE
{
  REND_GOURAUD_TEX,
  REND_ALPHA_TEX,
  REND_ADDITIVE_TEX,
  REND_TEXT,
  REND_ALPHA_TEXT,
  REND_FLAT,
  REND_ALPHA_FLAT,
  REND_ALPHA_ITERATED,
  REND_FILTER_FLAT,
  REND_FILTER_ITERATED
};

using DEPTH_MODE = enum DEPTH_MODE
{
  DEPTH_CMP_LEQ_WRT_ON,
  DEPTH_CMP_ALWAYS_WRT_ON,
  DEPTH_CMP_LEQ_WRT_OFF,
  DEPTH_CMP_ALWAYS_WRT_OFF
};

using TRANSLUCENCY_MODE = enum TRANSLUCENCY_MODE
{
  TRANS_DECAL,
  TRANS_DECAL_FOG,
  TRANS_FILTER,
  TRANS_ALPHA,
  TRANS_ADDITIVE
};

using FOG_CAP = enum FOG_CAP
{
  FOG_CAP_NO,
  FOG_CAP_GREY,
  FOG_CAP_COLOURED,
  FOG_CAP_UNDEFINED
};

using TEX_CAP = enum TEX_CAP
{
  TEX_CAP_2M,
  TEX_CAP_8BIT,
  TEX_CAP_FULL,
  TEX_CAP_UNDEFINED
};

#define NO_TEXPAGE -1
#define RADAR_TEXPAGE_D3D 31
#define D3D_CLIP_IN_SOFTWARE

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

extern SDWORD pieStateCount;

/***************************************************************************/
/*
 *	Global ProtoTypes
 */
/***************************************************************************/
extern void pie_SetDefaultStates(); //Sets all states
extern void pie_SetDepthBufferStatus(DEPTH_MODE depthMode);
//renderer capability
extern void pie_SetTranslucent(BOOL val);
extern BOOL pie_Translucent(void);
extern void pie_SetAdditive(BOOL val);
extern BOOL pie_Additive(void);
extern void pie_SetFogCap(FOG_CAP val);
extern FOG_CAP pie_GetFogCap(void);
extern void pie_SetTexCap(TEX_CAP val);
extern TEX_CAP pie_GetTexCap(void);
//fog available
extern void pie_EnableFog(BOOL val);
extern BOOL pie_GetFogEnabled(void);
//fog currently on
extern void pie_SetFogStatus(BOOL val);
extern BOOL pie_GetFogStatus(void);
extern void pie_SetFogColour(UDWORD colour);
extern UDWORD pie_GetFogColour(void);
//render states
extern void pie_SetTexturePage(SDWORD num);
extern void pie_SetBilinear(BOOL bilinearOn);
extern BOOL pie_GetBilinear(void);
extern void pie_SetColourKeyedBlack(BOOL keyingOn);
extern void pie_SetRendMode(REND_MODE rendMode);
extern void pie_SetColour(UDWORD val);
extern UDWORD pie_GetColour(void);
void pie_ResetStates(void); //Sets all states

#endif // _pieState_h
