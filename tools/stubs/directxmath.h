/* Cross-check shim for DirectXMath.h. NOT part of the build.
 *
 * crosscheck.py copies this into the shadow tree's stub directory because
 * mingw-w64's directxmath.h is a 326-line storage-types header -- XMFLOAT3,
 * XMINT3 and friends -- with no XMVECTOR, no XMMATRIX and none of the
 * functions, so the Phase 10 render maths could not be syntax-checked
 * against it. MSVC never sees this file: it uses the real header from the
 * Windows SDK.
 *
 * That makes this weaker than the rest of the cross-check. It verifies our
 * use of DirectXMath against a transcription of the API rather than against
 * the SDK, so it catches arity, spelling and type errors in our code but
 * cannot catch a mistake in the transcription itself. The declarations below
 * were transcribed from the DirectXMath reference on Microsoft Learn and
 * cross-checked against the public microsoft/DirectXMath repository; the
 * calling-convention aliases collapse to by-value/by-reference forms that
 * are call-compatible at the source level. The implementations are plain
 * scalar C++ so the shapes type-check; nothing links or runs them.
 *
 * Only the surface the tree uses (or Phase 10's later stages will use) is
 * transcribed. Extend it as call sites need more, keeping each addition
 * faithful to the SDK signature.
 *
 * The Windows CI build remains the authority, as it does for the linker.
 */

#ifndef _DIRECTXMATH_CROSSCHECK_STUB_H_
#define _DIRECTXMATH_CROSSCHECK_STUB_H_

#include <cmath>
#include <cstdint>

#define XM_CALLCONV
#define XM_CONST const
#define XM_CONSTEXPR constexpr

namespace DirectX
{

constexpr float XM_PI = 3.141592654f;
constexpr float XM_2PI = 6.283185307f;
constexpr float XM_1DIVPI = 0.318309886f;
constexpr float XM_1DIV2PI = 0.159154943f;
constexpr float XM_PIDIV2 = 1.570796327f;
constexpr float XM_PIDIV4 = 0.785398163f;

constexpr float XMConvertToRadians(float fDegrees) { return fDegrees * (XM_PI / 180.0f); }
constexpr float XMConvertToDegrees(float fRadians) { return fRadians * (180.0f / XM_PI); }

/* The real XMVECTOR is __m128; only the accessor functions below may touch
 * the lanes, which is also the contract the real header imposes. */
struct XMVECTOR
{
  float v[4];
};

using FXMVECTOR = const XMVECTOR;
using GXMVECTOR = const XMVECTOR;
using HXMVECTOR = const XMVECTOR&;
using CXMVECTOR = const XMVECTOR&;

struct XMFLOAT2
{
  float x, y;
  XMFLOAT2() = default;
  constexpr XMFLOAT2(float _x, float _y) : x(_x), y(_y) {}
};

struct XMFLOAT3
{
  float x, y, z;
  XMFLOAT3() = default;
  constexpr XMFLOAT3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

struct XMFLOAT4
{
  float x, y, z, w;
  XMFLOAT4() = default;
  constexpr XMFLOAT4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct XMINT3
{
  std::int32_t x, y, z;
  XMINT3() = default;
  constexpr XMINT3(std::int32_t _x, std::int32_t _y, std::int32_t _z) : x(_x), y(_y), z(_z) {}
};

struct XMFLOAT4X4
{
  float m[4][4];
  XMFLOAT4X4() = default;
};

struct XMMATRIX;
using FXMMATRIX = const XMMATRIX&;
using CXMMATRIX = const XMMATRIX&;

XMMATRIX XM_CALLCONV XMMatrixMultiply(FXMMATRIX M1, CXMMATRIX M2);

struct XMMATRIX
{
  XMVECTOR r[4];
  XMMATRIX() = default;
  XMMATRIX XM_CALLCONV operator*(FXMMATRIX M) const;
};

// Vector construction and accessors

inline XMVECTOR XM_CALLCONV XMVectorSet(float x, float y, float z, float w) { return XMVECTOR{{x, y, z, w}}; }
inline XMVECTOR XM_CALLCONV XMVectorZero() { return XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f); }
inline XMVECTOR XM_CALLCONV XMVectorReplicate(float Value) { return XMVectorSet(Value, Value, Value, Value); }
inline float XM_CALLCONV XMVectorGetX(FXMVECTOR V) { return V.v[0]; }
inline float XM_CALLCONV XMVectorGetY(FXMVECTOR V) { return V.v[1]; }
inline float XM_CALLCONV XMVectorGetZ(FXMVECTOR V) { return V.v[2]; }
inline float XM_CALLCONV XMVectorGetW(FXMVECTOR V) { return V.v[3]; }

// Vector arithmetic

inline XMVECTOR XM_CALLCONV XMVectorAdd(FXMVECTOR V1, FXMVECTOR V2)
{
  return XMVectorSet(V1.v[0] + V2.v[0], V1.v[1] + V2.v[1], V1.v[2] + V2.v[2], V1.v[3] + V2.v[3]);
}

inline XMVECTOR XM_CALLCONV XMVectorSubtract(FXMVECTOR V1, FXMVECTOR V2)
{
  return XMVectorSet(V1.v[0] - V2.v[0], V1.v[1] - V2.v[1], V1.v[2] - V2.v[2], V1.v[3] - V2.v[3]);
}

inline XMVECTOR XM_CALLCONV XMVectorMultiply(FXMVECTOR V1, FXMVECTOR V2)
{
  return XMVectorSet(V1.v[0] * V2.v[0], V1.v[1] * V2.v[1], V1.v[2] * V2.v[2], V1.v[3] * V2.v[3]);
}

inline XMVECTOR XM_CALLCONV XMVectorDivide(FXMVECTOR V1, FXMVECTOR V2)
{
  return XMVectorSet(V1.v[0] / V2.v[0], V1.v[1] / V2.v[1], V1.v[2] / V2.v[2], V1.v[3] / V2.v[3]);
}

inline XMVECTOR XM_CALLCONV XMVectorScale(FXMVECTOR V, float ScaleFactor)
{
  return XMVectorSet(V.v[0] * ScaleFactor, V.v[1] * ScaleFactor, V.v[2] * ScaleFactor, V.v[3] * ScaleFactor);
}

inline XMVECTOR XM_CALLCONV XMVectorNegate(FXMVECTOR V) { return XMVectorSet(-V.v[0], -V.v[1], -V.v[2], -V.v[3]); }

inline XMVECTOR operator+(FXMVECTOR V1, FXMVECTOR V2) { return XMVectorAdd(V1, V2); }
inline XMVECTOR operator-(FXMVECTOR V1, FXMVECTOR V2) { return XMVectorSubtract(V1, V2); }
inline XMVECTOR operator*(FXMVECTOR V1, FXMVECTOR V2) { return XMVectorMultiply(V1, V2); }
inline XMVECTOR operator/(FXMVECTOR V1, FXMVECTOR V2) { return XMVectorDivide(V1, V2); }
inline XMVECTOR operator*(FXMVECTOR V, float S) { return XMVectorScale(V, S); }
inline XMVECTOR operator*(float S, FXMVECTOR V) { return XMVectorScale(V, S); }
inline XMVECTOR operator-(FXMVECTOR V) { return XMVectorNegate(V); }

// 3D vector functions

inline XMVECTOR XM_CALLCONV XMVector3Dot(FXMVECTOR V1, FXMVECTOR V2)
{
  return XMVectorReplicate(V1.v[0] * V2.v[0] + V1.v[1] * V2.v[1] + V1.v[2] * V2.v[2]);
}

inline XMVECTOR XM_CALLCONV XMVector3Cross(FXMVECTOR V1, FXMVECTOR V2)
{
  return XMVectorSet(V1.v[1] * V2.v[2] - V1.v[2] * V2.v[1],
                     V1.v[2] * V2.v[0] - V1.v[0] * V2.v[2],
                     V1.v[0] * V2.v[1] - V1.v[1] * V2.v[0], 0.0f);
}

inline XMVECTOR XM_CALLCONV XMVector3LengthSq(FXMVECTOR V) { return XMVector3Dot(V, V); }

inline XMVECTOR XM_CALLCONV XMVector3Length(FXMVECTOR V) { return XMVectorReplicate(std::sqrt(XMVectorGetX(XMVector3LengthSq(V)))); }

inline XMVECTOR XM_CALLCONV XMVector3Normalize(FXMVECTOR V)
{
  const float length = XMVectorGetX(XMVector3Length(V));
  return XMVectorSet(V.v[0] / length, V.v[1] / length, V.v[2] / length, 0.0f);
}

// Load and store

inline XMVECTOR XM_CALLCONV XMLoadFloat3(const XMFLOAT3* pSource) { return XMVectorSet(pSource->x, pSource->y, pSource->z, 0.0f); }

inline XMVECTOR XM_CALLCONV XMLoadSInt3(const XMINT3* pSource)
{
  return XMVectorSet(static_cast<float>(pSource->x), static_cast<float>(pSource->y), static_cast<float>(pSource->z), 0.0f);
}

inline void XM_CALLCONV XMStoreFloat3(XMFLOAT3* pDestination, FXMVECTOR V)
{
  pDestination->x = V.v[0];
  pDestination->y = V.v[1];
  pDestination->z = V.v[2];
}

inline XMMATRIX XM_CALLCONV XMLoadFloat4x4(const XMFLOAT4X4* pSource)
{
  XMMATRIX M;
  for (int i = 0; i < 4; i++)
    M.r[i] = XMVectorSet(pSource->m[i][0], pSource->m[i][1], pSource->m[i][2], pSource->m[i][3]);
  return M;
}

inline void XM_CALLCONV XMStoreFloat4x4(XMFLOAT4X4* pDestination, FXMMATRIX M)
{
  for (int i = 0; i < 4; i++)
  {
    pDestination->m[i][0] = M.r[i].v[0];
    pDestination->m[i][1] = M.r[i].v[1];
    pDestination->m[i][2] = M.r[i].v[2];
    pDestination->m[i][3] = M.r[i].v[3];
  }
}

// Scalar functions

inline float XMScalarSin(float Value) { return std::sin(Value); }
inline float XMScalarCos(float Value) { return std::cos(Value); }
inline float XMScalarASin(float Value) { return std::asin(Value); }
inline float XMScalarACos(float Value) { return std::acos(Value); }

inline void XMScalarSinCos(float* pSin, float* pCos, float Value)
{
  *pSin = std::sin(Value);
  *pCos = std::cos(Value);
}

inline float XMScalarModAngle(float Value)
{
  float wrapped = Value + XM_PI;
  wrapped = wrapped - (XM_2PI * std::floor(wrapped * XM_1DIV2PI));
  return wrapped - XM_PI;
}

// Matrix functions -- row-major, row-vector convention as the SDK header

inline XMMATRIX XM_CALLCONV XMMatrixIdentity()
{
  XMMATRIX M;
  M.r[0] = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
  M.r[1] = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
  M.r[2] = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
  M.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
  return M;
}

inline XMMATRIX XM_CALLCONV XMMatrixTranslation(float OffsetX, float OffsetY, float OffsetZ)
{
  XMMATRIX M = XMMatrixIdentity();
  M.r[3] = XMVectorSet(OffsetX, OffsetY, OffsetZ, 1.0f);
  return M;
}

inline XMMATRIX XM_CALLCONV XMMatrixScaling(float ScaleX, float ScaleY, float ScaleZ)
{
  XMMATRIX M = XMMatrixIdentity();
  M.r[0] = XMVectorSet(ScaleX, 0.0f, 0.0f, 0.0f);
  M.r[1] = XMVectorSet(0.0f, ScaleY, 0.0f, 0.0f);
  M.r[2] = XMVectorSet(0.0f, 0.0f, ScaleZ, 0.0f);
  return M;
}

inline XMMATRIX XM_CALLCONV XMMatrixRotationX(float Angle)
{
  const float s = std::sin(Angle);
  const float c = std::cos(Angle);
  XMMATRIX M = XMMatrixIdentity();
  M.r[1] = XMVectorSet(0.0f, c, s, 0.0f);
  M.r[2] = XMVectorSet(0.0f, -s, c, 0.0f);
  return M;
}

inline XMMATRIX XM_CALLCONV XMMatrixRotationY(float Angle)
{
  const float s = std::sin(Angle);
  const float c = std::cos(Angle);
  XMMATRIX M = XMMatrixIdentity();
  M.r[0] = XMVectorSet(c, 0.0f, -s, 0.0f);
  M.r[2] = XMVectorSet(s, 0.0f, c, 0.0f);
  return M;
}

inline XMMATRIX XM_CALLCONV XMMatrixRotationZ(float Angle)
{
  const float s = std::sin(Angle);
  const float c = std::cos(Angle);
  XMMATRIX M = XMMatrixIdentity();
  M.r[0] = XMVectorSet(c, s, 0.0f, 0.0f);
  M.r[1] = XMVectorSet(-s, c, 0.0f, 0.0f);
  return M;
}

inline XMMATRIX XM_CALLCONV XMMatrixMultiply(FXMMATRIX M1, CXMMATRIX M2)
{
  XMMATRIX R;
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      R.r[i].v[j] = M1.r[i].v[0] * M2.r[0].v[j] + M1.r[i].v[1] * M2.r[1].v[j] + M1.r[i].v[2] * M2.r[2].v[j] +
        M1.r[i].v[3] * M2.r[3].v[j];
    }
  }
  return R;
}

inline XMMATRIX XM_CALLCONV XMMATRIX::operator*(FXMMATRIX M) const { return XMMatrixMultiply(*this, M); }

inline XMVECTOR XM_CALLCONV XMVector3Transform(FXMVECTOR V, FXMMATRIX M)
{
  XMVECTOR R;
  for (int j = 0; j < 4; j++)
    R.v[j] = V.v[0] * M.r[0].v[j] + V.v[1] * M.r[1].v[j] + V.v[2] * M.r[2].v[j] + M.r[3].v[j];
  return R;
}

inline XMVECTOR XM_CALLCONV XMVector3TransformCoord(FXMVECTOR V, FXMMATRIX M)
{
  const XMVECTOR R = XMVector3Transform(V, M);
  return XMVectorSet(R.v[0] / R.v[3], R.v[1] / R.v[3], R.v[2] / R.v[3], 1.0f);
}

} // namespace DirectX

#endif /* _DIRECTXMATH_CROSSCHECK_STUB_H_ */
