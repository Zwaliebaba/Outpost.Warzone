/*
 * NetCert.h
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

#ifndef _NETCERT_H_
#define _NETCERT_H_

/***************************************************************************/

#define	NETCERT_HASH_SIZE	20		/* SHA-1, which is the shape MsQuic asks for */

/***************************************************************************/

/* Makes sure a host certificate exists and writes its thumbprint out. Cheap
 * to call twice: the second call finds the first one's certificate.
 */
BOOL netcert_Acquire(BYTE abThumbprint[NETCERT_HASH_SIZE]);

/* Removes it again, private key included. Called when the host stops hosting,
 * so that a machine that has finished playing is not left carrying a key.
 */
void netcert_Release(void);

/***************************************************************************/

#endif	// _NETCERT_H_

/***************************************************************************/
