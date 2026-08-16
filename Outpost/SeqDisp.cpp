#include "pch.h"
/*
 * SeqDisp.c		(Sequence Display)
 *
 * Functions for the display of the Escape Sequences
 *
 */
#include "Frame.h"
#include "Screen.h"
#include "Widget.h"
#include "RendMode.h"
#include "SeqDisp.h"
#include "Sequence.h"
#include "Loop.h"
#include "PieFunc.h"
#include "PieState.h"
#include "HCI.h"//for font
#include "AudioSystem.h"
#include "Music.h"
#include "Deliverance.h"
#include "WarzoneConfig.h"

#include "MultiPlay.h"
#include "GTime.h"
#include "Mission.h"
#include "Script.h"
#include "ScriptTabs.h"
#include "Design.h"
#include "Wrappers.h"
#include "Palette.h"

/***************************************************************************/
/*
 *	local Definitions
 */
/***************************************************************************/
#define DUMMY_VIDEO
#define RPL_WIDTH 640
#define RPL_HEIGHT 480
#define RPL_DEPTH 2	//bytes, 16bit
#define RPL_MASK_555 0x7fff	//15bit
#define RPL_FRAME_TIME frameDuration	//milliseconds
#define STD_FRAME_TIME 40 //milliseconds
#define VIDEO_PLAYBACK_WIDTH 640
#define VIDEO_PLAYBACK_HEIGHT 480
#define VIDEO_PLAYBACK_DELAY 0
#define MAX_TEXT_OVERLAYS 32
#define MAX_SEQ_LIST	  6
#define SUBTITLE_BOX_MIN 430
#define SUBTITLE_BOX_MAX 480

using SEQTEXT = struct
{
  char pText[MAX_STR_LENGTH];
  SDWORD x;
  SDWORD y;
  SDWORD startFrame;
  SDWORD endFrame;
  BOOL bSubtitle;
};

using SEQLIST = struct
{
  char* pSeq; //name of the sequence to play
  char* pAudio; //name of the wav to play
  BOOL bSeqLoop; //loop this sequence
  SDWORD currentText; //cuurent number of text messages for this seq
  SEQTEXT aText[MAX_TEXT_OVERLAYS]; //text data to display for this sequence
};

/***************************************************************************/
/*
 *	local Variables
 */
/***************************************************************************/


BOOL bSeqInit = FALSE;
BOOL bSeqPlaying = FALSE;
BOOL bAudioPlaying = FALSE;
BOOL bHoldSeqForAudio = FALSE;
BOOL bSeq8Bit = TRUE;
BOOL bHardPath = FALSE;
BOOL bSeqSubtitles = TRUE;
char aHardPath[MAX_STR_LENGTH];
char aVideoName[MAX_STR_LENGTH];
char aAudioName[MAX_STR_LENGTH];
char aTextName[MAX_STR_LENGTH];
char aSubtitleName[MAX_STR_LENGTH];
char* pVideoBuffer = nullptr;
VIDEO_MODE videoMode;
PERF_MODE perfMode = VIDEO_PERF_FULLSCREEN;
static SDWORD frameSkip = 1;
SEQLIST aSeqList[MAX_SEQ_LIST];
static SDWORD currentSeq = -1;
static SDWORD currentPlaySeq = -1;
static SDWORD frameDuration = 40;


static int videoFrameTime = 0;
static SDWORD frame = 0;

/***************************************************************************/
/*
 *	local ProtoTypes
 */
/***************************************************************************/

void clearVideoBuffer(iSurface* surface);
void seq_SetVideoPath(void);

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

/* Renders a video sequence specified by filename to a buffer*/
BOOL seq_RenderVideoToBuffer(iSurface* pSurface, char* sequenceName, int time, int seqCommand)
{
  SDWORD frameLag;
  int videoTime;
  BOOL state = TRUE;
  FILE* pFileHandle;
  UNUSEDPARAMETER(pSurface);

  if (seqCommand == SEQUENCE_KILL)
  {
    //stop the movie
    seq_ShutDown();
    bSeqPlaying = FALSE;
    return TRUE;
  }

  if (!bSeqInit)
  {
    //this is done in HCI intInitialise() now
    //if ((bSeqInit = seq_SetupVideoBuffers()) == FALSE)
  }

  if (!bSeqPlaying)
  {
    //set a valid video path if there is one
    if (!bHardPath)
      seq_SetVideoPath();

    if (!bHardPath)
    {
      DEBUG_ASSERT_TEXT(FALSE, "seq_StartFullScreenVideo: sequence path not found");
      return FALSE;
    }

    seq_BuildVideoName(aHardPath, sequenceName, aVideoName);

    /* Probed rather than assumed: nine of the movies the game names have no
     * source in any format, so a missing file here is reachable and the
     * player just gets no sequence. */
    pFileHandle = fopen(aVideoName, "rb");
    if (pFileHandle != nullptr)
      fclose(pFileHandle);

    Neuron::SetFont(WFont);
    Neuron::SetTextColour(-1);

    videoMode = VIDEO_D3D_WINDOW;

    //for new timing
    frame = 0;
    videoFrameTime = GetTickCount();

    if ((bSeqPlaying = seq_SetSequenceForBuffer(aVideoName, videoMode, videoFrameTime, perfMode)) == FALSE)
    {
#ifdef DUMMY_VIDEO
      if ((bSeqPlaying = seq_SetSequenceForBuffer("noVideo.mp4", videoMode, time, perfMode)) == TRUE)
        return TRUE;
#endif
      DEBUG_ASSERT_TEXT(FALSE, "seq_RenderVideoToBuffer: unable to initialise sequence {}",aVideoName);
      return FALSE;
    }
    bSeqPlaying = TRUE;
    frameDuration = seq_GetFrameTimeInClicks();
    //jps 18 feb 99		seq_RenderOneFrameToBuffer(pVideoBuffer, 1);//skip frame if behind
  }

  if (frame != VIDEO_FINISHED)
  {
    //new call with timing
    //poll the sequence player while timing the video
    videoTime = GetTickCount();
    while (videoTime < (videoFrameTime + RPL_FRAME_TIME))
    {
      seq_RefreshVideoBuffers();
      videoTime = GetTickCount();
    }
    frameLag = videoTime - videoFrameTime;
    frameLag /= RPL_FRAME_TIME; // if were running slow frame lag will be greater than 1
    videoFrameTime += frameLag * RPL_FRAME_TIME; //frame Lag should be 1 (most of the time)   
    //call sequence player to decode a frame
    frame = seq_RenderOneFrameToBuffer(pVideoBuffer, frameLag, 2, 0); //skip frame if behind
    //old call
  }

  if (frame == VIDEO_FINISHED)
  {
    if (seqCommand == SEQUENCE_LOOP)
    {
      bSeqPlaying = FALSE;
      state = TRUE;
    }
    if (seqCommand == SEQUENCE_HOLD)
    {
      //wait for call to stop video
      state = TRUE;
    }
    else
    {
      state = FALSE;
      seq_ShutDown();
      bSeqPlaying = FALSE;
    }
  }
  else if (frame < 0) //an ERROR
  {
    Neuron::DebugTrace("VIDEO FRAME ERROR {}\n",frame);
    state = FALSE;
    seq_ShutDown();
    bSeqPlaying = FALSE;
  }

  return state;
}

BOOL seq_BlitBufferToScreen(char* screen, SDWORD screenStride, SDWORD xOffset, SDWORD yOffset)
{
  SDWORD width, height;
  seq_GetFrameSize(&width, &height);

  pie_DownLoadBufferToScreen(pVideoBuffer, xOffset, yOffset, width, height, (2 * width));
  return TRUE;
}

void clearVideoBuffer(iSurface* surface)
{
  UDWORD i;
  UDWORD* toClear;

  toClear = (UDWORD*)surface->buffer;
  for (i = 0; i < static_cast<UDWORD>(surface->size / 4); i++)
    *toClear++ = 0xFCFCFCFC;
}

BOOL seq_ReleaseVideoBuffers(void)
{
  delete[] pVideoBuffer;
  pVideoBuffer = nullptr;
  return TRUE;
}

BOOL seq_SetupVideoBuffers(void)
{
  SDWORD mallocSize;
  //assume 320 * 240 * 16bit playback surface
  mallocSize = (RPL_WIDTH * RPL_HEIGHT * RPL_DEPTH);
  if ((pVideoBuffer = new (std::nothrow) char[mallocSize]) == nullptr)
    return FALSE;

  /* The 555-to-palette-index lookup that was built here went unread from the
   * day the RPL decoder was replaced; the MP4 decoder hands over RGB. */

  return TRUE;
}

/* Builds the on-disk name of a movie: the video path plus the sequence name.
 *
 * Every name the game holds is `.mp4` now -- the call sites here and in
 * WinMain, Mission, FrontEnd and ScriptFuncs, and every record in
 * GameData/messages. There was briefly an extension rewrite in this function,
 * translating the old `.rpl` names as they went past; it is gone, because a
 * name that has to be corrected before it can be used is a name stored wrong.
 *
 * Save games never held movie names to begin with: they store message ids, and
 * the name comes back from the message data on load.
 */
void seq_BuildVideoName(const char* pPath, const char* pSeqName, char* pOut)
{
  DEBUG_ASSERT_TEXT((strlen(pSeqName) + strlen(pPath)) < MAX_STR_LENGTH, "sequence path+name greater than max string");

  strcpy(pOut, pPath);
  strcat(pOut, pSeqName);
}

void seq_SetVideoPath(void)
{
  WIN32_FIND_DATA findData;
  HANDLE fileHandle;

  /* set up the hard disc path */
  if (!bHardPath)
  {
    strcpy(aHardPath, "sequences\\");
    /* This glob decides whether the hard-disk path is usable at all, so it has
     * to name the container that is actually there: it decides whether the
     * hard-disk video path is usable at all, so a stale extension here reports
     * "no videos installed" and every sequence in the game silently stops.
     */
    fileHandle = FindFirstFile("sequences\\*.mp4", &findData);
    if (fileHandle == INVALID_HANDLE_VALUE)
      bHardPath = FALSE;
    else
    {
      bHardPath = TRUE;
      FindClose(fileHandle);
    }
  }
}

BOOL SeqEndCallBack(AUDIO_SAMPLE* psSample)
{
  psSample;
  bAudioPlaying = FALSE;
  Neuron::DebugTrace("************* briefing ended **************\n");

  return TRUE;
}

//full screenvideo functions
BOOL seq_StartFullScreenVideo(char* videoName, char* audioName)
{
  SDWORD i;
  FILE* pFileHandle;
  bHoldSeqForAudio = FALSE;

  frameSkip = 1;
  switch (war_GetSeqMode())
  {
  case SEQ_SMALL:
    perfMode = VIDEO_PERF_WINDOW;
    break;
  case SEQ_SKIP:
    frameSkip = 2;
    perfMode = VIDEO_PERF_SKIP_FRAMES;
    break;
  default: case SEQ_FULL:
    perfMode = VIDEO_PERF_FULLSCREEN;
    break;
  }

  //set a valid video path if there is one
  if (!bHardPath)
    seq_SetVideoPath();

  if (!bHardPath)
  {
    DEBUG_ASSERT_TEXT(FALSE, "seq_StartFullScreenVideo: sequence path not found");
    return FALSE;
  }

  seq_BuildVideoName(aHardPath, videoName, aVideoName);

  pFileHandle = fopen(aVideoName, "rb");
  if (pFileHandle != nullptr)
    fclose(pFileHandle);

  //set audio path
  if (audioName != nullptr)
  {
    DEBUG_ASSERT_TEXT(strlen(audioName)<244, "sequence path+name greater than max string");
    strcpy(aAudioName, "sequenceAudio\\");
    strcat(aAudioName, audioName);
  }

  //start video mode
  if (loop_GetVideoMode() == 0)
  {
    Music::Pause();
    loop_SetVideoPlaybackMode();
    Neuron::SetFont(WFont);
    Neuron::SetTextColour(-1);
  }

  if (audioName != nullptr)
  {
    DEBUG_ASSERT_TEXT(strlen(audioName)<244, "sequence path+name greater than max string");
    strcpy(aAudioName, "sequenceAudio\\");
    strcat(aAudioName, audioName);
  }

  //for new timing
  frame = 0;
  videoFrameTime = GetTickCount();

  if (!seq_SetSequence(aVideoName, videoFrameTime + VIDEO_PLAYBACK_DELAY, pVideoBuffer, perfMode))
  {
#ifdef DUMMY_VIDEO
    if (seq_SetSequence("noVideo.mp4", videoFrameTime + VIDEO_PLAYBACK_DELAY, pVideoBuffer, perfMode))
    {
      strcpy(aAudioName, "noVideo.wav");
      return TRUE;
    }
#endif
    seq_StopFullScreenVideo();
    return FALSE;
  }
  if (perfMode != VIDEO_PERF_SKIP_FRAMES) //JPS fix for video problems with some sound cards 9 may 99
    frameDuration = seq_GetFrameTimeInClicks();
  else
  {
    frameDuration = (seq_GetFrameTimeInClicks() * 112) / 100;
    //JPS fix for video problems with some sound cards 9 may 99
  }

  if (audioName == nullptr)
    bAudioPlaying = FALSE;
  else
  {
    bAudioPlaying = AudioSystem::PlayStream(aAudioName, AUDIO_VOL_MAX, SeqEndCallBack);
    DEBUG_ASSERT_TEXT(bAudioPlaying == TRUE, "seq_StartFullScreenVideo: unable to initialise sound {}",aAudioName);
  }

  return TRUE;
}

BOOL seq_UpdateFullScreenVideo(CLEAR_MODE* pbClear)
{
  SDWORD i;
  SDWORD frame, frameLag, realFrame;
  SDWORD subMin, subMax;
  int videoTime;
  static int videoFrameTime = 0, textFrame = 0;
  BOOL bMoreThanOneSequenceLine = FALSE;

  if (seq_GetCurrentFrame() == 0)
  {
    //for new timing
    frame = 0;
    videoFrameTime = GetTickCount();
    textFrame = 0;
  }

  subMin = SUBTITLE_BOX_MAX + D_H;
  subMax = SUBTITLE_BOX_MIN + D_H;

  //get any text lines over bottom of the video
  realFrame = textFrame + 1;
  for (i = 0; i < MAX_TEXT_OVERLAYS; i++)
  {
    if (aSeqList[currentPlaySeq].aText[i].pText[0] != 0)
    {
      if (aSeqList[currentPlaySeq].aText[i].bSubtitle == TRUE)
      {
        if ((realFrame >= aSeqList[currentPlaySeq].aText[i].startFrame) && (realFrame <= aSeqList[currentPlaySeq].aText[i].endFrame))
        {
          if (subMin > aSeqList[currentPlaySeq].aText[i].y)
          {
            if (aSeqList[currentPlaySeq].aText[i].y > SUBTITLE_BOX_MIN)
              subMin = aSeqList[currentPlaySeq].aText[i].y;
          }
          if (subMax < aSeqList[currentPlaySeq].aText[i].y)
            subMax = aSeqList[currentPlaySeq].aText[i].y;
        }
        else if (aSeqList[currentPlaySeq].bSeqLoop) //if its a looped video always draw the text
        {
          if (subMin >= aSeqList[currentPlaySeq].aText[i].y)
          {
            if (aSeqList[currentPlaySeq].aText[i].y > SUBTITLE_BOX_MIN)
              subMin = aSeqList[currentPlaySeq].aText[i].y;
          }
          if (subMax < aSeqList[currentPlaySeq].aText[i].y)
            subMax = aSeqList[currentPlaySeq].aText[i].y;
        }
      }
      if ((realFrame >= aSeqList[currentPlaySeq].aText[i].endFrame) && (realFrame < (aSeqList[currentPlaySeq].aText[i].endFrame +
        frameSkip)))
      {
        if (pbClear != nullptr)
        {
          if (perfMode != VIDEO_PERF_FULLSCREEN)
            *pbClear = CLEAR_BLACK;
        }
      }
    }
  }

  subMin -= D_H; //adjust video window here because text is already ofset for big screens
  subMax -= D_H;

  if (subMin < SUBTITLE_BOX_MIN)
    subMin = SUBTITLE_BOX_MIN;
  if (subMax > SUBTITLE_BOX_MAX)
    subMax = SUBTITLE_BOX_MAX;

  if (subMax > subMin)
    bMoreThanOneSequenceLine = TRUE;

  if (bHoldSeqForAudio == FALSE)
  {
    if (perfMode != VIDEO_PERF_SKIP_FRAMES)
    {
      //version 1.00 release code
      //poll the sequence player while timing the video
      videoTime = GetTickCount();
      while (videoTime < (videoFrameTime + (RPL_FRAME_TIME * frameSkip)))
      {
        videoTime = GetTickCount();
        seq_RefreshVideoBuffers();
      }
      frameLag = videoTime - videoFrameTime;
      frameLag /= RPL_FRAME_TIME; // if were running slow frame lag will be greater than 1
      videoFrameTime += frameLag * RPL_FRAME_TIME; //frame Lag should be 1 (most of the time)   
      //call sequence player to decode a frame
      frame = seq_RenderOneFrame(frameLag, subMin, subMax);
    }
    else
    {
      //new version with timeing removed
      //poll the sequence player while timing the video
      videoTime = GetTickCount();
      while (videoTime < (videoFrameTime + (RPL_FRAME_TIME * frameSkip)))
      {
        videoTime = GetTickCount();
        seq_RefreshVideoBuffers();
      }
      videoFrameTime += frameSkip * RPL_FRAME_TIME; //frame Lag should be 1 (most of the time)   
      //call sequence player to decode a frame
      frame = seq_RenderOneFrame(frameSkip, subMin, subMax);
    }
  }
  else
  {
    //call sequence player to download last frame
    frame = seq_RenderOneFrame(0, 2, 0);
  }
  //print any text over the video
  realFrame = textFrame + 1;
  for (i = 0; i < MAX_TEXT_OVERLAYS; i++)
  {
    if (aSeqList[currentPlaySeq].aText[i].pText[0] != 0)
    {
      if ((realFrame >= aSeqList[currentPlaySeq].aText[i].startFrame) && (realFrame <= aSeqList[currentPlaySeq].aText[i].endFrame))
      {
        if (bMoreThanOneSequenceLine)
          aSeqList[currentPlaySeq].aText[i].x = 20 + D_W;
        pie_DrawTextToBackBuffer((unsigned char*)&(aSeqList[currentPlaySeq].aText[i].pText[0]), aSeqList[currentPlaySeq].aText[i].x,
                                 aSeqList[currentPlaySeq].aText[i].y);
      }
      else if (aSeqList[currentPlaySeq].bSeqLoop) //if its a looped video always draw the text
      {
        if (bMoreThanOneSequenceLine)
          aSeqList[currentPlaySeq].aText[i].x = 20 + D_W;
        pie_DrawTextToBackBuffer((unsigned char*)&(aSeqList[currentPlaySeq].aText[i].pText[0]), aSeqList[currentPlaySeq].aText[i].x,
                                 aSeqList[currentPlaySeq].aText[i].y);
      }
    }
  }

  textFrame = frame * RPL_FRAME_TIME / STD_FRAME_TIME;

  if ((frame == VIDEO_FINISHED) || (bHoldSeqForAudio))
  {
#ifndef SEQ_LOOP
    if (bAudioPlaying)
#endif
    {
      if (aSeqList[currentPlaySeq].bSeqLoop)
      {
        seq_ClearMovie();
        if (!seq_SetSequence(aVideoName, GetTickCount() + VIDEO_PLAYBACK_DELAY, pVideoBuffer, perfMode))
          bHoldSeqForAudio = TRUE;
        frameDuration = seq_GetFrameTimeInClicks();
      }
      else
        bHoldSeqForAudio = TRUE;
      return TRUE; //should hold the video
    }
#ifndef SEQ_LOOP
    //should terminate the video
    return FALSE;
#endif
  }
  if (frame < 0) //an ERROR
  {
    Neuron::DebugTrace("VIDEO FRAME ERROR {}\n",frame);
    return FALSE;
  }

  return TRUE;
}

BOOL seq_StopFullScreenVideo(void)
{
  if (!seq_AnySeqLeft())
    loop_ClearVideoPlaybackMode();

  seq_ShutDown();

  return TRUE;
}

BOOL seq_GetVideoSize(SDWORD* pWidth, SDWORD* pHeight) { return seq_GetFrameSize(pWidth, pHeight); }

#define BUFFER_WIDTH 600
#define FOLLOW_ON_JUSTIFICATION 160
#define MIN_JUSTIFICATION 40

// add a string at x,y or add string below last line if x and y are 0
BOOL seq_AddTextForVideo(UBYTE* pText, SDWORD xOffset, SDWORD yOffset, SDWORD startFrame, SDWORD endFrame, SDWORD bJustify,
                         UDWORD PSXSeqNumber)
{
  SDWORD sourceLength, currentLength;
  char* currentText;
  SDWORD justification;
  static SDWORD lastX;

  Neuron::SetFont(WFont);

  DEBUG_ASSERT_TEXT(aSeqList[currentSeq].currentText < MAX_TEXT_OVERLAYS, "seq_AddTextForVideo: too many text lines");

  sourceLength = strlen((const char*)pText);
  currentLength = sourceLength;
  currentText = &(aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].pText[0]);

  //if the string is bigger than the buffer get the last end of the last fullword in the buffer
  if (currentLength >= MAX_STR_LENGTH)
  {
    currentLength = MAX_STR_LENGTH - 1;
    //get end of the last word
    while ((pText[currentLength] != ' ') && (currentLength > 0)) { currentLength--; }
    currentLength--;
  }

  memcpy(currentText, pText, currentLength);
  currentText[currentLength] = 0; //terminate the string what ever

  //check the string is shortenough to print
  //if not take a word of the end and try again
  while (Neuron::GetTextWidth((unsigned char*)currentText) > BUFFER_WIDTH)
  {
    currentLength--;
    while ((pText[currentLength] != ' ') && (currentLength > 0)) { currentLength--; }
    currentText[currentLength] = 0; //terminate the string what ever
  }
  currentText[currentLength] = 0; //terminate the string what ever

  //check if x and y are 0 and put text on next line
  if (((xOffset == 0) && (yOffset == 0)) && (currentLength > 0))
  {
    aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].x = lastX;
    aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].y = aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText - 1].y +
      Neuron::GetTextLineSize();
  }
  else
  {
    aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].x = xOffset + D_W;
    aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].y = yOffset + D_H;
  }
  lastX = aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].x;

  if ((bJustify) && (currentLength == sourceLength))
  {
    //justify this text
    justification = BUFFER_WIDTH - Neuron::GetTextWidth((unsigned char*)currentText);
    if ((bJustify == SEQ_TEXT_JUSTIFY) && (justification > MIN_JUSTIFICATION))
      aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].x += (justification / 2);
    else if ((bJustify == SEQ_TEXT_FOLLOW_ON) && (justification > FOLLOW_ON_JUSTIFICATION)) {}
  }

  //set start and finish times for the objects	
  aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].startFrame = startFrame;
  aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].endFrame = endFrame;
  aSeqList[currentSeq].aText[aSeqList[currentSeq].currentText].bSubtitle = bJustify;

  aSeqList[currentSeq].currentText++;
  if (aSeqList[currentSeq].currentText >= MAX_TEXT_OVERLAYS)
    aSeqList[currentSeq].currentText = 0;

  //check text is okay on the screen
  if (currentLength < sourceLength)
  {
    //RECURSE x= 0 y = 0 for nextLine
    if (bJustify == SEQ_TEXT_JUSTIFY)
      bJustify = SEQ_TEXT_POSITION;
    seq_AddTextForVideo(&pText[currentLength + 1], 0, 0, startFrame, endFrame, bJustify, 0);
  }
  return TRUE;
}

BOOL seq_ClearTextForVideo(void)
{
  SDWORD i, j;

  for (j = 0; j < MAX_SEQ_LIST; j++)
  {
    for (i = 0; i < MAX_TEXT_OVERLAYS; i++)
    {
      aSeqList[j].aText[i].pText[0] = 0;
      aSeqList[j].aText[i].x = 0;
      aSeqList[j].aText[i].y = 0;
      aSeqList[j].aText[i].startFrame = 0;
      aSeqList[j].aText[i].endFrame = 0;
      aSeqList[j].aText[i].bSubtitle = 0;
    }
    aSeqList[j].currentText = 0;
  }
  return TRUE;
}

BOOL seq_AddTextFromFile(STRING* pTextName, BOOL bJustify)
{
  UBYTE *pTextBuffer, *pCurrentLine, *pText;
  UDWORD fileSize;
  HANDLE fileHandle;
  WIN32_FIND_DATA findData;
  BOOL endOfFile = FALSE;
  SDWORD xOffset, yOffset, startFrame, endFrame;
  auto seps = (UBYTE*)"\n";

  strcpy(aTextName, "sequenceAudio\\");
  strcat(aTextName, pTextName);

  if (loadFileToBufferNoError(aTextName, DisplayBuffer, displayBufferSize, &fileSize) == FALSE)
    return FALSE;

  pTextBuffer = DisplayBuffer;
  pCurrentLine = (UBYTE*)strtok((char*)pTextBuffer, (const char*)seps);
  while (pCurrentLine != nullptr)
  {
    if (*pCurrentLine != '/')
    {
      if (sscanf((const char*)pCurrentLine, "%d %d %d %d", &xOffset, &yOffset, &startFrame, &endFrame) == 4)
      {
        //get the text
        pText = (UBYTE*)strrchr((const char*)pCurrentLine, '"');
        DEBUG_ASSERT_TEXT(pText != NULL, "seq_AddTextFromFile error parsing text file");
        if (pText != nullptr)
          *pText = static_cast<UBYTE>(0);
        pText = (UBYTE*)strchr((const char*)pCurrentLine, '"');
        DEBUG_ASSERT_TEXT(pText != NULL, "seq_AddTextFromFile error parsing text file");
        if (pText != nullptr)
          seq_AddTextForVideo(&pText[1], xOffset, yOffset, startFrame, endFrame, bJustify, 0);
      }
    }
    //get next line
    pCurrentLine = (UBYTE*)strtok(nullptr, (const char*)seps);
  }
  return TRUE;
}

//clear the sequence list
void seq_ClearSeqList(void)
{
  SDWORD i;

  seq_ClearTextForVideo();
  for (i = 0; i < MAX_SEQ_LIST; i++)
    aSeqList[i].pSeq = nullptr;
  currentSeq = -1;
  currentPlaySeq = -1;
}

//add a sequence to the list to be played
void seq_AddSeqToList(STRING* pSeqName, STRING* pAudioName, STRING* pTextName, BOOL bLoop, UDWORD PSXSeqNumber)
{
  SDWORD strLen;
  currentSeq++;

  if ((currentSeq) >= MAX_SEQ_LIST)
  {
    DEBUG_ASSERT_TEXT(FALSE, "seq_AddSeqToList: too many sequences");
    return;
  }
#ifdef SEQ_LOOP
  bLoop = TRUE;
#endif

  //OK so add it to the list
  aSeqList[currentSeq].pSeq = pSeqName;
  aSeqList[currentSeq].pAudio = pAudioName;
  aSeqList[currentSeq].bSeqLoop = bLoop;
  if (pTextName != nullptr)
    seq_AddTextFromFile(pTextName, FALSE); //SEQ_TEXT_POSITION);//ordinary text not justified

  if (bSeqSubtitles)
  {
    //check for a subtitle file
    strLen = strlen(pSeqName);
    DEBUG_ASSERT_TEXT(strLen < MAX_STR_LENGTH, "seq_AddSeqToList: sequence name error");
    strcpy(aSubtitleName, pSeqName);
    aSubtitleName[strLen - 4] = 0;
    strcat(aSubtitleName, ".txt");
    seq_AddTextFromFile(aSubtitleName, TRUE); //SEQ_TEXT_JUSTIFY);//subtitles centre justified
  }
}

/*checks to see if there are any sequences left in the list to play*/
BOOL seq_AnySeqLeft(void)
{
  UBYTE nextSeq;

  nextSeq = static_cast<UBYTE>(currentPlaySeq + 1);

  //check haven't reached end
  if (nextSeq > MAX_SEQ_LIST)
    return FALSE;
  if (aSeqList[nextSeq].pSeq)
    return TRUE;
  return FALSE;
}

void seqDispPlayNext(void)
{
  BOOL bPlayedOK;

  screen_StopBackDrop();

  currentPlaySeq++;
  if (currentPlaySeq >= MAX_SEQ_LIST)
    bPlayedOK = FALSE;
  else
    bPlayedOK = seq_StartFullScreenVideo(aSeqList[currentPlaySeq].pSeq, aSeqList[currentPlaySeq].pAudio);

  if (bPlayedOK == FALSE)
  {
    //don't do the callback if we're playing the win/lose video
    if (!getScriptWinLoseVideo())
      eventFireCallbackTrigger(CALL_VIDEO_QUIT);
    else
      displayGameOver(getScriptWinLoseVideo() == PLAY_WIN);
  }
}

/*returns the next sequence in the list to play*/
void seq_StartNextFullScreenVideo(void) { seqDispPlayNext(); }

void seq_SetSubtitles(BOOL bNewState) { bSeqSubtitles = bNewState; }

BOOL seq_GetSubtitles(void) { return bSeqSubtitles; }

/*play a video now and clear all other videos, front end use only*/
/*
BOOL seq_PlayVideo(char* pSeq, char* pAudio)
{
	seq_ClearSeqList();//other wise me might trigger these videos when we finish
	seq_StartFullScreenVideo(pSeq, pAudio);
	while (loop_GetVideoStatus())
	{
		videoLoop();
	}
	return TRUE;
}
*/
