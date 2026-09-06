/*
 * UiEvents.h
 *
 * The client's side of ServerMessage::UiEvent: showing what the server's
 * script VM asked for.
 */

#pragma once

#include "Protocol.h"

/// Carries out one UiEvent on this client.
///
/// What a mission script used to do by calling addMessage, centreView,
/// playSound, the console or the sequence player directly, it now asks for
/// from the server (Docs/ServerAuthority.md stage D), and this is where the
/// asking becomes the call. An event addressed to a player this client is not
/// showing is ignored, exactly as the instincts ignored a player who was not
/// selectedPlayer.
void ApplyUiEvent(const Neuron::ServerUiEvent& _event);
