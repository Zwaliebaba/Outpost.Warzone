#include "pch.h"
#include <assert.h>
/***************************************************************************/
/*
 * imdload.c
 *
 * updated to load version 4 files
 * 
 * changes at version 4;
 *		pcx name as string
 *		pcx filepath
 *		cut down vertex list
 *
 */
/***************************************************************************/

#include <stdio.h>
#include <directxmath.h>

#include "Frame.h"
#include "FrameResource.h"
#include "RenderMatrix.h" //for surface normals
#include "Model.h"
#include "IMD.h"	// for imd structures
#include "RendMode.h"
#include "IvisPatch.h"
#include "Tex.h"		// texture page loading

#include "BSPFunc.h"	// for imd functions

#ifndef FINALBUILD
#define ALLOW_TEXTPIES	// With this define enabled we are allowed to load and process ascii pie files ... with it removed we are only allowed binary ones !
#endif

#ifdef ALLOW_TEXTPIES

// Static variables
static uint32 _IMD_FLAGS;
static char _IMD_NAME[MAX_FILE_PATH];
static int32 _IMD_VER;
static VERTEXID vertexTable[IMD_MAX_POINTS];
static char imagePath[MAX_FILE_PATH] = {""};

// local prototypes

static iIMDShape* _imd_load_level(UBYTE** FileData, UBYTE* FileDataEnd, int nlevels, int texpage);
static char* _imd_get_path(char* filename, char* path);
BOOL CheckColourKey(iIMDShape* psShape);

// Special re-mix of sscanf that moves the string pointer along
int __cdecl sscanf1(char** stringPos, const char* format, ...)
/*
 * 'S'tring 'SCAN', 'F'ormatted
 */
{
  va_list arglist;
  char skipFormat[256];
  char* string;
  int retval;
  int consumed;
  UDWORD i, j;

  string = *stringPos;

  va_start(arglist, format);
  retval = vsscanf(string, format, arglist);
  va_end(arglist);

  /* Replay the same parse with every conversion suppressed, so it takes
   * no arguments and %n can report how far along the string it reached.
   */
  j = 0;
  for (i = 0; format[i] != '\0' AND j < sizeof(skipFormat) - 4; i++)
  {
    skipFormat[j++] = format[i];
    if (format[i] == '%' AND format[i + 1] != '\0')
    {
      if (format[i + 1] == '%')
        skipFormat[j++] = format[++i];
      else
        skipFormat[j++] = '*';
    }
  }
  skipFormat[j++] = '%';
  skipFormat[j++] = 'n';
  skipFormat[j] = '\0';

  consumed = 0;
  sscanf(string, skipFormat, &consumed);

  *stringPos = string + consumed;

  return (retval);
}

char GetCh1(UBYTE** data)
{
  char Byte;
  char* pointer;
  pointer = (char*)*data;
  Byte = *pointer++;
  *data = (UBYTE*)pointer;
  return Byte;
}

BOOL AtEndOfFile(char* CurPos, char* EndOfFile)
{
  while ((*CurPos == 0x09) || (*CurPos == 0x0a) || (*CurPos == 0x0d) || (*CurPos == 0x20) || (*CurPos == 0x00))
  {
    CurPos++;
    if (CurPos >= EndOfFile)
      return TRUE;
  }

  if (CurPos >= EndOfFile)
    return TRUE;
  return FALSE;
}

//*************************************************************************
//*** load IMD shape
//*
//* params	filename = IMD file to load (including extention)
//*
//* returns	pointer to imd shape def
//*
//******

BOOL TESTDEBUG = FALSE;

#define POST_LEVEL_TEXTURELOAD			// load the polygon level ... then load the texture     .... Gareths code

iIMDShape* Neuron::IMDLoad(char* filename, iBool palkeep)
{
  iIMDShape* pIMD;
  UBYTE *pFileData, *pFileDataStart;
  UDWORD FileSize;
  BOOL res;
  UBYTE path[MAX_FILE_PATH];


  strcpy(_IMD_NAME, filename);
  strlwr(_IMD_NAME);

  _imd_get_path(filename, (char*)path);

  if (strlen((const char*)path) != 0)
  {
    if (strlen(imagePath) != 0)
    {
      if ((strlen((const char*)path) + strlen(imagePath)) > MAX_FILE_PATH)
      {
        Neuron::DebugTrace("(iv_IMDLoad) image path too long for load file\n");
        return nullptr;
      }
      strcat(imagePath, (const char*)path);
    }
  }

  res = loadFile(_IMD_NAME, &pFileData, &FileSize);
  if (res == FALSE)
  {
    Neuron::DebugTrace("(iv_IMDLoad) unable to load file\n");
    return nullptr;
  }

  {
    UDWORD* tp;
    UDWORD tt;

    tp = (UDWORD*)pFileData;

    tt = *tp;
    //         'BPIE'
    if (tt == 0x45495042)
    {
      delete[] pFileData; // free the file up
      pFileData = nullptr;
      return nullptr;
    }
  }

  pFileDataStart = pFileData;
  pIMD = Neuron::ProcessIMD(&pFileData, pFileData + FileSize, path, (UBYTE*)imagePath, palkeep);

  delete[] pFileDataStart; // free the file up
  pFileDataStart = nullptr;

  return (pIMD);
}

static UDWORD IMDcount = 0;
static UDWORD IMDPolycount = 0;
static UDWORD IMDVertexcount = 0;
static UDWORD IMDPoints = 0;
static UDWORD IMDTexAnims = 0;
static UDWORD IMDConnectors = 0;

void DumpIMDInfo(void)
{
  Neuron::DebugTrace("imds loaded    ={} - using {} bytes\n",IMDcount,IMDcount*sizeof(iIMDShape));
  Neuron::DebugTrace("polys loaded   ={} - using {} bytes\n",IMDPolycount,IMDPolycount*sizeof(iIMDPoly));
  Neuron::DebugTrace("vertices loaded={} - using {} bytes\n",IMDVertexcount,IMDVertexcount*(sizeof(VERTEXID)+sizeof(iVertex)));
  Neuron::DebugTrace("points loaded  ={} - using {} bytes\n",IMDPoints,IMDPoints*sizeof(iVector));
  Neuron::DebugTrace("connectors     ={} - using {} bytes\n",IMDConnectors,IMDConnectors*sizeof(iVector) );
}

static char texfile[64]; //Last loaded texture page filename

char* GetLastLoadedTexturePage(void) { return texfile; }

// ppFileData is incremented to the end of the file on exit!
iIMDShape* Neuron::ProcessIMD(UBYTE** ppFileData, UBYTE* FileDataEnd, UBYTE* IMDpath, UBYTE* PCXpath, iBool palkeep)
{
  char buffer[MAX_FILE_PATH], texType[MAX_FILE_PATH], ch; //, *str;
  int i, nlevels, ptype, pwidth, pheight, texpage;
  iIMDShape *s, *psShape;
  BOOL bColourKey = TRUE;
  BOOL bTextured = FALSE;
#ifdef BSPIMD
  UDWORD level;
#endif

  IMDcount++;

  if (sscanf1((char**)ppFileData, "%s %d", buffer, &_IMD_VER) != 2)
  {
    Neuron::DebugTrace("(IMDLoad) file corrupt -A\n");
    assert(2+2==5); // always fail
    return nullptr;
  }

  if ((strcmp(IMD_NAME, buffer) != 0) && (strcmp(PIE_NAME, buffer) != 0))
  {
    Neuron::DebugTrace("({} {})\n",buffer,_IMD_VER);
    Neuron::DebugTrace("(IMDLoad) not an IMD file!\n");
    return nullptr;
  }

  //Now supporting version 4 files
  if ((_IMD_VER < 1) || (_IMD_VER > 4))
  {
    Neuron::DebugTrace("(IMDLoad) file version not supported\n");
    return nullptr;
  }

  if (sscanf1((char**)ppFileData, "%s %x", buffer, &_IMD_FLAGS) != 2)
  {
    Neuron::DebugTrace("(IMDLoad) file corrupt -B\n");
    return nullptr;
  }

  texpage = -1;

  // get texture page if specified
  if (_IMD_FLAGS & IMD_XTEX)
  {
    if (_IMD_VER == 1)
    {
      if (sscanf1((char**)ppFileData, "%s %d %s %d %d", buffer, &ptype, &texfile, &pwidth, &pheight) != 5)
      {
        Neuron::DebugTrace("(IMDLoad) file corrupt -C\n");
        return nullptr;
      }
      if (strcmp(buffer, "TEXTURE") != 0)
      {
        Neuron::DebugTrace("(IMDLoad) expecting 'TEXTURE' directive\n");
        return nullptr;
      }
      bTextured = TRUE;
    }
    else //version 2 copes with long file names
    {
      if (sscanf1((char**)ppFileData, "%s %d", buffer, &ptype) != 2)
      {
        Neuron::DebugTrace("(IMDLoad) file corrupt -D\n");
        return nullptr;
      }

      if (strcmp(buffer, "TEXTURE") == 0)
      {
        ch = GetCh1(ppFileData);

        for (i = 0; (i < 80) && ((ch = GetCh1(ppFileData)) != EOF) && (ch != '.'); i++) // yummy
          texfile[i] = ch;

        if (sscanf1((char**)ppFileData, "%s", texType) != 1)
        {
          Neuron::DebugTrace("(IMDLoad) file corrupt -E\n");
          return nullptr;
        }

        /* "pcx" here is the PIE format's texture type tag, not a file that
         * exists: the art is DDS since the palette removal, and for the
         * page-* names the extension appended below is truncated away
         * before the lookup anyway. The tag stays so the shipped .pie
         * files keep parsing. */
        if (strcmp(texType, "pcx") != 0)
        {
          Neuron::DebugTrace("(IMDLoad) file corrupt -F\n");
          return nullptr;
        }

        texfile[i] = 0;

        strcat(texfile, ".dds");

        if (sscanf1((char**)ppFileData, "%d %d", &pwidth, &pheight) != 2)
        {
          Neuron::DebugTrace("(IMDLoad) file corrupt -G\n");
          return nullptr;
        }
        bTextured = TRUE;
      }
      else if (strcmp(buffer, "NOTEXTURE") == 0)
      {
        if (sscanf1((char**)ppFileData, "%s %d %d", &texfile, &pwidth, &pheight) != 3)
        {
          Neuron::DebugTrace("(IMDLoad) file corrupt -H\n");
          return nullptr;
        }
      }
      else
      {
        Neuron::DebugTrace("(IMDLoad) expecting 'TEXTURE' directive\n");
        return nullptr;
      }
    }

#ifndef PIETOOL		// The BSP tool should not reduce the texture page name down (please)
    // Super scrummy hack to reduce texture page names down to the page id
    if (bTextured)
    {
      resToLower(texfile);
      if (strncmp(texfile, "page-", 5) == 0)
      {
        for (i = 5; i < static_cast<SDWORD>(strlen(texfile)); i++)
        {
          if (!isdigit(texfile[i]))
            break;
        }
        texfile[i] = 0;
      }
    }
#endif

#ifdef PRE_LEVEL_TEXTURELOAD
    if (bTextured)
    {
      texpage = Neuron::TexLoadNew(IMDpath, texfile, ptype, palkeep,FALSE);
      if (texpage < 0) { texpage = Neuron::TexLoadNew(PCXpath, texfile, ptype, palkeep,FALSE); }
      if (texpage < 0) { 
#ifdef ALLOW_NONTEXTURED
    TESTDEBUG = TRUE; texpage = -1;
#else
    Neuron::DebugTrace("(IMDLoad) could not load/alloc tex page {} or {}/{}\n", (const char*)IMDpath, (const char*)PCXpath, texfile); return NULL;
#endif
			}
		}
		else
    {
      texpage = -1;
    }
#endif
  }

  if (sscanf1((char**)ppFileData, "%s %d", buffer, &nlevels) != 2)
  {
    Neuron::DebugTrace("(IMDLoad) file corrupt -I\n");
    return nullptr;
  }

  if (strcmp(buffer, "LEVELS") != 0)
  {
    Neuron::DebugTrace("(IMDLoad) expecting 'LEVELS' directive\n");
    return nullptr;
  }

#ifdef BSPIMD
  // if we might have BSP then we need to preread the LEVEL directive
  if (sscanf1((char**)ppFileData, "%s %d", buffer, &level) != 2)
  {
    Neuron::DebugTrace("(_load_level) file corrupt -J\n");
    return nullptr;
  }

  if (strcmp(buffer, "LEVEL") != 0)
  {
    Neuron::DebugTrace("(_load_level) expecting 'LEVEL' directive\n");
    return nullptr;
  }
#endif

  s = _imd_load_level(ppFileData, FileDataEnd, nlevels, texpage);

#ifdef POST_LEVEL_TEXTURELOAD
  // load texture page if specified
  if ((s != nullptr) && (_IMD_FLAGS & IMD_XTEX))
  {
    bColourKey = TRUE; // CheckColourKey( s );//TRUE not the only imd using this texture
    if (bTextured)
    {
      /* Note call to new texture page loader that doesn't actually load!!!!!!!!!! */
      texpage = Neuron::TexLoadNew((char*)IMDpath, texfile, ptype, palkeep, bColourKey);
      if (texpage < 0)
        texpage = Neuron::TexLoadNew((char*)PCXpath, texfile, ptype, palkeep, bColourKey);

      if (texpage < 0)
      {
        Neuron::DebugTrace("(IMDLoad) could not load/alloc tex page {} or {}/{}\n", (const char*)IMDpath, (const char*)PCXpath, texfile);
        return nullptr;
      }
    }
    else
      texpage = -1;
    /* assign tex page to levels */
    psShape = s;
    while (psShape != nullptr)
    {
      psShape->texpage = texpage;
      psShape = psShape->next;
    }
  }
#endif


  return (s);
}

//*************************************************************************
//*** load shape level polygons
//*
//* pre		fp open
//*			s allocated, s->npolys set
//*
//* params	fp = currently open shape file pointer
//*			s	= pointer to shape level
//*
//* on exit	s->polys allocated (iFSDPoly * s->npolys
//*			s->pindex allocated for each poly
//* returns	FALSE on error (memory allocation failure/bad file format)
//*
//******

static iBool _imd_load_polys(UBYTE** ppFileData, UBYTE* FileDataEnd, iIMDShape* s)
{
  int i, j; //, anim;
  iVector* points;
  iIMDPoly* poly;
  int nFrames, pbRate, tWidth, tHeight;

  //assumes points already set
  points = s->points;

  IMDPolycount += s->npolys;
  s->numFrames = 0;
  s->animInterval = 0;
  s->polys = static_cast<iIMDPoly*>(IVIS_HEAP_ALLOC(sizeof(iIMDPoly) * s->npolys));

  if (s->polys)
  {
    poly = s->polys;

    for (i = 0; i < s->npolys; i++, poly++)
    {
      {
        UDWORD flags, npnts;

        if (sscanf1((char**)ppFileData, "%x %d", &flags, &npnts) != 2)
          Neuron::DebugTrace("(_load_polys) [poly {}] error loading flags and npoints\n", i);

        poly->flags = flags;

        if (flags & PIE_NO_CULL)
          s->flags |= IMD_NOCULLSOME;

        poly->npnts = npnts;
      }

      IMDVertexcount += poly->npnts;

      poly->pindex = static_cast<VERTEXID*>(IVIS_HEAP_ALLOC(sizeof(VERTEXID) * poly->npnts));

      if ((poly->vrt = static_cast<iVertex*>(IVIS_HEAP_ALLOC(sizeof(iVertex) * poly->npnts))) == nullptr)
      {
        Neuron::DebugTrace("(_load_polys) [poly {}] memory alloc fail (vertex struct)\n", i);
        return FALSE;
      }

      if (poly->pindex)
      {
        for (j = 0; j < poly->npnts; j++)
        {
          int NewID;

          if (sscanf1((char**)ppFileData, "%d", &NewID) != 1)
          {
            Neuron::DebugTrace("failed poly {}. point {} [{}]\n",i,j,_IMD_NAME);

            Neuron::DebugTrace("(_load_polys) [poly {}] error reading poly indices\n", i);
            return FALSE;
          }
          poly->pindex[j] = vertexTable[NewID];
        }
      }
      else
      {
        Neuron::DebugTrace("(_load_polys) [poly {}] memory alloc fail (poly indices)\n", i);
        return FALSE;
      }

      if (poly->flags & IMD_TEXANIM)
      {
        IMDTexAnims++;
        if ((poly->pTexAnim = static_cast<iTexAnim*>(IVIS_HEAP_ALLOC(sizeof(iTexAnim)))) == nullptr)
        {
          Neuron::DebugTrace("(_load_polys) [poly {}] memory alloc fail (iTexAnim struct)\n", i);
          return FALSE;
        }
        // even the psx needs to skip the data
        if (sscanf1((char**)ppFileData, "%d %d %d %d", &nFrames, &pbRate, &tWidth, &tHeight) != 4)
        {
          Neuron::DebugTrace("(_load_polys) [poly {}] error reading texanim data\n", i);
          return FALSE;
        }

        DEBUG_ASSERT_TEXT(tWidth>0, "_imd_load_polys: texture width = {}", tWidth);
        DEBUG_ASSERT_TEXT(tHeight>0, "_imd_load_polys: texture height = {}", tHeight);

        poly->pTexAnim->nFrames = nFrames;

        /* Assumes same number of frames per poly */

        s->numFrames = nFrames;
        poly->pTexAnim->playbackRate = pbRate;

        /* Uses Max metric playback rate */
        s->animInterval = pbRate;
        poly->pTexAnim->textureWidth = tWidth;
        poly->pTexAnim->textureHeight = tHeight;
      }
      else
        poly->pTexAnim = nullptr;
      if (poly->vrt && (poly->flags & (IMD_TEX | IMD_PSXTEX)))
      {
        for (j = 0; j < poly->npnts; j++)
        {
          int32 VertexU, VertexV;
          if (sscanf1((char**)ppFileData, "%d %d", &VertexU, &VertexV) != 2)
          {
            Neuron::DebugTrace("(_load_polys) [poly {}] error reading tex outline\n", i);
            return FALSE;
          }

          poly->vrt[j].u = VertexU;
          poly->vrt[j].v = VertexV;
          poly->vrt[j].g = 255;
        }
      }

#ifdef BSPIMD
      poly->BSP_NextPoly = BSPPOLYID_TERMINATE; // make it end end of the BSP chain by default
#endif
    }
  }
  else
    return FALSE;

  return TRUE;
}

#ifdef BSPIMD

// The order for the BSP section of the IMD is :-
// LEFTLINK  FORWARD_POLYGONS_LIST_TEMRINATED_BY_-1  BACKWARD_POLYGONS_LIST_TEMRINATED_BY_-1  RIGHTLINK

#define GETBSPTRIANGLE(polyid) (&(s->polys[(polyid)]))

static iBool _imd_load_bsp(UBYTE** ppFileData, UBYTE* FileDataEnd, iIMDShape* s, UWORD BSPNodeCount)
{
  UWORD Node;
  PSBSPTREENODE NodeList; // An pointer to an array of  nodes
  iIMDPoly* IMDTri; // pointer to a polygon ... for handling the link list in the bsp

  if (s->npolys > BSPPOLYID_MAXPOLYID)
    Neuron::DebugTrace("(_imd_load_bsp) Too many polygons in IMD for BSP to handle\n");

  // Build table of nodes - we sort out the links later 
  NodeList = new (std::nothrow) BSPTREENODE[BSPNodeCount]; // Allocate the entire node tree

  memset(NodeList, 0, (sizeof(BSPTREENODE)) * BSPNodeCount); // Zero it out ... we need to make all pointers NULL

  for (Node = 0; Node < BSPNodeCount; Node++)
  {
    BSPTREENODE* psNode;

    SDWORD NodeID; // Temp storage area for a node ID
    SDWORD PolygonID, FirstPolygonID; // Temp storage area for a polygon ID

    psNode = &(NodeList[Node]);

    FirstPolygonID = -1; // This indicates the first polygon in the forward facing BSP list

    InitNode(psNode);

    if (sscanf1((char**)ppFileData, "%d", &NodeID) != 1) // Check that we read 1 parameter ok
    {
      Neuron::DebugTrace("(_load_bsp) - needed a left node!\n");
      return FALSE;
    }
    psNode->link[LEFT] = (PSBSPTREENODE)NodeID; // This could be -1 indicating an empty node 

    // Get forward facing polygon list - never empty apart from root node 
    while (true)
    {
      if (sscanf1((char**)ppFileData, "%d", &PolygonID) != 1) // Get a valid polygon number
      {
        Neuron::DebugTrace("(_load_bsp) - needed a polygon number\n");
        return FALSE;
      }

      if (PolygonID == -1)
        break;

      if ((PolygonID < 0) || (PolygonID >= s->npolys))
      {
        Neuron::DebugTrace("(_load_bsp) - bad polygon number\n");
        return FALSE;
      }

      if (FirstPolygonID == -1)
        FirstPolygonID = PolygonID;

      IMDTri = GETBSPTRIANGLE(PolygonID);
      if (IMDTri->BSP_NextPoly != BSPPOLYID_TERMINATE)
        Neuron::DebugTrace("(_load_bsp) - Polygon is mentioned more than once in the BSP\n");

      IMDTri->BSP_NextPoly = psNode->TriSameDir;
      psNode->TriSameDir = PolygonID;
    }

    // Generate the plane equation - if weve got any polygons
    if (FirstPolygonID != -1)
      GetPlane(s, FirstPolygonID, &(psNode->Plane));
    else
      memset(&psNode->Plane, 0, sizeof(PLANE)); // Clear the plane equation 

    // Get reverse facing polygon list - frequently empty
    while (true)
    {
      if (sscanf1((char**)ppFileData, "%d", &PolygonID) != 1) // Get a valid polygon number
      {
        Neuron::DebugTrace("(_load_bsp) - needed a polygon number\n");
        return FALSE;
      }

      if (PolygonID == -1)
        break;
      if ((PolygonID < 0) || (PolygonID >= s->npolys))
      {
        Neuron::DebugTrace("(_load_bsp) - bad polygon number\n");
        return FALSE;
      }

      // Insert into the list 
      IMDTri = GETBSPTRIANGLE(PolygonID);
      if (IMDTri->BSP_NextPoly != BSPPOLYID_TERMINATE)
        Neuron::DebugTrace("(_load_bsp) - Polygon is mentioned more than once in the BSP\n");

      IMDTri->BSP_NextPoly = psNode->TriOppoDir;
      psNode->TriOppoDir = PolygonID;
    }

    if (sscanf1((char**)ppFileData, "%d", &NodeID) != 1) // Check that we read 1 parameter ok
    {
      Neuron::DebugTrace("(_load_bsp) - needed a right node!\n");
      return FALSE;
    }
    psNode->link[RIGHT] = (PSBSPTREENODE)NodeID; // This could be -1 indicating an empty node 
  }

  // Now fix all the links
  for (Node = 0; Node < BSPNodeCount; Node++)
  {
    BSPTREENODE* psNode;
    int NodeID;

    psNode = &(NodeList[Node]);

    if ((SDWORD)(psNode->link[LEFT]) == -1)
      psNode->link[LEFT] = nullptr; // if its zero then its an empty link 
    else
    {
      NodeID = (SDWORD)psNode->link[LEFT];
      psNode->link[LEFT] = &NodeList[NodeID];
    }

    if ((SDWORD)(psNode->link[RIGHT]) == -1)
      psNode->link[RIGHT] = nullptr; // if its zero then its an empty link 
    else
    {
      NodeID = (SDWORD)psNode->link[RIGHT];
      psNode->link[RIGHT] = &NodeList[NodeID];
    }
  }

  s->BSPNode = &NodeList[0]; // Set the shape node list to the root node ... this can be used to FREE up the BSP memory if we needed to

  return TRUE;
}
#endif

BOOL ReadPoints(UBYTE** ppFileData, UBYTE* FileDataEnd, iIMDShape* s)
{
  int i;
  iVector* p;
  int lastPoint, match, j;
  SDWORD newX, newY, newZ;

  p = s->points;

  lastPoint = 0;

  for (i = 0; i < s->npoints; i++)
  {
    if (sscanf1((char**)ppFileData, "%ld %ld %ld", &(newX), &(newY), &(newZ)) != 3)
    {
      Neuron::DebugTrace("(_load_points) file corrupt -K\n");
      return FALSE;
    }

    //check for duplicate points
    match = -1;
    j = 0;

    while ((j < lastPoint) && (match == -1))
    //		while((j < i) && (match == -1))
    {
      if (newX == p[j].x)
      {
        if (newY == p[j].y)
        {
          if (newZ == p[j].z)
            match = j;
        }
      }
      j++;
    }

    if (match == -1) //new point
    {
      p[lastPoint].x = newX;
      p[lastPoint].y = newY;
      p[lastPoint].z = newZ;
      vertexTable[i] = lastPoint;
      lastPoint++;
    }
    else
      vertexTable[i] = match;
  }

  //clear remaining table
  for (i = s->npoints; i < IMD_MAX_POINTS; i++)
    vertexTable[i] = -1;

  s->npoints = lastPoint;

  return (TRUE);
}

//*************************************************************************
//*** load shape level vertices
//*
//* pre		fp open
//*			s allocated, s->npoints set
//*
//* params	fp 		= currently open shape file pointer
//*			s			= pointer to shape level
//*
//* on exit	s->points allocated (iVector * s->npoints
//* returns	FALSE on error (memory allocation failure/bad file format)
//*
//******

// I'll put in an alternative version for the PSX - this whole routine probably won't be needed anyway
static iBool _imd_load_points(UBYTE** ppFileData, UBYTE* FileDataEnd, iIMDShape* s)

{
  using namespace DirectX;

  int i;
  iVector* p;
  int32 tempXMax, tempXMin, tempZMax, tempZMin, extremeX, extremeZ;
  int32 xmax, ymax, zmax;

  //load the points then pass through a second time to setup bounding datavalues

  IMDPoints += s->npoints;

  s->points = p = static_cast<iVector*>(IVIS_HEAP_ALLOC(sizeof(iVector) * s->npoints));
  if (p == nullptr)
    return FALSE;

  if (ReadPoints(ppFileData, FileDataEnd, s) == FALSE)
    return FALSE;

  s->xmax = s->ymax = s->zmax = tempXMax = tempZMax = -FP12_MULTIPLIER;
  s->xmin = s->ymin = s->zmin = tempXMin = tempZMin = FP12_MULTIPLIER;

  // the extreme point along each axis, for the tight-sphere seed below
  XMFLOAT3 vxmin(static_cast<float>(FP12_MULTIPLIER), 0.0f, 0.0f);
  XMFLOAT3 vxmax(static_cast<float>(-FP12_MULTIPLIER), 0.0f, 0.0f);
  XMFLOAT3 vymin(0.0f, static_cast<float>(FP12_MULTIPLIER), 0.0f);
  XMFLOAT3 vymax(0.0f, static_cast<float>(-FP12_MULTIPLIER), 0.0f);
  XMFLOAT3 vzmin(0.0f, 0.0f, static_cast<float>(FP12_MULTIPLIER));
  XMFLOAT3 vzmax(0.0f, 0.0f, static_cast<float>(-FP12_MULTIPLIER));

  // set up bounding data for minimum number of vertices	
  for (i = 0; i < s->npoints; i++, p++)
  {
    if (p->x > s->xmax)
      s->xmax = p->x;
    if (p->x < s->xmin)
      s->xmin = p->x;

    /* Biggest x coord so far within our height window? */
    if ((p->x > tempXMax) && (p->y > DROID_VIS_LOWER) && (p->y < DROID_VIS_UPPER))
      tempXMax = p->x;

    /* Smallest x coord so far within our height window? */
    if ((p->x < tempXMin) && (p->y > DROID_VIS_LOWER) && (p->y < DROID_VIS_UPPER))
      tempXMin = p->x;

    if (p->y > s->ymax)
      s->ymax = p->y;
    if (p->y < s->ymin)
      s->ymin = p->y;

    if (p->z > s->zmax)
      s->zmax = p->z;
    if (p->z < s->zmin)
      s->zmin = p->z;

    /* Biggest z coord so far within our height window? */
    if ((p->z > tempZMax) && (p->y > DROID_VIS_LOWER) && (p->y < DROID_VIS_UPPER))
      tempZMax = p->z;

    /* Smallest z coord so far within our height window? */
    if ((p->z < tempZMax) && (p->y > DROID_VIS_LOWER) && (p->y < DROID_VIS_UPPER))
      tempZMin = p->z;

    // for tight sphere calculations

    const XMFLOAT3 point(static_cast<float>(p->x), static_cast<float>(p->y), static_cast<float>(p->z));

    if (point.x < vxmin.x)
      vxmin = point;

    if (point.x > vxmax.x)
      vxmax = point;

    if (point.y < vymin.y)
      vymin = point;

    if (point.y > vymax.y)
      vymax = point;

    if (point.z < vzmin.z)
      vzmin = point;

    if (point.z > vzmax.z)
      vzmax = point;
  }

  /* Centered about origin I can do the '-' thing below!! */
  extremeX = pie_MAX(tempXMax, -tempXMin);
  extremeZ = pie_MAX(tempZMax, -tempZMin);

  s->visRadius = pie_MAX(extremeX, extremeZ);

  xmax = pie_MAX(s->xmax, -s->xmin);
  ymax = pie_MAX(s->ymax, -s->ymin);
  zmax = pie_MAX(s->zmax, -s->zmin);

  s->radius = pie_MAX(xmax, (pie_MAX(ymax,zmax)));

  s->sradius = static_cast<SDWORD>((float)sqrt(xmax * xmax + ymax * ymax + zmax * zmax));

  // START: tight bounding sphere

  // set the spans to the squared distances between each axis' extreme pair

  const XMVECTOR xminV = XMLoadFloat3(&vxmin);
  const XMVECTOR xmaxV = XMLoadFloat3(&vxmax);
  const XMVECTOR yminV = XMLoadFloat3(&vymin);
  const XMVECTOR ymaxV = XMLoadFloat3(&vymax);
  const XMVECTOR zminV = XMLoadFloat3(&vzmin);
  const XMVECTOR zmaxV = XMLoadFloat3(&vzmax);

  const float xspan = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(xmaxV, xminV)));
  const float yspan = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(ymaxV, yminV)));
  const float zspan = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(zmaxV, zminV)));

  // set points dia1 & dia2 to maximally seperated pair

  // assume xspan biggest

  XMVECTOR dia1 = xminV;
  XMVECTOR dia2 = xmaxV;
  float maxspan = xspan;

  if (yspan > maxspan)
  {
    maxspan = yspan;
    dia1 = yminV;
    dia2 = ymaxV;
  }

  if (zspan > maxspan)
  {
    maxspan = zspan;
    dia1 = zminV;
    dia2 = zmaxV;
  }

  // dia1, dia2 diameter of initial sphere

  XMVECTOR cen = XMVectorScale(XMVectorAdd(dia1, dia2), 0.5f);

  // calc initial radius

  float rad_sq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(dia2, cen)));
  float rad = sqrtf(rad_sq);

  for (p = s->points, i = 0; i < s->npoints; i++, p++)
  {
    const XMVECTOR point =
      XMVectorSet(static_cast<float>(p->x), static_cast<float>(p->y), static_cast<float>(p->z), 0.0f);
    const float old_to_p_sq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(point, cen)));

    // do r**2 first
    if (old_to_p_sq > rad_sq)
    {
      // this point outside current sphere
      const float old_to_p = sqrtf(old_to_p_sq);
      // radius of new sphere
      rad = (rad + old_to_p) / 2.0f;
      // rad**2 for next compare
      rad_sq = rad * rad;
      const float old_to_new = old_to_p - rad;
      // centre of new sphere
      cen = XMVectorScale(XMVectorAdd(XMVectorScale(cen, rad), XMVectorScale(point, old_to_new)), 1.0f / old_to_p);
    }
  }

  XMFLOAT3 centre;
  XMStoreFloat3(&centre, cen);
  s->ocen.x = static_cast<int32>(centre.x);
  s->ocen.y = static_cast<int32>(centre.y);
  s->ocen.z = static_cast<int32>(centre.z);
  s->oradius = static_cast<int32>(rad);

  // END: tight bounding sphere
  return TRUE;
}

static iBool _imd_load_connectors(UBYTE** ppFileData, UBYTE* FileDataEnd, iIMDShape* s)
{
  int i;
  iVector* p;
  SDWORD newX, newY, newZ;

  IMDConnectors += s->nconnectors;

  if ((s->connectors = static_cast<iVector*>(IVIS_HEAP_ALLOC(sizeof(iVector) * s->nconnectors))) == nullptr)
  {
    Neuron::DebugTrace("(_load_connectors) IVIS_HEAP_ALLOC fail\n");
    return FALSE;
  }

  p = s->connectors;

  for (i = 0; i < s->nconnectors; i++, p++)
  {
    if (sscanf1((char**)ppFileData, "%ld %ld %ld", &(newX), &(newY), &(newZ)) != 3)
    {
      Neuron::DebugTrace("(_load_connectors) file corrupt -M\n");
      return FALSE;
    }
    p->x = newX;
    p->y = newY;
    p->z = newZ;
  }

  return TRUE;
}

//*************************************************************************
//*** load shape levels recurrsively
//*
//* pre		fp open
//*
//* params	fp 		= currently open shape file pointer
//*			s			= pointer to shape level
//*			texpage	= texture page number if IMD_TEX
//*
//* on exit	s allocated
//* returns	pointer to iFSDShape structure (or NULL on error)
//*
//******

static iIMDShape* _imd_load_level(UBYTE** ppFileData, UBYTE* FileDataEnd, int nlevels, int texpage)
{
  iIMDShape* s;
  char buffer[MAX_FILE_PATH];
  int n;
  int npolys;

#ifdef BSPIMD
#endif

  if (nlevels == 0)
    return nullptr;

  s = static_cast<iIMDShape*>(IVIS_HEAP_ALLOC(sizeof(iIMDShape)));

  if (s)
  {
    s->points = nullptr;
    s->polys = nullptr;
    s->connectors = nullptr;

    s->texanims = nullptr;
    s->next = nullptr;

    // if we can be sure that there is no bsp ... the we check for level number at this point
#ifndef BSPIMD
    if (sscanf1(ppFileData, "%s %d", buffer, &level) != 2)
    {
      Neuron::DebugTrace("(_load_level) file corrupt\n");
      return NULL;
    } if (strcmp(buffer, "LEVEL") != 0)
    {
      Neuron::DebugTrace("(_load_level) expecting 'LEVEL' directive\n");
      return NULL;
    }
#endif

    s->flags = _IMD_FLAGS;
    s->texpage = texpage;

    if (sscanf1((char**)ppFileData, "%s %d", buffer, &n) != 2)
    {
      Neuron::DebugTrace("(_load_level) file corrupt\n");
      return nullptr;
    }

    // load texture anims if specified

    // load points

    if (strcmp(buffer, "POINTS") != 0)
    {
      Neuron::DebugTrace("(_load_level) expecting 'POINTS' directive\n");
      return nullptr;
    }

    if (n > IMD_MAX_POINTS)
      Neuron::DebugTrace("(_load_level) Too many points in IMD\n");

    s->npoints = n;

    // Some imd/pie's were greater than the max number of points causing all sorts of memory overflows (blfact2)
    //
    // There was no check / error handling!
    //

    _imd_load_points(ppFileData, FileDataEnd, s);

    if (sscanf1((char**)ppFileData, "%s %d", buffer, &npolys) != 2)
    {
      Neuron::DebugTrace("(_load_level) file corrupt\n");
      return nullptr;
    }

    s->npolys = npolys;

    if (strcmp(buffer, "POLYGONS") != 0)
    {
      Neuron::DebugTrace("(_load_level) expecting 'POLYGONS' directive\n");
      return nullptr;
    }

    _imd_load_polys(ppFileData, FileDataEnd, s);

    //NOW load optional stuff
    {
      BOOL OptionalsCompleted;
#ifdef BSPIMD
      s->BSPNode = nullptr; // Zero the bsp node pointer to zero as a default
#endif

      s->nconnectors = 0; // Default number of connectors must be 0 ( this was'nt being done PBD. )

      OptionalsCompleted = FALSE;

      while (OptionalsCompleted == FALSE)
      {
        if (AtEndOfFile((char*)*ppFileData, (char*)FileDataEnd) == TRUE)
        {
          OptionalsCompleted = TRUE;
          break;
        }

        // Scans in the line ... if we don't get 2 parameters then quit
        if (sscanf1((char**)ppFileData, "%s %d", buffer, &n) != 2)
        {
          OptionalsCompleted = TRUE;
          break;
        }

        // check for next level ... or might be a BSP      - This should handle an imd if it has a BSP tree attached to it
        // might be "BSP" or "LEVEL"
        if (strcmp(buffer, "LEVEL") == 0)
        {
          s->next = _imd_load_level(ppFileData, FileDataEnd, nlevels - 1, texpage);
        }
#ifdef BSPIMD
        else if (strcmp(buffer, "BSP") == 0)
          _imd_load_bsp(ppFileData, FileDataEnd, s, static_cast<UWORD>(n));
#endif
        else if (strcmp(buffer, "CONNECTORS") == 0)
        {
          //load connector stuff
          s->nconnectors = n;
          _imd_load_connectors(ppFileData, FileDataEnd, s);
        }
        else
        {
          /* Was `&n` against a %d - it printed the address of the local, not
           * the count. Harmless until now because the message went into a
           * global nobody read; passing the value is what was meant. */
          Neuron::DebugTrace("(_load_level) unexpected directive {} {}\n", buffer, n);
          OptionalsCompleted = TRUE;
          break;
        }
      }
    }
  }
  return s;
}

BOOL Neuron::setImagePath(char* path)
{
  int i;
  strcpy(imagePath, path);
  i = strlen(imagePath);
  if (imagePath[i] != '\\')
  {
    imagePath[i] = '\\';
    imagePath[i + 1] = 0;
  }
  return TRUE;
}

static char* _imd_get_path(char* filename, char* path)
{
  int n, i;

  n = strlen(filename);

  for (i = n - 1; i >= 0 && (filename[i] != '\\'); i--);

  if (i < 0)
    path[0] = '\0';
  else
  {
    memcpy(path, filename, i + 1);
    path[i + 1] = '\0';
  }

  return path;
}

/***************************************************************************/

BOOL CheckColourKey(iIMDShape* psShape)
{
  int i;

  /* check model override flags else check all polys for colorkey flag */
  if (_IMD_FLAGS & IMD_XTRANS)
    return TRUE;

  /* loop over levels in model */
  while (psShape != nullptr)
  {
    /* loop over polys in level */
    for (i = 0; i < psShape->npolys; i++)
    {
      /* break if transparent poly found */
      if (psShape->polys[i].flags & PIE_COLOURKEYED)
        return TRUE;
    }

    /* next level */
    psShape = psShape->next;
  }

  return FALSE;
}

static int32 _imd_find_scale(int32 value, int32 limit)

{
  int n;

  for (n = 0; value > limit; n++)
    value >>= 1;

  return n;
}

#else

/*

	Version of some routines for Binary pie files only !!!

*/

iIMDShape* Neuron::ProcessIMD(UBYTE** ppFileData, UBYTE* FileDataEnd, UBYTE* IMDpath, UBYTE* PCXpath, iBool palkeep) { return (NULL); }iIMDShape*
Neuron::IMDLoad(char* filename, iBool palkeep) { return (NULL); }

#endif		// ALLOW_TEXTPIES

/*
		Binary format pie loading code - currently only for playstation
*/

// Load a binary pie file - now handles multiple levels !
iIMDShape* Neuron::ProcessBPIE(iIMDShape* InputPie, UDWORD SizeOfInputData)
{
  return (nullptr); // return NULL on pc
}
