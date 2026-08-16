#include "pch.h"
/***************************************************************************/
/*
 * TexMan.cpp
 *
 * The texture pages the renderer draws from.
 *
 * The Direct3D 6 version of this file was mostly a video memory budget: it
 * asked DirectDraw how much texture memory was free, chose between 32 full
 * size pages, a mix of 256 and 128 pixel pages, and an 8 bit palettised mode,
 * created the pages as DirectDraw surfaces, and uploaded each texture through
 * a system memory staging surface and a Blt.
 *
 * Direct3D 9's managed pool does all of that: it keeps a system memory copy
 * of every texture and moves it in and out of video memory as needed. So the
 * pages are simply 32 managed 256x256 A8R8G8B8 textures, and uploading one
 * is a lock, a palette lookup and an unlock. That is what deleted the paging
 * logic the migration plan expected to lose here.
 */
/***************************************************************************/

#include "Frame.h"
#include "RenderTypes.h"
#include "PieState.h"
#include "Palette.h"
#include "Render.h"
#include "TexMan.h"
#include "Tex.h"

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

#define MAX_TEX_PAGES TEX_MAX
#define TEXTURE_SIZE 256

/* The radar occupies the top left corner of its page rather than the whole
 * of it, which is what dtm_GetRadarTexImageSize reports to the blit code. */
#define RADAR_SIZE 128

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

/* The game palette packed into A8R8G8B8. Entry zero is the transparent one -
 * it carries an alpha of zero, and the alpha test throws it away. That is
 * what DirectDraw's colour key on black used to do.
 */
static UDWORD texPal32Bit[PALETTE_SIZE];

/* The page currently bound to stage 0, so that a redundant SetTexture can be
 * skipped and so that a device reset can put the binding back. */
static SDWORD currentTexPage = -1;

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/

static BOOL dtm_BuildTexturePalette(void);
static BOOL dtm_UploadImage(LPDIRECT3DTEXTURE9 psTexture, UDWORD width, UDWORD height, UBYTE* pImageData);

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

/*
 *	dtm_Initialise
 *
 *	Called once the device exists. Creates the texture pages in the managed
 *	pool, which is the whole of what used to be a video memory negotiation.
 */
BOOL dtm_Initialise(void)
{
  LPDIRECT3DDEVICE9 psDevice = d3d_GetDevice();
  HRESULT hResult;
  SDWORD i;

  if (psDevice == nullptr)
    return FALSE;

  for (i = 0; i < MAX_TEX_PAGES; i++)
    _TEX_PAGE[i].psTexture = nullptr;

  for (i = 0; i < MAX_TEX_PAGES; i++)
  {
    hResult = psDevice->CreateTexture(TEXTURE_SIZE, TEXTURE_SIZE, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &_TEX_PAGE[i].psTexture,
                                      nullptr);
    if (hResult != D3D_OK)
    {
      Neuron::Fatal("dtm_Initialise: couldn't create texture page {}:\n{}", i, DXErrorToString(hResult));
      return FALSE;
    }
  }

  currentTexPage = -1;
  dtm_SetTexturePage(0);

  dtm_ApplyTextureStates();

  return TRUE;
}

/***************************************************************************/

BOOL dtm_ReleaseTextures(void)
{
  SDWORD i;

  for (i = 0; i < MAX_TEX_PAGES; i++) { RELEASE(_TEX_PAGE[i].psTexture); }

  currentTexPage = -1;

  return TRUE;
}

/***************************************************************************/

BOOL dtm_ReLoadTexture(SDWORD i)
{
  if (i < 0 || i >= RADAR_TEXPAGE_D3D)
    return FALSE;
  /* set pie texture pointer */
  if (_TEX_PAGE[i].tex.bmp == nullptr)
    return FALSE;
  return dtm_LoadTexSurface(&_TEX_PAGE[i].tex, i);
}

/***************************************************************************/

BOOL dtm_ReloadAllTextures(void)
{
  SDWORD i;

  for (i = 0; i < MAX_TEX_PAGES; i++)
  {
    /* set pie texture pointer */
    if ((_TEX_PAGE[i].tex.bmp != nullptr) && (i != RADAR_TEXPAGE_D3D))
      dtm_LoadTexSurface(&_TEX_PAGE[i].tex, i);
  }
  return TRUE;
}

/***************************************************************************/
/*
 * dtm_RestoreTextures
 *
 * Managed textures survive a device reset with their contents intact, so
 * unlike the DirectDraw version this only has to put the binding back.
 * Pages that were never created - if the reset followed a device that was
 * recreated rather than reset - are refilled.
 */
BOOL dtm_RestoreTextures(void)
{
  SDWORD page;

  if (_TEX_PAGE[0].psTexture == nullptr)
    return FALSE;

  page = currentTexPage;
  currentTexPage = -1;
  dtm_SetTexturePage(page);

  return TRUE;
}

/***************************************************************************/

void dtm_SetTexturePage(SDWORD i)
{
  LPDIRECT3DDEVICE9 psDevice = d3d_GetDevice();

  if (psDevice == nullptr)
    return;

  if (i == currentTexPage)
    return;

  if ((i >= 0) && (i < MAX_TEX_PAGES))
    (void)psDevice->SetTexture(0, _TEX_PAGE[i].psTexture);
  else
    (void)psDevice->SetTexture(0, nullptr);

  currentTexPage = i;
}

/***************************************************************************/

void dtm_ApplyTextureStates(void)
{
  LPDIRECT3DDEVICE9 psDevice = d3d_GetDevice();

  if (psDevice == nullptr)
    return;

  (void)psDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  (void)psDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  (void)psDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

  /* Filtering moved from the texture stage to the sampler in Direct3D 9. */
  (void)psDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
  (void)psDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
  (void)psDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  (void)psDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
  (void)psDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

  /* The binding is part of the device state a reset discards too. */
  if ((currentTexPage >= 0) && (currentTexPage < MAX_TEX_PAGES))
    (void)psDevice->SetTexture(0, _TEX_PAGE[currentTexPage].psTexture);
}

/***************************************************************************/

void dtm_SetBilinear(BOOL bBilinearOn)
{
  LPDIRECT3DDEVICE9 psDevice = d3d_GetDevice();
  HRESULT hResult;
  D3DTEXTUREFILTERTYPE filter = bBilinearOn ? D3DTEXF_LINEAR : D3DTEXF_POINT;

  if (psDevice == nullptr)
    return;

  hResult = psDevice->SetSamplerState(0, D3DSAMP_MINFILTER, filter);
  DEBUG_ASSERT_TEXT(hResult == D3D_OK, "dtm_SetBilinear: min filter failed\n{}", DXErrorToString(hResult));
  hResult = psDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, filter);
  DEBUG_ASSERT_TEXT(hResult == D3D_OK, "dtm_SetBilinear: mag filter failed\n{}", DXErrorToString(hResult));
}

/***************************************************************************/
/*
 * dtm_UploadImage
 *
 * Expand palettised image data into the top left of a texture page.
 *
 * Direct3D 6 needed a system memory staging surface and a Blt to get here,
 * because a video memory surface could not be written directly. A managed
 * texture is backed by system memory, so this writes into it and lets the
 * runtime move it.
 */
static BOOL dtm_UploadImage(LPDIRECT3DTEXTURE9 psTexture, UDWORD width, UDWORD height, UBYTE* pImageData)
{
  HRESULT hResult;
  D3DLOCKED_RECT sLocked;
  D3DSURFACE_DESC sDesc;
  UDWORD x, y;
  UBYTE* pSrc;
  UDWORD* pDest;

  if ((psTexture == nullptr) || (pImageData == nullptr))
    return FALSE;

  /* Rebuild the packed palette each time. It costs 256 iterations against a
   * 65536 pixel upload, and it means a texture loaded after the game palette
   * changes gets the palette it is actually meant to have. */
  if (!dtm_BuildTexturePalette())
    return FALSE;

  hResult = psTexture->GetLevelDesc(0, &sDesc);
  if (hResult != D3D_OK)
  {
    DEBUG_ASSERT_TEXT(FALSE, "dtm_UploadImage: GetLevelDesc failed\n{}", DXErrorToString(hResult));
    return FALSE;
  }

  hResult = psTexture->LockRect(0, &sLocked, nullptr, 0);
  if (hResult != D3D_OK)
  {
    DEBUG_ASSERT_TEXT(FALSE, "dtm_UploadImage: LockRect failed\n{}", DXErrorToString(hResult));
    return FALSE;
  }

  pSrc = pImageData;
  for (y = 0; (y < height) && (y < sDesc.Height); y++)
  {
    pDest = (UDWORD*)(static_cast<UBYTE*>(sLocked.pBits) + sLocked.Pitch * y);
    for (x = 0; (x < width) && (x < sDesc.Width); x++)
      pDest[x] = texPal32Bit[pSrc[x]];
    pSrc += width;
  }

  (void)psTexture->UnlockRect(0);

  return TRUE;
}

/***************************************************************************/

BOOL dtm_LoadTexSurface(iTexture* psIvisTex, SDWORD index)
{
  if ((index < 0) || (index >= MAX_TEX_PAGES))
  {
    DEBUG_ASSERT_TEXT(FALSE, "dtm_LoadTexSurface: texture page {} out of range", index);
    return FALSE;
  }

  return dtm_UploadImage(_TEX_PAGE[index].psTexture, psIvisTex->width, psIvisTex->height, psIvisTex->bmp);
}

/***************************************************************************/

BOOL dtm_LoadRadarSurface(BYTE* radarBuffer)
{
  return dtm_UploadImage(_TEX_PAGE[RADAR_TEXPAGE_D3D].psTexture, RADAR_SIZE, RADAR_SIZE, (UBYTE*)radarBuffer);
}

/***************************************************************************/

SDWORD dtm_GetRadarTexImageSize(void)
{
  /* The radar image sits in the top left RADAR_SIZE pixels of a full size
   * page - every page is full size now, so this no longer depends on which
   * texture mode was negotiated at start up. */
  return RADAR_SIZE;
}

/***************************************************************************/
/*
 * dtm_BuildTexturePalette
 *
 * Pack the game palette into A8R8G8B8, with entry zero transparent.
 *
 * Direct3D 6 needed this in whatever 16 bit layout the card had handed back,
 * which is what the bit mask scanning in the old dtm_Build16BitTexturePalette
 * was for. The texture format is ours to choose now, so the packing is fixed.
 */
static BOOL dtm_BuildTexturePalette(void)
{
  iColour* psPal;
  UDWORD i;

  psPal = pie_GetGamePal();
  if (psPal == nullptr)
    return FALSE;

  for (i = 0; i < PALETTE_SIZE; i++)
  {
    UDWORD alpha = (i == 0) ? 0x00000000 : 0xff000000;

    texPal32Bit[i] = alpha | (static_cast<UDWORD>(psPal[i].r) << 16) | (static_cast<UDWORD>(psPal[i].g) << 8) | static_cast<UDWORD>(psPal[i].
      b);
  }

  return TRUE;
}
