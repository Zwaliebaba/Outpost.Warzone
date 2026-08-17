#include "RenderTypes.h"
/***************************************************************************/

BOOL arrowInit(void);
void arrowShutDown(void);
BOOL arrowAdd(SDWORD iBaseX, SDWORD iBaseY, SDWORD iBaseZ, SDWORD iHeadX, SDWORD iHeadY, SDWORD iHeadZ, UDWORD iColour);
void arrowDrawAll(void);
extern void draw3dLine(iVector* src, iVector* dest, UDWORD col);

/***************************************************************************/
