/*
 * HostCertificate.h
 *
 * The certificate a host needs before QUIC will talk to it.
 *
 * TLS is not optional in QUIC, so a machine that hosts a game has to present a
 * certificate. There is no certificate authority in the picture and there is
 * no name to certify -- players type an address -- so this generates a
 * self-signed one and clients are told not to validate it.
 *
 * Be plain about what that is worth. It encrypts the traffic, which DirectPlay
 * did not, so a passive listener on the network learns nothing. It does not
 * authenticate the host, so somebody able to intercept the connection could
 * pose as one. That is a real gap and the fix is a named server with a real
 * certificate, which is what the relay server in the plan becomes.
 */

#pragma once

namespace Neuron
{

/***************************************************************************/

class HostCertificate
{
public:
  static constexpr UDWORD HashSize = 20;	/* SHA-1, the shape MsQuic asks for */

  /* Makes sure a host certificate exists and writes its thumbprint out. Cheap
   * to call twice: the second call finds the first one's certificate.
   */
  static BOOL Acquire(BYTE _thumbprint[HashSize]);

  /* Removes it again, private key included. Called when the host stops
   * hosting, so that a machine that has finished playing is not left carrying
   * a key.
   */
  static void Release();
};

/***************************************************************************/

} // namespace Neuron

/***************************************************************************/
