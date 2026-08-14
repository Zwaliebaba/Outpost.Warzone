/*
 * Trig.h
 *
 * Interface to trig lookup tables
 *
 */
#ifndef _trig_h
#define _trig_h

/* The number of units around a full circle */
#define TRIG_DEGREES	360

/* conversion macros */
#define DEG_TO_RAD(x)	(x*PI/180.0)
#define RAD_TO_DEG(x)	(x*180.0/PI)

/* Initialise the Trig tables */
extern BOOL trigInitialise(void);

/* Shutdown the trig tables */
extern void trigShutDown(void);

/* Lookup trig functions */
extern float trigSin(SDWORD angle);
extern float trigCos(SDWORD angle);
extern float trigInvSin(float val);
extern float trigInvCos(float val);

/* Supposedly fast lookup sqrt - unfortunately it's probably slower than the FPU sqrt :-( */
extern float trigIntSqrt(UDWORD val);

#endif
