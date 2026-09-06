#include "pch.h"
#include "UiEvents.h"

#include "AudioID.h"
#include "AudioSystem.h"
#include "Console.h"
#include "Debug.h"
#include "Display.h"
#include "Display3D.h"
#include "Frame.h"
#include "HCI.h"
#include "IntelMap.h"
#include "Map.h"
#include "Message.h"
#include "Player.h"
#include "ScriptExtern.h"
#include "SeqDisp.h"

#include <cstring>

namespace
{
using Neuron::ServerUiEvent;
using Neuron::UiEventEveryPlayer;
using Neuron::UiEventKind;
using Neuron::UiEventNameChars;
using Neuron::UiEventTextChars;

/// The sequence player keeps the pointers it is handed rather than copying the
/// names, so what a PlayVideo carries has to outlive the event. One sequence
/// at a time is all scrPlayVideo ever queued, so one pair of buffers is enough.
char g_videoName[UiEventNameChars];
char g_videoText[UiEventTextChars];

/// Whether an event addressed to _player is for the player this client shows.
[[nodiscard]] bool ForThisClient(std::uint8_t _player)
{
  return _player == UiEventEveryPlayer || _player == selectedPlayer;
}

void AddIntelligenceMessage(const ServerUiEvent& _event)
{
  if (_event.player >= MAX_PLAYERS)
    return;

  /* The view data is named rather than pointed at, because a pointer means
     nothing across the boundary. getViewData wants a writable string. */
  char name[UiEventNameChars];
  std::strcpy(name, _event.name);
  VIEWDATA* view = getViewData(name);
  if (view == nullptr)
  {
    Neuron::DebugTrace("UiEvent: no view data named {}\n", _event.name);
    return;
  }

  MESSAGE* message = addMessage(_event.a, FALSE, _event.player);
  if (message == nullptr)
    return;

  message->pViewData = reinterpret_cast<MSG_VIEWDATA*>(view);
  if (_event.a == MSG_PROXIMITY)
  {
    /* A proximity message sits on the terrain, never under it. */
    auto* proximity = static_cast<VIEW_PROXIMITY*>(view->pData);
    const UDWORD height = map_Height(proximity->x, proximity->y);
    if (proximity->z < height)
      proximity->z = height;
  }

  if (_event.b != 0)
  {
    displayImmediateMessage(message);
    stopReticuleButtonFlash(IDRET_INTEL_MAP);
  }
}

void RemoveIntelligenceMessage(const ServerUiEvent& _event)
{
  if (_event.player >= MAX_PLAYERS)
    return;

  char name[UiEventNameChars];
  std::strcpy(name, _event.name);
  VIEWDATA* view = getViewData(name);
  if (view == nullptr)
  {
    Neuron::DebugTrace("UiEvent: no view data named {}\n", _event.name);
    return;
  }

  MESSAGE* message = findMessage(reinterpret_cast<MSG_VIEWDATA*>(view), static_cast<MESSAGE_TYPE>(_event.a), _event.player);
  if (message == nullptr)
  {
    Neuron::DebugTrace("UiEvent: no message to remove for {}\n", _event.name);
    return;
  }

  removeMessage(message, _event.player);
}

void ShowConsoleText(const ServerUiEvent& _event)
{
  if (!ForThisClient(_event.player))
    return;

  /* addConsoleMessage copies the text, but takes it as a writable string. */
  char text[UiEventTextChars];
  std::strcpy(text, _event.text);

  const bool permanent = _event.a != 0;
  permitNewConsoleMessages(TRUE);
  if (permanent)
    setConsolePermanence(TRUE, TRUE);
  addConsoleMessage(text, CENTRE_JUSTIFY);
  if (permanent)
    permitNewConsoleMessages(FALSE);
}

void PlayVideo(const ServerUiEvent& _event)
{
  std::strcpy(g_videoName, _event.name);
  std::strcpy(g_videoText, _event.text);

  seq_ClearSeqList();
  seq_AddSeqToList(g_videoName, nullptr, g_videoText, FALSE, 0);
  seq_StartNextFullScreenVideo();
}
} // namespace

void ApplyUiEvent(const ServerUiEvent& _event)
{
  switch (_event.kind)
  {
  case UiEventKind::AddMessage:
    AddIntelligenceMessage(_event);
    break;

  case UiEventKind::RemoveMessage:
    RemoveIntelligenceMessage(_event);
    break;

  case UiEventKind::CentreView:
    if (ForThisClient(_event.player))
      setViewPos(_event.a, _event.b, FALSE);
    break;

  case UiEventKind::PlaySound:
    if (ForThisClient(_event.player))
    {
      AudioSystem::QueueTrack(static_cast<std::int32_t>(_event.a));
      if (bInTutorial)
        AudioSystem::QueueTrack(ID_SOUND_OF_SILENCE);
    }
    break;

  case UiEventKind::ConsoleText:
    ShowConsoleText(_event);
    break;

  case UiEventKind::ClearConsole:
    if (ForThisClient(_event.player))
      flushConsoleMessages();
    break;

  case UiEventKind::TutorialEnd:
    if (ForThisClient(_event.player))
      initConsoleMessages();
    break;

  case UiEventKind::PlayVideo:
    if (ForThisClient(_event.player))
      PlayVideo(_event);
    break;

  default:
    /* Decode refused any kind this build has no name for, so nothing reaches
       here that is not listed above. */
    break;
  }
}
