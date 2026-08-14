/*
 * Weapons.h
 *
 * Definitions for the weapons
 *
 */
#ifndef _weapons_h
#define _weapons_h

using WEAPON = struct _weapon
{
  UDWORD nStat; // The stats for the weapon type
  UDWORD hitPoints;
  UDWORD ammo;
  UDWORD lastFired; // When the weapon last fired
  UDWORD recoilValue;
};

#endif
