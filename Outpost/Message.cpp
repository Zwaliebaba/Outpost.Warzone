#include "pch.h"
/*
 * Message.c		
 *
 * Functions for the messages shown in the Intelligence Map 
 *
 */
#include <stdio.h>

#include "Frame.h"
#include "Message.h"
#include "Stats.h"
#include "Text.h"
#include "Console.h"
#include "AudioSystem.h"
#include "AudioID.h"
#include "HCI.h"
#include "Model.h"
#include "IMD.h"
#include "ObjMem.h"
#include "Map.h"
#include "MultiPlay.h"

//max number of text strings or sequences for viewdata
#define MAX_DATA		4

//array of pointers for the view data
VIEWDATA_LIST* apsViewData;

/* The id number for the next message allocated
 * Each message will have a unique id number irrespective of type
 */
UDWORD msgID = 0;

static UDWORD currentNumProxDisplays;
/* The list of messages allocated */
MESSAGE* apsMessages[MAX_PLAYERS];

/* The list of proximity displays allocated */
PROXIMITY_DISPLAY* apsProxDisp[MAX_PLAYERS];

/* The current tutorial message - there is only ever one at a time. They are displayed 
when called by the script. They are not to be re-displayed*/

/* The IMD to use for the proximity messages */
iIMDShape* pProximityMsgIMD;

//function declarations
static void addProximityDisplay(MESSAGE* psMessage, BOOL proxPos, UDWORD player);
static void removeProxDisp(MESSAGE* psMessage, UDWORD player);
static void checkMessages(MSG_VIEWDATA* psViewData);

/* Creating a new message 
 * new is a pointer to a pointer to the new message
 * type is the type of the message
 */
// ajl modified for netgames
extern UDWORD selectedPlayer;

#define CREATE_MSG(ppNew, msgType) \
	*(ppNew) = new (std::nothrow) MESSAGE; \
	if (*(ppNew) != NULL) \
	{ \
		(*(ppNew))->type = msgType; \
		(*(ppNew))->id = (msgID<<3)|selectedPlayer; \
		msgID++; \
	}

/* Add the message to the BOTTOM of the list
 * list is a pointer to the message list
 * Order is now CAMPAIGN, MISSION, RESEARCH/PROXIMITY
 */
#define ADD_MSG(list, msg, player) \
	 \
	if (list[player] == NULL) \
	{ \
		list[player] = msg; \
	} \
	else \
	{ \
		MESSAGE *psCurr, *psPrev; \
        switch (msg->type) \
        { \
            case MSG_CAMPAIGN: \
                /*add to bottom of the list*/ \
    		    for(psCurr = list[player]; psCurr->psNext != NULL; \
	    		    psCurr = psCurr->psNext) \
		        { \
		        } \
		        psCurr->psNext = msg; \
                msg->psNext = NULL; \
                break; \
            case MSG_MISSION: \
                /*add it before the first campaign message */ \
    		    for(psCurr = list[player]; psCurr->psNext != NULL AND psCurr->type == MSG_CAMPAIGN; \
	    		    psCurr = psCurr->psNext) \
		        { \
                    psPrev = psCurr; \
		        } \
                psPrev->psNext = msg; \
                msg->psNext = psCurr; \
                break; \
            case MSG_RESEARCH: \
            case MSG_PROXIMITY: \
                /*add it before the first mission message */ \
    		    for(psCurr = list[player]; psCurr->psNext != NULL AND psCurr->type == MSG_MISSION; \
	    		    psCurr = psCurr->psNext) \
		        { \
                   psPrev = psCurr; \
		        } \
		        psPrev->psNext = msg; \
                msg->psNext = psCurr; \
                break; \
        } \
	}

void add_msg(MESSAGE* list[MAX_PLAYERS], MESSAGE* msg, UDWORD player)
{
  if (list[player] == nullptr)
  {
    list[player] = msg;
    msg->psNext = nullptr;
  }
  else
  {
    MESSAGE *psCurr, *psPrev;

    psCurr = psPrev = nullptr;
    switch (msg->type)
    {
    case MSG_CAMPAIGN:
      /*add it before the first mission/research/prox message */
      for (psCurr = list[player]; psCurr != nullptr; psCurr = psCurr->psNext)
      {
        if (psCurr->type == MSG_MISSION OR psCurr->type == MSG_RESEARCH OR psCurr->type == MSG_PROXIMITY)
          break;
        psPrev = psCurr;
      }
      if (psPrev)
      {
        psPrev->psNext = msg;
        msg->psNext = psCurr;
      }
      else
      {
        //must be top of list
        psPrev = list[player];
        list[player] = msg;
        msg->psNext = psPrev;
      }
      break;
    case MSG_MISSION:
      /*add it before the first research/prox message */
      for (psCurr = list[player]; psCurr != nullptr; psCurr = psCurr->psNext)
      {
        if (psCurr->type == MSG_RESEARCH OR psCurr->type == MSG_PROXIMITY)
          break;
        psPrev = psCurr;
      }
      if (psPrev)
      {
        psPrev->psNext = msg;
        msg->psNext = psCurr;
      }
      else
      {
        //must be top of list
        psPrev = list[player];
        list[player] = msg;
        msg->psNext = psPrev;
      }
      break;
    case MSG_RESEARCH:
    case MSG_PROXIMITY:
      /*add it to the bottom of the list */
      for (psCurr = list[player]; psCurr->psNext != nullptr; psCurr = psCurr->psNext) {}
      psCurr->psNext = msg;
      msg->psNext = nullptr;
      break;
    }
  }
}

/* Remove a message from the list 
 * list is a pointer to the message list
 * del is a pointer to the message to remove
*/
#define REMOVEMSG(list, del, player) \
	 \
	if (list[player] == del) \
	{ \
		list[player] = list[player]->psNext; \
		delete del; \
	} \
	else \
	{ \
		MESSAGE *psPrev = NULL, *psCurr; \
		for(psCurr = list[player]; (psCurr != del) && (psCurr != NULL); \
			psCurr = psCurr->psNext) \
		{ \
			psPrev = psCurr; \
		} \
		DEBUG_ASSERT_TEXT(psCurr != NULL,  \
			"removeMessage: message not found"); \
		if (psCurr != NULL) \
		{ \
			psPrev->psNext = psCurr->psNext; \
			delete del; \
		} \
	}

#define RELEASEALLMSG(list) \
	{ \
		UDWORD	i; \
		MESSAGE	*psCurr, *psNext; \
		for(i=0; i<MAX_PLAYERS; i++) \
		{ \
			for(psCurr = list[i]; psCurr != NULL; psCurr = psNext) \
			{ \
		 		psNext = psCurr->psNext; \
				delete psCurr; \
			} \
			list[i] = NULL; \
		} \
	}

BOOL messageInitVars(void)
{
  int i;

  msgID = 0;
  currentNumProxDisplays = 0;

  for (i = 0; i < MAX_PLAYERS; i++)
  {
    apsMessages[i] = nullptr;
    apsProxDisp[i] = nullptr;
  }

  pProximityMsgIMD = nullptr;

  return TRUE;
}

BOOL initViewData(void)
{
  return TRUE;
}

//destroys the viewdata heap
void viewDataHeapShutDown(void) {  }

/*Add a message to the list */
MESSAGE* addMessage(UDWORD msgType, BOOL proxPos, UDWORD player)
{
  MESSAGE* psMsgToAdd = nullptr;

  //first create a message of the required type
  CREATE_MSG(&psMsgToAdd, static_cast<MESSAGE_TYPE>(msgType));
  if (!psMsgToAdd)
    return nullptr;
  //then add to the players' list
  add_msg(apsMessages, psMsgToAdd, player);

  //initialise the message data
  psMsgToAdd->player = player;
  psMsgToAdd->pViewData = nullptr;
  psMsgToAdd->read = FALSE;

  //add a proximity display
  if (msgType == MSG_PROXIMITY)
    addProximityDisplay(psMsgToAdd, proxPos, player);
  //	 else
  //		 //make the reticule button flash as long as not prox msg or multiplayer game.

  return psMsgToAdd;
}

/* adds a proximity display - holds varaibles that enable the message to be 
 displayed in the Intelligence Screen*/
void addProximityDisplay(MESSAGE* psMessage, BOOL proxPos, UDWORD player)
{
  PROXIMITY_DISPLAY* psToAdd;

  //create the proximity display
  psToAdd = new (std::nothrow) PROXIMITY_DISPLAY;
  if (psToAdd != nullptr)
  {
    if (proxPos)
      psToAdd->type = POS_PROXOBJ;
    else
      psToAdd->type = POS_PROXDATA;
    psToAdd->psMessage = psMessage;
    psToAdd->screenX = 0;
    psToAdd->screenY = 0;
    psToAdd->screenR = 0;
    psToAdd->player = player;
    psToAdd->timeLastDrawn = 0;
    psToAdd->frameNumber = 0;
    psToAdd->selected = FALSE;
    psToAdd->strobe = 0;
  }

  //now add it to the top of the list
  psToAdd->psNext = apsProxDisp[player];
  apsProxDisp[player] = psToAdd;

  //add a button to the interface
  intAddProximityButton(psToAdd, currentNumProxDisplays);
  currentNumProxDisplays++;
}

/*remove a message */
void removeMessage(MESSAGE* psDel, UDWORD player)
{
  if (psDel->type == MSG_PROXIMITY)
    removeProxDisp(psDel, player);
  REMOVEMSG(apsMessages, psDel, player);
}

/* remove a proximity display */
void removeProxDisp(MESSAGE* psMessage, UDWORD player)
{
  PROXIMITY_DISPLAY *psCurr, *psPrev;

  //find the proximity display for this message
  if (apsProxDisp[player]->psMessage == psMessage)
  {
    psCurr = apsProxDisp[player];
    apsProxDisp[player] = apsProxDisp[player]->psNext;
    intRemoveProximityButton(psCurr);
    delete psCurr;
  }
  else
  {
    psPrev = apsProxDisp[player];
    for (psCurr = apsProxDisp[player]; psCurr != nullptr; psCurr = psCurr->psNext)
    {
      //compare the pointers
      if (psCurr->psMessage == psMessage)
      {
        psPrev->psNext = psCurr->psNext;
        intRemoveProximityButton(psCurr);
        delete psCurr;
        break;
      }
      psPrev = psCurr;
    }
  }
}

/* Remove all Messages*/
void freeMessages(void)
{
  releaseAllProxDisp();
  RELEASEALLMSG(apsMessages);
}

/* removes all the proximity displays */
void releaseAllProxDisp(void)
{
  UDWORD player;
  PROXIMITY_DISPLAY *psCurr, *psNext;

  for (player = 0; player < MAX_PLAYERS; player++)
  {
    for (psCurr = apsProxDisp[player]; psCurr != nullptr; psCurr = psNext)
    {
      psNext = psCurr->psNext;
      //remove message associated with this display
      removeMessage(psCurr->psMessage, player);
    }
    apsProxDisp[player] = nullptr;
  }
  //re-initialise variables
  currentNumProxDisplays = 0;
}

BOOL initMessage(void)
{
#ifdef VIDEO_TEST
  MESSAGE* psMessage;
#endif

  //set up the imd used for proximity messages
  pProximityMsgIMD = static_cast<iIMDShape*>(resGetData("IMD", "arrow.pie"));
  if (pProximityMsgIMD == nullptr)
  {
    Neuron::Fatal("Unable to load Proximity Message PIE");
    return FALSE;
  }

  //initialise the tutorial message - only used by scripts

  //JPS add message to get on screen video
#ifdef VIDEO_TEST
  //mission
  psMessage = addMessage(MSG_MISSION, FALSE, 0); if (psMessage) { psMessage->pViewData = (MSG_VIEWDATA*)getViewData("MB1A_MSG"); }
  //campaign
  psMessage = addMessage(MSG_CAMPAIGN, FALSE, 0); if (psMessage) { psMessage->pViewData = (MSG_VIEWDATA*)getViewData("CMB1_MSG"); }
#endif

  return TRUE;
}

BOOL addToViewDataList(VIEWDATA* psViewData, UBYTE numData)
{
  VIEWDATA_LIST* psAdd;

  psAdd = new (std::nothrow) VIEWDATA_LIST;
  if (psAdd != nullptr)
  {
    psAdd->psViewData = psViewData;
    psAdd->numViewData = numData;
    //add to top of list
    psAdd->psNext = apsViewData;
    apsViewData = psAdd;
    return TRUE;
  }
  return FALSE;
}

/*load the view data for the messages from the file */
VIEWDATA* loadViewData(SBYTE* pViewMsgData, UDWORD bufferSize)
{
  UDWORD i, id, dataInc, seqInc, numFrames, numData, count, count2;
  VIEWDATA *psViewData, *pData;
  VIEW_RESEARCH* psViewRes;
  VIEW_REPLAY* psViewReplay;
  STRING name[MAX_STR_LENGTH], imdName[MAX_NAME_SIZE], string[MAX_STR_LENGTH], imdName2[MAX_NAME_SIZE];
  STRING audioName[MAX_STR_LENGTH];
  SDWORD LocX, LocY, LocZ, proxType, audioID;

  //keep the start so we release it at the end

  numData = numCR((UBYTE*)pViewMsgData, bufferSize);
  if (numData > UBYTE_MAX)
  {
    Neuron::Fatal("loadViewData: Didn't expect 256 viewData messages!");
    return nullptr;
  }

  //allocate space for the data
  psViewData = new (std::nothrow) VIEWDATA[numData];
  if (psViewData == nullptr)
  {
    Neuron::Fatal("Unable to allocate memory for viewdata");
    return nullptr;
  }

  //add to array list
  addToViewDataList(psViewData, static_cast<UBYTE>(numData));

  //save so can pass the value back
  pData = psViewData;

  for (i = 0; i < numData; i++)
  {
    UDWORD numText;

    memset(psViewData, 0, sizeof(VIEWDATA));

    name[0] = '\0';

    //read the data into the storage - the data is delimeted using comma's
    sscanf1(&pViewMsgData, "%[^','],%d,", &name, &numText);

    //check not loading up too many text strings
    if (numText > MAX_DATA)
    {
      Neuron::Fatal("loadViewData: too many text strings for {}", psViewData->pName);
      return nullptr;
    }
    psViewData->numText = static_cast<UBYTE>(numText);

    //allocate storage for the name
    psViewData->pName = new (std::nothrow) STRING[(strlen(name))+1];
    if (psViewData->pName == nullptr)
    {
      Neuron::Fatal("ViewData Name - Out of memory");
      return nullptr;
    }
    strcpy(psViewData->pName, name);

    //allocate space for text strings
    if (psViewData->numText) { psViewData->ppTextMsg = new (std::nothrow) STRING*[psViewData->numText]; }

    //read in the data for the text strings
    for (dataInc = 0; dataInc < psViewData->numText; dataInc++)
    {
      name[0] = '\0';
      sscanf1(&pViewMsgData, "%[^','],", &name);

      //get the ID for the string
      if (!strresGetIDNum(psStringRes, name, &id))
      {
        Neuron::Fatal("Cannot find the view data string id {} ", name);
        return nullptr;
      }
      //get the string from the id
      psViewData->ppTextMsg[dataInc] = strresGetString(psStringRes, id);
    }

    sscanf1(&pViewMsgData, "%d,", &psViewData->type);

    //allocate data according to type
    switch (psViewData->type)
    {
    case VIEW_RES:
      psViewData->pData = new (std::nothrow) VIEW_RESEARCH[1];
      if (psViewData->pData == nullptr)
      {
        Neuron::Fatal("Unable to allocate memory");
        return nullptr;
      }
      imdName[0] = '\0';
      imdName2[0] = '\0';
      string[0] = '\0';
      audioName[0] = '\0';
      //sscanf(pViewMsgData, "%[^','],%[^','],%[^','],%[^','],%d", 
      sscanf1(&pViewMsgData, "%[^','],%[^','],%[^','],%[^','],%d,", &imdName, &imdName2, &string, &audioName, &numFrames);
      psViewRes = static_cast<VIEW_RESEARCH*>(psViewData->pData);
      psViewRes->pIMD = static_cast<iIMDShape*>(resGetData("IMD", imdName));
      if (psViewRes->pIMD == nullptr)
      {
        Neuron::Fatal("Cannot find the PIE for message {}", name);
        return nullptr;
      }
      if (strcmp(imdName2, "0"))
      {
        psViewRes->pIMD2 = static_cast<iIMDShape*>(resGetData("IMD", imdName2));
        if (psViewRes->pIMD2 == nullptr)
        {
          Neuron::Fatal("Cannot find the 2nd PIE for message {}", name);
          return nullptr;
        }
      }
      else
        psViewRes->pIMD2 = nullptr;
      strcpy(psViewRes->sequenceName, string);
      //get the audio text string
      if (strcmp(audioName, "0"))
      {
        //allocate space
        psViewRes->pAudio = new (std::nothrow) STRING[strlen(audioName) + 1];
        if (psViewRes->pAudio == nullptr)
        {
          Neuron::Fatal("loadViewData - Out of memory");
          return nullptr;
        }
        strcpy(psViewRes->pAudio, audioName);
      }
      else
        psViewRes->pAudio = nullptr;
      //this is for the PSX only
      psViewRes->numFrames = static_cast<UWORD>(numFrames);
      break;
    case VIEW_RPL:
    case VIEW_RPLX:
      // This is now also used for the stream playing on the PSX 
      // NOTE: on the psx the last entry (audioID) is used as the number of frames in the stream
      psViewData->pData = new (std::nothrow) VIEW_REPLAY[1];
      if (psViewData->pData == nullptr)
      {
        Neuron::Fatal("Unable to allocate memory");
        return nullptr;
      }
      psViewReplay = static_cast<VIEW_REPLAY*>(psViewData->pData);

      //read in number of sequences for this message
      sscanf1(&pViewMsgData, "%d,", &count);

      if (count > MAX_DATA)
      {
        Neuron::Fatal("loadViewData: too many sequence for {}", psViewData->pName);
        return nullptr;
      }

      psViewReplay->numSeq = static_cast<UBYTE>(count);

      //allocate space for the sequences
      psViewReplay->pSeqList = new (std::nothrow) SEQ_DISPLAY[psViewReplay->numSeq];

      //read in the data for the sequences
      for (dataInc = 0; dataInc < psViewReplay->numSeq; dataInc++)
      {
        name[0] = '\0';
        //load extradat for extended type only
        if (psViewData->type == VIEW_RPL)
        {
          sscanf1(&pViewMsgData, "%[^','],%d,", &name, &count);
          if (count > MAX_DATA)
          {
            Neuron::Fatal("loadViewData: too many strings for {}", psViewData->pName);
            return nullptr;
          }
          psViewReplay->pSeqList[dataInc].numText = static_cast<UBYTE>(count);
          //set the flag to default
          psViewReplay->pSeqList[dataInc].flag = 0;
        }
        else //extended type
        {
          sscanf1(&pViewMsgData, "%[^','],%d,%d,", &name, &count, &count2);
          if (count > MAX_DATA)
          {
            Neuron::Fatal("loadViewData: invalid video playback flag {}", psViewData->pName);
            return nullptr;
          }
          psViewReplay->pSeqList[dataInc].flag = static_cast<UBYTE>(count);
          //check not loading up too many text strings
          if (count2 > MAX_DATA)
          {
            Neuron::Fatal("loadViewData: too many text strings for seq for {}", psViewData->pName);
            return nullptr;
          }
          psViewReplay->pSeqList[dataInc].numText = static_cast<UBYTE>(count2);
        }
        strcpy(psViewReplay->pSeqList[dataInc].sequenceName, name);

        //get the text strings for this sequence - if any
        //allocate space for text strings
        if (psViewReplay->pSeqList[dataInc].numText)
        {
          psViewReplay->pSeqList[dataInc].ppTextMsg = new (std::nothrow) STRING*[psViewReplay->pSeqList[dataInc].numText];
        }
        //read in the data for the text strings
        for (seqInc = 0; seqInc < psViewReplay->pSeqList[dataInc].numText; seqInc++)
        {
          name[0] = '\0';
          sscanf1(&pViewMsgData, "%[^','],", &name);
          //get the ID for the string
          if (!strresGetIDNum(psStringRes, name, &id))
          {
            Neuron::Fatal("Cannot find the view data string id {} ", name);
            return nullptr;
          }
          //get the string from the id
          psViewReplay->pSeqList[dataInc].ppTextMsg[seqInc] = strresGetString(psStringRes, id);
        }
        //get the audio text string
        sscanf1(&pViewMsgData, "%[^','], %d,", &audioName, &count);

        DEBUG_ASSERT_TEXT(count < UWORD_MAX, "loadViewData: numFrames too high for {}", name);

        psViewReplay->pSeqList[dataInc].numFrames = static_cast<UWORD>(count);

        if (strcmp(audioName, "0"))
        {
          //allocate space
          psViewReplay->pSeqList[dataInc].pAudio = new (std::nothrow) STRING[strlen(audioName) + 1];
          if (psViewReplay->pSeqList[dataInc].pAudio == nullptr)
          {
            Neuron::Fatal("loadViewData - Out of memory");
            return nullptr;
          }
          strcpy(psViewReplay->pSeqList[dataInc].pAudio, audioName);
        }
        else
          psViewReplay->pSeqList[dataInc].pAudio = nullptr;
      }
      psViewData->type = VIEW_RPL; //no longer need to know if it is extended type
      break;

    case VIEW_PROX:
      psViewData->pData = new (std::nothrow) VIEW_PROXIMITY[1];
      if (psViewData->pData == nullptr)
      {
        Neuron::Fatal("Unable to allocate memory");
        return nullptr;
      }

      audioName[0] = '\0';
      sscanf1(&pViewMsgData, "%d,%d,%d,%[^','],%d", &LocX, &LocY, &LocZ, &audioName, &proxType);

      //allocate audioID
      if (strcmp(audioName, "0") == 0)
        audioID = NO_SOUND;
      else
      {
        if (audioID_GetIDFromStr(audioName, &audioID) == FALSE)
        {
          Neuron::Fatal("loadViewData: couldn't get ID {} for weapon sound {}", audioID, audioName);
          return FALSE;
        }

        if (((audioID < 0) || (audioID >= ID_MAX_SOUND)) && (audioID != NO_SOUND))
        {
          Neuron::Fatal("Invalid Weapon Sound ID - {} for weapon {}", audioID, audioName);
          return FALSE;
        }
      }

      static_cast<VIEW_PROXIMITY*>(psViewData->pData)->audioID = audioID;

      if (LocX < 0)
      {
        DEBUG_ASSERT_TEXT(FALSE, "loadViewData: Negative X coord for prox message - {}",name);
        return nullptr;
      }
      static_cast<VIEW_PROXIMITY*>(psViewData->pData)->x = static_cast<UDWORD>(LocX);
      if (LocY < 0)
      {
        DEBUG_ASSERT_TEXT(FALSE, "loadViewData: Negative Y coord for prox message - {}",name);
        return nullptr;
      }
      static_cast<VIEW_PROXIMITY*>(psViewData->pData)->y = static_cast<UDWORD>(LocY);
      if (LocZ < 0)
      {
        DEBUG_ASSERT_TEXT(FALSE, "loadViewData: Negative Z coord for prox message - {}",name);
        return nullptr;
      }
      static_cast<VIEW_PROXIMITY*>(psViewData->pData)->z = static_cast<UDWORD>(LocZ);

      if (proxType > PROX_TYPES)
      {
        DEBUG_ASSERT_TEXT(FALSE, "Invalid proximity message sub type - {}", name);
        return nullptr;
      }
      static_cast<VIEW_PROXIMITY*>(psViewData->pData)->proxType = static_cast<PROX_TYPE>(proxType);
      break;
    default: Neuron::Fatal("Unknown ViewData type");
      return nullptr;
    }
    //increment the pointer to the start of the next record
    pViewMsgData = strchr(pViewMsgData, '\n') + 1;
    //increment the list to the start of the next storage block
    psViewData++;
  }

  return pData;
}

/*get the view data identified by the name */
VIEWDATA* getViewData(STRING* pName)
{
  VIEWDATA_LIST* psList;
  UBYTE i;

  DEBUG_ASSERT_TEXT(strlen(pName)< MAX_STR_SIZE, "getViewData: verbose message name");

  for (psList = apsViewData; psList != nullptr; psList = psList->psNext)
  {
    for (i = 0; i < psList->numViewData; i++)
    {
      //compare the strings
      if (!strcmp(psList->psViewData[i].pName, pName))
        return &psList->psViewData[i];
    }
  }

  Neuron::Fatal("Unable to find viewdata for message {}", pName);
  return nullptr;
}

BOOL messageShutdown(void)
{
  freeMessages();

  return TRUE;
}

/* Release the viewdata memory */
void viewDataShutDown(VIEWDATA* psViewData)
{
  VIEWDATA_LIST *psList, *psPrev;
  UDWORD seqInc;
  VIEW_REPLAY* psViewReplay;
  VIEW_RESEARCH* psViewRes;
  UBYTE i;

  psPrev = apsViewData;

  for (psList = apsViewData; psList != nullptr; psList = psList->psNext)
  {
    if (psList->psViewData == psViewData)
    {
      for (i = 0; i < psList->numViewData; i++)
      {
        psViewData = &psList->psViewData[i];

        //check for any messages using this viewdata
        checkMessages((MSG_VIEWDATA*)psViewData);

        delete[] psViewData->pName;
        psViewData->pName = nullptr;
        //free the space allocated for the text messages
        if (psViewData->numText) { delete[] psViewData->ppTextMsg; }

        //free the space allocated for multiple sequences
        if (psViewData->type == VIEW_RPL)
        {
          psViewReplay = static_cast<VIEW_REPLAY*>(psViewData->pData);
          if (psViewReplay->numSeq)
          {
            for (seqInc = 0; seqInc < psViewReplay->numSeq; seqInc++)
            {
              //free the space allocated for the text messages
              if (psViewReplay->pSeqList[seqInc].numText) { delete[] psViewReplay->pSeqList[seqInc].ppTextMsg; }
              if (psViewReplay->pSeqList[seqInc].pAudio) { delete[] psViewReplay->pSeqList[seqInc].pAudio; }
            }
            delete[] psViewReplay->pSeqList;
            psViewReplay->pSeqList = nullptr;
          }
        }
        else if (psViewData->type == VIEW_RES)
        {
          psViewRes = static_cast<VIEW_RESEARCH*>(psViewData->pData);
          if (psViewRes->pAudio) { delete[] psViewRes->pAudio; }
        }
        delete[] psViewData->pData;
        psViewData->pData = nullptr;
      }
      delete[] psList->psViewData;
      psList->psViewData = nullptr;
      if (psList == apsViewData)
      {
        apsViewData = psList->psNext;
        delete psList;
      }
      else
      {
        psPrev->psNext = psList->psNext;
        delete psList;
      }
      break;
    }
  }
  psPrev = psList;
}

/* Looks through the players list of messages to find one with the same viewData 
pointer and which is the same type of message - used in scriptFuncs */
MESSAGE* findMessage(MSG_VIEWDATA* pViewData, MESSAGE_TYPE type, UDWORD player)
{
  MESSAGE* psCurr;

  for (psCurr = apsMessages[player]; psCurr != nullptr; psCurr = psCurr->psNext)
  {
    if (psCurr->type == type AND psCurr->pViewData == pViewData)
      return psCurr;
  }

  //not found the message so return NULL
  return nullptr;
}

/* 'displays' a proximity display*/
void displayProximityMessage(PROXIMITY_DISPLAY* psProxDisp)
{
  FEATURE* psFeature;
  VIEWDATA* psViewData;
  VIEW_PROXIMITY* psViewProx;

  if (psProxDisp->type == POS_PROXDATA)
  {
    psViewData = (VIEWDATA*)psProxDisp->psMessage->pViewData;

    //display text - if any
    if (psViewData->ppTextMsg)
      addConsoleMessage(psViewData->ppTextMsg[0], DEFAULT_JUSTIFY);

    //play message - if any
    psViewProx = static_cast<VIEW_PROXIMITY*>(psViewData->pData);
    if (psViewProx->audioID != NO_AUDIO_MSG) { AudioSystem::QueueTrackPos(psViewProx->audioID, psViewProx->x, psViewProx->y, psViewProx->z); }
  }
  else if (psProxDisp->type == POS_PROXOBJ)
  {
    DEBUG_ASSERT_TEXT(((BASE_OBJECT *)psProxDisp->psMessage->pViewData)->type == OBJ_FEATURE, "displayProximityMessage: invalid feature");

    psFeature = (FEATURE*)psProxDisp->psMessage->pViewData;
    if (psFeature->psStats->subType == FEAT_OIL_RESOURCE)
    {
      //play default audio message for oil resource
      AudioSystem::QueueTrackPos(ID_SOUND_RESOURCE_HERE, psFeature->x, psFeature->y, psFeature->z);
    }
    else if (psFeature->psStats->subType == FEAT_GEN_ARTE)
    {
      //play default audio message for artefact
      AudioSystem::QueueTrackPos(ID_SOUND_ARTIFACT, psFeature->x, psFeature->y, psFeature->z);
    }
  }

  //set the read flag
  psProxDisp->psMessage->read = TRUE;
}

/*void storeProximityScreenCoords(MESSAGE *psMessage, SDWORD x, SDWORD y)
{
	PROXIMITY_DISPLAY		*psProxDisp = NULL;

	psProxDisp = getProximityDisplay(psMessage);
	if (psProxDisp)
	{
		psProxDisp->screenX = x;
		psProxDisp->screenY = y;
	}
	else
	{
		ASSERT((FALSE, "Unable to find proximity display"));
	}
}*/

PROXIMITY_DISPLAY* getProximityDisplay(MESSAGE* psMessage)
{
  PROXIMITY_DISPLAY* psCurr;

  if (apsProxDisp[psMessage->player]->psMessage == psMessage)
    return apsProxDisp[psMessage->player];
  for (psCurr = apsProxDisp[psMessage->player]; psCurr != nullptr; psCurr = psCurr->psNext)
  {
    if (psCurr->psMessage == psMessage)
      return psCurr;
  }
  return nullptr;
}

//check for any messages using this viewdata and remove them
void checkMessages(MSG_VIEWDATA* psViewData)
{
  MESSAGE *psCurr, *psNext;
  UDWORD i;

  for (i = 0; i < MAX_PLAYERS; i++)
  {
    for (psCurr = apsMessages[i]; psCurr != nullptr; psCurr = psNext)
    {
      psNext = psCurr->psNext;
      if (psCurr->pViewData == psViewData)
        removeMessage(psCurr, i);
    }
  }
}

//add proximity messages for all untapped VISIBLE oil resources
void addOilResourceProximities(void)
{
  FEATURE* psFeat;
  MESSAGE* psMessage;

  //look thru the features to find oil resources
  for (psFeat = apsFeatureLists[0]; psFeat != nullptr; psFeat = psFeat->psNext)
  {
    if (psFeat->psStats->subType == FEAT_OIL_RESOURCE)
    {
      //check to see if the feature is visible to the selected player
      if (psFeat->visible[selectedPlayer])
      {
        //if there isn't an oil derrick built on it
        if (!TILE_HAS_STRUCTURE(mapTile(psFeat->x >> TILE_SHIFT, psFeat->y >> TILE_SHIFT)))
        {
          //add a proximity message
          psMessage = addMessage(MSG_PROXIMITY, TRUE, selectedPlayer);
          if (psMessage)
            psMessage->pViewData = (MSG_VIEWDATA*)psFeat;
        }
      }
    }
  }
}
