#include "pch.h"
/***************************************************************************/
/*
 * HostCertificate.cpp
 *
 * Generating the host's self-signed certificate, and taking it away again.
 *
 * This is CryptoAPI rather than QUIC, which is why it is not in Transport.cpp.
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
#include "HostCertificate.h"

/***************************************************************************/

namespace Neuron
{
namespace
{

/* The subject, which is also how the certificate is found again. Nothing
 * validates it -- clients are told not to -- so it exists to be recognisable
 * to a player who goes looking in certmgr.
 */
constexpr const char* CertificateSubject = "CN=Outpost.Warzone Host";

/* The key container name. Fixed rather than a fresh GUID each time so that a
 * game killed mid-session leaves at most one behind.
 */
constexpr const wchar_t* CertificateKeyName = L"Outpost.Warzone Host Key";

constexpr DWORD CertificateKeyBits = 2048;
constexpr UDWORD CertificateValidDays = 30;

/***************************************************************************/

/* Opens the store the certificate lives in. CURRENT_USER rather than
 * LOCAL_MACHINE: writing to the machine store needs administrator rights, and
 * a game should not.
 */
HCERTSTORE OpenPersonalStore(void)
{
  HCERTSTORE store;

  store = CertOpenStore(CERT_STORE_PROV_SYSTEM_A, 0, 0,
                         CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG, "MY");
  if (store == nullptr)
    Neuron::DebugTrace("netcert: cannot open the personal certificate store, {}\n",
                       static_cast<UDWORD>(GetLastError()));

  return store;
}

/***************************************************************************/

/* Finds ours among however many certificates the user has. Matching on the
 * subject string is enough because the subject is one we invented.
 */
PCCERT_CONTEXT FindOurCertificate(HCERTSTORE store)
{
  return CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A,
                                    CertificateSubject, nullptr);
}

/***************************************************************************/

/* Creates the private key. Persisted rather than ephemeral: Schannel reaches
 * the key through the certificate's provider info, which names a container,
 * and an ephemeral key has no container to name.
 */
BOOL CreatePrivateKey(void)
{
  NCRYPT_PROV_HANDLE provider = 0;
  NCRYPT_KEY_HANDLE key = 0;
  SECURITY_STATUS ss;
  DWORD bits = CertificateKeyBits;
  BOOL ok = FALSE;

  ss = NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0);
  if (ss != ERROR_SUCCESS)
  {
    Neuron::DebugTrace("netcert: NCryptOpenStorageProvider failed, 0x{:08x}\n", static_cast<UDWORD>(ss));
    return FALSE;
  }

  /* OVERWRITE because a previous run may have left a container of this name
   * with no certificate pointing at it, and creating it again would otherwise
   * fail forever.
   */
  ss = NCryptCreatePersistedKey(provider, &key, NCRYPT_RSA_ALGORITHM, CertificateKeyName, 0,
                                NCRYPT_OVERWRITE_KEY_FLAG);
  if (ss != ERROR_SUCCESS)
  {
    Neuron::DebugTrace("netcert: NCryptCreatePersistedKey failed, 0x{:08x}\n", static_cast<UDWORD>(ss));
    NCryptFreeObject(provider);
    return FALSE;
  }

  ss = NCryptSetProperty(key, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&bits),
                         sizeof(bits), 0);
  if (ss != ERROR_SUCCESS)
    Neuron::DebugTrace("netcert: NCryptSetProperty(length) failed, 0x{:08x}\n", static_cast<UDWORD>(ss));
  else
  {
    ss = NCryptFinalizeKey(key, 0);
    if (ss != ERROR_SUCCESS)
      Neuron::DebugTrace("netcert: NCryptFinalizeKey failed, 0x{:08x}\n", static_cast<UDWORD>(ss));
    else
      ok = TRUE;
  }

  NCryptFreeObject(key);
  NCryptFreeObject(provider);

  return ok;
}

/***************************************************************************/

/* Deletes the key container the certificate names. Reached through the
 * certificate rather than by opening the container directly, so that a
 * container belonging to something else cannot be destroyed by a name clash.
 */
void DeletePrivateKey(PCCERT_CONTEXT cert)
{
  NCRYPT_KEY_HANDLE key = 0;
  DWORD keySpec = 0;
  BOOL callerMustFree = FALSE;

  if (!CryptAcquireCertificatePrivateKey(cert, CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG, nullptr, &key,
                                         &keySpec, &callerMustFree))
    return;

  /* NCryptDeleteKey frees the handle whether it succeeds or not, so the
   * caller-free flag must not lead to a second free afterwards.
   */
  if (callerMustFree)
    NCryptDeleteKey(key, 0);
  else
    NCryptFreeObject(key);
}

/***************************************************************************/

/* The extension list. Schannel will pick a certificate for a server only if it
 * is good for server authentication, and a self-signed certificate with no
 * extended key usage at all is a coin toss across Windows versions -- so say
 * so explicitly rather than relying on the default.
 */
BOOL BuildExtensions(CERT_EXTENSIONS* _extensions, CERT_EXTENSION* _extension, CRYPT_DATA_BLOB* _encoded)
{
  CERT_ENHKEY_USAGE usage;
  LPSTR usageOids[1];

  usageOids[0] = const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH);
  usage.cUsageIdentifier = 1;
  usage.rgpszUsageIdentifier = usageOids;

  _encoded->pbData = nullptr;
  _encoded->cbData = 0;

  if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &usage,
                           CRYPT_ENCODE_ALLOC_FLAG, nullptr, &_encoded->pbData, &_encoded->cbData))
  {
    Neuron::DebugTrace("netcert: cannot encode the key usage extension, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    return FALSE;
  }

  _extension->pszObjId = const_cast<LPSTR>(szOID_ENHANCED_KEY_USAGE);
  _extension->fCritical = FALSE;
  _extension->Value = *_encoded;

  _extensions->cExtension = 1;
  _extensions->rgExtension = _extension;

  return TRUE;
}

/***************************************************************************/

/* Builds the certificate and puts it in the store. The caller has already
 * looked for an existing one.
 */
PCCERT_CONTEXT CreateCertificate(HCERTSTORE store)
{
  CERT_NAME_BLOB subject = {0, nullptr};
  CRYPT_KEY_PROV_INFO providerInfo = {};
  CRYPT_ALGORITHM_IDENTIFIER algorithm = {};
  CERT_EXTENSIONS extensions = {};
  CERT_EXTENSION extension = {};
  CRYPT_DATA_BLOB encodedUsage = {};
  SYSTEMTIME startTime, endTime;
  FILETIME startFileTime, endFileTime;
  ULARGE_INTEGER endTicks;
  PCCERT_CONTEXT cert = nullptr;
  PCCERT_CONTEXT stored = nullptr;

  if (!CreatePrivateKey())
    return nullptr;

  /* CertStrToNameA is called twice: once for the size, once for the bytes. */
  if (!CertStrToNameA(X509_ASN_ENCODING, CertificateSubject, CERT_X500_NAME_STR, nullptr, nullptr,
                      &subject.cbData, nullptr))
  {
    Neuron::DebugTrace("netcert: cannot measure the subject name, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    return nullptr;
  }

  subject.pbData = static_cast<BYTE*>(LocalAlloc(LPTR, subject.cbData));
  if (subject.pbData == nullptr)
    return nullptr;

  if (!CertStrToNameA(X509_ASN_ENCODING, CertificateSubject, CERT_X500_NAME_STR, nullptr,
                      subject.pbData, &subject.cbData, nullptr))
  {
    Neuron::DebugTrace("netcert: cannot encode the subject name, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    LocalFree(subject.pbData);
    return nullptr;
  }

  /* dwProvType zero says the container belongs to a CNG provider rather than
   * a legacy CSP, which is what NCryptCreatePersistedKey made. Getting this
   * wrong makes Schannel fail to find the key at handshake time rather than
   * here, so it is worth being sure of.
   */
  providerInfo.pwszContainerName = const_cast<LPWSTR>(CertificateKeyName);
  providerInfo.pwszProvName = const_cast<LPWSTR>(MS_KEY_STORAGE_PROVIDER);
  providerInfo.dwProvType = 0;
  providerInfo.dwFlags = 0;
  providerInfo.dwKeySpec = 0;

  /* SHA-256 rather than the SHA-1 default: TLS 1.3 will not sign with SHA-1
   * and a certificate it cannot use is no certificate at all.
   */
  algorithm.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);

  GetSystemTime(&startTime);
  SystemTimeToFileTime(&startTime, &startFileTime);
  endTicks.LowPart = startFileTime.dwLowDateTime;
  endTicks.HighPart = startFileTime.dwHighDateTime;
  endTicks.QuadPart += static_cast<ULONGLONG>(CertificateValidDays) * 24 * 60 * 60 * 10000000ULL;
  endFileTime.dwLowDateTime = endTicks.LowPart;
  endFileTime.dwHighDateTime = endTicks.HighPart;
  FileTimeToSystemTime(&endFileTime, &endTime);

  if (BuildExtensions(&extensions, &extension, &encodedUsage))
  {
    cert = CertCreateSelfSignCertificate(0, &subject, 0, &providerInfo, &algorithm, &startTime, &endTime,
                                          &extensions);
    if (cert == nullptr)
      Neuron::DebugTrace("netcert: CertCreateSelfSignCertificate failed, {}\n",
                         static_cast<UDWORD>(GetLastError()));
  }

  if (encodedUsage.pbData != nullptr)
    LocalFree(encodedUsage.pbData);
  LocalFree(subject.pbData);

  if (cert == nullptr)
    return nullptr;

  /* The context that comes back from the store is the one to keep: it is the
   * one carrying the store's own properties, and it is what the thumbprint is
   * then read from.
   */
  if (!CertAddCertificateContextToStore(store, cert, CERT_STORE_ADD_REPLACE_EXISTING, &stored))
  {
    Neuron::DebugTrace("netcert: cannot add the certificate to the store, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    CertFreeCertificateContext(cert);
    return nullptr;
  }

  CertFreeCertificateContext(cert);

  return stored;
}

/***************************************************************************/

} // anonymous namespace

/***************************************************************************/

BOOL HostCertificate::Acquire(BYTE _thumbprint[HashSize])
{
  HCERTSTORE store;
  PCCERT_CONTEXT cert;
  DWORD hashBytes = HostCertificate::HashSize;
  BOOL ok;

  store = OpenPersonalStore();
  if (store == nullptr)
    return FALSE;

  cert = FindOurCertificate(store);
  if (cert == nullptr)
    cert = CreateCertificate(store);

  if (cert == nullptr)
  {
    CertCloseStore(store, 0);
    return FALSE;
  }

  /* CERT_HASH_PROP_ID is the SHA-1 of the whole certificate, which is what
   * certmgr calls the thumbprint and what QUIC_CERTIFICATE_HASH wants.
   */
  ok = CertGetCertificateContextProperty(cert, CERT_HASH_PROP_ID, _thumbprint, &hashBytes);
  if (!ok || hashBytes != HostCertificate::HashSize)
  {
    Neuron::DebugTrace("netcert: cannot read the certificate _thumbprint, {}\n",
                       static_cast<UDWORD>(GetLastError()));
    ok = FALSE;
  }

  CertFreeCertificateContext(cert);
  CertCloseStore(store, 0);

  return ok;
}

/***************************************************************************/

void HostCertificate::Release(void)
{
  HCERTSTORE store;
  PCCERT_CONTEXT cert;

  store = OpenPersonalStore();
  if (store == nullptr)
    return;

  cert = FindOurCertificate(store);
  if (cert != nullptr)
  {
    DeletePrivateKey(cert);

    /* CertDeleteCertificateFromStore frees the context whatever it returns, so
     * there is nothing left to release afterwards.
     */
    if (!CertDeleteCertificateFromStore(cert))
      Neuron::DebugTrace("netcert: cannot delete the certificate, {}\n",
                         static_cast<UDWORD>(GetLastError()));
  }

  CertCloseStore(store, 0);
}

/***************************************************************************/

} // namespace Neuron

/***************************************************************************/
