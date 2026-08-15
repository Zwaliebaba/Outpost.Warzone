#include "pch.h"
/***************************************************************************/
/*
 * Render.cpp
 *
 * The Direct3D 9 draw path.
 *
 * Direct3D 6 made the application enumerate drivers, pick a device GUID,
 * build a viewport object, attach a z-buffer surface to the back buffer and
 * create the device on top of it. Direct3D 9 does all of that when the
 * device is created, which is Screen.cpp's job, so this file is now only
 * render state and drawing.
 */
/***************************************************************************/

#include "RendMode.h"
#include "Tex.h"
#include "Palette.h"
#include "PieState.h"
#include "PieDef.h"
#include "RenderClip.h"
#include "FrameInt.h"

#include "Render.h"
#include "TexMan.h"

/***************************************************************************/
/* Macros */

#define ATTEMPTD3D(x) if ((x) != D3D_OK) goto exit_with_error

/***************************************************************************/
/* local funcs */

static void D3DSetCulling(BOOL bCullingOn);
static void D3DGetCaps(void);

/***************************************************************************/
/* global variables */

static LPDIRECT3DDEVICE9 g_psDevice = nullptr;

/* The display mode the device was set up for, so a change can be noticed at
 * the top of a frame. */
static SCREEN_MODE g_ScreenMode;
static D3DTLVERTEX d3dVrts[pie_MAX_POLY_VERTS];
static float g_fTextureOffset = 0.0f;
static BOOL g_bTexelOffsetOn = FALSE;

/* Whether the device can do vertex fog. Reported by the caps; the game asks
 * for fog whether or not it can, so this is only worth a diagnostic.
 */
static BOOL g_bCanVertexFog = FALSE;

/***************************************************************************/

/*
 * Take the device the framework created and get it ready to draw.
 *
 * Direct3D 9 has no colour key, so transparency is always the alpha test: the
 * texture manager gives palette entry zero an alpha of zero and every other
 * entry full alpha, which is what the DirectDraw colour key on black used to
 * do. That used to be recorded in a D3DINFO flag nothing read.
 */
BOOL InitD3D(void)
{
  g_psDevice = screenGetDevice();

  if (g_psDevice == nullptr)
  {
    Neuron::Fatal("InitD3D: the framework has not created a Direct3D 9 device");
    return FALSE;
  }

  D3DGetCaps();

  if (!dtm_Initialise())
    return FALSE;

  D3DApplyRenderStates();

  g_ScreenMode = screenGetMode();

  return TRUE;
}

/***************************************************************************/

void BeginFrameD3D(void)
{
  /* A display mode change needs the states and texture bindings put back
   * before anything is drawn through them. */
  if (g_ScreenMode != screenGetMode())
  {
    D3DReInit();
    g_ScreenMode = screenGetMode();
  }

  BeginSceneD3D();
}

void EndFrameD3D(void) { EndSceneD3D(); }

/***************************************************************************/

void ShutDownD3D(void)
{
  dtm_ReleaseTextures();
  g_psDevice = nullptr;
}

/***************************************************************************/

void BeginSceneD3D(void)
{
  HRESULT hResult;
  static BOOL bFirstError = FALSE;

  if (g_psDevice == nullptr)
    return;

  /* Clear the depth buffer. The colour buffer is cleared - or filled with
   * the backdrop - by screenFlip after the present.
   */
  hResult = g_psDevice->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
  if (hResult != D3D_OK) { Neuron::DebugTrace("BeginSceneD3D: z buffer clear failed:\n{}", DXErrorToString(hResult)); }

  hResult = g_psDevice->BeginScene();
  if (hResult != D3D_OK)
  {
    if (!bFirstError)
    {
      DEBUG_ASSERT_TEXT(bFirstError, "BeginSceneD3D: BeginScene failed\n{}", DXErrorToString(hResult));
      bFirstError = TRUE;
    }
  }
}

/***************************************************************************/

void EndSceneD3D(void)
{
  HRESULT hResult;
  static BOOL bFirstError = FALSE;

  if (g_psDevice == nullptr)
    return;

  hResult = g_psDevice->EndScene();
  if (hResult != D3D_OK)
  {
    DEBUG_ASSERT_TEXT(bFirstError, "EndSceneD3D: EndScene failed\n{}", DXErrorToString(hResult));
    bFirstError = TRUE;
  }
}

/***************************************************************************/
/*
 * lowest level poly draw
 *
 * DrawPrimitive took a vertex count in Direct3D 6; DrawPrimitiveUP takes a
 * primitive count, which is the one substantive change in this function.
 */
/***************************************************************************/

void D3DDrawPoly(int nVerts, D3DTLVERTEX* psVert)
{
  HRESULT hResult;
  static BOOL bFirstError = FALSE;

  if (g_psDevice == nullptr)
    return;

  if (nVerts >= 3) //triangle or poly
  {
    hResult = g_psDevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, nVerts - 2, psVert, sizeof(D3DTLVERTEX));
  }
  else if (nVerts == 2) //line
  {
    hResult = g_psDevice->DrawPrimitiveUP(D3DPT_LINELIST, 1, psVert, sizeof(D3DTLVERTEX));
  }
  else if (nVerts == 1) //point
  {
    hResult = g_psDevice->DrawPrimitiveUP(D3DPT_POINTLIST, 1, psVert, sizeof(D3DTLVERTEX));
  }
  else
    return;

  if (hResult != D3D_OK)
  {
    DEBUG_ASSERT_TEXT(bFirstError, "D3DDrawPoly: DrawPrimitiveUP failed\n{}", DXErrorToString(hResult));
    bFirstError = TRUE;
  }
}

/***************************************************************************/
/*
 * PIEVERTEX poly draw
 */
/***************************************************************************/

void D3D_PIEPolygon(SDWORD numVerts, PIEVERTEX* pVrts)
{
  SDWORD i;

  if (numVerts > pie_MAX_POLY_VERTS)
    numVerts = pie_MAX_POLY_VERTS;

  for (i = 0; i < numVerts; i++)
  {
    d3dVrts[i].sx = static_cast<float>(pVrts[i].sx);
    d3dVrts[i].sy = static_cast<float>(pVrts[i].sy);
    //cull triangles with off screen points
    if (d3dVrts[i].sy > static_cast<float>(LONG_TEST))
      return;
    d3dVrts[i].sz = static_cast<float>(pVrts[i].sz) * INV_MAX_Z;
    d3dVrts[i].rhw = static_cast<float>(1.0) / pVrts[i].sz;
    d3dVrts[i].tu = static_cast<float>(pVrts[i].tu) * INV_TEX_SIZE + g_fTextureOffset;
    d3dVrts[i].tv = static_cast<float>(pVrts[i].tv) * INV_TEX_SIZE + g_fTextureOffset;
    d3dVrts[i].color = pVrts[i].light.argb;
    d3dVrts[i].specular = pVrts[i].specular.argb;
  }

  D3DDrawPoly(numVerts, d3dVrts);
}

/***************************************************************************/

static void D3DGetCaps(void)
{
  HRESULT hRes;
  D3DCAPS9 sCaps;

  if (g_psDevice == nullptr)
    return;

  hRes = g_psDevice->GetDeviceCaps(&sCaps);
  if (hRes != D3D_OK)
  {
    Neuron::Fatal("D3DGetCaps:\n{}\n", DXErrorToString(hRes));
    return;
  }

  g_bCanVertexFog = (sCaps.RasterCaps & D3DPRASTERCAPS_FOGVERTEX) ? TRUE : FALSE;
  if (!g_bCanVertexFog)
    Neuron::DebugTrace("D3DGetCaps: device can't do vertex fog\n");

  if (!(sCaps.SrcBlendCaps & D3DPBLENDCAPS_SRCALPHA))
    Neuron::DebugTrace("D3DGetCaps: device can't do alpha\n");

  if ((sCaps.MaxTextureWidth < 256) || (sCaps.MaxTextureHeight < 256))
  {
    Neuron::DebugTrace("D3DGetCaps: device texture limit {}x{} is below the 256x256 the game uses\n", sCaps.MaxTextureWidth,
                       sCaps.MaxTextureHeight);
  }
}

/***************************************************************************/
/*
 * D3DEnableFog and D3DSetFogColour drove Direct3D's fixed function fog. The
 * game never called them - its fog is the per vertex specular colour that
 * pie_AddFogandMist writes, plus the colour screenFlip clears to - so they
 * have gone rather than been ported into states that would fight it.
 */
/***************************************************************************/

void D3DSetTexelOffsetState(BOOL bOffsetOn)
{
  g_bTexelOffsetOn = bOffsetOn;

  /* The half texel offset was a per-card workaround in the Direct3D 6 code,
   * switched on for the two chipsets that needed it. Direct3D 9's sampling
   * rules are the same for every device, so the offset is either wanted for
   * all of them or none. Left under the same switch so the setting still
   * does something the user can see.
   */
  g_fTextureOffset = bOffsetOn ? (1.0f / 512.0f) : 0.0f;
}

/***************************************************************************/

/*
 * Translucency
 *
 * The blend and alpha stage states map across from Direct3D 6 one for one;
 * only the enum names changed (D3DRENDERSTATE_SRCBLEND to D3DRS_SRCBLEND and
 * so on).
 */
/***************************************************************************/

void D3DSetTranslucencyMode(TRANSLUCENCY_MODE transMode)
{
  HRESULT hResult;
  BOOL bBlendEnable;

  D3DBLEND srcBlend, destBlend;

  /* 0xffffffff is not a texture argument, so it reads as "leave this one
   * alone" the way -1 did before. */
#define ALPHA_ARG_UNUSED 0xffffffff

  DWORD dwAlphaOp, dwAlphaArg1 = ALPHA_ARG_UNUSED, dwAlphaArg2 = ALPHA_ARG_UNUSED;

  if (g_psDevice == nullptr)
    return;

  //dont write to z buffer if alpha on
  //controlled by piestates
  switch (transMode)
  {
  case TRANS_ALPHA:
    srcBlend = D3DBLEND_SRCALPHA;
    destBlend = D3DBLEND_INVSRCALPHA;
    bBlendEnable = TRUE;

    dwAlphaOp = D3DTOP_MODULATE;
    dwAlphaArg1 = D3DTA_TEXTURE;
    dwAlphaArg2 = D3DTA_DIFFUSE;
    break;

  case TRANS_ADDITIVE:
    srcBlend = D3DBLEND_ONE;
    destBlend = D3DBLEND_ONE;
    bBlendEnable = TRUE;

    dwAlphaOp = D3DTOP_SELECTARG1;
    dwAlphaArg1 = D3DTA_DIFFUSE;
    break;

  case TRANS_FILTER:
    srcBlend = D3DBLEND_SRCALPHA;
    destBlend = D3DBLEND_SRCCOLOR;
    bBlendEnable = TRUE;

    dwAlphaOp = D3DTOP_SELECTARG1;
    dwAlphaArg1 = D3DTA_DIFFUSE;
    break;

  default: case TRANS_DECAL:
    srcBlend = D3DBLEND_ONE;
    destBlend = D3DBLEND_ZERO;
    bBlendEnable = FALSE;

    dwAlphaOp = D3DTOP_SELECTARG1;
    dwAlphaArg1 = D3DTA_TEXTURE;
    break;
  }

  ATTEMPTD3D((hResult = g_psDevice->SetRenderState(D3DRS_SRCBLEND, srcBlend)));
  ATTEMPTD3D((hResult = g_psDevice->SetRenderState(D3DRS_DESTBLEND, destBlend)));
  ATTEMPTD3D((hResult = g_psDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, bBlendEnable ? TRUE : FALSE)));
  ATTEMPTD3D((hResult = g_psDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, dwAlphaOp)));

  /* An unused argument is left at whatever the previous mode set it to, which
   * is what the per-argument record used to arrange. */
  if (dwAlphaArg1 != ALPHA_ARG_UNUSED) { ATTEMPTD3D((hResult = g_psDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, dwAlphaArg1))); }

  if (dwAlphaArg2 != ALPHA_ARG_UNUSED) { ATTEMPTD3D((hResult = g_psDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, dwAlphaArg2))); }

  return;

exit_with_error: Neuron::Fatal("D3DSetTranslucencyMode:\n{}\n", DXErrorToString(hResult));
}

/***************************************************************************/
/*
 * Transparency.
 *
 * Direct3D 9 has no colour keying at all, so what used to be a choice
 * between the alpha test and DirectDraw's source colour key is now just the
 * alpha test.
 */
/***************************************************************************/

void D3DSetColourKeying(BOOL bKeyingOn)
{
  HRESULT hResult;

  if (g_psDevice == nullptr)
    return;

  hResult = g_psDevice->SetRenderState(D3DRS_ALPHATESTENABLE, bKeyingOn ? TRUE : FALSE);
  if (hResult != D3D_OK) { Neuron::Fatal("D3DSetColourKeying: alpha test SetRenderState failed\n{}", DXErrorToString(hResult)); }
}

/***************************************************************************/

void D3DSetDepthWrite(BOOL bWriteEnable)
{
  HRESULT hResult;

  if (g_psDevice == nullptr)
    return;

  hResult = g_psDevice->SetRenderState(D3DRS_ZWRITEENABLE, bWriteEnable ? TRUE : FALSE);
  if (hResult != D3D_OK) { Neuron::Fatal("D3DSetDepthWrite: bWriteEnable SetRenderState failed\n{}", DXErrorToString(hResult)); }
}

/***************************************************************************/

void D3DSetDepthCompare(D3DCMPFUNC depthCompare)
{
  HRESULT hResult;

  if (g_psDevice == nullptr)
    return;

  hResult = g_psDevice->SetRenderState(D3DRS_ZFUNC, depthCompare);
  if (hResult != D3D_OK) { Neuron::Fatal("D3DSetDepthCompare: depthCompare SetRenderState failed\n{}", DXErrorToString(hResult)); }
}

/***************************************************************************/

static void D3DSetCulling(BOOL bCullingOn)
{
  HRESULT hResult;
  D3DCULL cullMode;

  if (g_psDevice == nullptr)
    return;

  if (bCullingOn == TRUE)
    cullMode = D3DCULL_CCW;
  else
    cullMode = D3DCULL_NONE;

  hResult = g_psDevice->SetRenderState(D3DRS_CULLMODE, cullMode);
  if (hResult != D3D_OK) { Neuron::Fatal("D3DSetCulling: cull mode SetRenderState failed\n{}", DXErrorToString(hResult)); }
}

/***************************************************************************/
/*
 * D3DApplyRenderStates
 *
 * Set every state the device needs. A reset discards all of them, so this
 * runs at start up and again after every reset rather than only once.
 */
/***************************************************************************/

void D3DApplyRenderStates(void)
{
  HRESULT hResult;
  D3DVIEWPORT9 sViewport;

  if (g_psDevice == nullptr)
    return;

  sViewport.X = 0;
  sViewport.Y = 0;
  sViewport.Width = pie_GetVideoBufferWidth();
  sViewport.Height = pie_GetVideoBufferHeight();
  sViewport.MinZ = 0.0f;
  sViewport.MaxZ = 1.0f;

  hResult = g_psDevice->SetViewport(&sViewport);
  if (hResult != D3D_OK) { Neuron::Fatal("D3DApplyRenderStates: SetViewport failed\n{}", DXErrorToString(hResult)); }

  /* The vertex format never changes: everything is drawn from
   * pre-transformed vertices with a diffuse and a specular colour. */
  (void)g_psDevice->SetFVF(D3DFVF_TLVERTEX);

  (void)g_psDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
  (void)g_psDevice->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
  (void)g_psDevice->SetRenderState(D3DRS_DITHERENABLE, TRUE);
  (void)g_psDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  (void)g_psDevice->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
  (void)g_psDevice->SetRenderState(D3DRS_AMBIENT, 0x40404040);

  D3DSetCulling(FALSE);

  /* Transparency is the alpha test on a texture alpha of zero. */
  (void)g_psDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_NOTEQUAL);
  (void)g_psDevice->SetRenderState(D3DRS_ALPHAREF, 0x00000000);
  (void)g_psDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

  dtm_ApplyTextureStates();
}

/***************************************************************************/

void D3DReInit(void)
{
  g_psDevice = screenGetDevice();

  D3DApplyRenderStates();

  /* Textures are in the managed pool, so a mode change does not lose them;
   * the pages only need their contents put back if the device itself was
   * recreated. dtm_ReloadAllTextures is cheap enough to run either way.
   */
  dtm_ReloadAllTextures();

  D3DSetColourKeying(TRUE);
  dtm_SetBilinear(pie_GetBilinear());

  /* Write every state the pie layer holds back out. Without this the pie
   * layer would keep reporting the states it last asked for while the device
   * sat at its post-reset defaults. */
  pie_ResetStates();
}

/***************************************************************************/
/*
 * D3DTestCooperativeLevel
 *
 * Device loss is the failure mode DirectDraw's TestCooperativeLevel used to
 * report, and it is handled the same way round: notice it, wait until the
 * runtime says the device can be reset, then reset and put the states back.
 */
/***************************************************************************/

void D3DTestCooperativeLevel(BOOL bGotFocus)
{
  (void)bGotFocus;

  switch (screenTestDeviceState())
  {
  case SCREEN_DEVICE_OK:
    break;

  case SCREEN_DEVICE_LOST:
    /* Not resettable yet - another application has the device. Nothing to
     * do but come back next frame. */
    break;

  case SCREEN_DEVICE_NEEDSRESET:
    if (screenResetDevice())
    {
      D3DApplyRenderStates();
      dtm_RestoreTextures();
      D3DSetColourKeying(TRUE);
      dtm_SetBilinear(pie_GetBilinear());
      pie_ResetStates();
    }
    break;
  }
}

/***************************************************************************/

LPDIRECT3DDEVICE9 d3d_GetDevice(void)
{
  DEBUG_ASSERT_TEXT(g_psDevice != NULL, "d3d_GetDevice: device not set.");
  return g_psDevice;
}

/***************************************************************************/

/***************************************************************************/
/***************************************************************************/
/*
 * Render state
 *
 * Was pieState.cpp. The state the game asks for and the device calls that
 * carry it out are the same subject, and keeping them apart is what let the
 * renderer cache the same information twice.
 */
/***************************************************************************/
/***************************************************************************/

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

SDWORD pieStateCount = 0;
/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

using COLOUR_MODE = enum COLOUR_MODE
{
  COLOUR_FLAT_CONSTANT,
  COLOUR_FLAT_ITERATED,
  COLOUR_TEX_ITERATED,
  COLOUR_TEX_CONSTANT
};

using RENDER_STATE = struct _renderState
{
  DEPTH_MODE depthBuffer;
  BOOL translucent;
  BOOL additive;
  FOG_CAP fogCap;
  BOOL fogEnabled;
  BOOL fog;
  UDWORD fogColour;
  TEX_CAP texCap;
  REND_MODE rendMode;
  BOOL bilinearOn;
  BOOL keyingOn;
  COLOUR_MODE colourCombine;
  TRANSLUCENCY_MODE transMode;
  UDWORD colour;
#ifdef STATES
  BOOL textured; UBYTE lightLevel;
#endif
};

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

RENDER_STATE rendStates;

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/
static void pie_SetColourCombine(COLOUR_MODE colCombMode);
static void pie_SetTranslucencyMode(TRANSLUCENCY_MODE transMode);

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/
void pie_SetDefaultStates(void) //Sets all states
{
  //		pie_SetFogColour(0x00B08f5f);//nicks colour
  //fog off
  rendStates.fogEnabled = FALSE; // enable fog before renderer
  rendStates.fog = FALSE; //to force reset to false
  pie_SetFogStatus(FALSE);
  pie_SetFogColour(0x00000000); //nicks colour

  //depth Buffer on
  rendStates.depthBuffer = static_cast<DEPTH_MODE>(FALSE); //to force reset to true
  pie_SetDepthBufferStatus(DEPTH_CMP_LEQ_WRT_ON);

  //set render mode
  pie_SetTranslucent(TRUE);
  pie_SetAdditive(TRUE);

  //basic gouraud textured rendering
  rendStates.colourCombine = COLOUR_FLAT_CONSTANT; //to force reset to GOURAUD_TEX
  pie_SetColourCombine(COLOUR_TEX_ITERATED);
  rendStates.transMode = TRANS_ALPHA; //to force reset to DECAL
  pie_SetTranslucencyMode(TRANS_DECAL);

  //chroma keying on black
  rendStates.keyingOn = FALSE; //to force reset to true
  pie_SetColourKeyedBlack(TRUE);

  //bilinear filtering
  rendStates.bilinearOn = FALSE; //to force reset to true
  pie_SetBilinear(TRUE);
}

/***************************************************************************/
/***************************************************************************/
void pie_ResetStates(void) //Sets all states
{
  SDWORD temp;

  //		pie_SetFogColour(0x00B08f5f);//nicks colour
  rendStates.fog = !rendStates.fog; //to force reset
  pie_SetFogStatus(!rendStates.fog);

  //depth Buffer on
  temp = rendStates.depthBuffer;
  rendStates.depthBuffer = static_cast<DEPTH_MODE>(-1); //to force reset
  pie_SetDepthBufferStatus(static_cast<DEPTH_MODE>(temp));

  //set render mode

  //basic gouraud textured rendering
  temp = rendStates.colourCombine;
  rendStates.colourCombine = static_cast<COLOUR_MODE>(-1); //to force reset
  pie_SetColourCombine(static_cast<COLOUR_MODE>(temp));

  temp = rendStates.transMode;
  rendStates.transMode = static_cast<TRANSLUCENCY_MODE>(-1); //to force reset
  pie_SetTranslucencyMode(static_cast<TRANSLUCENCY_MODE>(temp));

  //chroma keying on black
  temp = rendStates.keyingOn;
  rendStates.keyingOn = -1; //to force reset
  pie_SetColourKeyedBlack(temp);

  //bilinear filtering
  temp = rendStates.bilinearOn;
  rendStates.bilinearOn = -1; //to force reset
  pie_SetBilinear(temp);
}

/***************************************************************************/
/***************************************************************************/
void pie_SetDepthBufferStatus(DEPTH_MODE depthMode)
{
#ifndef PIETOOL
  if (rendStates.depthBuffer != depthMode)
  {
    rendStates.depthBuffer = depthMode;
    switch (depthMode)
    {
    case DEPTH_CMP_LEQ_WRT_ON:
      D3DSetDepthCompare(D3DCMP_LESSEQUAL);
      D3DSetDepthWrite(TRUE);
      break;
    case DEPTH_CMP_ALWAYS_WRT_ON:
      D3DSetDepthCompare(D3DCMP_ALWAYS);
      D3DSetDepthWrite(TRUE);
      break;
    case DEPTH_CMP_LEQ_WRT_OFF:
      D3DSetDepthCompare(D3DCMP_LESSEQUAL);
      D3DSetDepthWrite(FALSE);
      break;
    case DEPTH_CMP_ALWAYS_WRT_OFF:
      D3DSetDepthCompare(D3DCMP_ALWAYS);
      D3DSetDepthWrite(FALSE);
      break;
    }
  }
#endif
}

//***************************************************************************
//
// pie_SetTranslucent(BOOL val);
//
// Global enable/disable Translucent effects 
//
//***************************************************************************

void pie_SetTranslucent(BOOL val) { rendStates.translucent = val; }

BOOL pie_Translucent(void) { return rendStates.translucent; }

//***************************************************************************
//
// pie_SetAdditive(BOOL val);
//
// Global enable/disable Additive effects 
//
//***************************************************************************

void pie_SetAdditive(BOOL val) { rendStates.additive = val; }

BOOL pie_Additive(void) { return rendStates.additive; }

//***************************************************************************
//
// pie_SetCaps(BOOL val);
//
// HIGHEST LEVEL enable/disable modes 
//
//***************************************************************************

void pie_SetFogCap(FOG_CAP val) { rendStates.fogCap = val; }

FOG_CAP pie_GetFogCap(void) { return rendStates.fogCap; }

void pie_SetTexCap(TEX_CAP val) { rendStates.texCap = val; }

TEX_CAP pie_GetTexCap(void) { return rendStates.texCap; }

//***************************************************************************
//
// pie_EnableFog(BOOL val)
//
// Global enable/disable fog to allow fog to be turned of ingame 
//
//***************************************************************************

void pie_EnableFog(BOOL val)
{
  if (rendStates.fogCap == FOG_CAP_NO)
    val = FALSE;
  if (rendStates.fogEnabled != val)
  {
    rendStates.fogEnabled = val;
    if (val == TRUE)
    {
      //			pie_SetFogColour(0x0078684f);//(nicks colour + 404040)/2
      pie_SetFogColour(0x00B08f5f); //nicks colour
    }
    else
      pie_SetFogColour(0x00000000); //clear background to black
  }
}

BOOL pie_GetFogEnabled(void) { return rendStates.fogEnabled; }

//***************************************************************************
//
// pie_SetFogStatus(BOOL val)
//
// Toggle fog on and off for rendering objects inside or outside the 3D world
//
//***************************************************************************

void pie_SetFogStatus(BOOL val)
{
  if (rendStates.fogEnabled)
  {
    //fog enabled so toggle if required 
    if (rendStates.fog != val)
      rendStates.fog = val;
  }
  else
  {
    //fog disabled so turn it off if not off already 
    if (rendStates.fog != FALSE)
      rendStates.fog = FALSE;
  }
}

BOOL pie_GetFogStatus(void) { return rendStates.fog; }

/***************************************************************************/
void pie_SetFogColour(UDWORD colour)
{
  UDWORD grey;
  if (rendStates.fogCap == FOG_CAP_GREY)
  {
    grey = colour & 0xff;
    colour >>= 8;
    grey += (colour & 0xff);
    colour >>= 8;
    grey += (colour & 0xff);
    grey /= 3;
    grey &= 0xff; //check only
    colour = grey + (grey << 8) + (grey << 16);
    rendStates.fogColour = colour;
  }
  else if (rendStates.fogCap == FOG_CAP_NO)
    rendStates.fogColour = 0;
  else
    rendStates.fogColour = colour;
}

UDWORD pie_GetFogColour(void) { return rendStates.fogColour; }

/***************************************************************************/
void pie_SetTexturePage(SDWORD num)
{
#ifndef PIETOOL
  /* dtm_SetTexturePage keeps the record of what is bound, and is the one that
   * has to be right after a device reset. Caching the page here as well meant
   * two records of the same fact, and this one was never reset. */
  dtm_SetTexturePage(num < 0 ? -1 : num);
#endif
}

/***************************************************************************/
void pie_SetRendMode(REND_MODE rendMode)
{
  if (rendMode != rendStates.rendMode)
  {
    rendStates.rendMode = rendMode;
    switch (rendMode)
    {
    case REND_GOURAUD_TEX:
      pie_SetColourCombine(COLOUR_TEX_ITERATED);
      pie_SetTranslucencyMode(TRANS_DECAL);
      break;
    case REND_ALPHA_TEX:
      pie_SetColourCombine(COLOUR_TEX_ITERATED);
      pie_SetTranslucencyMode(TRANS_ALPHA);
      break;
    case REND_ADDITIVE_TEX:
      pie_SetColourCombine(COLOUR_TEX_ITERATED);
      pie_SetTranslucencyMode(TRANS_ADDITIVE);
      break;
    case REND_TEXT:
      pie_SetColourCombine(COLOUR_TEX_CONSTANT);
      pie_SetTranslucencyMode(TRANS_DECAL);
      break;
    case REND_ALPHA_TEXT:
      pie_SetColourCombine(COLOUR_TEX_CONSTANT);
      pie_SetTranslucencyMode(TRANS_ALPHA);
      break;
    case REND_ALPHA_FLAT:
      pie_SetColourCombine(COLOUR_FLAT_CONSTANT);
      pie_SetTranslucencyMode(TRANS_ALPHA);
      break;
    case REND_ALPHA_ITERATED:
      pie_SetColourCombine(COLOUR_FLAT_ITERATED);
      pie_SetTranslucencyMode(TRANS_ADDITIVE);
      break;
    case REND_FILTER_FLAT:
      pie_SetColourCombine(COLOUR_FLAT_CONSTANT);
      pie_SetTranslucencyMode(TRANS_FILTER);
      break;
    case REND_FILTER_ITERATED:
      pie_SetColourCombine(COLOUR_FLAT_CONSTANT);
      pie_SetTranslucencyMode(TRANS_ALPHA);
      break;
    case REND_FLAT:
      pie_SetColourCombine(COLOUR_FLAT_CONSTANT);
      pie_SetTranslucencyMode(TRANS_DECAL);
    default:
      break;
    }
  }
}

/***************************************************************************/

void pie_SetColourKeyedBlack(BOOL keyingOn)
{
#ifndef PIETOOL
  if (keyingOn != rendStates.keyingOn)
  {
    rendStates.keyingOn = keyingOn;
    pieStateCount++;
    D3DSetColourKeying(keyingOn);
  }
#endif
}

/***************************************************************************/
void pie_SetBilinear(BOOL bilinearOn)
{
#ifndef PIETOOL
  if (bilinearOn != rendStates.bilinearOn)
  {
    rendStates.bilinearOn = bilinearOn;
    pieStateCount++;
    dtm_SetBilinear(bilinearOn);
  }
#endif
}

BOOL pie_GetBilinear(void)
{
#ifndef PIETOOL
  return rendStates.bilinearOn;
#else
  return FALSE;
#endif
}

/***************************************************************************/
static void pie_SetColourCombine(COLOUR_MODE colCombMode)
{
#ifndef PIETOOL	//ffs

  if (colCombMode != rendStates.colourCombine)
  {
    rendStates.colourCombine = colCombMode;
    pieStateCount++;
    switch (colCombMode)
    {
    case COLOUR_TEX_CONSTANT:
      break;
    case COLOUR_FLAT_CONSTANT:
    case COLOUR_FLAT_ITERATED:
      pie_SetTexturePage(-1);
      break;
    case COLOUR_TEX_ITERATED: default:
      break;
    }
  }
#endif
}

/***************************************************************************/
static void pie_SetTranslucencyMode(TRANSLUCENCY_MODE transMode)
{
#ifndef PIETOOL
  if (transMode != rendStates.transMode)
  {
    rendStates.transMode = transMode;
    pieStateCount++;
    D3DSetTranslucencyMode(transMode);
  }
#endif
}

/***************************************************************************/
// set the constant colour used in text and flat render modes
/***************************************************************************/
void pie_SetColour(UDWORD colour)
{
  if (colour != rendStates.colour)
  {
    rendStates.colour = colour;
    pieStateCount++;
  }
}

/***************************************************************************/
// get the constant colour used in text and flat render modes
/***************************************************************************/
UDWORD pie_GetColour(void) { return rendStates.colour; }

/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
