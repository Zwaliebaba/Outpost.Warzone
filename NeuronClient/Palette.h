#ifndef _palette_h
#define _palette_h
#include "RenderTypes.h"

//*************************************************************************

/* The sixteen primaries, packed A8R8G8B8.
 *
 * This header is what is left of the palette: the COL_* names were indices
 * into a 256 entry global palette - the nearest entry to each of the values
 * below, resolved when palette.bin loaded - until the palette removal
 * (MigrationPlan.md) converted the art to true colour DDS and the colours
 * to the packed values the old code asked the palette for.
 *
 * COL_BLACK stays a whisker off pure black: the nearest-colour search never
 * returned the transparent entry 0, and colour-keyed draws still treat pure
 * black as transparent. */
#define COL_TRANS			0
#define COL_BLACK			0xff010101
#define COL_BLUE			0xff000080
#define COL_GREEN			0xff008000
#define COL_CYAN			0xff008080
#define COL_RED				0xff800000
#define COL_MAGENTA  		0xff800080
#define COL_BROWN			0xff804000
#define COL_GREY			0xff808080
#define COL_DARKGREY		0xff202020
#define COL_LIGHTBLUE		0xff0000ff
#define COL_LIGHTGREEN		0xff00ff00
#define COL_LIGHTCYAN		0xff00ffff
#define COL_LIGHTRED		0xffff0000
#define COL_LIGHTMAGENTA	0xffff00ff
#define COL_YELLOW			0xffffff00
#define COL_WHITE			0xffffffff

#endif
