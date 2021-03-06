#include "OpenGLDrv.h"
#include "gl_sky.h"
#include "gl_math.h"
#include "gl_backend.h"


namespace OpenGLDrv {

static float skyMins[2][6], skyMaxs[2][6];


#define	ON_EPSILON		0.1f			// point on plane side epsilon
#define	MAX_CLIP_VERTS	256				// 64 is not enough, has some maps with complex sky surfaces;
										// can check sky surface size on map loading stage and simplify too
										// complex surfaces (for example, make a bounding box)

enum {SIDE_FRONT, SIDE_BACK, SIDE_ON};

// bitmask
#define SKY_FRUSTUM		1
#define SKY_SURF		2


static void ClipSkyPolygon(CVec3 *verts, int numVerts, int stage)
{
	if (!numVerts) return;				// empty polygon

	if (numVerts > MAX_CLIP_VERTS - 2)	// may require to add 2 verts for splitting
		Com_DropError("ClipSkyPolygon: overflow");

	int		i;

	if (stage == 6)
	{
		// fully clipped -- update skyMins/skyMaxs
		// decide which face it maps to
		CVec3 v = verts[0];
		const CVec3 *vp = verts + 1;
		for (i = 1; i < numVerts; i++, vp++)
			v.Add(*vp);
		CVec3	av;
		av.Set(fabs(v[0]), fabs(v[1]), fabs(v[2]));
		// Here: v = sum vector, av = abs(v)
		int		axis;
		if (av[0] > av[1] && av[0] > av[2])
			axis = IsNegative(v[0]);
		else if (av[1] > av[2] && av[1] > av[0])
			axis = IsNegative(v[1]) + 2;
		else
			axis = IsNegative(v[2]) + 4;

		// project new texture coords
		for (i = 0; i < numVerts; i++, verts++)
		{
			static const int vecToSt[6][3] = {	// s = [0]/[2], t = [1]/[2]
				{-2, 3, 1},
				{ 2, 3,-1},
				{ 1, 3, 2},
				{-1, 3,-2},
				{-2,-1, 3},
				{-2, 1,-3}
			};

			int		j;

			j = vecToSt[axis][2];
			float dv = (j < 0) ? -(*verts)[-j - 1] : (*verts)[j - 1];
			if (dv < 0.001f) continue;	// don't divide by zero

			j = vecToSt[axis][0];
			float s = (j < 0) ? -(*verts)[-j - 1] / dv : (*verts)[j - 1] / dv;

			j = vecToSt[axis][1];
			float t = (j < 0) ? -(*verts)[-j - 1] / dv : (*verts)[j - 1] / dv;

			EXPAND_BOUNDS(s, skyMins[0][axis], skyMaxs[0][axis]);
			EXPAND_BOUNDS(t, skyMins[1][axis], skyMaxs[1][axis]);
		}
		return;
	}

	int		sides[MAX_CLIP_VERTS];
	float	dists[MAX_CLIP_VERTS];

	static const CVec3 clipNormals[6] = {
		{ 1, 1, 0},
		{ 1,-1, 0},
		{ 0,-1, 1},
		{ 0, 1, 1},
		{ 1, 0, 1},
		{-1, 0, 1}
	};

	bool front = false, back = false;
	const CVec3 &norm = clipNormals[stage];
	for (i = 0; i < numVerts; i++)
	{
		float d = dot(verts[i], norm);
		dists[i] = d;
		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
	}

	if (!front || !back)
	{
		// polygon is not clipped by current plane
		ClipSkyPolygon(verts, numVerts, stage + 1);
		return;
	}

	// clip polygon

	// make a vertex loop for simplification of the algorithm
	sides[numVerts] = sides[0];
	dists[numVerts] = dists[0];
	verts[numVerts] = verts[0];
	// init new polygons
	CVec3	poly1[MAX_CLIP_VERTS], poly2[MAX_CLIP_VERTS];	// new polys
	int		poly1size = 0, poly2size = 0;					// number of verts in new polys

	const CVec3 *vec = verts;
	for (i = 0; i < numVerts; i++, vec++)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			poly1[poly1size++] = *vec;
			break;
		case SIDE_BACK:
			poly2[poly2size++] = *vec;
			break;
		case SIDE_ON:
			poly1[poly1size++] = *vec;
			poly2[poly2size++] = *vec;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;		// line placed on one side of clip plane

		// line intersects clip plane: split line to 2 parts by adding new point
		float d = dists[i] / (dists[i] - dists[i+1]);
		CVec3 tmp;
		Lerp(vec[0], vec[1], d, tmp);
		poly1[poly1size++] = poly2[poly2size++] = tmp;
	}

	// process new polys
	if (poly1size) ClipSkyPolygon(poly1, poly1size, stage + 1);
	if (poly2size) ClipSkyPolygon(poly2, poly2size, stage + 1);
}


/*-----------------------------------------------------------------------------
	Interface
-----------------------------------------------------------------------------*/


#define SKY_TESS_SIZE	4
#define SKY_CELLS		(SKY_TESS_SIZE*2)
#define SKY_VERTS		(SKY_TESS_SIZE*2+1)

#if SKY_VERTS * SKY_VERTS > MAX_VERTEXES
#	error Not enough vertex buffer size: cannot tesselate sky surface
#endif


static byte skySideVisible[6];
static byte skyVis[6][SKY_CELLS*SKY_CELLS];

static bool skyRotated;
static CAxis rotAxis;

void ClearSky()
{
	memset(skyVis, 0, sizeof(skyVis));
	memset(skySideVisible, 0, sizeof(skySideVisible));
	// rotate sky
	float angle = vp.time * gl_skyShader->skyRotate;
	if (angle)
	{
		skyRotated = true;
		BuildRotationAxis(rotAxis, gl_skyShader->skyAxis, angle);
	}
	else
		skyRotated = false;
}


static bool SkyVisible()
{
	int		i;
	byte	*p;
	for (i = 0, p = skyVis[0]; i < sizeof(skyVis); i++, p++)
		if (*p == (SKY_FRUSTUM|SKY_SURF)) return true;
	return false;
}


static void AddSkyTris(const CVec3 *verts, int numVerts, int vertexSize, const int *indexes, int numIndexes, byte flag = SKY_SURF)
{
	guardSlow(AddSkyTris);

	if (gl_state.useFastSky) return;

	// clear bounds for all sky box planes
	int side;
	for (side = 0; side < 6; side++)
	{
		skyMins[0][side] = skyMins[1][side] =  BIG_NUMBER;
		skyMaxs[0][side] = skyMaxs[1][side] = -BIG_NUMBER;
	}
	// add verts to bounds
	CVec3 drawVerts[MAX_CLIP_VERTS]; // buffer for polygon
	int i;
	const CVec3 *src;
	// transform all surface verts
	assert(numVerts <= MAX_CLIP_VERTS);
	for (i = 0, src = verts; i < numVerts; i++, src = OffsetPointer(src, vertexSize))
	{
		if (skyRotated)
			TransformPoint(vp.view.origin, rotAxis, *src, drawVerts[i]);
		else
			VectorSubtract(*src, vp.view.origin, drawVerts[i]);
	}
	// compute surface bounds on skybox
#if 1
	// version for any vertex order
	const int *idx, *idxLast;
	for (idx = indexes, idxLast = indexes + numIndexes; idx < idxLast; idx += 3)
	{
		CVec3 tri[MAX_CLIP_VERTS];
		assert(idx[0] < numVerts && idx[1] < numVerts && idx[2] < numVerts);
		tri[0] = drawVerts[idx[0]];
		tri[1] = drawVerts[idx[1]];
		tri[2] = drawVerts[idx[2]];
		ClipSkyPolygon(tri, 3, 0);
	}
	assert(idx == idxLast);		// should be multiple of 3
#else
	// this version requires special verts order; can be used safely
	//for q1/q2 sky surfaces (our own vertex order)
	ClipSkyPolygon(drawVerts, numVerts, 0);
#endif

	// analyse skyMins/skyMaxs, detect occupied cells
	for (side = 0; side < 6; side++)
	{
		if (skyMins[0][side] > skyMaxs[0][side]) continue;		// not appied to this side

		skySideVisible[side] |= flag;
		// get cell's "x" and "w"
		int x = appFloor((skyMins[0][side] + 1) * SKY_TESS_SIZE);	// left
		int w = appCeil ((skyMaxs[0][side] + 1) * SKY_TESS_SIZE);	// right
		// get cell's "y" and "h"
		int y = appFloor((skyMins[1][side] + 1) * SKY_TESS_SIZE);	// bottom (or top ?)
		int h = appCeil ((skyMaxs[1][side] + 1) * SKY_TESS_SIZE);	// top (or bottom)
#if 1
		x = bound(x, 0, SKY_CELLS);		// avoid precision errors: when we can get floor((mins==-1 + 1)*SIZE) -> -1 (should be 0)
		w = bound(w, 0, SKY_CELLS);
		y = bound(y, 0, SKY_CELLS);
		h = bound(h, 0, SKY_CELLS);
#else
		if (x < 0 || w < 0 || y < 0 || h < 0 ||
			x > SKY_CELLS || y > SKY_CELLS || w > SKY_CELLS || h > SKY_CELLS)
			appError("x/y/w/h: %d %d %d %d\n"
			"mins[%g %g] maxs[%g %g]", x, y, w, h,
			skyMins[0][side], skyMins[1][side], skyMaxs[0][side], skyMaxs[1][side]);
#endif
		w -= x;							// w and h will be always > 0, bacause skyMins[] < skyMaxs[]
		h -= y;
		// fill skyVis rect (x, y, w, h)
		byte *ptr = skyVis[side] + y * SKY_CELLS + x;
		int stride = SKY_CELLS - w;
		for (int i = 0; i < h; i++)
		{
			for (int j = 0; j < w; j++)
				*ptr++ |= flag;
			ptr += stride;
		}
	}

	unguardfSlow(("numVerts=%d", numVerts));
}

#if !NO_DEBUG
static void DrawSkyDebug(const CVec3 *verts, int numVerts, int vertexSize, const int *indexes, int numIndexes, color_t &color)
{
	GL_SetMultitexture(0);
	GL_State(GLSTATE_POLYGON_LINE|GLSTATE_DEPTHWRITE);
	GL_DepthRange(DEPTH_NEAR);
	glDisableClientState(GL_COLOR_ARRAY);
//	glColor3f(0.8f, 0.8f, 1.0f);
	glColor4ubv(color.c);
	GL_CullFace(CULL_NONE);
	glBegin(GL_TRIANGLES);
	for (int i = 0; i < numIndexes; i++)
		glVertex3fv(OffsetPointer(verts, indexes[i] * vertexSize)->v);
	glEnd();

	GL_DepthRange(DEPTH_NORMAL);
}

#define DRAW_SKY_SURF(c)	\
	if (!showDebug)			\
		AddSkyTris(&verts[0].xyz, numVerts, sizeof(verts[0]), indexes, numIndexes); \
	else					\
	{						\
		color_t color;		\
		color.rgba = c;		\
		DrawSkyDebug(&verts[0].xyz, numVerts, sizeof(verts[0]), indexes, numIndexes, color); \
	}

#else

#define DRAW_SKY_SURF		\
		AddSkyTris(&verts[0].xyz, numVerts, sizeof(verts[0]), indexes, numIndexes); \

#endif

void surfaceBase_t::AddToSky(bool showDebug)
{
	DrawTextLeft("bad sky surface", RGB(1,0,0));
}


void surfacePlanar_t::AddToSky(bool showDebug)
{
	DRAW_SKY_SURF(RGB(1,0.5,0.5));
}

// similar to surfacePlanar_t::AddToSky(), but uses different vertex type
void surfaceTrisurf_t::AddToSky(bool showDebug)
{
	DRAW_SKY_SURF(RGB(0.5,0.5,1));
}


/*-----------------------------------------------------------------------------
	Tesselation
-----------------------------------------------------------------------------*/

static float skyDist;

// In: s, t in range [-1..1]; out: tex = {s,t}, vec
static int AddSkyVec(float s, float t, int axis, bufVertex_t *&vec, bufTexCoordSrc_t *&tex)
{
	static const int stToVec[6][3] = {	// 1 = s, 2 = t, 3 = zFar
		{ 3,-1, 2},
		{-3, 1, 2},
		{ 1, 3, 2},
		{-1,-3, 2},
		{-2,-1, 3},		// look straight up
		{ 2,-1,-3}		// look straight down
	};

	CVec3 b;
	b.Set(s * skyDist, t * skyDist, skyDist);

	for (int i = 0; i < 3; i++)
	{
		int tmp = stToVec[axis][i];
		vec->xyz[i] = (tmp < 0) ? -b[-tmp - 1] : b[tmp - 1];
	}

	// texcoords for skybox
	if (gl_skyShader->width)						// default sky shader may have 0 here
	{
		// fix skybox side seams
		float fix = 1.0f - 1.0f / gl_skyShader->width;
		s = (s * fix + 1) / 2;						// [-1,1] -> [0,1]
		s = bound(s, 0, 1);
		t = (1 - t * fix) / 2;						// [-1,1] -> [1,0]
		t = bound(t, 0, 1);
	}
	tex->lm[0] = s;
	tex->lm[1] = t;
	// texcoords for clouds
	//!!!!! change, cache
	//????? should cache vec, tex->lm[] and tex->tex[]
	CVec3 v;
	v = vec->xyz;
	float x = v[0];
	float y = v[1];
	float z = v[2];
	float r = 4096 + gl_skyShader->cloudHeight;		// 4096 - came from q3
	float h = gl_skyShader->cloudHeight;
	if (h < 20) h = 20;
	float r1 = r-h;
	// find intersection of sky sphere with generated vector
	float f = z*z*r1*r1 + (x*x+y*y+z*z)*(2*h*r-h*h);
	v.Scale((-z * r1 + SQRTFAST(f)) / (x*x + y*y + z*z));
	// offset sphere to get polar coordinates
	v[2] += r1;
	// get polar coordinates
	v.NormalizeFast();
	tex->tex[0] = ACOS_FUNC(v[0]);
	tex->tex[1] = ACOS_FUNC(v[1]);

	vec++;
	tex++;

	return gl_numVerts++;
}


static void TesselateSkySide(int side, bufVertex_t *vec, bufTexCoordSrc_t *tex)
{
	guard(TesselateSkySide);

#if 0
	DrawTextLeft(va("side %d:", side));
	for (int k = 0; k < SKY_CELLS; k++)
	{
		byte	*p;
		static const char f[4] = {' ', '.', 'O', 'X'};

		p = skyVis[side] + k * SKY_CELLS;
#define C(x) f[p[x]]
		DrawTextLeft(va("[ %c %c %c %c %c %c %c %c ]", C(0),C(1),C(2),C(3),C(4),C(5),C(6),C(7)), RGB(1,0.5,0.5));
#undef C
		p += SKY_CELLS;
	}
#endif

	gl_numIndexes = 0;
	if (skySideVisible[side] != (SKY_FRUSTUM|SKY_SURF)) return;

	// generate side vertexes
	int grid[SKY_VERTS*SKY_VERTS];
	memset(grid, 0, sizeof(grid));	// "0" is valid, because index=0 only for upper-left vertex, which is used only for 1 cell ...
	byte *ptr = skyVis[side];
	int *grid1 = grid;
	int *grid2 = grid + SKY_VERTS;

	gl_numVerts = 0;
	int *idx = gl_indexesArray;

	for (float t = -1; t < 1; t += 1.0f / SKY_TESS_SIZE)
	{
		for (float s = -1; s < 1; s += 1.0f / SKY_TESS_SIZE, ptr++, grid1++, grid2++)
		{
			if (*ptr != (SKY_FRUSTUM|SKY_SURF)) continue;		// this cell is not visible
			// this 2 verts can be filled by previous line
			if (!grid1[0])	grid1[0] = AddSkyVec(s, t, side, vec, tex);
			if (!grid1[1])	grid1[1] = AddSkyVec(s + 1.0f / SKY_TESS_SIZE, t, side, vec, tex);
			// this vertex can be filled by previous cell
			if (!grid2[0])	grid2[0] = AddSkyVec(s, t + 1.0f / SKY_TESS_SIZE, side, vec, tex);
			// this vertex cannot be filled by previous cells
			grid2[1] = AddSkyVec(s + 1.0f / SKY_TESS_SIZE, t + 1.0f / SKY_TESS_SIZE, side, vec, tex);
			// generate indexes
			// g1(1) ----- g1+1(2)
			//  |           |
			//  |           |
			// g2(3) ----- g2+1(4)
			*idx++ = grid1[0]; *idx++ = grid2[0]; *idx++ = grid1[1];	// triangle 1 (1-3-2)
			*idx++ = grid2[0]; *idx++ = grid2[1]; *idx++ = grid1[1];	// triangle 2 (3-4-2)
			gl_numIndexes += 6;		// overflow is compile-time checked (see "#error" ...)
		}
		// sky verts are not wrapped -- skip seam
		grid1++;
		grid2++;
	}
//	DrawTextLeft(va("side %d:  %d verts  %d idx", side, gl_numVerts, numIndexes));

	unguard;
}


/*-----------------------------------------------------------------------------
	Drawing
-----------------------------------------------------------------------------*/

#define SKY_FRUST_DIST	10				// 1 is not enough - bad FP precision
//#define VISUALIZE_SKY_FRUSTUM		1	// NOTE: SKY_FRUST_DIST should be at least gl_znear->value to make rect visible

// NOTE: currentShader is set to gl_skyShader before calling this function
void DrawSky()
{
	guard(DrawSky);

	LOG_STRING("***** DrawSky() *****");
	if (gl_state.useFastSky) return;

	// build frustum cover
	CVec3	fv[4];
	CVec3	tmp, tmp1, up, right;
	VectorMA(vp.view.origin, SKY_FRUST_DIST, vp.view.axis[0], tmp);
	VectorScale(vp.view.axis[1], SKY_FRUST_DIST * vp.t_fov_x * 1.05, right);	// *1.05 -- to avoid FP precision bugs
	VectorScale(vp.view.axis[2], SKY_FRUST_DIST * vp.t_fov_y * 1.05, up);
#if VISUALIZE_SKY_FRUSTUM
	right.Scale(0.9);
	up.Scale(0.9);
#endif
	VectorAdd(tmp, up, tmp1);				// up
	VectorAdd(tmp1, right, fv[0]);
	VectorSubtract(tmp1, right, fv[1]);
	VectorSubtract(tmp, up, tmp1);			// down
	VectorSubtract(tmp1, right, fv[2]);
	VectorAdd(tmp1, right, fv[3]);
	// rasterize frustum
	static const int frustInds[] = { 0, 1, 3, 1, 2, 3 };
	AddSkyTris(ARRAY_ARG(fv), sizeof(CVec3), ARRAY_ARG(frustInds), SKY_FRUSTUM);

	if (!SkyVisible()) return;				// all sky surfaces are outside frustum

	// draw sky
	shader_t *shader = gl_skyShader;
	shaderStage_t *stage = shader->stages[0];
	assert(gl_skyShader->numStages && stage);

	GL_DepthRange(SHOWSKY ? DEPTH_NEAR : DEPTH_FAR);
	GL_EnableFog(false);
	// if we will add "NODEPTHTEST" if gl_showSky mode -- DEPTHWITE will no effect
	stage->glState = (SHOWSKY == 1) ? GLSTATE_DEPTHWRITE : GLSTATE_NODEPTHTEST;

	glPushMatrix();
	// modify modelview matrix
	glTranslatef(VECTOR_ARG(vp.view.origin));
	if (shader->skyRotate)
		glRotatef(vp.time * shader->skyRotate, VECTOR_ARG(shader->skyAxis));

	skyDist = vp.zFar / 3;					// any non-zero value not works on TNT2 (but works with GeForce2)

	for (int side = 0; side < 6; side++)
	{
		TesselateSkySide(side, vb->verts, srcTexCoord);
		if (!gl_numIndexes) continue;		// no surfaces on this side
		// if gl_skyShader == gl_defaultSkyShader, then skyBox[] will be NULL (disabled texturing)
		if (shader->useSkyBox)
			stage->mapImage[0] = shader->skyBox[side];
		BK_FlushShader();
	}
	glPopMatrix();

#if VISUALIZE_SKY_FRUSTUM
	glPushMatrix();
	glLoadMatrixf(&vp.modelMatrix[0][0]);	// world matrix
	GL_SetMultitexture(0);
	GL_State(GLSTATE_POLYGON_LINE|GLSTATE_DEPTHWRITE);
	GL_DepthRange(DEPTH_NEAR);
	glDisableClientState(GL_COLOR_ARRAY);
	glColor3f(0, 0, 0);
	GL_CullFace(CULL_NONE);
	glBegin(GL_QUADS);
	glVertex3fv(fv[0].v);
	glVertex3fv(fv[1].v);
	glVertex3fv(fv[2].v);
	glVertex3fv(fv[3].v);
	glEnd();
	glPopMatrix();
#endif

	GL_DepthRange(DEPTH_NORMAL);

	unguard;
}


} // namespace
