#ifndef __GL_MATH_INCLUDED__
#define __GL_MATH_INCLUDED__


#include "gl_frontend.h"


namespace OpenGLDrv {


#define TABLE_SIZE	1024
#define TABLE_MASK	(TABLE_SIZE-1)

//?? make tables of size [TABLE_SIZE+1] to avoid some precision out-of-table errors
extern float sinTable[], squareTable[], triangleTable[], sawtoothTable[];
extern float asinTable[];
extern float atanTable[], atanTable2[];

// for float 0..1 == 0..2*pi
#define PERIODIC_FUNC(tbl,val)			tbl[appRound((val)*TABLE_SIZE) & TABLE_MASK]
// for integer 0..range == 0..2*pi
#define PERIODIC_FUNC2(tbl,val,range)	tbl[((val) * (int)(TABLE_SIZE / (range))) & TABLE_MASK]

// NOTE: SIN/COS_FUNC(v) will accept float value in a range 0..1 ~ 0..2*PI
#define SIN_FUNC(val)					PERIODIC_FUNC(sinTable,val)
#define SIN_FUNC2(val,range)			PERIODIC_FUNC2(sinTable,val,range)
#define COS_FUNC(val)					PERIODIC_FUNC(sinTable,(val)+0.25f)
#define COS_FUNC2(val,range)			PERIODIC_FUNC2(sinTable,(val)+(range)/4,range)

// WARNING: these functions are not periodic and not clamped; input value should be exactly in [-1..1] range
// Used (TABLE_SIZE-0.1f) to avoid [TABLE_SIZE*2] index for 1.0f value, should be [TABLE_SIZE*2-1] (OR: use index clamping ??)
#define ASIN_FUNC(val)					( asinTable[appFloor((val) * (TABLE_SIZE-0.1f)) + TABLE_SIZE] )
#define ACOS_FUNC(val)					( M_PI / 2 - asinTable[appFloor((val) * (TABLE_SIZE-0.1f)) + TABLE_SIZE] )


inline float ATAN2_FUNC(float y, float x)
{
#if 0
	if (x == 0) return y > 0 ? M_PI / 2 : - M_PI / 2;
#else
	static const float x0[2] = { M_PI / 2, -M_PI / 2 };
	if (x == 0) return x0[IsNegative(y)];
#endif

	float val = y / x;
#if 0
	float m = 1;
	if (IsNegative(val))
	{
		FNegate(val);
		m = -1;
	}
#else
	unsigned s;
	FAbsSign2(val, val, s);
#endif
	if (val <= 1.0f)
		val = atanTable[appRound(val * (TABLE_SIZE-1))];
	else
		val = atanTable2[appRound(1.0f / val * (TABLE_SIZE-1))];
	FChangeSign(val, s);
#if 0
	if (IsNegative(x))
	{
		if (y > 0)
			val += M_PI;
		else
			val -= M_PI;
	}
#else
	static const float x1[] = { 0, 0, M_PI, -M_PI };
	val += x1[IsNegative(x) * 2 + IsNegative(y)];
#endif
	return val;
}

#define SATURATE(c,l,v) c = l+(c-l)*v; c = bound(c, 0, 255);


extern const float *mathFuncs[];


//!! these tables are unused now
/*extern float sqrtTable[];
extern int   noiseTablei[];
extern float noiseTablef[]; */


void InitFuncTables();	//?? InitMath()
void BuildRotationAxis(CAxis &r, const CVec3 &axis, float angle);
bool GetBoxRect(const refEntity_t *ent, const CVec3 &size2, float mins2[2], float maxs2[2], bool clamp = true);
bool ProjectToScreen(const CVec3 &pos, int scr[2]);


void SaturateColor3f(CVec3 &color);
void SaturateColor4b(color_t *c);


} // namespace

#endif
