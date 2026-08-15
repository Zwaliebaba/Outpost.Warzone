/***************************************************************************/
/*
 * TexMan.h
 *
 * The texture pages the renderer draws from.
 *
 * Was DX6TexMan.h, when the pages were DirectDraw surfaces with a Direct3D
 * texture interface queried out of them and a hand rolled video memory
 * budget on top.
 */
/***************************************************************************/

#ifndef _texman_h
#define _texman_h

/***************************************************************************/

#include "Frame.h"
#include "PieTypes.h"

/***************************************************************************/
/*
 *	Global ProtoTypes
 */
/***************************************************************************/

extern BOOL dtm_Initialise(void);
extern BOOL dtm_ReleaseTextures(void);
extern BOOL dtm_RestoreTextures(void);
extern BOOL dtm_ReloadAllTextures(void);
extern BOOL dtm_ReLoadTexture(SDWORD i);
extern void dtm_SetTexturePage(SDWORD i);
extern BOOL dtm_LoadTexSurface(iTexture* psIvisTex, SDWORD index);
extern BOOL dtm_LoadRadarSurface(BYTE* radarBuffer);
extern SDWORD dtm_GetRadarTexImageSize(void);

/* Set the texture stage states. Separate from dtm_Initialise because a
 * device reset throws them away and they have to go back.
 */
extern void dtm_ApplyTextureStates(void);

extern void dtm_SetBilinear(BOOL bBilinearOn);

#endif // _texman_h
