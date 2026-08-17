#include "pch.h"

#include "Frame.h"
#include "GTime.h"
#include "AnimObj.h"

#include <iterator>
#include <list>

/***************************************************************************/
/* global variables */

/* The live animation objects.
 *
 * This was HashTabl.cpp - a hand-rolled table that was also an untyped
 * allocator (new UBYTE[size], never constructed) and carried a single
 * table-wide iteration cursor. Three responsibilities in one object.
 *
 * A list rather than a hash map, even though the module is a lookup by
 * (parent, anim id): animObj_Update fires each animation's done callback
 * mid-iteration, and those callbacks add and remove animations -
 * droidBurntCallback adds the fall-over animation from inside the loop.
 * An unordered_map rehashes on insert, which invalidates the loop's
 * iterator; a list invalidates only the iterator to the element erased, so
 * the update loop stays valid whatever a callback does. Callers also keep
 * the ANIM_OBJECT* (STRUCTURE::psCurAnim, DROID::psCurAnim), which the
 * list's stable element addresses satisfy for free.
 *
 * The lookup that costs is animObj_Find, called from the renderer for
 * on-screen objects that have a visible animation - a linear scan of at
 * most a few hundred entries, which is cheaper than the bookkeeping a
 * separate index would need to stay correct across those callbacks.
 */
static std::list<ANIM_OBJECT> g_animObjects;

static ANIMOBJDIEDTESTFUNC g_pDiedFunc;

/***************************************************************************/
/*
 * Anim functions
 */
/***************************************************************************/

BOOL animObj_Init(ANIMOBJDIEDTESTFUNC pDiedFunc)
{
  g_animObjects.clear();

  /* set global died test function */
  g_pDiedFunc = pDiedFunc;

  return TRUE;
}

/***************************************************************************/

BOOL animObj_Shutdown(void)
{
  g_animObjects.clear();

  return TRUE;
}

/***************************************************************************/

void animObj_SetDoneFunc(ANIM_OBJECT* psObj, ANIMOBJDONEFUNC pDoneFunc) { psObj->pDoneFunc = pDoneFunc; }

/***************************************************************************/

void animObj_Update(void)
{
  ANIM_OBJECT* psObj;
  SDWORD dwTime;
  BOOL bRemove;

  /* The successor is taken before the body runs, because the done callback
   * below can append to the list. Holding it across the erase is also a
   * fix: the table this replaced advanced its one shared cursor inside
   * RemoveElement even though GetNext had already stepped past the element
   * just returned, so removing an animation skipped the next one in the
   * same bucket until the following frame. */
  for (auto it = g_animObjects.begin(); it != g_animObjects.end();)
  {
    const auto next = std::next(it);

    psObj = &*it;
    bRemove = FALSE;

    /* test whether parent object has died */
    if (g_pDiedFunc != nullptr)
      bRemove = (g_pDiedFunc)(psObj->psParent);

    /* remove any expired (non-looping) animations */
    if ((bRemove == FALSE) && (psObj->uwCycles != 0))
    {
      dwTime = gameTime - psObj->udwStartTime - psObj->udwStartDelay;

      if (dwTime > (psObj->psAnim->uwAnimTime * psObj->uwCycles))
      {
        /* fire callback if set */
        if (psObj->pDoneFunc != nullptr)
          (psObj->pDoneFunc)(psObj);

        bRemove = TRUE;
      }
    }

    /* remove object if flagged */
    if (bRemove == TRUE)
      g_animObjects.erase(it);

    it = next;
  }
}

/***************************************************************************/
/*
 * anim_Add
 *
 * uwCycles=0 for infinite looping
 */
/***************************************************************************/

ANIM_OBJECT* animObj_Add(void* pParentObj, int iAnimID, UDWORD udwStartDelay, UWORD uwCycles)
{
  ANIM_OBJECT* psObj;
  BASEANIM* psAnim = anim_GetAnim(static_cast<UWORD>(iAnimID));
  UWORD i, uwObj;

  DEBUG_ASSERT_TEXT(psAnim != NULL, "anim_AddAnimObject: anim id {} not found\n", iAnimID);

  /* Value initialised, where the table this replaced handed back raw
   * uninitialised bytes. */
  psObj = &g_animObjects.emplace_back();

  /* init object */
  psObj->uwID = static_cast<UWORD>(iAnimID);
  psObj->psAnim = (ANIM3D*)psAnim;
  psObj->udwStartTime = gameTime;
  psObj->udwStartDelay = udwStartDelay;
  psObj->uwCycles = uwCycles;
  psObj->bVisible = TRUE;
  psObj->psParent = pParentObj;
  psObj->pDoneFunc = nullptr;

  /* allocate component objects */
  if (psAnim->animType == ANIM_3D_TRANS)
    uwObj = psAnim->uwObj;
  else
    uwObj = psAnim->uwStates;

  if (uwObj > ANIM_MAX_COMPONENTS)
    Neuron::Fatal("animObj_Add: number of components too small\n");

  /* set parent pointer and shape pointer */
  for (i = 0; i < uwObj; i++)
  {
    psObj->apComponents[i].psParent = pParentObj;
    psObj->apComponents[i].psShape = psObj->psAnim->apFrame[i];
  }

  return psObj;
}

/***************************************************************************/
/*
 * animObj_GetFrame3D
 *
 * returns NULL if animation not started yet
 */
/***************************************************************************/

UWORD animObj_GetFrame3D(ANIM_OBJECT* psObj, UWORD uwObj, VECTOR3D* psVecPos, DirectX::XMFLOAT3* psVecRot, VECTOR3D* psVecScale)
{
  ANIM3D* psAnim;

  /* get local anim pointer */
  psAnim = psObj->psAnim;

  return anim_GetFrame3D(psAnim, uwObj, gameTime, psObj->udwStartTime, psObj->udwStartDelay, psVecPos, psVecRot, psVecScale);
}

/***************************************************************************/

ANIM_OBJECT* animObj_Find(void* pParentObj, int iAnimID)
{
  for (auto& sObj : g_animObjects)
  {
    if ((sObj.psParent == pParentObj) && (sObj.uwID == iAnimID))
      return &sObj;
  }

  return nullptr;
}

/***************************************************************************/

BOOL animObj_Remove(ANIM_OBJECT** ppsObj, int iAnimID)
{
  /* Matched on the element as well as the key, as the table this replaced
   * did: the same animation may be attached to a parent more than once, and
   * only this object is meant to go. */
  BOOL bRemOK = FALSE;

  for (auto it = g_animObjects.begin(); it != g_animObjects.end(); ++it)
  {
    if ((&*it == *ppsObj) && (it->psParent == (*ppsObj)->psParent) && (it->uwID == iAnimID))
    {
      g_animObjects.erase(it);
      bRemOK = TRUE;
      break;
    }
  }

  //init the animation
  *ppsObj = nullptr;

  return bRemOK;
}

/***************************************************************************/
