#include "pch.h"
#include "Frame.h"
#include "Widget.h"
#include "WidgInt.h"
#include "Label.h"
#include "Form.h"
#include "Tip.h"
#include "RendMode.h"

/* Create a button widget data structure */
BOOL labelCreate(W_LABEL** ppsWidget, W_LABINIT* psInit)
{
  /* Do some validation on the initialisation struct */
  if (psInit->style & ~(WLAB_PLAIN | WLAB_ALIGNLEFT | WLAB_ALIGNRIGHT | WLAB_ALIGNCENTRE | WIDG_HIDDEN))
  {
    DEBUG_ASSERT_TEXT(FALSE, "Unknown button style");
    return FALSE;
  }

  /* Allocate the required memory */
  *ppsWidget = new (std::nothrow) W_LABEL;
  if (*ppsWidget == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "Out of memory");
    return FALSE;
  }
  /* Allocate the memory for the tip and copy it if necessary */
  if (psInit->pTip)
  {
#if W_USE_STRHEAP
    if (!widgAllocCopyString(&(*ppsWidget)->pTip, psInit->pTip))
    {
      /* Out of memory - just carry on without the tip */
      DEBUG_ASSERT_TEXT(FALSE, "buttonCreate: Out of memory");
      (*ppsWidget)->pTip = NULL;
    }
#else
    (*ppsWidget)->pTip = psInit->pTip;
#endif
  }
  else
    (*ppsWidget)->pTip = nullptr;

  /* Initialise the structure */
  (*ppsWidget)->type = WIDG_LABEL;
  (*ppsWidget)->id = psInit->id;
  (*ppsWidget)->formID = psInit->formID;
  (*ppsWidget)->style = psInit->style;
  (*ppsWidget)->x = psInit->x;
  (*ppsWidget)->y = psInit->y;
  (*ppsWidget)->width = psInit->width;
  (*ppsWidget)->height = psInit->height;
  if (psInit->pDisplay)
    (*ppsWidget)->display = psInit->pDisplay;
  else
    (*ppsWidget)->display = labelDisplay;
  (*ppsWidget)->callback = psInit->pCallback;
  (*ppsWidget)->pUserData = psInit->pUserData;
  (*ppsWidget)->UserData = psInit->UserData;
  (*ppsWidget)->FontID = psInit->FontID;

  if (psInit->pText)
    widgCopyString((*ppsWidget)->aText, psInit->pText);
  else
    *(*ppsWidget)->aText = 0;

  return TRUE;
}

/* Free the memory used by a button */
void labelFree(W_LABEL* psWidget)
{
#if W_USE_STRHEAP
  if (psWidget->pTip) { widgFreeString(psWidget->pTip); }
#endif

  delete psWidget;
}

/* label display function */
void labelDisplay(WIDGET* psWidget, UDWORD xOffset, UDWORD yOffset, UDWORD* pColours)
{
  SDWORD fx, fy, fw;
  W_LABEL* psLabel;
  int FontID;

  psLabel = (W_LABEL*)psWidget;
  FontID = psLabel->FontID;

  Neuron::SetFont(FontID);
  Neuron::SetTextColour(*(pColours + WCOL_TEXT));
  if (psLabel->style & WLAB_ALIGNCENTRE)
  {
    fw = Neuron::GetTextWidth((unsigned char*)psLabel->aText);
    fx = xOffset + psLabel->x + (psLabel->width - fw) / 2;
  }
  else if (psLabel->style & WLAB_ALIGNRIGHT)
  {
    fw = Neuron::GetTextWidth((unsigned char*)psLabel->aText);
    fx = xOffset + psLabel->x + psLabel->width - fw;
  }
  else
    fx = xOffset + psLabel->x;
  fy = yOffset + psLabel->y + (psLabel->height - Neuron::GetTextLineSize()) / 2 - Neuron::GetTextAboveBase();
  //	fy = yOffset + psLabel->y + (psLabel->height -
  pie_DrawText((unsigned char*)psLabel->aText, fx, fy);
}

/* Respond to a mouse moving over a label */
void labelHiLite(W_LABEL* psWidget, W_CONTEXT* psContext)
{
  psWidget->state |= WLABEL_HILITE;

  /* If there is a tip string start the tool tip */
  if (psWidget->pTip)
  {
    tipStart((WIDGET*)psWidget, psWidget->pTip, psContext->psScreen->TipFontID, (UDWORD*)psContext->psForm->aColours,
             psWidget->x + psContext->xOffset, psWidget->y + psContext->yOffset, psWidget->width, psWidget->height);
  }
}

/* Respond to the mouse moving off a label */
void labelHiLiteLost(W_LABEL* psWidget)
{
  psWidget->state &= ~(WLABEL_HILITE);
  if (psWidget->pTip)
    tipStop((WIDGET*)psWidget);
}
