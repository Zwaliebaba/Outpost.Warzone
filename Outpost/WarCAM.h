#ifndef _warcam_h
/* Prevent multiple inclusion */
#define _warcam_h

#include <directxmath.h>

#include "RenderTypes.h"

#define X_UPDATE 0x1
#define Y_UPDATE 0x2
#define Z_UPDATE 0x4

#define CAM_X_ONLY	X_UPDATE
#define CAM_Y_ONLY	Y_UPDATE
#define CAM_Z_ONLY	Z_UPDATE

#define CAM_X_AND_Y	(X_UPDATE + Y_UPDATE)
#define CAM_X_AND_Z	(X_UPDATE + Z_UPDATE)
#define CAM_Y_AND_Z	(Y_UPDATE + Z_UPDATE)

#define CAM_ALL	(X_UPDATE + Y_UPDATE + Z_UPDATE)

#define ACCEL_CONSTANT			(64.0f / 10.0f)	//((float)6.4)
#define VELOCITY_CONSTANT		4.0f		//((float)4.0)
#define ROT_ACCEL_CONSTANT		4.0f		//((float)5.0)
#define ROT_VELOCITY_CONSTANT	4.0f		//((float)4.0)

#define CAM_X_SHIFT	((VISIBLE_XTILES/2)*128)
#define CAM_Z_SHIFT	((VISIBLE_YTILES/2)*128)

/* The different tracking states */
enum
{
  CAM_INACTIVE,
  CAM_REQUEST,
  CAM_TRACKING,
  CAM_RESET,
  CAM_TRACK_OBJECT,
  CAM_TRACK_LOCATION
};

/* Storage for old viewnagles etc */
using WARCAM = struct _warcam
{
  UDWORD status;
  UDWORD trackClass;
  UDWORD lastUpdate;
  iView oldView;

  DirectX::XMFLOAT3 acceleration;
  DirectX::XMFLOAT3 velocity;
  DirectX::XMFLOAT3 position;

  DirectX::XMFLOAT3 rotation;
  DirectX::XMFLOAT3 rotVel;
  DirectX::XMFLOAT3 rotAccel;

  UDWORD oldDistance;
  BASE_OBJECT* target;
};

/* Externally referenced functions */
extern void initWarCam(void);
extern void setWarCamActive(BOOL status);
extern BOOL getWarCamStatus(void);
extern void camToggleStatus(void);
extern BOOL processWarCam(void);
extern void camToggleInfo(void);
extern void requestRadarTrack(SDWORD x, SDWORD y);
extern BOOL getRadarTrackingStatus(void);
extern void dispWarCamLogo(void);
extern void toggleRadarAllignment(void);
extern void camInformOfRotation(const DirectX::XMFLOAT3* rotation);
extern BASE_OBJECT* camFindDroidTarget(void);
extern DROID* getTrackingDroid(void);
extern UDWORD getNumDroidsSelected(void);
extern void camAllignWithTarget(BASE_OBJECT* psTarget);

extern float accelConstant, velocityConstant, rotAccelConstant, rotVelocityConstant;

#endif
