/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// client.h -- primary header for client

#include "qcommon.h"
#include "protocol.h"

#include "ref.h"

#include "vid.h"
#include "screen.h"
#include "sound.h"
#include "input.h"
#include "keys.h"
#include "console.h"
#include "cdaudio.h"
#include "cl_playermodel.h"

//=============================================================================

//#define GUN_DEBUG				// uncomment to enable gun_xxx debugging commands

struct frame_t					//?? rename
{
	bool	valid;				// cleared if delta parsing was invalid
	int		serverframe;
	int		servertime;			// server time the message is valid for (in msec)
	int		deltaframe;
	byte	areabits[MAX_MAP_AREAS/8]; // portalarea visibility bits
	player_state_t playerstate;
	int		num_entities;
	int		parse_entities;		// non-masked index into cl_parse_entities array
};


struct clEntityState_t : public entityStateEx_t
{
	bool	valid;				// when "false", additional fields are not initialized
	CAxis	axis;
	CBox	bounds;
	CVec3	center;
	float	radius;
};


struct centity_t
{
	unsigned clientInfoId;		// id from linked clientInfo_t (for detection of animation restart)
	clEntityState_t baseline;	// delta from this if not from a previous frame
	clEntityState_t current;
	clEntityState_t prev;		// will always be valid, but might just be a copy of current
	animState_t legsAnim;		// animation state for Quake3 player model
	animState_t torsoAnim;
	animState_t headAnim;		// used only angles

	int		serverframe;		// if not current, this ent isn't in the frame

	int		trailcount;			// for diminishing grenade trails
	CVec3	lerp_origin;		// for trails (variable hz)

	int		fly_stoptime;
};

#define NEW_FX	(cl_newfx->integer)


extern char cl_weaponmodels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
extern int num_cl_weaponmodels;

#define	CMD_BACKUP		64		// allow a lot of command backups for very fast systems

//
// the client_state_t structure is wiped completely at every
// server map change
//
struct client_state_t
{
	int		timeoutcount;

	int		timedemoFrames;
	int		timedemoStart;
	int		timedemoLongestFrame;

	bool	rendererReady;		// false if on new level or restarting renderer
	bool	sound_prepped;		// ambient sounds can start
	bool	forceViewFrame;		// should draw 1 frame of world scene from network data

	int		parse_entities;		// index (not anded off) into cl_parse_entities[]

	usercmd_t cmds[CMD_BACKUP];	// each mesage will send several old cmds
	int		cmd_time[CMD_BACKUP];	// time sent, for calculating pings
	short	predicted_origins[CMD_BACKUP][3];	// for debug comparing against server

	float	predicted_step;		// for stair up smoothing
	unsigned predicted_step_time;

	CVec3	predicted_origin;	// generated by CL_PredictMovement
	CVec3	predicted_angles;
	CVec3	prediction_error;

	frame_t	frame;				// received from server
	int		surpressCount;		// number of messages rate supressed
	frame_t	frames[UPDATE_BACKUP];
	frame_t	*oldFrame;			// points to previous frame; used for interpolation

	// the client maintains its own idea of view angles, which are
	// sent to the server each frame.  It is cleared to 0 upon entering each level.
	// the server sends a delta each frame which is added to the locally
	// tracked view angles to account for standing on rotating objects,
	// and teleport direction changes
	CVec3	viewangles;

	int		time;				// this is the time value that the client
								// is rendering at, in msec; always <= cls.realtime
	double	ftime;				// same as "time/1000", in sec; more precisious than "time"
	int		overtime;			// amount of time clamped (used for detection of hang server); ms
	float	lerpfrac;			// between oldframe and frame

	refdef_t refdef;

	CVec3	modelorg;			// center of client entity (used prediction for 1st person and server data for 3rd person)
	//?? replace with CAxis (forward=[0], right=-[1], up=[2])
	CVec3	v_forward, v_right, v_up; // set when refdef.angles is set

	//
	// transient data from server
	//
	char	layout[1024];		// HUD info
	int		inventory[MAX_ITEMS];

	bool	cinematicActive;

	//
	// server state information
	//
	bool	attractloop;		// running the attract loop, any key will menu
	int		servercount;		// server identification for prespawns
	char	gamedir[MAX_QPATH];
	int		playernum;

	char	configstrings[MAX_CONFIGSTRINGS][MAX_QPATH];

	//
	// locally derived information from server state
	//
	CRenderModel	*model_draw[MAX_MODELS];
	cmodel_t *model_clip[MAX_MODELS];

	sfx_t	*sound_precache[MAX_SOUNDS];
	CBasicImage	*image_precache[MAX_IMAGES];

	clientInfo_t clientInfo[MAX_CLIENTS];
};

extern	client_state_t	cl;

/*
==================================================================

the client_static_t structure is persistant through an arbitrary number
of server connections

==================================================================
*/

typedef enum {
	ca_uninitialized,
	ca_disconnected, 			// not talking to a server
	ca_connecting,				// sending request packets to the server
	ca_connected,				// netchan_t established, waiting for svc_serverdata
	ca_active					// game views should be displayed
} connstate_t;

typedef enum {
	dl_none,
	dl_model,
	dl_sound,
	dl_skin,
	dl_single
} dltype_t;						// download type

typedef enum {key_game, key_console, key_message, key_menu, key_bindingMenu} keydest_t;

struct client_static_t
{
	connstate_t	state;
	keydest_t	key_dest;
	bool		keep_console;	// do not hide console even if menu active

	int			realtime;		// always increasing, no clamping, etc
	float		frametime;		// seconds since last frame
	bool		netFrameDropped;

	/*----- screen rendering information -----*/
	int			disable_servercount; // when we receive a frame and cl.servercount
								// > cls.disable_servercount, end loading plaque
								//????? check requirement of this var
	bool		loading;

	/*-------- connection information --------*/
	char		servername[MAX_OSPATH];	// name of server from original connect
	float		connect_time;	// for connection retransmits

	netchan_t	netchan;
	int			serverProtocol;	// in case we are doing some kind of version hack

	int			challenge;		// from the server to use for connecting

	FILE		*download;		// file transfer from server
	char		downloadtempname[MAX_OSPATH];
	char		downloadname[MAX_OSPATH];
	int			downloadnumber;
	dltype_t	downloadtype;
	int			downloadpercent;

	// demo recording info must be here, so it isn't cleared on level change
	bool		demorecording;
	bool		demowaiting;	// don't record until a non-delta message is received
	FILE		*demofile;

	bool		newprotocol;
};

extern client_static_t	cls;

//=============================================================================

//
// cvars
//
extern	cvar_t	*cl_gun;
extern	cvar_t	*cl_add_blend;
extern	cvar_t	*cl_add_lights;
extern	cvar_t	*cl_add_particles;
extern	cvar_t	*cl_add_entities;
extern	cvar_t	*cl_predict;
extern	cvar_t	*cl_footsteps;
extern	cvar_t	*cl_noskins;

extern	cvar_t	*cl_upspeed;
extern	cvar_t	*cl_forwardspeed;
extern	cvar_t	*cl_sidespeed;

extern	cvar_t	*cl_yawspeed;
extern	cvar_t	*cl_pitchspeed;

extern	cvar_t	*cl_run;

extern	cvar_t	*cl_anglespeedkey;

extern	cvar_t	*cl_shownet;
extern	cvar_t	*cl_showmiss;
extern	cvar_t	*cl_showclamp;

extern	cvar_t	*lookspring;
extern	cvar_t	*lookstrafe;
extern	cvar_t	*sensitivity;

extern	cvar_t	*m_pitch;
extern	cvar_t	*m_yaw;
extern	cvar_t	*m_forward;
extern	cvar_t	*m_side;

extern	cvar_t	*freelook;

extern	cvar_t	*cl_paused;

extern	cvar_t	*cl_vwep;
extern	cvar_t	*hand;
extern	cvar_t	*cl_3rd_person;
extern	cvar_t	*cl_cameraDist, *cl_cameraHeight, *cl_cameraAngle;

extern  cvar_t  *cl_extProtocol;

extern	cvar_t	*cl_newfx;
extern	cvar_t	*cl_showbboxes;
extern	cvar_t	*r_sfx_pause;

extern	cvar_t	*cl_infps;

extern	cvar_t	*gender, *gender_auto, *skin;

extern	centity_t	*cl_entities;	// [MAX_EDICTS]

// the cl_parse_entities must be large enough to hold UPDATE_BACKUP frames of
// entities, so that when a delta compressed message arives from the server
// it can be un-deltad from the original
#define	MAX_PARSE_ENTITIES	1024
extern	clEntityState_t	cl_parse_entities[MAX_PARSE_ENTITIES];

//=============================================================================

#define MAX_SUSTAINS		32
//ROGUE
struct cl_sustain_t
{
	int		id;
	int		type;
	int		endtime;
	int		nextthink;
	int		thinkinterval;
	CVec3	org;
	CVec3	dir;
	int		color;
	int		count;
	int		magnitude;
	void	(*think)(cl_sustain_t *self);
};


//---------------- particles ---------------------


void CL_ClearParticles (void);
void CL_UpdateParticles (void);
particle_t *CL_AllocParticle (void);
beam_t *CL_AllocParticleBeam (const CVec3 &start, const CVec3 &end, float radius, float fadeTime);
void CL_MetalSparks (const CVec3 &pos, const CVec3 &dir, int count);

extern particle_t *active_particles;
extern beam_t	*active_beams;

#define	PARTICLE_GRAVITY			80
#define BLASTER_PARTICLE_COLOR		0xE0
#define INSTANT_PARTICLE			-10000.0	//??

//--------------- lightstyles --------------------

void CL_SetLightstyle (int i, const char *s);
void CL_RunLightStyles (void);
extern lightstyle_t cl_lightstyles[MAX_LIGHTSTYLES];


//----------------- dlights ----------------------

struct cdlight_t
{
	int		key;				// so entities can reuse same entry
	CVec3	color;
	CVec3	origin;
	float	radius;
	float	die;				// stop lighting after this time
};

cdlight_t *CL_AllocDlight (int key, const CVec3 &origin);
void CL_AddDLights (void);


//------------------------------------------------


void CL_ClearTEnts (void);
void CL_BlasterTrail (const CVec3 &start, const CVec3 &end);
void CL_RailTrail (const CVec3 &start, const CVec3 &end);
void CL_RailTrailExt (const CVec3 &start, const CVec3 &end, byte rType, byte rColor);
void CL_BubbleTrail (const CVec3 &start, const CVec3 &end);
void CL_FlagTrail (const CVec3 &start, const CVec3 &end, int color);

// XATRIX
void CL_IonripperTrail (const CVec3 &start, const CVec3 &end);


void CL_ParticleSteamEffect2(cl_sustain_t *self);
void CL_TeleporterParticles (clEntityState_t *ent);
void CL_ParticleEffect (const CVec3 &org, const CVec3 &dir, int color, int count);
void CL_ParticleEffect2 (const CVec3 &org, const CVec3 &dir, int color, int count);
void CL_ParticleEffect3 (const CVec3 &org, const CVec3 &dir, int color, int count);

void CL_BlasterParticles (const CVec3 &org, const CVec3 &dir, unsigned color);
// ROGUE
void CL_BlasterTrail2 (const CVec3 &start, const CVec3 &end);
void CL_DebugTrail (const CVec3 &start, const CVec3 &end);
void CL_Flashlight (int ent, const CVec3 &pos);
void CL_ForceWall (const CVec3 &start, const CVec3 &end, int color);
void CL_BubbleTrail2 (const CVec3 &start, const CVec3 &end, int dist);
void CL_Heatbeam (const CVec3 &start, const CVec3 &end);
void CL_ParticleSteamEffect (const CVec3 &org, const CVec3 &dir, int color, int count, int magnitude);
void CL_TrackerTrail (const CVec3 &start, const CVec3 &end, int particleColor);
void CL_TagTrail (const CVec3 &start, const CVec3 &end, int color);
void CL_ColorFlash (const CVec3 &pos, int ent, int intensity, float r, float g, float b);
void CL_Tracker_Shell(const CVec3 &origin);
void CL_MonsterPlasma_Shell(const CVec3 &origin);
void CL_ColorExplosionParticles (const CVec3 &org, int color, int run);
void CL_ParticleSmokeEffect (const CVec3 &org, const CVec3 &dir, int color, int count, int magnitude);
void CL_Widowbeamout (cl_sustain_t *self);
void CL_Nukeblast (cl_sustain_t *self);
void CL_WidowSplash (const CVec3 &org);

void CL_ParseDelta (clEntityState_t *from, clEntityState_t *to, int number, unsigned bits, bool baseline);
void CL_ParseFrame (void);

void CL_ParseTEnt (void);
void CL_ParseMuzzleFlash (void);
void CL_ParseMuzzleFlash2 (void);
void SmokeAndFlash(const CVec3 &origin);

void CL_AddEntities (void);
void CL_AddTEnts (void);

//=================================================

void CL_RegisterSounds (void);

void CL_Quit_f (void);

void IN_Accumulate (void);

void CL_ParseLayout (void);

void CL_AddEntityBox (clEntityState_t *st, unsigned rgba);


//
// cl_main.cpp
//
void CL_Init (void);

void CL_Pause (bool enable);
void CL_Disconnect (void);
void CL_Disconnect_f (void);
void CL_GetChallengePacket (void);

#define NUM_ADDRESSBOOK_ENTRIES 9
void CL_PingServers_f (void);

void CL_Snd_Restart_f (void);

void CL_WriteDemoMessage (void);


//
// cl_input.cpp
//

// declarations for exporting to in_win32.cpp:
typedef struct
{
	int			down[2];		// key nums holding it down
	unsigned	downtime;		// msec timestamp
	unsigned	msec;			// msec down this frame
	int			state;
} kbutton_t;
extern 	kbutton_t 	in_Strafe;
extern 	kbutton_t 	in_Speed;

void CL_InitInput (void);
void CL_SendCmd (void);
void CL_SendMove (usercmd_t *cmd);

void CL_ClearState (void);

void CL_ReadPackets (void);

int  CL_ReadFromServer (void);
void CL_WriteToServer (usercmd_t *cmd);
void CL_BaseMove (usercmd_t *cmd);

void IN_CenterView (void);

//
// cl_parse.cpp
//
extern	const char *svc_strings[svc_last];

void CL_ParseServerMessage (void);

#define SHOWNET(s)	\
	if (cl_shownet->integer >= 2) Com_Printf ("%3d:%s\n", net_message.readcount-1, s);

void CL_ParseClientinfo (int player);

//
// cl_view.cpp
//
extern CBasicImage *railSpiralShader, *railRingsShader, *railBeamShader;
#ifdef GUN_DEBUG
extern int gun_frame;
extern CRenderModel *gun_model;
#endif
extern float r_blend[4];

void V_Init (void);
void V_InitRenderer ();
bool V_RenderView ();

void V_AddEntity (entity_t *ent);
void V_AddEntity2 (entity_t *ent);
void AddEntityWithEffects (entity_t *ent, unsigned fx);
void AddEntityWithEffects2 (entity_t *ent, unsigned fx);

void V_AddLight (const CVec3 &org, float intensity, float r, float g, float b);
float CalcFov (float fov_x, float width, float height);

//
// cl_tent.cpp
//
void CL_RegisterTEntSounds (void);
void CL_RegisterTEntModels (void);
void CL_SmokeAndFlash(const CVec3 &origin);

//
// cl_fx.cpp
//
void CL_BigTeleportParticles (const CVec3 &org);
void CL_RocketTrail (const CVec3 &start, const CVec3 &end, centity_t *old);
void CL_DiminishingTrail (const CVec3 &start, const CVec3 &end, centity_t *old, int flags);
void CL_FlyEffect (centity_t *ent, const CVec3 &origin);
void CL_BfgParticles (entity_t *ent);
void CL_EntityEvent (clEntityState_t *ent);
// XATRIX
void CL_TrapParticles (entity_t *ent);

void CL_ClearEffects ();
void CL_ClearLightStyles ();

//
// menus
//
void M_Init (void);
void M_Keydown (int key);
void M_Draw (void);
void M_Menu_Main_f (void);
void M_ForceMenuOff (void);
void M_ForceMenuOn (void);
void M_AddToServerList (netadr_t adr, char *info);

struct menuFramework_t;
extern menuFramework_t *m_current;


//
// cl_pred.cpp
//
void CL_EntityTrace (trace_t &tr, const CVec3 &start, const CVec3 &end, const CBox &bounds, int contents);
void CL_Trace (trace_t &tr, const CVec3 &start, const CVec3 &end, const CBox &bounds, int contents);
void CL_PredictMovement (void);
void CL_CheckPredictionError (void);

//
// cl_download.cpp
//
void CL_ParseDownload (void);
void CL_Download_f (bool usage, int argc, char **argv);
void CL_Precache_f (int argc, char **argv);
