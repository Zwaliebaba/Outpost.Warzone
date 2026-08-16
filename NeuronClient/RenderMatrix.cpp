#include "pch.h"
/***************************************************************************/
/*
 * pieMatrix.c
 *
 * matrix functions for pumpkin image library.
 *
 */
/***************************************************************************/

#include <stdio.h>

#include "RenderTypes.h"
#include "RenderMatrix.h"
#include "RendMode.h"

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

/*

	Playstation and PC stuff   ... just the matrix stack & surface normal code is all thats needed on the PSX

*/

#define MATRIX_MAX	8

static SDMATRIX aMatrixStack[MATRIX_MAX];
SDMATRIX* psMatrix = &aMatrixStack[0];

void pie_VectorNormalise(iVector* v)

{
  int32 size;
  iVector av;

  av.x = pie_ABS(v->x);
  av.y = pie_ABS(v->y);
  av.z = pie_ABS(v->z);
  if (av.x >= av.y)
  {
    if (av.x > av.z)
      size = av.x + (av.z >> 2) + (av.y >> 2);
    else
      size = av.z + (av.x >> 2) + (av.y >> 2);
  }
  else
  {
    if (av.y > av.z)
      size = av.y + (av.z >> 2) + (av.x >> 2);
    else
      size = av.z + (av.y >> 2) + (av.x >> 2);
  }

  if (size > 0)
  {
    v->x = (v->x << FP12_SHIFT) / size;
    v->y = (v->y << FP12_SHIFT) / size;
    v->z = (v->z << FP12_SHIFT) / size;
  }
}

//*************************************************************************
//*** calculate surface normal
//*
//* params	p1,p2,p3	= points for forming 2 vector for cross product
//*			v			= normal vector returned << FP12_SHIFT
//*
//* eg		if a polygon (with n points in clockwise order) normal
//*			is required, p1 = point 0, p2 = point 1, p3 = point n-1
//*
//******

void pie_SurfaceNormal(iVector* p1, iVector* p2, iVector* p3, iVector* v)

{
  iVector a, b;

  a.x = p3->x - p1->x;
  a.y = p3->y - p1->y;
  a.z = p3->z - p1->z;
  pie_VectorNormalise(&a);

  b.x = p2->x - p1->x;
  b.y = p2->y - p1->y;
  b.z = p2->z - p1->z;
  pie_VectorNormalise(&b);

  v->x = ((a.y * b.z) - (a.z * b.y)) >> FP12_SHIFT;
  v->y = ((a.z * b.x) - (a.x * b.z)) >> FP12_SHIFT;
  v->z = ((a.x * b.y) - (a.y * b.x)) >> FP12_SHIFT;
  pie_VectorNormalise(v);
}

#define SC_TABLESIZE	4096

//*************************************************************************

static SDMATRIX _MATRIX_ID = {FP12_MULTIPLIER, 0, 0, 0,FP12_MULTIPLIER, 0, 0, 0,FP12_MULTIPLIER, 0L, 0L, 0L};
static SDWORD _MATRIX_INDEX;

//*************************************************************************

int aSinTable[SC_TABLESIZE + (SC_TABLESIZE / 4)];

//*************************************************************************
//*** create new matrix from current transformation matrix and make current
//*
//******

void pie_MatBegin(void)

{
  _MATRIX_INDEX++;
  if (_MATRIX_INDEX > 3)
    DEBUG_ASSERT_TEXT(_MATRIX_INDEX < MATRIX_MAX, "pie_MatBegin past top of the stack");
  psMatrix++;
  aMatrixStack[_MATRIX_INDEX] = aMatrixStack[_MATRIX_INDEX - 1];
}

//*************************************************************************
//*** make current transformation matrix previous one on stack
//*
//******

void pie_MatEnd(void)

{
  _MATRIX_INDEX--;
  DEBUG_ASSERT_TEXT(_MATRIX_INDEX >= 0, "pie_MatEnd of the bottom of the stack");
  psMatrix--;
}

//*************************************************************************
//*** matrix rotate y (yaw) current transformation matrix
//*
//******

void pie_MatRotY(int y)

{
  int32 t;
  int32 cra, sra;

  if (y != 0)
  {
    cra = COS(y);
    sra = SIN(y);

    t = ((cra * psMatrix->a) - (sra * psMatrix->g)) >> FP12_SHIFT;
    psMatrix->g = ((sra * psMatrix->a) + (cra * psMatrix->g)) >> FP12_SHIFT;
    psMatrix->a = t;

    t = ((cra * psMatrix->b) - (sra * psMatrix->h)) >> FP12_SHIFT;
    psMatrix->h = ((sra * psMatrix->b) + (cra * psMatrix->h)) >> FP12_SHIFT;
    psMatrix->b = t;

    t = ((cra * psMatrix->c) - (sra * psMatrix->i)) >> FP12_SHIFT;
    psMatrix->i = ((sra * psMatrix->c) + (cra * psMatrix->i)) >> FP12_SHIFT;
    psMatrix->c = t;
  }
}

//*************************************************************************
//*** matrix rotate z (roll) current transformation matrix
//*
//******

void pie_MatRotZ(int z)

{
  int32 t;
  int32 cra, sra;

  if (z != 0)
  {
    cra = COS(z);
    sra = SIN(z);

    t = ((cra * psMatrix->a) + (sra * psMatrix->d)) >> FP12_SHIFT;
    psMatrix->d = ((cra * psMatrix->d) - (sra * psMatrix->a)) >> FP12_SHIFT;
    psMatrix->a = t;

    t = ((cra * psMatrix->b) + (sra * psMatrix->e)) >> FP12_SHIFT;
    psMatrix->e = ((cra * psMatrix->e) - (sra * psMatrix->b)) >> FP12_SHIFT;
    psMatrix->b = t;

    t = ((cra * psMatrix->c) + (sra * psMatrix->f)) >> FP12_SHIFT;
    psMatrix->f = ((cra * psMatrix->f) - (sra * psMatrix->c)) >> FP12_SHIFT;
    psMatrix->c = t;
  }
}

//*************************************************************************
//*** matrix rotate x (pitch) current transformation matrix
//*
//******

void pie_MatRotX(int x)

{
  register int cra, sra;
  register int t;

  if (x != 0)
  {
    cra = COS(x);
    sra = SIN(x);

    t = ((cra * psMatrix->d) + (sra * psMatrix->g)) >> FP12_SHIFT;
    psMatrix->g = ((cra * psMatrix->g) - (sra * psMatrix->d)) >> FP12_SHIFT;
    psMatrix->d = t;

    t = ((cra * psMatrix->e) + (sra * psMatrix->h)) >> FP12_SHIFT;
    psMatrix->h = ((cra * psMatrix->h) - (sra * psMatrix->e)) >> FP12_SHIFT;
    psMatrix->e = t;

    t = ((cra * psMatrix->f) + (sra * psMatrix->i)) >> FP12_SHIFT;
    psMatrix->i = ((cra * psMatrix->i) - (sra * psMatrix->f)) >> FP12_SHIFT;
    psMatrix->f = t;
  }
}

//*************************************************************************
//*** 3D vector perspective projection
//*
//* params	v1 = 3D vector to project
//* 			v2 = pointer to 2D resultant vector
//*
//* on exit	v2 = projected vector
//*
//* returns	rotated and translated z component of v1
//*
//******

int32 pie_RotProj(iVector* v3d, iPoint* v2d)

{
  int32 zfx, zfy;
  int32 zz, x, y, z;

  x = v3d->x * psMatrix->a + v3d->y * psMatrix->d + v3d->z * psMatrix->g + psMatrix->j;
  y = v3d->x * psMatrix->b + v3d->y * psMatrix->e + v3d->z * psMatrix->h + psMatrix->k;
  z = v3d->x * psMatrix->c + v3d->y * psMatrix->f + v3d->z * psMatrix->i + psMatrix->l;

  zz = z >> STRETCHED_Z_SHIFT;

  zfx = z >> psRendSurface->xpshift;
  zfy = z >> psRendSurface->ypshift;

  if ((zfx <= 0) || (zfy <= 0))
  {
    v2d->x = LONG_WAY; //just along way off screen
    v2d->y = LONG_WAY;
  }
  else if (zz < MIN_STRETCHED_Z)
  {
    v2d->x = LONG_WAY; //just along way off screen
    v2d->y = LONG_WAY;
  }
  else
  {
    v2d->x = psRendSurface->xcentre + (x / zfx);
    v2d->y = psRendSurface->ycentre - (y / zfy);
  }

  return zz;
}

//*************************************************************************

void pie_SetGeometricOffset(int x, int y)

{
  psRendSurface->xcentre = x;
  psRendSurface->ycentre = y;
}

//*************************************************************************

BOOL pie_PieClockwise(PIEVERTEX* s) { return (((s[1].sy - s[0].sy) * (s[2].sx - s[1].sx)) <= ((s[1].sx - s[0].sx) * (s[2].sy - s[1].sy))); }

//*************************************************************************
//*** setup transformation matrices/quaternions and trig tables
//*
//******

void pie_MatInit(void)
{
  unsigned i, scsize;
  double conv, v;

  // sin/cos table

  scsize = SC_TABLESIZE + (SC_TABLESIZE / 4);
  conv = static_cast<float>((PI / (0.5 * SC_TABLESIZE)));

  for (i = 0; i < scsize; i++)
  {
    v = sin(i * conv) * FP12_MULTIPLIER;

    if (v >= 0.0)
      aSinTable[i] = static_cast<int32>(v + 0.5);
    else
      aSinTable[i] = static_cast<int32>(v - 0.5);
  }

  // reset the stack and make the first matrix identity

  psMatrix = &aMatrixStack[0];
  *psMatrix = _MATRIX_ID;

}
