//#define PROFILE_LOADING

#include "OpenGLDrv.h"
#include "gl_model.h"
#include "gl_math.h"
#include "gl_poly.h"
#include "gl_lightmap.h"


namespace OpenGLDrv {


bspModel_t map;


/*-----------------------------------------------------------------------------
	Common BSP loading code
-----------------------------------------------------------------------------*/

static void LoadFlares(lightFlare_t *data, int count)
{
	gl_flare_t *out;

	// get leafs/owner for already built flares (from SURF_AUTOFLARE)
	for (out = map.flares; out; out = out->next)
	{
		out->leaf = CM_FindLeaf(out->origin);
		if (out->surf) out->owner = out->surf->owner;
	}

	map.numFlares += count;
	for (int i = 0; i < count; i++, data = data->next)
	{
		out = new (map.dataChain) gl_flare_t;
		out->origin = data->origin;
		out->size   = data->size;
		out->radius = data->radius;
		out->color.rgba = data->color.rgba | RGBA(0,0,0,1);
		SaturateColor4b(&out->color);
		out->style  = data->style;
		out->leaf   = CM_FindLeaf(data->origin);
		if (data->model)
			out->owner = map.models + data->model;

		// insert into list as first
		out->next = map.flares;
		map.flares = out;
	}
}


//??#define MAX_ASPECT	(5.0f / 8)

static void BuildSurfFlare(surfaceBase_t *surf, const color_t *color, float intens)
{
	CVec3	origin, c;

	if (surf->type != SURFACE_PLANAR) return;
	surfacePlanar_t *pl = static_cast<surfacePlanar_t*>(surf);

//	float aspect = (pl->maxs2[0] - pl->mins2[0]) / (pl->maxs2[1] - pl->mins2[1]);
//	if (aspect < MAX_ASPECT || aspect > 1/MAX_ASPECT) return;	// not square

	float x = (pl->maxs2[0] + pl->mins2[0]) / 2;
	float y = (pl->maxs2[1] + pl->mins2[1]) / 2;
	VectorScale(pl->axis[0], x, origin);
	VectorMA(origin, y, pl->axis[1]);
	VectorMA(origin, pl->plane.dist + 1, pl->plane.normal);

	gl_flare_t *f = new (map.dataChain) gl_flare_t;

	f->origin = origin;
	f->surf   = surf;
//	f->owner  = surf->owner;			// cannot do it: models not yet loaded
	f->leaf   = NULL;
	f->size   = sqrt(intens) * 3;
	f->radius = 0;
	f->style  = 0;

	c[0] = color->c[0];
	c[1] = color->c[1];
	c[2] = color->c[2];
	NormalizeColor255(c, c);
	f->color.c[0] = appRound(c[0]);
	f->color.c[1] = appRound(c[1]);
	f->color.c[2] = appRound(c[2]);
	f->color.c[3] = 255;				// A
	SaturateColor4b(&f->color);

	// insert into list as first
	f->next = map.flares;
	map.flares = f;
	map.numFlares++;
}


static void LoadSlights(slight_t *data)
{
	float lightScale = 1;
	if (bspfile.type == map_q3 && r_q3map_overbright->integer)
		lightScale = 2;

	// copy slights from map
	for ( ; data; data = data->next)
	{
		// process sunlight
		if (data->sun)
		{
			CSunLight *sun = new (map.dataChain) CSunLight;
			sun->dir    = data->spotDir;
			sun->intens = data->intens * lightScale;
			sun->color  = data->color;
			sun->origin = data->origin;	// for HL suns
			// insert into list
			sun->next  = map.lights;
			map.lights = sun;
			continue;
		}

		// create light
		CPointLight *out;
		switch (data->type)
		{
		case sl_linear:
			out = new (map.dataChain) CLightLinear;
			break;
		case sl_inverse:
			out = new (map.dataChain) CLightInv;
			break;
		case sl_inverse2:
			out = new (map.dataChain) CLightInv2;
			break;
		case sl_nofade:
			out = new (map.dataChain) CLightNofade;
			break;
		default:
			Com_DropError("unknown point sl.type at %g %g %g\n", VECTOR_ARG(data->origin));
		}
		// copy data
		out->origin  = data->origin;
		out->color   = data->color;
		out->intens  = data->intens * lightScale;
		out->style   = data->style;
		out->spot    = data->spot != 0;
		out->spotDir = data->spotDir;
		out->spotDot = data->spotDot;
		out->focus   = data->focus;
		out->fade    = data->fade * lightScale;		// for linear light: grow brightness, but keep distance

		// insert into list
		if (out->style == 0)
		{
			out->next = map.lights;
			map.lights = out;
		}
		else
		{
			out->next = map.flashLights;
			map.flashLights = out;
		}

		// move away lights from nearby surfaces to avoid precision errors during computation
		for (int j = 0; j < 3; j++)
		{
			static const CBox bounds = {{-0.3, -0.3, -0.3}, {0.3, 0.3, 0.3}};
			trace_t	tr;
			CM_BoxTrace(tr, out->origin, out->origin, bounds, 0, CONTENTS_SOLID);
			CM_ClipTraceToModels(tr, out->origin, out->origin, bounds, CONTENTS_SOLID);
			if (tr.allsolid)
				VectorMA(out->origin, 0.5f, tr.plane.normal);
			else
				break;
		}

		SaturateColor3f(out->color);
	}
}


static void BuildSurfLight(surfacePlanar_t *pl, const color_t *color, float area, float intens, bool sky)
{
	CVec3	c;
	c.Set(color->c[0] / 255.0f, color->c[1] / 255.0f, color->c[2] / 255.0f);

	if (bspfile.sunCount && sky)
	{	// have sun -- params may be changed
		float	m;

		m = max(bspfile.sunSurface[0], bspfile.sunSurface[1]);
		m = max(m, bspfile.sunSurface[2]);
		if (m == 0 && !map.haveSunAmbient)
			return;									// no light from sky surfaces

		if (m > 0 && m <= 1)
			c = bspfile.sunSurface;					// sun_surface specified a color
		else if (m > 1)
		{
			// sun_surface specified an intensity, color -- from surface
			c[0] *= bspfile.sunSurface[0];
			c[1] *= bspfile.sunSurface[1];
			c[2] *= bspfile.sunSurface[2];
			intens = 1;
		}
		else if (m == 0)
			intens = 0;								// this surface will produce ambient light only
	}
	intens *= NormalizeColor(c, c);

	// create
	CSurfLight *sl = new (map.dataChain) CSurfLight;
	// insert into list
	sl->next = map.lights;
	map.lights = sl;
	// setup
	sl->color  = c;
	sl->sky    = sky;
	sl->intens = intens * area;
	sl->pl     = pl;

	if (bspfile.type == map_q2 || bspfile.type == map_kp)
	{
		// qrad3 does this (see MakePatchForFace() ...)
		sl->color[0] *= sl->color[0];
		sl->color[1] *= sl->color[1];
		sl->color[2] *= sl->color[2];
	}

	SaturateColor3f(sl->color);

	pl->light = sl;
}


static float GetSurfArea(surfacePlanar_t *s)
{
	float area = 0;

	for (int i = 0; i < s->numIndexes; i += 3)
	{
		int idx1 = s->indexes[i];
		int idx2 = s->indexes[i+1];
		int idx3 = s->indexes[i+2];
		CVec3	d1, d2, cr;
		VectorSubtract(s->verts[idx1].xyz, s->verts[idx2].xyz, d1);
		VectorSubtract(s->verts[idx1].xyz, s->verts[idx3].xyz, d2);
		cross(d1, d2, cr);
		// we use a property of cross product vector length = parallelogram area = 2 * triangle area
		area += cr.GetLength() / 2;
	}
	return area;
}


static void SetNodesAlpha()
{
	CBspLeaf *leaf;
	CBspNode *node;
	surfaceBase_t **psurf;
	int		i, j;

	// enumerate leafs
	for (leaf = bspfile.leafs, i = 0; i < bspfile.numLeafs; leaf++, i++)
		// check for alpha surfaces
		for (psurf = leaf->ex->faces, j = 0; j < leaf->numFaces; psurf++, j++)
		{
			if ((*psurf)->shader->style & (SHADER_ALPHA|SHADER_TRANS33|SHADER_TRANS66))
			{
				// set "haveAlpha" for leaf and all parent nodes
				leaf->ex->haveAlpha = true;

				for (node = leaf->parent; node; node = node->parent) node->ex->haveAlpha = true;
				break;
			}
		}
}


static void LoadLeafsNodes(CBspNode *nodes, int numNodes, CBspLeaf *leafs, int numLeafs)
{
	int		i;

	// Load nodes
	CBspNodeExtra *nodeEx = new (map.dataChain) CBspNodeExtra [numNodes];
	for (i = 0; i < numNodes; i++, nodes++, nodeEx++)
		nodes->ex = nodeEx;

	// Load leafs
	CBspLeafExtra *leafEx = new (map.dataChain) CBspLeafExtra [numLeafs];
	for (i = 0; i < numLeafs; i++, leafs++, leafEx++)
	{
		leafs->ex = leafEx;
		leafEx->faces = map.leafFaces + leafs->firstFace;
	}

	// Setup node/leaf parents
	SetNodesAlpha();
}


// T is "unsigned short" or "unsigned int"
// Q3's leafFaces are int, Q1/2 - short
template<class T> static void LoadLeafSurfaces(const T *data, int count)
{
	surfaceBase_t **out;

	map.numLeafFaces = count;
	map.leafFaces = out = new (map.dataChain) surfaceBase_t* [count];
	for (int i = 0; i < count; i++, data++, out++)
		*out = map.faces[*data];
}


static void BuildPlanarSurf(surfacePlanar_t *pl)
{
	// generate axis
	if (pl->plane.normal[2] == 1 || pl->plane.normal[2] == -1)
	{
		pl->axis[0][0] = pl->axis[0][2] = 0;
		pl->axis[1][1] = pl->axis[1][2] = 0;
		pl->axis[0][1] = pl->axis[1][0] = 1;
	}
	else
	{
		static const CVec3 up = {0, 0, 1};

		cross(pl->plane.normal, up, pl->axis[0]);
		pl->axis[0].Normalize();
		cross(pl->plane.normal, pl->axis[0], pl->axis[1]);
	}
	// compute 2D bounds
	float min1, max1, min2, max2;
	min1 = min2 =  BIG_NUMBER;
	max1 = max2 = -BIG_NUMBER;
	int	i;
	vertex_t *v;
	pl->bounds.Clear();
	for (i = 0, v = pl->verts; i < pl->numVerts; i++, v++)
	{
		float p1 = dot(v->xyz, pl->axis[0]);
		float p2 = dot(v->xyz, pl->axis[1]);
		v->pos[0] = p1;
		v->pos[1] = p2;
		EXPAND_BOUNDS(p1, min1, max1);
		EXPAND_BOUNDS(p2, min2, max2);
		pl->bounds.Expand(v->xyz);
	}
	pl->mins2[0] = min1;
	pl->mins2[1] = min2;
	pl->maxs2[0] = max1;
	pl->maxs2[1] = max2;
}


static void LoadInlineModels(CBspModel *data, int count)
{
	inlineModel_t	*out;

	map.models = out = new (map.dataChain) inlineModel_t [count];
	map.numModels = count;

	for (int i = 0; i < count; i++, data++, out++)
	{
		CALL_CONSTRUCTOR(out);
		out->Name.sprintf("*%d", i);
		out->size = -1;							// do not delete in FreeModels()
		modelsArray[modelCount++] = out;

		*(static_cast<CBspModel*>(out)) = *data;
		// create surface list
		out->numFaces = data->numFaces;
		out->faces    = new (map.dataChain) surfaceBase_t* [out->numFaces];

		int k = data->firstFace;
		for (int j = 0; j < out->numFaces; j++, k++)
		{
			surfaceBase_t *s = map.faces[k];
			out->faces[j] = s;
			s->owner = i == 0 ? NULL : out;		// model 0 is world
		}
	}
}


/*-----------------------------------------------------------------------------
	Loading Quake2 BSP file
-----------------------------------------------------------------------------*/

//-------------------------- surface loading ----------------------------------

static unsigned SurfFlagsToShaderFlags2(unsigned flags)
{
	int f = SHADER_WALL;
	if (flags & SURF_ALPHA)		f |= SHADER_ALPHA;
	if (flags & SURF_TRANS33)	f |= SHADER_TRANS33;
	if (flags & SURF_TRANS66)	f |= SHADER_TRANS66;
	if (flags & SURF_SKY && SHOWSKY != 2) f |= SHADER_SKY;
	if (flags & SURF_FLOWING)	f |= SHADER_SCROLL;
	if (flags & SURF_WARP)		f |= SHADER_TURB;
	if (flags & SURF_SPECULAR)	f |= SHADER_ENVMAP;
	if (flags & SURF_DIFFUSE)	f |= SHADER_ENVMAP2;
	return f;
}


static bool CanEnvmap(const dBspFace_t *surf, int headnode, CVec3 **pverts, int numVerts)
{
//	if (!gl_autoReflect->integer)		??
//		return false;

	CVec3	mid, p1, p2;

	// find middle point
	assert(numVerts);
	mid.Zero();
	for (int i = 0; i < numVerts; i++)
		mid.Add(*pverts[i]);
	mid.Scale(1.0f / numVerts);
	// get trace points
	CVec3 &norm = (bspfile.planes + surf->planenum)->normal;
	// vector with length 1 is not enough for non-axial surfaces
	VectorMA(mid,  2, norm, p1);
	VectorMA(mid, -2, norm, p2);
	// perform trace
	trace_t	trace;
	//?? can add MASK_WATER to contents, and make Alice-line water with envmap and distorted normal
	if (surf->side) Exchange(p1, p2);
	CM_BoxTrace(trace, p1, p2, nullBox, headnode, MASK_SOLID);
	CM_ClipTraceToModels(trace, p1, p2, nullBox, MASK_SOLID);
	if (trace.fraction < 1 && !(trace.contents & CONTENTS_MIST))	//?? make MYST to be non-"alpha=f(angle)"-dependent
		return true;

	return false;
}


static shader_t *CreateSurfShader2(unsigned sflags, const dBsp2Texinfo_t *stex, const dBsp2Texinfo_t *mapTextures)
{
	if (sflags & SHADER_SKY)
	{
		map.haveSkySurfaces = true;
		return gl_skyShader;
	}

	/*---------- check for texture animation ----------*/
	char textures[MAX_QPATH * MAX_STAGE_TEXTURES];
	const dBsp2Texinfo_t *ptex = stex;
	char *pname = textures;
	int numTextures = 0;
	while (true)
	{
		pname += appSprintf(pname, MAX_QPATH, "textures/%s", ptex->texture) + 1;	// length(format)+1
		numTextures++;

		int n = ptex->nexttexinfo;
		if (n < 0) break;			// no animations
		ptex = mapTextures + n;
		if (ptex == stex) break;	// loop
	}
	*pname = 0;						// make zero (NULL string) after ASCIIZ string

	if (numTextures > MAX_STAGE_TEXTURES)
		appWPrintf("%s: animation chain is too long (%d)\n", stex->texture, numTextures);
	else if (numTextures > 1)
		sflags |= SHADER_ANIM;

	return FindShader(textures, sflags);
}


static unsigned *GetQ1Palette()
{
	static unsigned q1palette[256];
	static bool loaded = false;

	if (loaded) return q1palette;
	loaded = true;

	// get the palette
	byte *pal = (byte*)GFileSystem->LoadFile("gfx/palette.lmp");
//	if (!pal) //-- file always exists now (resource)
//		Com_DropError("Can not load gfx/palette");

	const byte *p = pal;
	for (int i = 0; i < 256; i++, p += 3)
	{
		unsigned v = RGB255(p[0], p[1], p[2]);
		q1palette[i] = LittleLong(v);
	}
//	q1palette[255] &= LittleLong(0x00FFFFFF);		// #255 is transparent (alpha = 0) -- but Q1 uses this for menus/HUD only

	// free image
	delete pal;
	return q1palette;
}


static void CreatePalettedImage(const char *name, dBsp1Miptex_t *tex, bool useAlpha)
{
	if (!tex->offsets[0])
	{
		// use wad file! (half-life map)
		//!!

		//!! CreateImage8(Name, ......);
	}
	else
	{
		unsigned hlPalette[256];
		// find and load inline texture + load shader again
		unsigned *palette = NULL;
		if (bspfile.type == map_q1)
			palette = GetQ1Palette();
		else
		{
			byte *p = (byte*)(tex+1) + tex->width * tex->height * 85 / 64 + 2;	// 1+1/4+1/16+1/64 = (64+16+4+1)/64 = 85/64
			for (int i = 0; i < 256; i++, p += 3)
			{
				unsigned v = RGB255(p[0], p[1], p[2]);
				hlPalette[i] = LittleLong(v);
			}
			// HL uses palette index as translucent
#if 0
			if (useAlpha) hlPalette[255] &= LittleLong(RGBA(1,1,1,0));	// keep original color to allow non-translucent use (as HL does)
#else
			if (useAlpha) hlPalette[255] = 0;							// prevent appearance of blue borders
#endif
			palette = hlPalette;
		}
		if (FindImage(name, IMAGE_PICMIP|IMAGE_MIPMAP|IMAGE_WORLD)) return;	// image may be already created
		CreateImage8(name, (byte*)(tex+1), tex->width, tex->height, IMAGE_PICMIP|IMAGE_MIPMAP|IMAGE_WORLD, palette);
	}
}


static int FindMiptex1(const char *texName)
{
	for (int i = 0; i < bspfile.miptex1->nummiptex; i++)
		if (!(stricmp((char*)bspfile.miptex1 + bspfile.miptex1->dataofs[i], texName)))
			return i;
	return -1;
}


static shader_t *CreateSurfShader1(unsigned sflags, const dBsp2Texinfo_t *stex)
{
	if (sflags & SHADER_SKY)
	{
		map.haveSkySurfaces = true;
		map.SkyShader = "env/q1sky";
		return gl_skyShader;
	}

	// get pointer to 1st char of real texture name (for hl -- cut wads/wad_name/)
	const char *srcTexName;
	if (srcTexName = strrchr(stex->texture, '/'))
		srcTexName++;
	else
		srcTexName = stex->texture;

	if (srcTexName[0] == '+')
	{
		sflags |= SHADER_ANIM;
		if (!stex->texture[1])
		{
			appWPrintf("CreateSurfShader1(): bad anim texture name: %s\n", stex->texture);
			return gl_defaultShader;
		}
	}

	// null name (set in cmodel.cpp::LoadSurfaces1()) -> invisible surface
	//?? any texture or invisible texture ?
	if (!srcTexName[0]) return gl_defaultShader;

	TString<MAX_QPATH> Name;
	Name.sprintf("textures/%s", stex->texture);
	char *dstTexName = Name.rchr('/') + 1;

	// check: may be, shader already loaded
	shader_t *shader = FindShader(Name, sflags|SHADER_CHECKLOADED);
	if (shader) return shader;

	if (!(sflags & SHADER_ANIM))
	{
		//?? find a way to allow external textures for SHADER_ANIM
		//?? (currently, will simply create 1-stage shader)
		//?? (check: should not be collision with HL wad anim textures)
		// try external shader/texture
		// note: here we clear SHADER_ANIM flag (not needed for scripts, but
		// FindShader() will require name[MAX_QPATH][numAnimTextures] array)
		shader = FindShader(Name, (sflags|SHADER_CHECK) & ~SHADER_ANIM);
		if (shader) return shader;
	}

	// HERE: shader not yet loaded and have no external texture replacement

	int miptex = FindMiptex1(dstTexName);	// set in LoadSurfaces1() (cmodel.cpp)
	if (miptex < 0)
	{
		appWPrintf("CreateSurfShader1(%s): bad miptex index %d\n", *Name, miptex);
		return gl_defaultShader;
	}

	char textures[MAX_QPATH * MAX_STAGE_TEXTURES];
	char *pname = textures;

	bool wrapped = false;
	while (true)
	{
		if (miptex < 0)
		{
			// generate next name for animation
			assert(dstTexName[0] == '+');	// should be animated texture
			if (dstTexName[1] == '9')
				dstTexName[1] = 'a';
			else
				dstTexName[1]++;
			if (wrapped && dstTexName[1] == srcTexName[1])
				break;						// all frames found
			// get 'miptex' index
			miptex = FindMiptex1(dstTexName);
			if (miptex < 0 && srcTexName[1] != '0' && !wrapped)
			{
				// support for animations, started from non-0 frame
				wrapped = true;
				dstTexName[1] = '0'-1;		// next frame will be incremented
				continue;
			}
		}
		if (miptex < 0) break;				// no more textures for animation

		// find miptex
		dBsp1Miptex_t *tex = (dBsp1Miptex_t*)( (byte*)bspfile.miptex1 + bspfile.miptex1->dataofs[miptex] );
		CreatePalettedImage(Name, tex, (sflags & SHADER_ALPHA) != 0);
		// add Name to textures[]
		strcpy(pname, Name);
		pname = strchr(pname, 0) + 1;

		if (!(sflags & SHADER_ANIM)) break;	// not animated shader
		miptex = -1;						// force next anim texture to be searched

	}
	*pname = 0;								// make zero (NULL string) after ASCIIZ string
	// create shader

	shader = FindShader(textures, sflags);
	if (sflags & SHADER_ANIM)
		SetShaderAnimFreq(shader, bspfile.type == map_q1 ? 5 : 10);	// q1/hl animation frequency
	return shader;
}


static void InitSurfaceLightmap2(const dBspFace_t *face, surfacePlanar_t *surf, float mins[2], float maxs[2])
{
	int		size[2];				// lightmap size
	int		imins[2], imaxs[2];		// int mins/maxs, aligned to lightmap grid
	int		i;

	// round mins/maxs to a lightmap cell size
	imins[0] = appFloor(mins[0] / 16.0f) * 16;
	imins[1] = appFloor(mins[1] / 16.0f) * 16;
	imaxs[0] = appCeil(maxs[0] / 16.0f) * 16;
	imaxs[1] = appCeil(maxs[1] / 16.0f) * 16;
	// calculate lightmap size
	size[0] = (imaxs[0] - imins[0]) / 16 + 1;
	size[1] = (imaxs[1] - imins[1]) / 16 + 1;

	vertex_t *v = surf->verts;
	for (i = 0; i < surf->numVerts; i++, v++)
	{
		v->lm2[0] = (v->lm[0] - imins[0]) / 16;			// divide to lightmap unit size
		v->lm2[1] = (v->lm[1] - imins[1]) / 16;
		v->lm[0]  = v->lm2[0] + 0.5f;					// shift to texel center
		v->lm[1]  = v->lm2[1] + 0.5f;
	}
	// create dynamic lightmap
	dynamicLightmap_t *lm = new (map.dataChain) dynamicLightmap_t;
	surf->lightmap = lm;
	lm->w = size[0];
	lm->h = size[1];
	for (i = 0; i < 4; i++)								// enum styles
	{
		int st = face->styles[i];
		if (st == 255) break;
		lm->style[i]    = st;
		lm->modulate[i] = (i == 0 ? 128 : 0);			// initial state
		lm->source[i]   = (!map.monoLightmap)			// just offset in map lightData
			? (byte*)NULL + face->lightofs + i * size[0] * size[1] * 3	// Q2/HL
			: (byte*)NULL + face->lightofs + i * size[0] * size[1];		// Q1
	}
	lm->numStyles = i;
}


static void LoadSurfaces2(const dBspFace_t *surfs, int numSurfaces, const int *surfedges, const dEdge_t *edges,
	CVec3 *verts, const dBsp2Texinfo_t *tex, const CBspModel *models, int numModels)
{
	guard(LoadSurfaces2);

	int		j;

	map.numFaces = numSurfaces;
	map.faces = new (map.dataChain) surfaceBase_t* [numSurfaces];
	for (int i = 0; i < numSurfaces; i++, surfs++)
	{
		CVec3 *pverts[MAX_POLYVERTS];

		int numVerts = surfs->numedges;
		/*------- build vertex list -------*/
		const int *pedge = surfedges + surfs->firstedge;
		for (j = 0; j < numVerts; j++, pedge++)
		{
			int idx = *pedge;
			if (idx > 0)
				pverts[j] = verts + (edges+idx)->v[0];
			else
				pverts[j] = verts + (edges-idx)->v[1];
		}

		/*---- Generate shader with name and SURF_XXX flags ----*/
		const dBsp2Texinfo_t *stex = tex + surfs->texinfo;
		unsigned sflags = SurfFlagsToShaderFlags2(stex->flags);

		// find owner model
		//?? LoadInlineModels() will set owner too
		const CBspModel *owner;
		for (j = 0, owner = models; j < numModels; j++, owner++)
			if (i >= owner->firstFace && i < owner->firstFace + owner->numFaces)
				break;

		if (owner->flags & CMODEL_ALPHA)
			sflags |= SHADER_ALPHA;
		if (owner->color.c[3] != 255)
			sflags |= SHADER_FORCEALPHA|SHADER_ENT_ALPHA;
		if (bspfile.type == map_hl)
		{
			if (!(owner->flags & CMODEL_SCROLL))
				sflags &= ~SHADER_SCROLL;	// allow for CMODEL_SCROLL only
		}

#if 0
		if (Cvar_VariableInt("dlmodels"))	//??
		{
			if (owner->flags & CMODEL_MOVABLE && !(sflags & (SHADER_TRANS33|SHADER_TRANS66|SHADER_ALPHA)))
				sflags = SHADER_SKIN;
		}
#endif

		// check covered contents and update shader flags
		if (sflags & (SHADER_TRANS33|SHADER_TRANS66|SHADER_FORCEALPHA) && !(sflags &
			(SHADER_ENVMAP|SHADER_ENVMAP2|SHADER_TURB|SHADER_SCROLL)))	// && gl_autoReflect ??
		{
			if (CanEnvmap(surfs, owner->headnode, pverts, numVerts))
				sflags |= SHADER_ENVMAP;
		}

		// check for shader lightmap
		bool needLightmap = false;
		if (surfs->lightofs >= 0 && !(sflags & SHADER_SKIN))	//??
		{
			if (sflags & SHADER_SKY)
			{
				// get color of sun surface
				if (map.sunSurfColor[0] + map.sunSurfColor[1] + map.sunSurfColor[2] == 0)
				{
					image_t *img = FindImage(va("textures/%s", stex->texture), IMAGE_MIPMAP|IMAGE_PICMIP);	// find sky image only for taking its color
					if (img)
					{
						// do not require to divide by 255: will be normalized anyway
						// NOTE: here byte c[4] -> float[3]
						map.sunSurfColor.Set(VECTOR_ARG(img->color.c));
						NormalizeColor(map.sunSurfColor, map.sunSurfColor);
					}
					else
						map.sunSurfColor.Set(1, 1, 1);
				}
			}
			else
			{
				needLightmap = true;
				if (sflags & (SHADER_TRANS33|SHADER_TRANS66|SHADER_ALPHA|SHADER_TURB|SHADER_ENT_ALPHA))
				{
					if (surfs->lightofs > 0)	// when uninitialized by radiocity, will be 0 (not "-1")
						sflags |= SHADER_VERTEXLIGHT;
					else
						needLightmap = false;
				}
			}
		}

		// q1/hl can use lightofs==-1 to indicate dark lightmap
		bool darkLightmap = false;
		if ((bspfile.type == map_q1 || bspfile.type == map_hl) &&
			(sflags & (SHADER_WALL|SHADER_LIGHTMAP|SHADER_TURB)) == SHADER_WALL &&
			surfs->lightofs == -1)
		{
			needLightmap = true;
			darkLightmap = true;
		}

		if (needLightmap)
			sflags |= SHADER_LIGHTMAP;

		// create shader for surface
		shader_t *shader = (bspfile.type == map_q2 || bspfile.type == map_kp)
			? CreateSurfShader2(sflags, stex, tex)
			: CreateSurfShader1(sflags, stex);
		//?? update sflags from this (created) shader -- it may be scripted (with different flags)

		if (sflags & (SHADER_TRANS33|SHADER_TRANS66|SHADER_ALPHA|SHADER_TURB|SHADER_SKY))
			numVerts = RemoveCollinearPoints(pverts, numVerts);

		/* numTriangles = numVerts - 2 (3 verts = 1 tri, 4 verts = 2 tri etc.)
		 * numIndexes = numTriangles * 3
		 * Indexes: (0 1 2) (0 2 3) (0 3 4) ... (here: 5 verts, 3 triangles, 9 indexes)
		 */
		int numTris;
		if (shader->tessSize)
		{
			numTris  = SubdividePlane(pverts, numVerts, shader->tessSize);
			numVerts = subdivNumVerts;
		}
		else
			numTris = numVerts - 2;

		int numIndexes = numTris * 3;

		/*------- Prepare for vertex generation ----------------*/
		// alloc new surface
		surfacePlanar_t *s = new (map.dataChain) surfacePlanar_t;
		s->shader = shader;
		map.faces[i] = s;

		s->plane = bspfile.planes[surfs->planenum];
		if (surfs->side)
		{
			// backface (needed for backface culling)
			s->plane.normal.Negate();
			s->plane.dist = -s->plane.dist;
			s->plane.Setup();
		}
		s->numVerts   = numVerts;
		s->verts      = new (map.dataChain) vertex_t [numVerts];
		s->numIndexes = numIndexes;
		s->indexes    = new (map.dataChain) int [numIndexes];

		/*-------------- Generate indexes ----------------------*/
		if (shader->tessSize)
			GetSubdivideIndexes(s->indexes);
		else
		{
			int *pindex = s->indexes;
			for (j = 0; j < numTris; j++)
			{
				*pindex++ = 0;
				*pindex++ = j+1;
				*pindex++ = j+2;
			}
		}

		/*----------- Create surface vertexes ------------------*/
		vertex_t *v = s->verts;
		// ClearBounds2D(mins, maxs)
		float mins[2], maxs[2];				// surface extents
		mins[0] = mins[1] =  BIG_NUMBER;
		maxs[0] = maxs[1] = -BIG_NUMBER;
		// Enumerate vertexes, prepare data for lightmap
		for (j = 0; j < numVerts; j++, v++)
		{
			v->xyz = *pverts[j];
			if (sflags & SHADER_SKY) continue;

			float v1 = dot(v->xyz, stex->vecs[0].vec) + stex->vecs[0].offset;
			float v2 = dot(v->xyz, stex->vecs[1].vec) + stex->vecs[1].offset;
			// Update bounds
			EXPAND_BOUNDS(v1, mins[0], maxs[0]);
			EXPAND_BOUNDS(v2, mins[1], maxs[1]);
			// Texture coordinates
			if (!(sflags & SHADER_TURB)) //?? (!shader->tessSize)
			{
				assert(shader->width > 0 && shader->height > 0);
				v->st[0] = v1 / shader->width;
				v->st[1] = v2 / shader->height;
			}
			else
			{
				v->st[0] = v1 - stex->vecs[0].offset;
				v->st[1] = v2 - stex->vecs[1].offset;
			}
			// save intermediate data for lightmap texcoords
			v->lm[0] = v1;
			v->lm[1] = v2;
			// Vertex color
			v->c.rgba = RGBA(1,1,1,1);
		}

		/*----------------- Lightmap -------------------*/
		s->lightmap = NULL;
		if (needLightmap)
			InitSurfaceLightmap2(surfs, s, mins, maxs);
		// special case for q1/hl lightmap without data: dark lightmap
		// (other map formats -- fullbright texture)
		if (darkLightmap)
		{
			dynamicLightmap_t *lm = s->lightmap;
			lm->w = 0;
			lm->h = 0;
			static byte dark[] = { 0, 0, 0 };
			lm->source[0] = dark;		// hack: offset from lighting start
			v = s->verts;
			for (j = 0; j < numVerts; j++, v++)
			{
				v->lm[0] = v->lm[1] = 0.5f;
				v->lm2[0] = v->lm2[1] = 0;
			}
			lm->externalSource = true;
		}

		BuildPlanarSurf(s);
		if (stex->flags & SURF_LIGHT)		//!! + sky when ambient <> 0
		{
			static const color_t defColor = {96, 96, 96, 255};// {255, 255, 255, 255};

			float area = GetSurfArea(s);
			image_t *img = FindImage(va("textures/%s", stex->texture), IMAGE_MIPMAP);
			BuildSurfLight(s, img ? &img->color : &defColor, area, stex->value, (stex->flags & SURF_SKY) != 0);
			if (stex->flags & SURF_AUTOFLARE && !(stex->flags & SURF_SKY))
				BuildSurfFlare(s, img ? &img->color : &defColor, area);
		}

		// free allocated poly
		if (shader->tessSize)
			FreeSubdividedPlane();
	}

	unguard;
}


//------------------------ loading surface lightmaps --------------------------

static int LightmapCompare(surfacePlanar_t* const* s1, surfacePlanar_t* const* s2)
{
	const surfacePlanar_t *surf1 = *s1, *surf2 = *s2;

	byte *v1, *v2;
	v1 = (surf1->lightmap) ? surf1->lightmap->source[0] : NULL;
	v2 = (surf2->lightmap) ? surf2->lightmap->source[0] : NULL;

	if (v1 == v2 && v1)
	{
		// TRYLIGHTMAP should go after normal lightmaps
		if (surf1->shader->style & SHADER_VERTEXLIGHT)
			return 1;
		else if (surf2->shader->style & SHADER_VERTEXLIGHT)
			return -1;
		else // situation, when 2 shaders have save lightmap pointers, but no TRYLIGHTMAP (bad, but supported)
			return 0;
	}

	return v1 - v2;
}

static int ShaderCompare(surfacePlanar_t* const* surf1, surfacePlanar_t* const* surf2)
{
	return (*surf1)->shader->sortIndex - (*surf2)->shader->sortIndex;
}

static void GenerateLightmaps2(byte *lightData, int lightDataSize)
{
	guard(GenerateLightmaps2);

	int		i, k;
	surfacePlanar_t *s;
	dynamicLightmap_t *dl;
	surfacePlanar_t *sortedSurfaces[MAX_MAP_FACES];

	// NOTE: we assume here, that all Q2-bsp map faces are of surfacePlanar_t type

	LM_Init();		// reset lightmap status (even when vertex lighting used)

	int optLmTexels = 0;

	for (i = 0; i < map.numFaces; i++)
	{
		s = static_cast<surfacePlanar_t*>(map.faces[i]);
		sortedSurfaces[i] = s;		// prepare for sorting

		if (dl = s->lightmap)
		{
			// convert lightmap offset to pointer
			for (k = 0; k < dl->numStyles; k++)
				if (!dl->externalSource)
					dl->source[k] += (unsigned)lightData;

			if (map.monoLightmap) continue;				//?? add q1 map support here
			// optimize lightmaps
			color_t avg;
			if (LM_IsMonotone(dl, &avg))
			{
				//!! if no mtex (+env_add/env_combine) -- use 1-pixel lm; otherwise -- vertex lm
				//?? can check avg color too: if c[i] < 128 -- can be doubled, so -- vertex light
				if (1)
				{
					// replace lightmap block with a single texel
					//!! WARNING: this will modify lightmap in bsp_t (will be affect map reloads)
					//?? -OR- use pixel from center instead (modify dl->source[0]) ?
					byte *p = dl->source[0];
					p[0] = avg.c[0];
					p[1] = avg.c[1];
					p[2] = avg.c[2];
					vertex_t *v;
					for (k = 0, v = s->verts; k < s->numVerts; k++, v++)
						v->lm[0] = v->lm[1] = 0.5;		// all verts should point to a middle of lightmap texel
					optLmTexels += dl->w * dl->h - 1;
					dl->w = dl->h = 1;
				}
				else
				{
					// convert to a vertex lightmap
					//!! UNFINISHED: need SPECIAL vertex lm, which will be combined with dyn. lm with ENV_ADD
					s->shader = FindShader(s->shader->Name, s->shader->style | SHADER_VERTEXLIGHT);
					optLmTexels += dl->w * dl->h;
				}
			}
		}
	}
	Com_DPrintf("removed %d lm texels (%.3f blocks)\n", optLmTexels, (float)optLmTexels / (LIGHTMAP_SIZE * LIGHTMAP_SIZE));

	/*------------ find invalid lightmaps -------------*/
	// sort surfaces by lightmap data pointer
	if (lightDataSize)
	{
		// when map have no lightmaps, we do not need to sort them
		QSort(sortedSurfaces, map.numFaces, LightmapCompare);
	}

	// check for lightmap intersection
	// NOTE: nere we compare lm->source[0], which should always correspond to a lightstyle=0 (global lighting)
	byte *ptr = NULL;
	for (i = 0; i < map.numFaces; i++)
	{
		s = sortedSurfaces[i];
		dl = s->lightmap;
		if (!dl) continue;

		bool bad = false;
		int lmSize = (!map.monoLightmap)
			? dl->w * dl->h * dl->numStyles * 3			// Q2, HL
			: dl->w * dl->h * dl->numStyles;			// Q1
		if (dl->source[0] + lmSize > lightData + lightDataSize)
			bad = true;				// out of bsp file
		else if (ptr && ptr > dl->source[0] && (s->shader->style & SHADER_VERTEXLIGHT))
			bad = true;				// erased by previous block

		if (dl->externalSource) bad = false;

		//?? add check: if current is "try" interlaced with next "not try (normal)" - current is "bad"

		if (bad)
		{
			// remove lightmap info from the shader
			if (s->shader->lightmapNumber != LIGHTMAP_NONE)
			{
				// later (in this function), just don't create vertex colors for this surface
				Com_DPrintf("Disable lm for %s\n", *s->shader->Name);
				SetShaderLightmap(s->shader, LIGHTMAP_NONE);
//				appPrintf("  diff: %d\n", ptr - dl->source[0]);
//				appPrintf("  w: %d  h: %d  st: %d\n", dl->w, dl->h, dl->numStyles);
			}
			continue;
		}
		else
		{	// advance pointer
			ptr = dl->source[0] + lmSize;
		}

		// after this examination. we can resort lightstyles (cannot do this earlier because used source[0])
		LM_SortLightStyles(dl);
		if (!map.monoLightmap)
			LM_CheckMinlight(dl);		//?? check for q1 minlight ?
		// set shader lightstyles
		if (s->shader->lightmapNumber >= 0)
		{
			int styles = 0;
			bool fastOnly = true;
			dl->w2 = dl->w;
			dl->numFastStyles = 0;
			for (k = dl->numStyles - 1; k >= 0; k--)
			{
				byte t = dl->style[k];
				if (IS_FAST_STYLE(t))
				{
					styles = (styles << 8) + t;
					dl->w2 += dl->w;
					dl->numFastStyles++;
				}
				else
					fastOnly = false;
			}
			if (styles) s->shader = SetShaderLightstyles(s->shader, styles, fastOnly);
		}
	}

	/*-------------- vertex lighting ------------------*/
	// we should genetate lightmaps after validating of ALL lightmaps
	for (i = 0; i < map.numFaces; i++)
	{
		s = static_cast<surfacePlanar_t*>(map.faces[i]);
		if (!s->lightmap) continue;

		if (s->shader->lightmapNumber == LIGHTMAP_NONE)
		{	// lightmap was removed from shader - remove it from surface too
			s->lightmap = NULL;
			continue;
		}

		UpdateDynamicLightmap(s, true, 0);
	}

	/*----------- allocate lightmaps for surfaces -----------*/
	if (!gl_config.vertexLight)
	{
		// resort surfaces by shader
		QSort(sortedSurfaces, map.numFaces, ShaderCompare);

		lightmapBlock_t *bl = NULL;
		i = 0;
		while (i < map.numFaces)
		{
			s = sortedSurfaces[i];
			dl = s->lightmap;
			if (!dl || s->shader->lightmapNumber == LIGHTMAP_VERTEX)
			{
				i++;
				continue;
			}

			shader_t *shader = sortedSurfaces[i]->shader;
			// count number of surfaces with the same shader
			int i2;
			for (i2 = i + 1; i2 < map.numFaces; i2++)
				if (sortedSurfaces[i2]->shader != shader) break;
			int nextIndex = i2;

			// try to allocate all surfaces in a single lightmap block
			LM_Rewind();
			bool fit = false;
			while (!fit)
			{
				bl = LM_NextBlock();
				LM_Save(bl);					// save layout
				for (i2 = i; i2 < nextIndex; i2++)
				{
					fit = LM_AllocBlock(bl, sortedSurfaces[i2]->lightmap);
					if (!fit)
					{
						if (bl->empty)
						{	// not fit to EMPTY block -- allocate multiple blocks
							LM_Restore(bl);		// restore layout
							LM_Rewind();
							bl = NULL;			// do not repeat "Restore()"
							fit = true;
							break;
						}
						break;
					}
				}
				if (bl) LM_Restore(bl);			// ...
				else	bl = LM_NextBlock();	// was not fit to one block - use 1st available block
			}

			// Here: all surfaces fits "bl" block, or allocate multiple blocks started from "bl"
			for (i2 = i; i2 < nextIndex; i2++)
			{
				dl = sortedSurfaces[i2]->lightmap;
				while (!LM_AllocBlock(bl, dl))
				{
					if (bl->empty)
						Com_DropError("LM_AllocBlock: unable to allocate lightmap %d x %d\n", dl->w, dl->h);
					LM_Flush(bl);				// finilize block
					bl = LM_NextBlock();
				}
				if (!map.monoLightmap)
					LM_PutBlock(dl);
				else
					LM_PutBlock1(dl);
			}

			i = nextIndex;
		}
		LM_Done();

		// update surfaces
		for (i = 0; i < map.numFaces; i++)
		{
			s = sortedSurfaces[i];
			dl = s->lightmap;
			if (!dl || !dl->block) continue;

			// set lightmap for shader
			s->shader = SetShaderLightmap(s->shader, dl->block->index);

			// update vertex lightmap coords
			float s0 = (float)dl->s / LIGHTMAP_SIZE;
			float t0 = (float)dl->t / LIGHTMAP_SIZE;
			int		j;
			vertex_t *v;
			for (j = 0, v = s->verts; j < s->numVerts; j++, v++)
			{
				v->lm[0] = v->lm[0] / LIGHTMAP_SIZE + s0;
				v->lm[1] = v->lm[1] / LIGHTMAP_SIZE + t0;
			}
		}
	}

	unguard;
}


static void LoadBsp2()
{
	guard(LoadBsp2);

	// Load surfaces
START_PROFILE(LoadSurfaces2)
	LoadSurfaces2(bspfile.faces2, bspfile.numFaces, bspfile.surfedges, bspfile.edges, bspfile.vertexes2,
		bspfile.texinfo2, bspfile.models, bspfile.numModels);
END_PROFILE
	LoadLeafSurfaces(bspfile.leaffaces2, bspfile.numLeaffaces);
START_PROFILE(GenerateLightmaps2)
	GenerateLightmaps2(bspfile.lighting, bspfile.lightDataSize);
END_PROFILE
	// Load bsp (leafs and nodes)
	LoadLeafsNodes(bspfile.nodes, bspfile.numNodes, bspfile.leafs, bspfile.numLeafs);
	LoadInlineModels(bspfile.models, bspfile.numModels);

	switch (bspfile.fogMode)
	{
	case fog_exp:
		gl_fogMode    = GL_EXP;
		gl_fogDensity = bspfile.fogDens;
		break;
	case fog_exp2:
		gl_fogMode    = GL_EXP2;
		gl_fogDensity = bspfile.fogDens;
		break;
	case fog_linear:
		gl_fogMode    = GL_LINEAR;
		gl_fogStart   = bspfile.fogStart;
		gl_fogEnd     = bspfile.fogEnd;
		break;
	default:
		gl_fogMode = 0;
	}
	if (bspfile.fogMode)
		memcpy(gl_fogColor, bspfile.fogColor.v, sizeof(bspfile.fogColor));	// bspfile.fogColor is [3], but gl_fogColor is [4]
	else
		gl_fogColor[0] = gl_fogColor[1] = gl_fogColor[2] = 0;
	gl_fogColor[3] = 1;

	unguard;
}


/*-----------------------------------------------------------------------------
	Loading Quake1/Half-Life BSP file
-----------------------------------------------------------------------------*/

static void LoadSky1()
{
	if (bspfile.type != map_q1) return;

	// find sky texinfo
	int miptex = -1;
	for (int i = 0; i < bspfile.miptex1->nummiptex; i++)
		if (!(strncmp((char*)bspfile.miptex1 + bspfile.miptex1->dataofs[i], "sky", 3)))
		{
			// found
			miptex = i;
			break;
		}
	if (miptex < 0) return;							// sky was not found

	// create 2 sky textures
	dBsp1Miptex_t *tex = (dBsp1Miptex_t*)( (byte*)bspfile.miptex1 + bspfile.miptex1->dataofs[miptex] );
	if (tex->width != 256 || tex->height != 128)
	{
		Com_DPrintf("sky texture \"%s\" have wring size (%d x %d)\n", tex->name, tex->width, tex->height);
		return;
	}

	for (int idx = 0; idx < 2; idx++)
	{
		static const char *names[] = {"mask", "back"};
		byte *data = (byte*)(tex+1) + idx * 128;
		byte buf[128*128];
		for (int y = 0; y < 128; y++)
		{
			// NOTE: q1 uses color index #0 in mask texture as completely transparent
			memcpy(buf + y*128, data, 128);
			data += 256;
		}
		CreateImage8(va("env/q1sky_%s", names[idx]), buf, 128, 128, IMAGE_WORLD|IMAGE_MIPMAP, GetQ1Palette());
	}
}


static void LoadBsp1()
{
	guard(LoadBsp1);

	if (bspfile.type == map_q1) map.monoLightmap = true;

	// Load surfaces
START_PROFILE(LoadSurfaces2)
	LoadSurfaces2(bspfile.faces2, bspfile.numFaces, bspfile.surfedges, bspfile.edges, bspfile.vertexes2,
		bspfile.texinfo2, bspfile.models, bspfile.numModels);
END_PROFILE
	LoadLeafSurfaces(bspfile.leaffaces2, bspfile.numLeaffaces);
START_PROFILE(GenerateLightmaps2)
	GenerateLightmaps2(bspfile.lighting, bspfile.lightDataSize);
END_PROFILE
	// Load bsp (leafs and nodes)
	LoadLeafsNodes(bspfile.nodes, bspfile.numNodes, bspfile.leafs, bspfile.numLeafs);
	LoadInlineModels(bspfile.models, bspfile.numModels);
	LoadSky1();

	unguard;
}


/*-----------------------------------------------------------------------------
	Loading Quake3 BSP file
-----------------------------------------------------------------------------*/

#if 1
//!! TEMP; note: used for dBsp3Face_t::FLARE
class surfaceNull_t : public surfaceBase_t
{
public:
	inline surfaceNull_t() { type = SURFACE_BAD; }
	virtual void Tesselate(refEntity_t &ent)
	{}
};

static surfaceNull_t nullSurface;
#endif


// should be loaded before surfaces
static void LoadLightmaps3(byte *lightData, int lightDataSize)
{
	if (gl_config.vertexLight) return;

#define Q3_LM_BYTES  (Q3_LIGHTMAP_SIZE * Q3_LIGHTMAP_SIZE * 3)
	int numLightmaps = lightDataSize / Q3_LM_BYTES;
	int overbright = r_q3map_overbright->integer ? -1 : 0;
	for (int i = 0; i < numLightmaps; i++, lightData += Q3_LM_BYTES)
	{
		//?? move code to gl_lightmap.cpp ??
		byte buffer[Q3_LIGHTMAP_SIZE * Q3_LIGHTMAP_SIZE * 4]; // rgba
		LM_Copy(buffer, lightData, Q3_LIGHTMAP_SIZE * Q3_LIGHTMAP_SIZE, overbright);
		CreateImage(va("*lightmap%d", i), buffer, Q3_LIGHTMAP_SIZE, Q3_LIGHTMAP_SIZE,
			IMAGE_CLAMP|IMAGE_LIGHTMAP);
	}
}


static shader_t *CreateSurfShader3(unsigned sflags, const dBsp3Shader_t *stex, int lightmapNum)
{
	if (lightmapNum >= 0)	// map surface can force lightmapNum==-1 for "no light"
		sflags |= SHADER_LIGHTMAP;
	else if (lightmapNum == LIGHTMAP_VERTEX)
		sflags |= SHADER_VERTEXLIGHT;
	else
		lightmapNum = LIGHTMAP_NONE;

	if (stex->surfaceFlags & Q3_SURF_SKY) sflags = SHADER_SKY;
	shader_t *shader = FindShader(stex->name, sflags);

	if (shader->type == SHADERTYPE_SKY && !map.haveSkySurfaces)
	{
		// remember sky shader, mark map as "have sky"
		map.haveSkySurfaces = true;
		map.SkyShader = stex->name;
		// create sunlight
		CSunLight *sun = new (map.dataChain) CSunLight;
		sun->dir    = shader->sunDirection;
		sun->color  = shader->sunColor;
		sun->intens = sun->color.Normalize();
		// and insert into list
		sun->next  = map.lights;
		map.lights = sun;
	}

	if (shader->type == SHADERTYPE_SKY && SHOWSKY == 2)
		return gl_defaultShader;

	if (lightmapNum >= 0)
		shader = SetShaderLightmap(shader, lightmapNum);

	return shader;
}


static surfaceBase_t *LoadFakePlanarSurface3(const dBsp3Face_t *surf, dBsp3Vert_t *verts, unsigned *indexes,
	shader_t *shader)
{
	surfaceTrisurf_t *s = new (map.dataChain) surfaceTrisurf_t;
	s->shader = shader;
#if 1
	if (shader->type != SHADERTYPE_SKY && shader->lightValue)
		appWPrintf("Trisurf light: %s\n", *shader->Name);
#endif

	s->numVerts   = surf->numVerts;
	s->verts      = new (map.dataChain) vertexNormal_t [surf->numVerts];
	s->numIndexes = surf->numIndexes;
	s->indexes    = new (map.dataChain) int [surf->numIndexes];
	s->fogNum     = (surf->fogNum + 1) & 255;
	// copy verts
	vertexNormal_t *dst = s->verts;
	s->bounds.Clear();
	for (int j = 0; j < surf->numVerts; j++, verts++, dst++)
	{
		dst->xyz    = verts->v;
		dst->st[0]  = verts->st[0];
		dst->st[1]  = verts->st[1];
		dst->lm[0]  = verts->lm[0];
		dst->lm[1]  = verts->lm[1];
		dst->c.rgba = verts->c.rgba;		//!! saturate
		dst->normal = verts->normal;
		s->bounds.Expand(dst->xyz);
	}
	// copy indexes
	memcpy(s->indexes, indexes, surf->numIndexes * sizeof(int));
	return s;
}


static surfaceBase_t *LoadPlanarSurface3(const dBsp3Face_t *surf, dBsp3Vert_t *verts, unsigned *indexes,
	const dBsp3Shader_t *stex)
{
	unsigned sflags = SHADER_WALL;
	shader_t *shader = CreateSurfShader3(sflags, stex, surf->lightmapNum);

	if (surf->lightmapVecs[2][0] == 0 &&
		surf->lightmapVecs[2][1] == 0 &&
		surf->lightmapVecs[2][2] == 0)
	{
		// Third-party q3map2 compiler has special surface type: pre-tesselated patch.
		// It is stored in bsp as planar surface, but really it is not planar. Create
		// trisurf from it.
		return LoadFakePlanarSurface3(surf, verts, indexes, shader);
	}

	surfacePlanar_t *s = new (map.dataChain) surfacePlanar_t;
	s->shader     = shader;
	s->numVerts   = surf->numVerts;
	s->verts      = new (map.dataChain) vertex_t [surf->numVerts];
	s->numIndexes = surf->numIndexes;
	s->indexes    = new (map.dataChain) int [surf->numIndexes];
	s->fogNum     = (surf->fogNum + 1) & 255;
	// copy verts
	vertex_t *dst = s->verts;
	for (int i = 0; i < surf->numVerts; i++, verts++, dst++)
	{
		dst->xyz    = verts->v;
		dst->st[0]  = verts->st[0];
		dst->st[1]  = verts->st[1];
		dst->lm[0]  = verts->lm[0];
		dst->lm[1]  = verts->lm[1];
		dst->c.rgba = verts->c.rgba;		//!! saturate
		//?? check for same normal for all verts
	}
	// copy indexes
	memcpy(s->indexes, indexes, surf->numIndexes * sizeof(int));

	// surface plane
	s->plane.normal = surf->lightmapVecs[2]; // Q3 bsp format ...
	s->plane.dist   = dot(s->verts[0].xyz, s->plane.normal);
	s->plane.Setup();

	BuildPlanarSurf(s);
	// surface light
	if (shader->lightValue)
	{
		float area = GetSurfArea(s);
		float surfLightScale = (r_q3map_overbright->integer) ? 2 : 1;
		BuildSurfLight(s, &shader->lightColor, area, shader->lightValue * surfLightScale,
			false /*?? (stex->flags & SURF_SKY) != 0*/);
	}
	return s;
}


static surfaceBase_t *LoadTriangleSurface3(const dBsp3Face_t *surf, dBsp3Vert_t *verts, unsigned *indexes,
	const dBsp3Shader_t *stex)
{
	surfaceTrisurf_t *s = new (map.dataChain) surfaceTrisurf_t;
	shader_t *shader = CreateSurfShader3(SHADER_WALL, stex, LIGHTMAP_VERTEX);
	s->shader = shader;
#if 1
	if (shader->type != SHADERTYPE_SKY && shader->lightValue)
		appWPrintf("Trisurf light: %s\n", *shader->Name);
#endif

	s->numVerts   = surf->numVerts;
	s->verts      = new (map.dataChain) vertexNormal_t [surf->numVerts];
	s->numIndexes = surf->numIndexes;
	s->indexes    = new (map.dataChain) int [surf->numIndexes];
	s->fogNum     = (surf->fogNum + 1) & 255;
	// copy verts
	vertexNormal_t *dst = s->verts;
	s->bounds.Clear();
	for (int j = 0; j < surf->numVerts; j++, verts++, dst++)
	{
		dst->xyz    = verts->v;
		dst->st[0]  = verts->st[0];
		dst->st[1]  = verts->st[1];
		dst->lm[0]  = verts->lm[0];
		dst->lm[1]  = verts->lm[1];
		dst->c.rgba = verts->c.rgba;		//!! saturate
		dst->normal = verts->normal;
		s->bounds.Expand(dst->xyz);
	}
	// copy indexes
	memcpy(s->indexes, indexes, surf->numIndexes * sizeof(int));
	return s;
}


static void LoadSurfaces3(const dBsp3Face_t *surfs, int numSurfaces, dBsp3Vert_t *verts, unsigned *indexes,
	const dBsp3Shader_t *tex)
{
	guard(LoadSurfaces3);

	nullSurface.shader = gl_defaultShader;

	map.numFaces = numSurfaces;
	map.faces = new (map.dataChain) surfaceBase_t* [numSurfaces];
	for (int i = 0; i < numSurfaces; i++, surfs++)
	{
		switch (surfs->surfaceType)
		{
		case dBsp3Face_t::PLANAR:
			map.faces[i] = LoadPlanarSurface3(surfs, verts + surfs->firstVert, indexes + surfs->firstIndex,
				tex + surfs->shaderNum);
			break;

		case dBsp3Face_t::TRISURF:
			map.faces[i] = LoadTriangleSurface3(surfs, verts + surfs->firstVert, indexes + surfs->firstIndex,
				tex + surfs->shaderNum);
			break;

		case dBsp3Face_t::PATCH:
			{
				surfaceNull_t *s = new (map.dataChain) surfaceNull_t;	//!!!
				shader_t *shader = CreateSurfShader3(SHADER_WALL, tex + surfs->shaderNum, surfs->lightmapNum);
				s->shader = shader;
				map.faces[i] = s;
#if 1
				if (shader->type == SHADERTYPE_SKY)
					DebugPrintf("%s: curve sky: %s\n", *bspfile.Name, *shader->Name);
				else if (shader->lightValue)
					appWPrintf("Curve light: %s\n", *shader->Name);
#endif
			}
			break;

		case dBsp3Face_t::FLARE:			// will be loaded in LoadFlares3()
			map.faces[i] = &nullSurface;	// it's easier to do "nullSurface", than remove flares from surface lists
			break;

		default:
			Com_DropError("unknown surface type: %d", surfs->surfaceType);
		}
	}

	unguard;
}


// should be called after LoadInlineModels()
static void LoadFlares3(const dBsp3Face_t *surfs, int numSurfaces, const CBspModel *models, int numModels)
{
	for (int i = 0; i < numSurfaces; i++, surfs++)
	{
		if (surfs->surfaceType != dBsp3Face_t::FLARE) continue;

		map.numFlares++;
		gl_flare_t *out = new (map.dataChain) gl_flare_t;
		out->origin = surfs->flareOrigin;
		out->size   = 64;
		out->radius = 0;
		const CVec3& color = surfs->lightmapVecs[0];
		out->color.rgba = RGBS(color[0], color[1], color[2]);
		SaturateColor4b(&out->color);
		out->style  = 0;
		out->leaf   = CM_FindLeaf(out->origin);
		// insert into list as first
		out->next = map.flares;
		map.flares = out;

		// find owner model
		const CBspModel *model;
		int j;
		for (j = 1, model = models+1; j < numModels; j++, model++)
			if (i >= model->firstFace && i < model->firstFace + model->numFaces)
				break;
		if (j < numModels)
			out->owner = map.models + j;
	}
}


static void LoadFogs3(const dBsp3Fog_t *fogs, int numFogs, dBsp3Brush_t *brushes, dBsp3Brushside_t *sides,
	CPlane *planes)
{
	// NOTE: here we reserve fog index 0 for "no fog"; indices starting with 1
	map.numFogs = numFogs;
	map.fogs = new (map.dataChain) gl_fog_t [numFogs + 1];
	gl_fog_t *dst = map.fogs + 1;
	for (int i = 0; i < numFogs; i++, fogs++, dst++)
	{
		dBsp3Brush_t *brush = brushes + fogs->brushNum;
		// brushes in q3bsp always sorted with axial sides first
		// following code taken from q3 R_LoadFogs()
		dBsp3Brushside_t *side = sides + brush->firstside;
		for (int j = 0; j < 3; j++)
		{
			dst->bounds.mins[j] = -planes[side->planenum].dist;
			side++;
			dst->bounds.maxs[j] =  planes[side->planenum].dist;
			side++;
		}
		shader_t *shader = FindShader(fogs->name, SHADER_CHECK);
		if (!shader || shader->type != SHADERTYPE_FOG)
		{
			appWPrintf("No shader %s exists for fog volume %d\n", fogs->name, i);
			continue;
		}
//		appPrintf("FOG[%d]: shader %s (%08X)\n", i, *shader->Name, shader->fogColor.rgba);
//		appPrintf("         bounds %d {%g %g %g} {%g %g %g}\n", fogs->brushNum, VECTOR_ARG(dst->bounds.mins), VECTOR_ARG(dst->bounds.maxs));
		dst->color.rgba    = shader->fogColor.rgba;
		dst->texCoordScale = (shader->fogDist > 1.0f) ? 1.0f / (shader->fogDist * 8) : 1.0f/8;
		dst->hasSurface    = fogs->visibleSide != -1;
		if (dst->hasSurface)
		{
			dst->surface = planes[sides[brush->firstside + fogs->visibleSide].planenum];
			// inverse plane
			dst->surface.normal.Negate();
			dst->surface.dist = -dst->surface.dist;
		}
	}
}


static void LoadBsp3()
{
	guard(LoadBsp3);

	LoadLightmaps3(bspfile.lighting, bspfile.lightDataSize);
	LoadSurfaces3(bspfile.faces3, bspfile.numFaces, bspfile.vertexes3, bspfile.indexes3, bspfile.texinfo3);
	LoadLeafSurfaces(bspfile.leaffaces3, bspfile.numLeaffaces);
	LoadLeafsNodes(bspfile.nodes, bspfile.numNodes, bspfile.leafs, bspfile.numLeafs);
	LoadInlineModels(bspfile.models, bspfile.numModels);
	LoadFlares3(bspfile.faces3, bspfile.numFaces, bspfile.models, bspfile.numModels);
	LoadFogs3(bspfile.fogs, bspfile.numFogs, bspfile.brushes3, bspfile.brushsides3, bspfile.planes);

	unguard;
}


/*-----------------------------------------------------------------------------
	LoadWorldMap()
-----------------------------------------------------------------------------*/

void LoadWorldMap()
{
	guard(LoadWorldMap);

	FreeModels();					// will zero `map'
	map.dataChain = new CMemoryChain;

	STAT(clock(gl_ldStats.bspLoad));
	// map should be already loaded by client
	// load sun ambient info
	map.sunAmbient = bspfile.sunAmbient;
	if (bspfile.sunAmbient[0] + bspfile.sunAmbient[1] + bspfile.sunAmbient[2] > 0)
		map.haveSunAmbient = true;
	map.ambientLight = bspfile.ambientLight;

	switch (bspfile.type)
	{
	case map_q2:
	case map_kp:
		LoadBsp2();
		// get ambient light from lightmaps (place to all map types ??)
		if (map.ambientLight[0] + map.ambientLight[1] + map.ambientLight[2] == 0)
		{
			map.ambientLight[0] = lmMinlight.c[0] * 2;
			map.ambientLight[1] = lmMinlight.c[1] * 2;
			map.ambientLight[2] = lmMinlight.c[2] * 2;
			Com_DPrintf("Used minlight from lightmap {%d, %d, %d}\n", VECTOR_ARG(lmMinlight.c));
		}
		break;
	case map_q1:
	case map_hl:
		LoadBsp1();
		//?? ambient light?
		break;
	case map_q3:
		LoadBsp3();
		break;
	default:
		Com_DropError("R_LoadWorldMap: unknown BSP type");
	}
	LoadFlares(bspfile.flares, bspfile.numFlares);
	LoadSlights(bspfile.slights);
	PostLoadLights();
	InitLightGrid();
	STAT(unclock(gl_ldStats.bspLoad));

	unguard;
}


} // namespace
