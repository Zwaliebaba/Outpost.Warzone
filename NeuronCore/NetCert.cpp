#include "pch.h"
/***************************************************************************/
/*
 * NetCert.cpp
 *
 * Generating the host's self-signed certificate, and taking it away again.
 *
 * This is CryptoAPI rather than QUIC, which is why it is not in NetQuic.cpp.
 * The shape follows MsQuic's own selfsign_capi.c, because that is the version
 * known to work with Schannel: a persisted CNG key, a self-signed certificate
 * naming it, added to the current user's personal store, and identified to
 * MsQuic by SHA-1 thumbprint. Schannel resolves the private key by looking the
 * thumbprint up in that store, so the store step is not decoration.
 *
 * The certificate is created when a game is hosted and deleted when it stops,
 * key container included. If the game dies in between, the next host finds the
 * old certificate by subject and reuses it rather than adding a second.
 */
/***************************************************************************/

#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>

#include "Frame.h"
#include "NetCert.h"

/***************************************************************************/

/* The subject, which is also how the certificate is found again. Nothing
 * validates it -- clients are told not to -- so it exists to be recognisable
 * to a player who goes looking in certmgr.
 */
#define	NETCERT_SUBJECT			"CN=Outpost.Warzone Host"

/* The key container name. Fixed rather than a fresh GUID each time so that a
 * game killed mid-session leaves at most one behind.
 */
#define	NETCERT_KEY_NAME		L"Outpost.Warzone Host Key"

#define	NETCERT_KEY_BITS		2048
#define	NETCERT_VALID_DAYS		30

/***************************************************************************/

/* Opens the store the certificate lives in. CURRENT_USER rather than
 * LOCAL_MACHINE: writing to the machine store needs administrator rights, and
 * a game should not.
 */
static HCERTSTORE netcert_OpenStore(void)
{
  HCERTSTORE hStore;

  hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM_A, 0, 0,
                         CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG, "MY");
  if (hStore == nullptr)
    Neuron::DebugTrace("netcert: cannot open the personal certificate store, {}\n",
                       static_cast<UDWORD>(GetLastError()));

  return hStore;
}

/***************************************************************************/

/* Finds ours among however many certificates the user has. Matching on the
 * subject string is enough because the subject is one we invented.
 */
static PCCERT_CONTEXT netcert_Find(HCERTSTORE hStore)
{
  return CertFindCertificateInStore(hStore, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A,
                                    NETCERT_SUBJECT, nullptr);
}

/***************************************************************************/

/* Creates the private key. Persisted rather than ephemeral: Schannel reaches
 * the key through the certificate's provider info, which names a container,
 * and an ephemeral key has no container to name.
 */
static BOOL netcert_MakeKey(void)
{
  NCRYPT_PROV_HANDLE hProv = 0;
  NCRYPT_KEY_HANDLE hKey = 0;
  SECURITY_STATUS ss;
  DWORD dwBits = NETCERT_KEY_BITS;
  BOOL bOk = FALSE;

  ss = NCryptOpenStorageProvider(&hProv, MS_KEY_STORAGE_PROVIDER, 0);
  if (ss != ERROR_SUCCESS)
  {
    Neuron::DebugTrace("netcert: NCryptOpenStorageProvider failed, 0x{:08x}\n", static_cast<UDWORD>(ss));
    return FALSE;
  }

  /* OVERWRITE because a previous run may have left a container of this name
   * with no certificate pointing at it, and creating it again would otherwise
   * fail forever.
   */
  ss = NCryptCreatePersistedKey(hProv, &hKey, NCRYPT_RSA_ALGORITHM, NETCERT_KEY_NAME, 0,
                                NCRYPT_OVERWRITE_KEY_FLAG);
  if (ss != ERROR_SUCCESS)
  {
    Neuron::DebugTrace("netcert: NCryptCreatePersistedKey failed, 0x{:08x}\n", static_cast<UDWORD>(ss));
    NCryptFreeObject(hProv);
    return FALSE;
  }

  ss = NCryptSetProperty(hKey, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&dwBits),
                         sizeof(dwBits), 0);
  if (ss != ERROR_SUCCESS)
    Neuron::DebugTrace("netcert: NCryptSetProperty(length) failed, 0x{:08x}\n", static_cast<UDWORD>(ss));
  else
  {
    ss = NCryptFinalizeKey(hKey, 0);
    if (ss != ERROR_SUCCESS)
      Neuron::DebugTrace("netcert: NCryptFinalizeKey failed, 0x{:08x}\n", static_cast<UDWORD>(ss));
    else
      bOk = TRUE;
  }

  NCryptFreeObject(hKey);
  NCryptFreeObject(hProv);

  return bOk;
}

/***************************************************************************/

/* Deletes the key container the certificate names. Reached through the
 * certificate rather than by opening the container directly, so that a
 * container belonging to something else cannot be destroyed by a name clash.
 */
static void netcert_DeleteKey(PCCERT_CONTEXT pCert)
{
  NCRYPT_KEY_HANDLE hKey = 0;
  DWORD dwKeySpec = 0;
  BOOL bCallerFree = FALSE;

  if (!CryptAcquireCertificatePrivateKey(pCert, CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG, nullptr, &hKey,
                                         &dwKeySpec, &bCallerFree))
    return;

  /* NCryptDeleteKey frees the handle whether it succeeds or not, so the
   * caller-free flag must not lead to a second free afterwards.
   */
  if (bCallerFree)
    NCryptDeleteKey(hKey, 0);
  else
    NCryptFreeObject(hKey);
}

/***************************************************************************/

/* The extension list. Schannel will pick a certificate for a server only if it
 * is good for server authentication, and a self-signed certificate with no
 * extended key usage at all is a coin toss across Windows versions -- so say
 * so explicitly rather than relying on the default.
 */
static BOOL netcert_BuildExtensions(CERT_EXTENSIONS* psExtensions, CERT_EXTENSION* psExtension,
                                    CRYPT_DATA_BLOB* psEncoded)
{
  CERT_ENHKEY_USAGE sUsage;
  LPSTR apszUsage[1];

  apszUsage[0] = const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH);
  sUsage.cUsageIdentifier = 1;
  sUsage.rgpszUsageIdentifier = apszUsage;

  psEncoded->pbData = nullptr;
  psEncoded->cbData = 0;

  if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &sUsage,
                           CRYPT_ENCODE_ALLOC_FLAG, nullptr, &psEncoded->pbData, &psEncoded->cbData))
  {
    Neuron::DebugTrace("netcert: cannot encode the key usage extension, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    return FALSE;
  }

  psExtension->pszObjId = const_cast<LPSTR>(szOID_ENHANCED_KEY_USAGE);
  psExtension->fCritical = FALSE;
  psExtension->Value = *psEncoded;

  psExtensions->cExtension = 1;
  psExtensions->rgExtension = psExtension;

  return TRUE;
}

/***************************************************************************/

/* Builds the certificate and puts it in the store. The caller has already
 * looked for an existing one.
 */
static PCCERT_CONTEXT netcert_Create(HCERTSTORE hStore)
{
  CERT_NAME_BLOB sSubject = {0, nullptr};
  CRYPT_KEY_PROV_INFO sProvInfo = {};
  CRYPT_ALGORITHM_IDENTIFIER sAlgorithm = {};
  CERT_EXTENSIONS sExtensions = {};
  CERT_EXTENSION sExtension = {};
  CRYPT_DATA_BLOB sEncodedUsage = {};
  SYSTEMTIME sStart, sEnd;
  FILETIME ftStart, ftEnd;
  ULARGE_INTEGER uliEnd;
  PCCERT_CONTEXT pCert = nullptr;
  PCCERT_CONTEXT pStored = nullptr;

  if (!netcert_MakeKey())
    return nullptr;

  /* CertStrToNameA is called twice: once for the size, once for the bytes. */
  if (!CertStrToNameA(X509_ASN_ENCODING, NETCERT_SUBJECT, CERT_X500_NAME_STR, nullptr, nullptr,
                      &sSubject.cbData, nullptr))
  {
    Neuron::DebugTrace("netcert: cannot measure the subject name, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    return nullptr;
  }

  sSubject.pbData = static_cast<BYTE*>(LocalAlloc(LPTR, sSubject.cbData));
  if (sSubject.pbData == nullptr)
    return nullptr;

  if (!CertStrToNameA(X509_ASN_ENCODING, NETCERT_SUBJECT, CERT_X500_NAME_STR, nullptr,
                      sSubject.pbData, &sSubject.cbData, nullptr))
  {
    Neuron::DebugTrace("netcert: cannot encode the subject name, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    LocalFree(sSubject.pbData);
    return nullptr;
  }

  /* dwProvType zero says the container belongs to a CNG provider rather than
   * a legacy CSP, which is what NCryptCreatePersistedKey made. Getting this
   * wrong makes Schannel fail to find the key at handshake time rather than
   * here, so it is worth being sure of.
   */
  sProvInfo.pwszContainerName = const_cast<LPWSTR>(NETCERT_KEY_NAME);
  sProvInfo.pwszProvName = const_cast<LPWSTR>(MS_KEY_STORAGE_PROVIDER);
  sProvInfo.dwProvType = 0;
  sProvInfo.dwFlags = 0;
  sProvInfo.dwKeySpec = 0;

  /* SHA-256 rather than the SHA-1 default: TLS 1.3 will not sign with SHA-1
   * and a certificate it cannot use is no certificate at all.
   */
  sAlgorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);

  GetSystemTime(&sStart);
  SystemTimeToFileTime(&sStart, &ftStart);
  uliEnd.LowPart = ftStart.dwLowDateTime;
  uliEnd.HighPart = ftStart.dwHighDateTime;
  uliEnd.QuadPart += static_cast<ULONGLONG>(NETCERT_VALID_DAYS) * 24 * 60 * 60 * 10000000ULL;
  ftEnd.dwLowDateTime = uliEnd.LowPart;
  ftEnd.dwHighDateTime = uliEnd.HighPart;
  FileTimeToSystemTime(&ftEnd, &sEnd);

  if (netcert_BuildExtensions(&sExtensions, &sExtension, &sEncodedUsage))
  {
    pCert = CertCreateSelfSignCertificate(0, &sSubject, 0, &sProvInfo, &sAlgorithm, &sStart, &sEnd,
                                          &sExtensions);
    if (pCert == nullptr)
      Neuron::DebugTrace("netcert: CertCreateSelfSignCertificate failed, {}\n",
                         static_cast<UDWORD>(GetLastError()));
  }

  if (sEncodedUsage.pbData != nullptr)
    LocalFree(sEncodedUsage.pbData);
  LocalFree(sSubject.pbData);

  if (pCert == nullptr)
    return nullptr;

  /* The context that comes back from the store is the one to keep: it is the
   * one carrying the store's own properties, and it is what the thumbprint is
   * then read from.
   */
  if (!CertAddCertificateContextToStore(hStore, pCert, CERT_STORE_ADD_REPLACE_EXISTING, &pStored))
  {
    Neuron::DebugTrace("netcert: cannot add the certificate to the store, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    CertFreeCertificateContext(pCert);
    return nullptr;
  }

  CertFreeCertificateContext(pCert);

  return pStored;
}

/***************************************************************************/

BOOL netcert_Acquire(BYTE abThumbprint[NETCERT_HASH_SIZE])
{
  HCERTSTORE hStore;
  PCCERT_CONTEXT pCert;
  DWORD cbHash = NETCERT_HASH_SIZE;
  BOOL bOk;

  hStore = netcert_OpenStore();
  if (hStore == nullptr)
    return FALSE;

  pCert = netcert_Find(hStore);
  if (pCert == nullptr)
    pCert = netcert_Create(hStore);

  if (pCert == nullptr)
  {
    CertCloseStore(hStore, 0);
    return FALSE;
  }

  /* CERT_HASH_PROP_ID is the SHA-1 of the whole certificate, which is what
   * certmgr calls the thumbprint and what QUIC_CERTIFICATE_HASH wants.
   */
  bOk = CertGetCertificateContextProperty(pCert, CERT_HASH_PROP_ID, abThumbprint, &cbHash);
  if (!bOk || cbHash != NETCERT_HASH_SIZE)
  {
    Neuron::DebugTrace("netcert: cannot read the certificate thumbprint, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    bOk = FALSE;
  }

  CertFreeCertificateContext(pCert);
  CertCloseStore(hStore, 0);

  return bOk;
}

/***************************************************************************/

void netcert_Release(void)
{
  HCERTSTORE hStore;
  PCCERT_CONTEXT pCert;

  hStore = netcert_OpenStore();
  if (hStore == nullptr)
    return;

  pCert = netcert_Find(hStore);
  if (pCert != nullptr)
  {
    netcert_DeleteKey(pCert);

    /* CertDeleteCertificateFromStore frees the context whatever it returns, so
     * there is nothing left to release afterwards.
     */
    if (!CertDeleteCertificateFromStore(pCert))
      Neuron::DebugTrace("netcert: cannot delete the certificate, {}\n",
                         static_cast<UDWORD>(GetLastError()));
  }

  CertCloseStore(hStore, 0);
}

/***************************************************************************/
