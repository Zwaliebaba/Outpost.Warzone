#include "pch.h"

/* Allow frame header files to be singly included */
#define FRAME_LIB_INCLUDE

#include <assert.h>
#include "Types.h"
#include "Trig.h"

#define PI 3.141592654

/* Number of steps between -1 and 1 for the inverse tables */
#define TRIG_ACCURACY	4096
#define TRIG_ACCMASK	0x0fff

/* Number of entries in sqrt table */
#define SQRT_ACCURACY	4096
#define SQRT_ACCBITS	12

/* The trig functions */
#define SINFUNC		(float)sin
#define COSFUNC		(float)cos
#define ASINFUNC	(float)asin
#define ACOSFUNC	(float)acos

static float* aSin;
static float* aCos;
static float* aInvCos;
/* Square root table - not needed on PSX cos there is a fast hardware sqrt */
static float* aSqrt;
static float* aInvSin;

/* Initialise the Trig tables */
BOOL trigInitialise(void)
{
  float val, inc;
  UDWORD count;

  // Allocate the tables
  aSin = new (std::nothrow) float[TRIG_DEGREES];
  if (!aSin)
    return FALSE;
  aCos = new (std::nothrow) float[TRIG_DEGREES];
  if (!aCos)
    return FALSE;
  aInvSin = new (std::nothrow) float[TRIG_ACCURACY];
  if (!aInvSin)
    return FALSE;

  aInvCos = new (std::nothrow) float[TRIG_ACCURACY];
  if (!aInvCos)
    return FALSE;

  aSqrt = new (std::nothrow) float[SQRT_ACCURACY];
  if (!aSqrt)
    return FALSE;

  // Initialise the tables
  // inc = 2*PI/TRIG_DEGREES
  inc = (2.0f * (static_cast<float>(PI) / static_cast<float>(TRIG_DEGREES)));
  val = 0.0f;
  for (count = 0; count < TRIG_DEGREES; count++)
  {
    aSin[count] = SINFUNC(val);
    aCos[count] = COSFUNC(val);
    val += inc;
  }
  inc = (static_cast<float>(2) / static_cast<float>(TRIG_ACCURACY-1));
  val = -1.0f;
  for (count = 0; count < TRIG_ACCURACY; count++)
  {
    aInvSin[count] = (ASINFUNC(val) * (static_cast<float>(TRIG_DEGREES/2) / static_cast<float>(PI)));
    aInvCos[count] = (ACOSFUNC(val) * (static_cast<float>(TRIG_DEGREES/2) / static_cast<float>(PI)));
    val += inc;
  }

  // Build the sqrt table
  for (count = 0; count < SQRT_ACCURACY; count++)
  {
    val = static_cast<float>(count) / static_cast<float>((SQRT_ACCURACY / 2));
    aSqrt[count] = static_cast<float>(sqrt(val));
  }

  return TRUE;
}

/* Shutdown the trig tables */
void trigShutDown(void)
{
  delete[] aSin;
  aSin = nullptr;
  delete[] aCos;
  aCos = nullptr;
  delete[] aInvSin;
  aInvSin = nullptr;
  delete[] aInvCos;
  aInvCos = nullptr;
  delete[] aSqrt;
  aSqrt = nullptr;
}

/* Access the trig tables */
float trigSin(SDWORD angle)
{
  if (angle < 0)
  {
    angle = (-angle) % TRIG_DEGREES;
    angle = TRIG_DEGREES - angle;
  }
  else
    angle = angle % TRIG_DEGREES;
  return aSin[angle % TRIG_DEGREES];
}

float trigCos(SDWORD angle)
{
  if (angle < 0)
  {
    angle = (-angle) % TRIG_DEGREES;
    angle = TRIG_DEGREES - angle;
  }
  else
    angle = angle % TRIG_DEGREES;
  return aCos[angle % TRIG_DEGREES];
}

float trigInvSin(float val)
{
  SDWORD index;

  index = std::lrintf(val * static_cast<float>((TRIG_ACCURACY-1)/2)) + (TRIG_ACCURACY - 1) / 2;

  return aInvSin[index & TRIG_ACCMASK];
}

float trigInvCos(float val)
{
  SDWORD index;

  index = std::lrintf(val * static_cast<float>((TRIG_ACCURACY-1)/2)) + (TRIG_ACCURACY - 1) / 2;

  return aInvCos[index & TRIG_ACCMASK];
}

/* Fast lookup sqrt */
float trigIntSqrt(UDWORD val)
{
  UDWORD exp, mask;

  if (val == 0)
    return 0.0f;

  // find the exponent of the number
  mask = 0x80000000; // set the msb in the mask
  for (exp = 32; exp != 0; exp--)
  {
    if (val & mask)
      break;
    mask >>= 1;
  }

  // make all exponents even
  // odd exponents result in a mantissa of [1..2) rather than [0..1)
  if (exp & 1)
    exp -= 1;

  // need to shift the top bit to SQRT_BITS - left or right?
  if (exp >= SQRT_ACCBITS)
    val >>= exp - SQRT_ACCBITS + 1;
  else
    val <<= SQRT_ACCBITS - 1 - exp;

  // now generate the fractional part for the lookup table
  DEBUG_ASSERT_TEXT(val < SQRT_ACCURACY, "trigIntSqrt: aargh - table index out of range");
  return aSqrt[val] * static_cast<float>((UDWORD)1 << ((UDWORD)exp / 2));
}

#define DIVCNT (32)

#define ARCGAP (4096/DIVCNT)			// X2-X1

#define ARCMASK (ARCGAP-1)

/* */
