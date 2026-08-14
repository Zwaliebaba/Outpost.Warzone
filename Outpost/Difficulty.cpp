#include "pch.h"
// ------------------------------------------------------------------------------------
/* Simple short file - only because there was nowhere else for it logically to go */
/* Handes the difficulty level effects on gameplay */

/*
	Changed to allow seperate modifiers for enemy and player damage.
*/

// ------------------------------------------------------------------------------------
#include "Frame.h"
#include "Difficulty.h"
// ------------------------------------------------------------------------------------
DIFFICULTY_LEVEL presDifLevel = DL_NORMAL;
float fDifPlayerModifier;
float fDifEnemyModifier;

void getModifiers(float* Player, float* Enemy)
{
  *Player = fDifPlayerModifier;
  *Enemy = fDifEnemyModifier;
}

void setModifiers(float Player, float Enemy)
{
  fDifPlayerModifier = Player;
  fDifEnemyModifier = Enemy;
}

// ------------------------------------------------------------------------------------
/* Sets the game difficulty level */
void setDifficultyLevel(DIFFICULTY_LEVEL lev)
{
  switch (lev)
  {
  case DL_EASY:
    fDifPlayerModifier = (120.0f / 100.0f);
    fDifEnemyModifier = (100.0f / 100.0f);
    break;
  case DL_NORMAL:
    fDifPlayerModifier = (100.0f / 100.0f);
    fDifEnemyModifier = (100.0f / 100.0f);
    break;
  case DL_HARD:
    fDifPlayerModifier = (80.0f / 100.0f);
    fDifEnemyModifier = (100.0f / 100.0f);
    break;
  case DL_KILLER:
    fDifPlayerModifier = (999.0f / 100.0f); // 10 times
    fDifEnemyModifier = (1.0f / 100.0f); // almost nothing
    break;
  case DL_TOUGH:
    fDifPlayerModifier = (100.0f / 100.0f);
    fDifEnemyModifier = (50.0f / 100.0f); // they do less damage!
    break;
  default: Neuron::Fatal("Invalid difficulty level selected - forcing NORMAL");
    break;
  }

  presDifLevel = lev;
}

// ------------------------------------------------------------------------------------
/* Returns the difficulty level */
DIFFICULTY_LEVEL getDifficultyLevel(void) { return (presDifLevel); }

// ------------------------------------------------------------------------------------
SDWORD modifyForDifficultyLevel(SDWORD basicVal, BOOL IsPlayer)
{
  SDWORD retVal;

  // You can't garantee that we don't want damage modifiers in normal difficulty.	
  //	/* Unmodified! */
  //	if(getDifficultyLevel() == DL_NORMAL)

  if (IsPlayer)
    retVal = std::lround(basicVal*fDifPlayerModifier);
  else
    retVal = std::lround(basicVal*fDifEnemyModifier);

  return retVal;
}

// ------------------------------------------------------------------------------------
