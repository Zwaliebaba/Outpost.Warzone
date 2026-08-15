#include "pch.h"

/*
 * netcrypt.c
 *
 * 1999 pumpkin Studios
 * secure netplay services.
 *
 * Feistel cipher with XOR & addition as nonlinear mixing funcs. 
 * Based on Tiny Encryption Algorithm (TEA) by David Wheeler & Roger Needham
 * of the cambridge computer laboratory.
 * delta = delta = (sqrt(5) - 1).2^31
 *
 * This cipher is secure enough for time critical data, but probably
 * not secure enough for long term storage. eg, find for encrypting network packets
 * but not good enough for storing files securely on harddisk. Try encryptstrength=32;
 */

#include "Frame.h"
#include "NetPlay.h"

#define			ENCRYPTSTRENGTH 16		// 32=ample, 16=sufficient, 8=maybe ok, good dispersion after 6.
#define			NIBBLELENGTH	8		// bytes done per encrypt step.

// ////////////////////////////////////////////////////////////////////////
// Prototypes
UDWORD NEThashFile(STRING* pFileName);
UDWORD NEThashBuffer(UBYTE* pData, UDWORD size);

BOOL NETsetKey(UDWORD c1, UDWORD c2, UDWORD c3, UDWORD c4);

BOOL NETmangleData(long* input, long* result, UDWORD dataSize);
BOOL NETunmangleData(long* input, long* result, UDWORD dataSize);

// ////////////////////////////////////////////////////////////////////////
// make a hash value from an exe name.
UDWORD NEThashFile(STRING* pFileName)
{
  UDWORD hashval, c, *val;
  FILE* pFileHandle;
  STRING fileName[255];

  UBYTE inBuff[2048]; // must be multiple of 4 bytes.

  strcpy(fileName, pFileName);

  hashval = 0;

  Neuron::DebugTrace("NEThashFile: Hashing File\n");

  // open the file.
  pFileHandle = fopen(fileName, "rb"); // check file exists
  if (pFileHandle == nullptr)
  {
    Neuron::DebugTrace("NEThashFile: Failed\n");
    return 0; // failed
  }

  // multibyte/buff version
  while (fread(&inBuff, sizeof(inBuff), 1, pFileHandle) == 1) // get number of droids in force	
  {
    for (c = 0; c < 2048; c += 4)
    {
      val = (UDWORD*)&inBuff[c];
      hashval = hashval ^ *val;
    }
  }

  Neuron::DebugTrace("NEThashFile: Hash Complete :   *****  {}  ***** is todays magic number.\n",hashval);

  return hashval;
}

// ////////////////////////////////////////////////////////////////////////
// return a hash from a data buffer.

UDWORD NEThashBuffer(UBYTE* pData, UDWORD size)
{
  UDWORD hashval, *val;
  UDWORD pt;

  hashval = 0;
  pt = 0;

  while (pt < size)
  {
    val = (UDWORD*)(pData + pt);
    hashval = hashval ^ *val;
    pt += 4;
  }

  return hashval;
}

// ////////////////////////////////////////////////////////////////////////
// return a ubyte hash from a UDWORD value.
UBYTE NEThashVal(UDWORD value) { return (value ^ 13416564) % 246; }

// ////////////////////////////////////////////////////////////////////////
// set the key for the encrypter.
BOOL NETsetKey(UDWORD c1, UDWORD c2, UDWORD c3, UDWORD c4)
{
  if (c1)
    NetPlay.cryptKey[0] = c1;
  if (c2)
    NetPlay.cryptKey[1] = c2;
  if (c3)
    NetPlay.cryptKey[2] = c3;
  if (c4)
    NetPlay.cryptKey[3] = c4;
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// encrypt a byte sequence of nibblelength 
static BOOL mangle(long* v, long* w)
{
  unsigned long y = v[0], z = v[1], sum = 0, delta = 0x9E3779B9, n = ENCRYPTSTRENGTH;
  while (n-- > 0)
  {
    sum += delta;
    y += (z << 4) + NetPlay.cryptKey[0] ^ z + sum ^ (z >> 5) + NetPlay.cryptKey[1];
    z += (y << 4) + NetPlay.cryptKey[2] ^ y + sum ^ (y >> 5) + NetPlay.cryptKey[3];
  }
  w[0] = y;
  w[1] = z;
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// decrypt a byte sequence of nibblelength 
static BOOL unmangle(long* v, long* w)
{
  unsigned long y = v[0], z = v[1], sum, delta = 0x9E3779B9, n = ENCRYPTSTRENGTH;

  sum = delta * n; /* (generally sum =delta*n )*/
  while (n-- > 0)
  {
    z -= (y << 4) + NetPlay.cryptKey[2] ^ y + sum ^ (y >> 5) + NetPlay.cryptKey[3];
    y -= (z << 4) + NetPlay.cryptKey[0] ^ z + sum ^ (z >> 5) + NetPlay.cryptKey[1];
    sum -= delta;
  }
  w[0] = y;
  w[1] = z;
  return TRUE;
}

/* NETmanglePacket and NETunmanglePacket were here.
 *
 * They encrypted a NETMSG in place, padding it to a multiple of the cipher's
 * block length and marking the type with ENCRYPTFLAG so the far end knew to
 * reverse it. QUIC encrypts every byte of every packet with TLS 1.3, so there
 * was nothing left for them to add and they went with the transport swap. The
 * padding they needed is what NETMSG's paddedBytes field was for; the field is
 * still on the wire and now always zero.
 *
 * The rest of this file is not networking and never was, which is why it is
 * still here: NETmangleData obfuscates the player-stats file on disk,
 * NEThashFile and NEThashVal catch a mismatched executable when somebody
 * joins, and NEThashBuffer backs Data.cpp's cheat hashing.
 */

// ////////////////////////////////////////////////////////////////////////
// encrypt any datastream.
BOOL NETmangleData(long* input, long* result, UDWORD dataSize)
{
  long offset;

  offset = 0;

  if (dataSize % 8 != 0) //if message not multiple of 8 bytes,
  {
    Neuron::Fatal("NETmangleData: msg not a multiple of 8 bytes");
    return FALSE;
  }

  //  /4's are long form. since nibblelength is in char form
  while (offset != static_cast<long>(dataSize / 4))
  {
    mangle((input + offset), (result + offset));
    offset += NIBBLELENGTH / 4;
  }
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// decrypt any datastream.
BOOL NETunmangleData(long* input, long* result, UDWORD dataSize)
{
  long offset;

  memset(result, 0, dataSize);
  offset = 0;

  if (dataSize % 8 != 0) //if message not multiple of 8 bytes,
  {
    Neuron::Fatal("NETunmangleData: msg not a multiple of 8 bytes");
    return FALSE;
  }

  //  /4's are long form. since nibblelength is in char form
  while (offset != static_cast<long>(dataSize / 4))
  {
    unmangle((input + offset), (result + offset));
    offset += NIBBLELENGTH / 4;
  }
  return TRUE;
}
