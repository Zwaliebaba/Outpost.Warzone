#include "pch.h"
/***************************************************************************/

#include "RenderTypes.h"
#include "PieState.h"
#include "Arrow.h"

/***************************************************************************/

/***************************************************************************/

using ARROW = struct ARROW
{
  iVector vecBase;
  iVector vecHead;
  UDWORD iColour;
  struct ARROW* psNext;
};

/***************************************************************************/

/***************************************************************************/

ARROW* g_psArrowList = nullptr;

/***************************************************************************/

BOOL arrowInit(void)
{
  return TRUE;
}

/***************************************************************************/

void arrowShutDown(void) {  }

/***************************************************************************/

BOOL arrowAdd(SDWORD iBaseX, SDWORD iBaseY, SDWORD iBaseZ, SDWORD iHeadX, SDWORD iHeadY, SDWORD iHeadZ, UDWORD iColour)
{
  ARROW* psArrow;

  psArrow = new (std::nothrow) ARROW;
  if (psArrow == nullptr)
    return FALSE;

  /* ivis y,z swapped */
  psArrow->vecBase.x = iBaseX;
  psArrow->vecBase.y = iBaseZ;
  psArrow->vecBase.z = iBaseY;

  psArrow->vecHead.x = iHeadX;
  psArrow->vecHead.y = iHeadZ;
  psArrow->vecHead.z = iHeadY;

  psArrow->iColour = iColour;

  /* add to list */
  psArrow->psNext = g_psArrowList;
  g_psArrowList = psArrow;
}

/***************************************************************************/

void arrowDrawAll(void)
{
  ARROW *psArrow, *psArrowTemp;

  pie_SetDepthBufferStatus(DEPTH_CMP_ALWAYS_WRT_ON);
  pie_SetFogStatus(FALSE);

  /* draw and clear list */
  psArrow = g_psArrowList;

  while (psArrow != nullptr)
  {
    draw3dLine(&psArrow->vecHead, &psArrow->vecBase, psArrow->iColour);
    psArrowTemp = psArrow->psNext;
    delete psArrow;
    psArrow = psArrowTemp;
  }

  /* reset list */
  g_psArrowList = nullptr;

  pie_SetDepthBufferStatus(DEPTH_CMP_LEQ_WRT_ON);
}

/***************************************************************************/
