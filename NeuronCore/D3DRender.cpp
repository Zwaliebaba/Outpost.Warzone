#include "pch.h"
/***************************************************************************/
/*
 * D3DRender.cpp
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

#include "Ivi.h"
#include "RendMode.h"
#include "Tex.h"
#include "PiePalette.h"
#include "PieState.h"
#include "PieClip.h"
#include "FrameInt.h"

#include "D3DRender.h"
#include "TexMan.h"

/***************************************************************************/
/* Macros */

#define ATTEMPTD3D(x) if ((x) != D3D_OK) goto exit_with_error

/***************************************************************************/
/* local funcs */

static BOOL rend_InitD3D(void);
static void D3DSetCulling(BOOL bCullingOn);

/***************************************************************************/
/* global variables */

static D3DINFO g_sD3Dinfo;
static LPDIRECT3DDEVICE9 g_psDevice = nullptr;
static D3DTLVERTEX d3dVrts[pie_MAX_POLY_VERTS];
static float g_fTextureOffset = 0.0f;
static BOOL g_bTexelOffsetOn = FALSE;

/* Whether the device can do vertex fog. Reported by the caps; the game asks
 * for fog whether or not it can, so this is only worth a diagnostic.
 */
static BOOL g_bCanVertexFog = FALSE;

/***************************************************************************/

BOOL InitD3D(D3DINFO* psD3Dinfo)
{
  /* copy input struct */
  memcpy(&g_sD3Dinfo, psD3Dinfo, sizeof(D3DINFO));

  /* Direct3D 9 has no colour key, so transparency is always the alpha test.
   * The texture manager gives palette entry zero an alpha of zero and every
   * other entry full alpha, which is what the DirectDraw colour key on black
   * used to do.
   */
  g_sD3Dinfo.bAlphaKey = TRUE;

  return rend_InitD3D();
}

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

void D3DEnableFog(BOOL bEnable)
{
  HRESULT hRes;
  static BOOL bEnableLast = FALSE, bFirst = TRUE;

  if (g_psDevice == nullptr)
    return;

  if (bFirst || (bEnableLast != bEnable))
  {
    hRes = g_psDevice->SetRenderState(D3DRS_FOGENABLE, bEnable ? TRUE : FALSE);
    if (hRes != D3D_OK) { Neuron::Fatal("D3DEnableFog:\n{}\n", DXErrorToString(hRes)); }
  }

  if (bFirst)
    bFirst = FALSE;

  bEnableLast = bEnable;
}

/***************************************************************************/

void D3DSetFogColour(D3DCOLOR dwColor)
{
  HRESULT hRes;

  if (g_psDevice == nullptr)
    return;

  hRes = g_psDevice->SetRenderState(D3DRS_FOGCOLOR, dwColor);
  if (hRes != D3D_OK) { Neuron::Fatal("D3DSetFogColour:\n{}\n", DXErrorToString(hRes)); }
}

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

void D3DSetAlphaKey(BOOL bAlphaOn) { g_sD3Dinfo.bAlphaKey = bAlphaOn; }

/***************************************************************************/

BOOL D3DGetAlphaKey(void) { return g_sD3Dinfo.bAlphaKey; }

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
  static BOOL bFirst = TRUE, bBlendEnableLast = FALSE;
  BOOL bBlendEnable;

  static D3DBLEND srcBlendLast = D3DBLEND_ZERO, destBlendLast = D3DBLEND_ZERO;
  D3DBLEND srcBlend, destBlend;

  /* 0xffffffff is not a texture argument, so it reads as "leave this one
   * alone" the way -1 did before. */
#define ALPHA_ARG_UNUSED 0xffffffff

  static DWORD dwAlphaOpLast = D3DTOP_DISABLE, dwAlphaArg1Last = ALPHA_ARG_UNUSED, dwAlphaArg2Last = ALPHA_ARG_UNUSED;
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

  if (bFirst || (srcBlend != srcBlendLast)) { ATTEMPTD3D((hResult = g_psDevice->SetRenderState(D3DRS_SRCBLEND, srcBlend))); }

  if (bFirst || (destBlend != destBlendLast)) { ATTEMPTD3D((hResult = g_psDevice->SetRenderState(D3DRS_DESTBLEND, destBlend))); }

  if (bFirst || (bBlendEnable != bBlendEnableLast))
  {
    ATTEMPTD3D((hResult = g_psDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, bBlendEnable ? TRUE : FALSE)));
  }

  if (bFirst || (dwAlphaOp != dwAlphaOpLast)) { ATTEMPTD3D((hResult = g_psDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, dwAlphaOp))); }

  if ((bFirst || (dwAlphaArg1 != dwAlphaArg1Last)) && (dwAlphaArg1 != ALPHA_ARG_UNUSED))
  {
    ATTEMPTD3D((hResult = g_psDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, dwAlphaArg1)));
  }

  if ((bFirst || (dwAlphaArg2 != dwAlphaArg2Last)) && (dwAlphaArg2 != ALPHA_ARG_UNUSED))
  {
    ATTEMPTD3D((hResult = g_psDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, dwAlphaArg2)));
  }

  /* update statics */
  if (bFirst == TRUE)
    bFirst = FALSE;
  srcBlendLast = srcBlend;
  destBlendLast = destBlend;
  dwAlphaOpLast = dwAlphaOp;
  dwAlphaArg1Last = dwAlphaArg1;
  dwAlphaArg2Last = dwAlphaArg2;

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

void D3DSetDepthBuffer(BOOL bDepthBufferOn)
{
  HRESULT hResult;

  if (g_psDevice == nullptr)
    return;

  hResult = g_psDevice->SetRenderState(D3DRS_ZENABLE, bDepthBufferOn ? D3DZB_TRUE : D3DZB_FALSE);
  if (hResult != D3D_OK) { Neuron::Fatal("D3DSetDepthBuffer: bZBufferOn SetRenderState failed\n{}", DXErrorToString(hResult)); }
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
/*
 * rend_InitD3D
 *
 * Take the device the framework created and get it ready to draw.
 */
/***************************************************************************/

static BOOL rend_InitD3D(void)
{
  g_psDevice = screenGetDevice();

  if (g_psDevice == nullptr)
  {
    Neuron::Fatal("InitD3D: the framework has not created a Direct3D 9 device");
    return FALSE;
  }

  D3DGetCaps();

  /* init texture manager */
  if (!dtm_Initialise())
    return FALSE;

  D3DApplyRenderStates();

  return TRUE;
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

BOOL d3d_bHardware(void) { return g_sD3Dinfo.bHardware; }

LPDIRECT3DDEVICE9 d3d_GetDevice(void)
{
  DEBUG_ASSERT_TEXT(g_psDevice != NULL, "d3d_GetDevice: device not set.");
  return g_psDevice;
}

/***************************************************************************/
