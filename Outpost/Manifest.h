/*
 * Manifest.h
 *
 * The asset manifest: GameData/datasets.json, the merged replacement for
 * GameDesc.lev and the .wrf files. Units are named, ordered lists of
 * resource entries (one per former .wrf); datasets describe what each level
 * loads, in the shape levParse used to build.
 */
#ifndef _manifest_h
#define _manifest_h

/* Read and parse GameData/datasets.json, then register the level datasets.
   Called once at system initialisation; the parsed document stays resident
   so datasets can be re-registered after the level list is rebuilt. */
extern BOOL ManifestLoad(void);

/* Free the resident manifest document. */
extern void ManifestShutDown(void);

/* Rebuild the LEVEL_DATASET list from the resident document, after
   levShutDown/levInitialise cleared it. */
extern BOOL ManifestRegisterDataSets(void);

/* Whether a unit with this name exists ("wrf/frontend"; a legacy
   "wrf\\name.wrf" spelling is normalised first). */
extern BOOL ManifestHasUnit(const STRING* _name);

/* Load every resource a unit lists, stamped with _blockID, reading files
   through the scratch buffer - the replacement for resLoad on a .wrf. */
extern BOOL ManifestLoadUnit(const STRING* _name, SDWORD _blockID, UBYTE* _loadBuffer, SDWORD _bufferSize);

/* Bind the texture pages of a group from the texturePages table, resolving
   each page's hard/soft variant against the translucency setting. _create
   also builds the pages that have no slot yet, which a unit wants where its
   TEXPAGE entries used to be; binding alone leaves them to first use. */
extern BOOL ManifestApplyTextureSet(const STRING* _group, BOOL _create);

#endif
