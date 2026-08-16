#include "pch.h"
/*
 * FileRequester.cpp
 *
 * The ten-slot file requester popup. It does no file I/O: it lists what
 * matches a path and extension and returns the name picked or typed. See
 * FileRequester.h for why a widget this general was once called loadsave.
 */
#include "Frame.h"
#include "StrRes.h"
#include "Input.h"
#include "Widget.h"
#include "Palette.h"		// for predefined colours.
#include "RendMode.h"		// for boxfill
#include "HCI.h"
#include "FileRequester.h"
#include "MultiPlay.h"
#include "AudioSystem.h"
#include "AudioID.h"
#include "FrontEnd.h"
#include "IntDisplay.h"
#include "Text.h"

// ////////////////////////////////////////////////////////////////////////////
#define REQUESTER_X				130	+ D_W
#define REQUESTER_Y				170	+ D_H
#define REQUESTER_W				380
#define REQUESTER_H				200

#define MAX_REQUEST_NAME		60

#define REQUESTER_HGAP			5
#define REQUESTER_VGAP			5
#define REQUESTER_BANNER_DEPTH	25

#define SLOT_W				(REQUESTER_W -(3 * REQUESTER_HGAP)) /2
#define SLOT_H				(REQUESTER_H -(6 * REQUESTER_VGAP )- (REQUESTER_BANNER_DEPTH+REQUESTER_VGAP) ) /5

#define ID_REQUESTER				21000
#define REQUESTER_FORM			ID_REQUESTER+1		// back form.
#define REQUESTER_CANCEL			ID_REQUESTER+2		// cancel but.
#define REQUESTER_LABEL			ID_REQUESTER+3		// title text
#define REQUESTER_BANNER			ID_REQUESTER+4		// banner.

#define SLOT_START			ID_REQUESTER+5		// each of the buttons.	
#define SLOT_END			ID_REQUESTER+15

#define SLOT_EDIT			ID_REQUESTER+16		// name entry box.

// ////////////////////////////////////////////////////////////////////////////
BOOL requesterClose();
BOOL requesterRun();
BOOL requesterDisplay();
static BOOL _requesterOpen(BOOL bLoad, CHAR* sSearchPath, CHAR* sExtension, CHAR* title);
static BOOL _requesterRun(void);
static void displayRequesterBanner(struct _widget* psWidget, UDWORD xOffset, UDWORD yOffset, UDWORD* pColours);
static void displayRequesterSlot(struct _widget* psWidget, UDWORD xOffset, UDWORD yOffset, UDWORD* pColours);
static void displayRequesterEdit(struct _widget* psWidget, UDWORD xOffset, UDWORD yOffset, UDWORD* pColours);
void removeWildcards(char* pStr);

static W_SCREEN* psRequestScreen; // Widget screen for requester
static BOOL mode;
static UDWORD chosenSlotId;

BOOL bRequesterUp = FALSE; // true when interface is up and should be run.
STRING sRequestResult[255]; // filename returned;
STRING sDelete[MAX_STR_LENGTH];
BOOL bRequestLoad = FALSE;

static CHAR sPath[255];
static CHAR sExt[4];

// ////////////////////////////////////////////////////////////////////////////
BOOL requesterOpen(REQUESTER_MODE mode, CHAR* sSearchPath, CHAR* sExtension, CHAR* title)
{
  return _requesterOpen(mode == LOAD_FORCE, sSearchPath, sExtension, title);
}

// ////////////////////////////////////////////////////
static BOOL _requesterOpen(BOOL bLoad, CHAR* sSearchPath, CHAR* sExtension, CHAR* title)
{
  W_FORMINIT sFormInit;
  W_BUTINIT sButInit;
  W_LABINIT sLabInit;
  UDWORD slotCount;
  static STRING sSlots[10][64];
  STRING sTemp[255];

  WIN32_FIND_DATA found;
  HANDLE dir;

  mode = bLoad;

  if (GetCurrentDirectory(255, (char*)&sTemp) == 0)
    return FALSE; // failed, directory probably didn't exist.

  CreateDirectory(sSearchPath, nullptr); // create the directory required... fails if already there, so no problem.
  widgCreateScreen(&psRequestScreen); // init the screen.
  widgSetTipFont(psRequestScreen, WFont);

  /* add a form to place the tabbed form on */
  memset(&sFormInit, 0, sizeof(W_FORMINIT));
  sFormInit.formID = 0;
  sFormInit.id = REQUESTER_FORM;
  sFormInit.style = WFORM_PLAIN;
  sFormInit.x = static_cast<SWORD>((REQUESTER_X));
  sFormInit.y = static_cast<SWORD>((REQUESTER_Y));
  sFormInit.width = REQUESTER_W;
  sFormInit.height = REQUESTER_H;
  sFormInit.disableChildren = TRUE;
  sFormInit.pDisplay = intOpenPlainForm;
  widgAddForm(psRequestScreen, &sFormInit);

  // Add Banner
  sFormInit.formID = REQUESTER_FORM;
  sFormInit.id = REQUESTER_BANNER;
  sFormInit.x = REQUESTER_HGAP;
  sFormInit.y = REQUESTER_VGAP;
  sFormInit.width = REQUESTER_W - (2 * REQUESTER_HGAP);
  sFormInit.height = REQUESTER_BANNER_DEPTH;
  sFormInit.disableChildren = FALSE;
  sFormInit.pDisplay = displayRequesterBanner;
  sFormInit.pUserData = (VOID*)bLoad;
  widgAddForm(psRequestScreen, &sFormInit);

  // Add Banner Label
  memset(&sLabInit, 0, sizeof(W_LABINIT));
  sLabInit.formID = REQUESTER_BANNER;
  sLabInit.id = REQUESTER_LABEL;
  sLabInit.style = WLAB_ALIGNCENTRE;
  sLabInit.x = 0;
  sLabInit.y = 4;
  sLabInit.width = REQUESTER_W - (2 * REQUESTER_HGAP); //REQUESTER_W;
  sLabInit.height = 20;
  sLabInit.pText = title;
  sLabInit.FontID = WFont;
  widgAddLabel(psRequestScreen, &sLabInit);

  // add cancel.
  memset(&sButInit, 0, sizeof(W_BUTINIT));
  sButInit.formID = REQUESTER_BANNER;
  sButInit.x = 4;
  sButInit.y = 3;
  sButInit.width = Neuron::GetImageWidth(IntImages, IMAGE_NRUTER);
  sButInit.height = Neuron::GetImageHeight(IntImages, IMAGE_NRUTER);
  sButInit.pUserData = (void*)PACKDWORD_TRI(0, IMAGE_NRUTER, IMAGE_NRUTER);
  sButInit.id = REQUESTER_CANCEL;
  sButInit.style = WBUT_PLAIN;
  sButInit.pTip = strresGetString(psStringRes, STR_MISC_CLOSE);
  sButInit.FontID = WFont;
  sButInit.pDisplay = intDisplayImageHilight;
  widgAddButton(psRequestScreen, &sButInit);

  // add slots
  memset(&sButInit, 0, sizeof(W_BUTINIT));
  sButInit.formID = REQUESTER_FORM;
  sButInit.style = WBUT_PLAIN;
  sButInit.width = SLOT_W;
  sButInit.height = SLOT_H;
  sButInit.pDisplay = displayRequesterSlot;
  sButInit.FontID = WFont;

  for (slotCount = 0; slotCount < 10; slotCount++)
  {
    sButInit.id = slotCount + SLOT_START;

    if (slotCount < 5)
    {
      sButInit.x = REQUESTER_HGAP;
      sButInit.y = static_cast<SWORD>((REQUESTER_BANNER_DEPTH + (2 * REQUESTER_VGAP)) + (slotCount * (REQUESTER_VGAP + SLOT_H)));
    }
    else
    {
      sButInit.x = (2 * REQUESTER_HGAP) + SLOT_W;
      sButInit.y = static_cast<SWORD>((REQUESTER_BANNER_DEPTH + (2 * REQUESTER_VGAP)) + ((slotCount - 5) * (REQUESTER_VGAP + SLOT_H)));
    }
    widgAddButton(psRequestScreen, &sButInit);
  }

  // fill slots.
  slotCount = 0;

  sprintf(sTemp, "%s*.%s", sSearchPath, sExtension); // form search string.
  strcpy(sPath, sSearchPath); // setup locals.
  strcpy(sExt, sExtension);
  dir = FindFirstFile(sTemp, &found);
  if (dir != INVALID_HANDLE_VALUE)
  {
    while (TRUE)
    {
      /* Set the tip and add the button */
      found.cFileName[strlen(found.cFileName) - 4] = '\0'; // chop extension

      strcpy(sSlots[slotCount], found.cFileName); //store it!

      ((W_BUTTON*)widgGetFromID(psRequestScreen,SLOT_START + slotCount))->pTip = sSlots[slotCount];
      ((W_BUTTON*)widgGetFromID(psRequestScreen,SLOT_START + slotCount))->pText = sSlots[slotCount];

      slotCount++; // goto next but.

      if (!FindNextFile(dir, &found) || slotCount == 10) // only show upto 10 entrys.
        break;
    }
  }
  FindClose(dir);
  bRequesterUp = TRUE;
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
BOOL requesterClose(void)
{
  widgDelete(psRequestScreen,REQUESTER_FORM);
  bRequesterUp = FALSE;
  widgReleaseScreen(psRequestScreen);

  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
BOOL requesterRun(void) { return _requesterRun(); }

// ////////////////////////////////////////////////////////////////////////////
// Returns TRUE if cancel pressed or a valid game slot was selected.
// if when returning TRUE strlen(sRequestResult) != 0 then a valid game
// slot was selected otherwise cancel was selected..
static BOOL _requesterRun(void)
{
  UDWORD id = 0;
  W_EDBINIT sEdInit;
  CHAR sTemp[MAX_STR_LENGTH];
  UDWORD i;
  W_CONTEXT context;

  id = widgRunScreen(psRequestScreen);

  strcpy(sRequestResult, ""); // set returned filename to null;

  // cancel this operation...
  if (id == REQUESTER_CANCEL || CancelPressed())
    goto failure;

  // clicked a load entry
  if (id >= SLOT_START && id <= SLOT_END)
  {
    if (mode) // Loading, return that entry.
    {
      if (((W_BUTTON*)widgGetFromID(psRequestScreen, id))->pText)
        sprintf(sRequestResult, "%s%s.%s", sPath, ((W_BUTTON*)widgGetFromID(psRequestScreen, id))->pText, sExt);
      else
        goto failure; // clicked on an empty box

      goto success;
    }
    //  SAVING!add edit box at that position.
    if (!widgGetFromID(psRequestScreen,SLOT_EDIT))
    {
      // add blank box.
      memset(&sEdInit, 0, sizeof(W_EDBINIT));
      sEdInit.formID = REQUESTER_FORM;
      sEdInit.id = SLOT_EDIT;
      sEdInit.style = WEDB_PLAIN;
      sEdInit.x = widgGetFromID(psRequestScreen, id)->x;
      sEdInit.y = widgGetFromID(psRequestScreen, id)->y;
      sEdInit.width = widgGetFromID(psRequestScreen, id)->width;
      sEdInit.height = widgGetFromID(psRequestScreen, id)->height;
      sEdInit.pText = ((W_BUTTON*)widgGetFromID(psRequestScreen, id))->pText;
      sEdInit.FontID = WFont;
      sEdInit.pBoxDisplay = displayRequesterEdit;
      widgAddEditBox(psRequestScreen, &sEdInit);

      sprintf(sTemp, "%s%s.%s", sPath, ((W_BUTTON*)widgGetFromID(psRequestScreen, id))->pText, sExt);

      widgHide(psRequestScreen, id); // hide the old button
      chosenSlotId = id;

      strcpy(sDelete, sTemp); // prepare the savegame name.
      sTemp[strlen(sTemp) - 4] = '\0'; // strip extension

      // auto click in the edit box we just made.
      context.psScreen = psRequestScreen;
      context.psForm = (W_FORM*)psRequestScreen->psForm;
      context.xOffset = 0;
      context.yOffset = 0;
      context.mx = mouseX();
      context.my = mouseY();
      editBoxClicked((W_EDITBOX*)widgGetFromID(psRequestScreen,SLOT_EDIT), &context);
    }
    else
    {
      // clicked in a different box. shouldnt be possible!(since we autoclicked in editbox)
    }
  }

  // finished entering a name.
  if (id == SLOT_EDIT)
  {
    if (!keyPressed(KEY_RETURN)) // enter was not pushed, so not a vaild entry.	
    {
      widgDelete(psRequestScreen,SLOT_EDIT); //unselect this box, and go back ..
      widgReveal(psRequestScreen, chosenSlotId);
      return TRUE;
    }

    // scan to see if that game exists in another slot, if
    // so then fail.
    strcpy(sTemp, ((W_EDITBOX*)widgGetFromID(psRequestScreen, id))->aText);

    for (i = SLOT_START; i < SLOT_END; i++)
    {
      if (i != chosenSlotId)
      {
        if (((W_BUTTON*)widgGetFromID(psRequestScreen, i))->pText && strcmp(sTemp, ((W_BUTTON*)widgGetFromID(psRequestScreen, i))->pText) ==
          0)
        {
          widgDelete(psRequestScreen,SLOT_EDIT); //unselect this box, and go back ..
          widgReveal(psRequestScreen, chosenSlotId);
          // move mouse to same box..
          AudioSystem::PlayTrack(ID_SOUND_BUILD_FAIL);
          return TRUE;
        }
      }
    }

    // return with this name, as we've edited it.
    if (strlen(((W_EDITBOX*)widgGetFromID(psRequestScreen, id))->aText))
    {
      strcpy(sTemp, ((W_EDITBOX*)widgGetFromID(psRequestScreen, id))->aText);
      removeWildcards(sTemp);
      sprintf(sRequestResult, "%s%s.%s", sPath, sTemp, sExt);
      DeleteFile(sDelete); //only delete the old file if a new name fills the slot
    }
    else
      goto failure; // we entered a blank name..

    // we're done. saving.
    requesterClose();
    bRequestLoad = FALSE;
    return TRUE;
  }

  return FALSE;

  // failed and/or cancelled..
failure:
  requesterClose();
  bRequestLoad = FALSE;
  return TRUE;

  // success on load.
success:
  requesterClose();
  bRequestLoad = TRUE;
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// should be done when drawing the other widgets.
BOOL requesterDisplay(void)
{
  widgDisplayScreen(psRequestScreen); // display widgets.
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// STRING HANDLER, replaces dos wildcards in a string with harmless chars.
void removeWildcards(char* pStr)
{
  UDWORD i;

  for (i = 0; i < strlen(pStr); i++)
  {
    /*	if(   pStr[i] == '?' 
           || pStr[i] == '*'
           || pStr[i] == '"'
           || pStr[i] == '.' 
           || pStr[i] == '/' 
           || pStr[i] == '\\'
           || pStr[i] == '|' )
        {
          pStr[i] = '_';
        }
    */
    if (!isalnum(pStr[i]) && pStr[i] != ' ' && pStr[i] != '-' && pStr[i] != '+' && pStr[i] != '!')
      pStr[i] = '_';
  }

  if (strlen(pStr) >= MAX_REQUEST_NAME)
    pStr[MAX_REQUEST_NAME - 1] = 0;
}

// ////////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////////
// DISPLAY FUNCTIONS

static void displayRequesterBanner(struct _widget* psWidget, UDWORD xOffset, UDWORD yOffset, UDWORD* pColours)
{
  UDWORD col;
  UDWORD x = xOffset + psWidget->x;
  UDWORD y = yOffset + psWidget->y;

  UNUSEDPARAMETER(pColours);

  if (psWidget->pUserData)
    col = COL_GREEN;
  else
    col = COL_RED;

  pie_BoxFillIndex(x, y, x + psWidget->width, y + psWidget->height, col);
  pie_BoxFillIndex(x + 2, y + 2, x + psWidget->width - 2, y + psWidget->height - 2,COL_BLUE);
}

// ////////////////////////////////////////////////////////////////////////////
static void displayRequesterSlot(struct _widget* psWidget, UDWORD xOffset, UDWORD yOffset, UDWORD* pColours)
{
  UDWORD x = xOffset + psWidget->x;
  UDWORD y = yOffset + psWidget->y;
  UWORD im = static_cast<UWORD>(UNPACKDWORD_TRI_B((UDWORD)psWidget->pUserData));
  UWORD im2 = static_cast<UWORD>((UNPACKDWORD_TRI_C((UDWORD)psWidget->pUserData)));
  STRING butString[64];

  UNUSEDPARAMETER(pColours);
  drawBlueBox(x, y, psWidget->width, psWidget->height); //draw box
  if (((W_BUTTON*)psWidget)->pTip)
  {
    strcpy(butString, ((W_BUTTON*)psWidget)->pTip);

    Neuron::SetFont(WFont); // font
    Neuron::SetTextColour(-1); //colour

    while (Neuron::GetTextWidth((unsigned char*)butString) > psWidget->width) { butString[strlen(butString) - 1] = '\0'; }

    //draw text								
    pie_DrawText((unsigned char*)butString, x + 4, y + 17);
  }
}

// ////////////////////////////////////////////////////////////////////////////
static void displayRequesterEdit(struct _widget* psWidget, UDWORD xOffset, UDWORD yOffset, UDWORD* pColours)
{
  UDWORD x = xOffset + psWidget->x;
  UDWORD y = yOffset + psWidget->y;
  UDWORD w = psWidget->width;
  UDWORD h = psWidget->height;
  UNUSEDPARAMETER(pColours);

  pie_BoxFillIndex(x, y, x + w, y + h,COL_RED);
  pie_BoxFillIndex(x + 1, y + 1, x + w - 1, y + h - 1,COL_BLUE);
}

// ////////////////////////////////////////////////////////////////////////////
