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
//#include "q_shared2.h"
// we require some ri.func() declarations => some type defines etc
#include "qcommon.h"

#define DEG2RAD(a) ((a*M_PI)/180.0f)

vec3_t vec3_origin = {0, 0, 0};

//============================================================================

float NormalizeColor (const vec3_t in, vec3_t out)
{
	float	m, denom;

	m = max(in[0], in[1]);
	m = max(m, in[2]);

	if (!m)
		VectorClear (out);
	else
	{
		denom = 1.0f / m;
		out[0] = in[0] * denom;
		out[1] = in[1] * denom;
		out[2] = in[2] * denom;
	}
	return m;
}

float NormalizeColor255 (const vec3_t in, vec3_t out)
{
	float	m, denom;

	m = max(in[0], in[1]);
	m = max(m, in[2]);

	if (!m)
		VectorClear (out);
	else
	{
		denom = 255.0f / m;
		out[0] = in[0] * denom;
		out[1] = in[1] * denom;
		out[2] = in[2] * denom;
	}
	return m;
}

float ClampColor255 (const vec3_t in, vec3_t out)
{
	float	m;

	m = max(in[0], in[1]);
	m = max(m, in[2]);

	if (m < 255)
	{
		if (in != out)
			VectorCopy (in, out);
	}
	else
	{
		float	denom;

		denom = 255.0f / m;
		out[0] = in[0] * denom;
		out[1] = in[1] * denom;
		out[2] = in[2] * denom;
	}
	return m;
}

float Q_rsqrt (float number)
{
	long	i;
	float	x2, y;
	const float threehalfs = 1.5f;

	x2 = number * 0.5f;
	y  = number;
	i  = *(long*) &y;						// evil floating point bit level hacking
	i  = 0x5F3759DF - (i >> 1);
	y  = *(float*) &i;
	y  = y * (threehalfs - (x2 * y * y));	// 1st iteration
//	y  = y * (threehalfs - (x2 * y * y));	// 2nd iteration, this can be removed
	return y;
}


void RotatePointAroundVector (vec3_t dst, const vec3_t dir, const vec3_t point, float degrees)
{
	float	m[3][3], im[3][3], zrot[3][3], tmpmat[3][3], rot[3][3];
	int		i;
	vec3_t	vr, vup, vf;
	float	rad;

	vf[0] = dir[0];
	vf[1] = dir[1];
	vf[2] = dir[2];

	PerpendicularVector (vr, dir);
	CrossProduct (vr, vf, vup);

	m[0][0] = vr[0];
	m[1][0] = vr[1];
	m[2][0] = vr[2];

	m[0][1] = vup[0];
	m[1][1] = vup[1];
	m[2][1] = vup[2];

	m[0][2] = vf[0];
	m[1][2] = vf[1];
	m[2][2] = vf[2];

	memcpy (im, m, sizeof(im));

	im[0][1] = m[1][0];
	im[0][2] = m[2][0];
	im[1][0] = m[0][1];
	im[1][2] = m[2][1];
	im[2][0] = m[0][2];
	im[2][1] = m[1][2];

	memset (zrot, 0, sizeof(zrot));
	zrot[0][0] = zrot[1][1] = zrot[2][2] = 1.0f;

	rad = DEG2RAD(degrees);
	zrot[0][0] = zrot[1][1] = cos (rad);
	zrot[0][1] = sin (rad);
	zrot[1][0] = -sin (rad);

	MatrixMultiply (m, zrot, tmpmat);
	MatrixMultiply (tmpmat, im, rot);

	for (i = 0; i < 3; i++)
		dst[i] = rot[i][0] * point[0] + rot[i][1] * point[1] + rot[i][2] * point[2];
}


void AngleVectors (const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	float	angle;
	float	sr, sp, sy, cr, cp, cy;

	if (angles[YAW])
	{
		angle = angles[YAW] * (M_PI*2 / 360);
		sy = sin (angle);
		cy = cos (angle);
	}
	else
	{
		sy = 0;
		cy = 1;
	}

	if (angles[PITCH])
	{
		angle = angles[PITCH] * (M_PI*2 / 360);
		sp = sin (angle);
		cp = cos (angle);
	}
	else
	{
		sp = 0;
		cp = 1;
	}

	if (right || up)
	{
		if (angles[ROLL])
		{
			angle = angles[ROLL] * (M_PI*2 / 360);
			sr = sin (angle);
			cr = cos (angle);
		}
		else
		{
			sr = 0;
			cr = 1;
		}
	}

	if (forward)
	{
		forward[0] = cp*cy;
		forward[1] = cp*sy;
		forward[2] = -sp;
	}
	if (right)
	{
		right[0] = -sr*sp*cy + cr*sy;
		right[1] = -sr*sp*sy - cr*cy;
		right[2] = -sr*cp;
	}
	if (up)
	{
		up[0] = cr*sp*cy + sr*sy;
		up[1] = cr*sp*sy - sr*cy;
		up[2] = cr*cp;
	}
}


void ProjectPointOnPlane (vec3_t dst, const vec3_t p, const vec3_t normal)
{
	float d;
	vec3_t n;
	float inv_denom;

	inv_denom = 1.0f / DotProduct (normal, normal);

	d = DotProduct (normal, p) * inv_denom;

	n[0] = normal[0] * inv_denom;
	n[1] = normal[1] * inv_denom;
	n[2] = normal[2] * inv_denom;

	dst[0] = p[0] - d * n[0];
	dst[1] = p[1] - d * n[1];
	dst[2] = p[2] - d * n[2];
}

// assumes "src" is normalized
void PerpendicularVector (vec3_t dst, const vec3_t src)
{
	int		pos, i;
	float	minelem;
	vec3_t	tempvec;

	// find the smallest magnitude axially aligned vector
	minelem = 1;
	for (i = 0, pos = 0; i < 3; i++)
	{
		if (fabs (src[i]) < minelem)
		{
			pos = i;
			minelem = fabs (src[i]);
		}
	}
	tempvec[0] = tempvec[1] = tempvec[2] = 0;
	tempvec[pos] = 1.0f;

	// project the point onto the plane defined by src
	ProjectPointOnPlane (dst, tempvec, src);

	// normalize the result
	VectorNormalize (dst);
}


// Gives "forward" (normalized!) and makes 2 additional axes for it
void MakeNormalVectors (vec3_t forward, vec3_t right, vec3_t up)
{
	float	d;

	// this rotate and negate guarantees a vector
	// not colinear with the original
	right[1] = -forward[0];
	right[2] = forward[1];
	right[0] = forward[2];

	d = DotProduct (right, forward);
	VectorMA (right, -d, forward, right);
	VectorNormalize (right);
	CrossProduct (right, forward, up);
}


/*
================
MatrixMultiply
================
*/
void MatrixMultiply (float in1[3][3], float in2[3][3], float out[3][3])
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
}


/*
=================
SetPlaneSignbits

for fast BoxOnPlaneSide() test
=================
*/
void SetPlaneSignbits (cplane_t *out)
{
	out->signbits = IsNegative (out->normal[0]) + (IsNegative (out->normal[1]) << 1) + (IsNegative (out->normal[2]) << 2);
}


/*
===============
LerpAngle
===============
*/
//!! incorrect in most cases (should be slerp); used by cl_ents.cpp only
float LerpAngle (float a2, float a1, float frac)
{
	if (a1 - a2 > 180)
		a1 -= 360;
	if (a1 - a2 < -180)
		a1 += 360;
	return a2 + frac * (a1 - a2);
}


float anglemod (float a)
{
#if 0
	if (a >= 0)
		a -= 360 * appFloor (a/360);
	else
		a += 360 * (1 + appFloor (-a/360));
#endif
	a = (360.0f/65536) * (appRound (a*(65536.0f/360)) & 65535);
	return a;
}

//	int		i;
//	vec3_t	corners[2];


/*
// this is the slow, general version
int BoxOnPlaneSide2 (vec3_t emins, vec3_t emaxs, cplane_t *p)
{
	int		i;
	float	dist1, dist2;
	int		sides;
	vec3_t	corners[2];

	for (i=0 ; i<3 ; i++)
	{
		if (p->normal[i] < 0)
		{
			corners[0][i] = emins[i];
			corners[1][i] = emaxs[i];
		}
		else
		{
			corners[1][i] = emins[i];
			corners[0][i] = emaxs[i];
		}
	}
	dist1 = DotProduct (p->normal, corners[0]) - p->dist;
	dist2 = DotProduct (p->normal, corners[1]) - p->dist;
	sides = 0;
	if (dist1 >= 0)
		sides = 1;
	if (dist2 < 0)
		sides |= 2;

	return sides;
}
*/

/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/

int BoxOnPlaneSide (const vec3_t mins, const vec3_t maxs, const cplane_t *p)
{
	float i0 = p->normal[0] * mins[0];
	float a0 = p->normal[0] * maxs[0];
	float i1 = p->normal[1] * mins[1];
	float a1 = p->normal[1] * maxs[1];
	float i2 = p->normal[2] * mins[2];
	float a2 = p->normal[2] * maxs[2];

	float	dist1, dist2;
	switch (p->signbits)
	{
	case 0:
		dist1 = a0 + a1 + a2;
		dist2 = i0 + i1 + i2;
		break;
	case 1:
		dist1 = i0 + a1 + a2;
		dist2 = a0 + i1 + i2;
		break;
	case 2:
		dist1 = a0 + i1 + a2;
		dist2 = i0 + a1 + i2;
		break;
	case 3:
		dist1 = i0 + i1 + a2;
		dist2 = a0 + a1 + i2;
		break;
	case 4:
		dist1 = a0 + a1 + i2;
		dist2 = i0 + i1 + a2;
		break;
	case 5:
		dist1 = i0 + a1 + i2;
		dist2 = a0 + i1 + a2;
		break;
	case 6:
		dist1 = a0 + i1 + i2;
		dist2 = i0 + a1 + a2;
		break;
	case 7:
//	default:	// shut up compiler
		dist1 = i0 + i1 + i2;
		dist2 = a0 + a1 + a2;
		break;
//	default:
//		dist1 = dist2 = 0;		// shut up compiler
//		break;
	}

	int sides = 0;
#if 0
	if (dist1 >= p->dist)
		sides = 1;
	if (dist2 < p->dist)
		sides |= 2;
#else
	//?? faster
	dist1 = p->dist - dist1;
	dist2 = dist2 - p->dist;
	sides = IsNegative (dist1) + (IsNegative (dist2) << 1);
#endif

	return sides;
}

void ClearBounds (vec3_t mins, vec3_t maxs)
{
	mins[0] = mins[1] = mins[2] = BIG_NUMBER;
	maxs[0] = maxs[1] = maxs[2] = -BIG_NUMBER;
}

void AddPointToBounds (const vec3_t v, vec3_t mins, vec3_t maxs)
{
	for (int i = 0; i < 3; i++)
	{
		float val = v[i];
		EXPAND_BOUNDS(val, mins[i], maxs[i]);
	}
}


bool VectorCompare (const vec3_t v1, const vec3_t v2)
{
	if (v1[0] != v2[0] || v1[1] != v2[1] || v1[2] != v2[2])
		return false;

	return true;
}


float VectorNormalize (vec3_t v)
{
	float	length, ilength;

	length = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
	length = sqrt (length);

	if (length)
	{
		ilength = 1.0f / length;
		VectorScale (v, ilength, v);
	}

	return length;
}

float VectorNormalize2 (const vec3_t v, vec3_t out)
{
	float	length, ilength;

	length = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
	length = sqrt (length);

	if (length)
	{
		ilength = 1.0f / length;
		VectorScale (v, ilength, out);
	}

	return length;
}

float VectorNormalizeFast (vec3_t v)
{
	float len2, denom;

	len2 = DotProduct (v, v);
	denom = Q_rsqrt (len2);
	VectorScale (v, denom, v);

	return len2 * denom;
}

void CrossProduct (const vec3_t v1, const vec3_t v2, vec3_t cross)
{
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

float VectorLength (const vec3_t v)
{
	return sqrt (v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

float VectorDistance (const vec3_t vec1, const vec3_t vec2)
{
	vec3_t	vec;

	VectorSubtract (vec1, vec2, vec);
	return VectorLength (vec);
}

void VectorInverse (vec3_t v)
{
	v[0] = -v[0];
	v[1] = -v[1];
	v[2] = -v[2];
}

void VectorScale (const vec3_t in, float scale, vec3_t out)
{
	out[0] = in[0]*scale;
	out[1] = in[1]*scale;
	out[2] = in[2]*scale;
}

void AnglesToAxis (const vec3_t angles, vec3_t axis[3])
{
	vec3_t	right;

	// AngleVectors() returns "right" instead of "y axis"
	AngleVectors (angles, axis[0], right, axis[2]);
	VectorNegate (right, axis[1]);
}

void AxisClear (vec3_t axis[3])
{
	axis[0][0] = 1;
	axis[0][1] = 0;
	axis[0][2] = 0;
	axis[1][0] = 0;
	axis[1][1] = 1;
	axis[1][2] = 0;
	axis[2][0] = 0;
	axis[2][1] = 0;
	axis[2][2] = 1;
}

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

short ShortSwap (short l)
{
	byte b1 = l & 255;
	byte b2 = (l>>8) & 255;

	return (b1<<8) + b2;
}

short ShortNoSwap (short l)
{
	return l;
}

int LongSwap (int l)
{
	byte b1 = l & 255;
	byte b2 = (l>>8) & 255;
	byte b3 = (l>>16) & 255;
	byte b4 = (l>>24) & 255;

	return ((int)b1<<24) + ((int)b2<<16) + ((int)b3<<8) + b4;
}

int	LongNoSwap (int l)
{
	return l;
}

float FloatSwap (float f)
{
	union
	{
		float	f;
		byte	b[4];
	} dat1, dat2;


	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

float FloatNoSwap (float f)
{
	return f;
}

#ifndef LITTLE_ENDIAN

static bool bigendian;

// can't just use function pointers, or dll linkage can
// mess up when qcommon is included in multiple places
short	(*_BigShort) (short l);
short	(*_LittleShort) (short l);
int		(*_LittleLong) (int l);
float	(*_LittleFloat) (float l);

short	BigShort(short l){return _BigShort(l);}
short	LittleShort(short l) {return _LittleShort(l);}
int		LittleLong (int l) {return _LittleLong(l);}
float	LittleFloat (float l) {return _LittleFloat(l);}

/*
================
Swap_Init
================
*/
void Swap_Init (void)
{
	static const byte swaptest[2] = {1,0};

	// set the byte swapping variables in a portable manner
	if ( *(short *)swaptest == 1)
	{
		bigendian = false;
		_BigShort = ShortSwap;
		_LittleShort = ShortNoSwap;
		_LittleLong = LongNoSwap;
		_LittleFloat = FloatNoSwap;
	}
	else
	{
		bigendian = true;
		_BigShort = ShortNoSwap;
		_LittleShort = ShortSwap;
		_LittleLong = LongSwap;
		_LittleFloat = FloatSwap;
	}

}

#endif

static char com_token[MAX_STRING_CHARS];
static int	com_lines;

/*
==============
COM_Parse

Parse a token out of a string
==============
*/
static const char *SkipWhitespace (const char *data, bool *hasNewLines)
{
	int		c;

	while ((c = *data) <= ' ')
	{
		if (!c) return NULL;
		if (c == '\n')
		{
			com_lines++;
			*hasNewLines = true;
		}
		data++;
	}

	return data;
}

char *COM_Parse (const char *&data_p, bool allowLineBreaks)
{
	int		c, len;
	bool	hasNewLines;
	const char *data;

	data = data_p;
	len = c = 0;
	com_token[0] = 0;
	hasNewLines = false;

	if (!data)					// all data is out
		return com_token;		// ""

	while (1)
	{
		// skip whitespace
		data = SkipWhitespace (data, &hasNewLines);
		if (!data)
		{
			data_p = NULL;
			return com_token;
		}
		if (hasNewLines && !allowLineBreaks)
		{
			data_p = data;
			return com_token;
		}

		c = *data;

		// skip double slash comments
		if (c == '/' && data[1] == '/')
		{
			data += 2;
			while (*data && *data != '\n') data++;
		}
		// skip /* */ comments
		else if (c=='/' && data[1] == '*')
		{
			data += 2;
			while (*data && (*data != '*' || data[1] != '/'))
				data++;
			if (*data)
				data += 2;
		}
		else
			break;
	}

	// handle quoted strings
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c == '\"' && *data == '\"')
			{
				// doubled quotes
				data++;				// skip both quotes
				if (len < sizeof(com_token))
					com_token[len++] = c;
				continue;
			}

			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				data_p = (char *) data;
				return com_token;
			}

			if (len < sizeof(com_token))
				com_token[len++] = c;
		}
	}

	// parse a regular word
	do
	{
		if (len < sizeof(com_token))
		{
			com_token[len] = c;
			len++;
		}
		data++;
		c = *data;
		if (c == '\n')
			com_lines++;
	} while (c > 32);

	if (len == sizeof(com_token))
	{
//		Com_WPrintf ("Token exceeded %d chars, discarded.\n", sizeof(com_token));
		len = 0;
	}
	com_token[len] = 0;

	data_p = (char *) data;
	return com_token;
}

const char *COM_QuoteString (const char *str, bool alwaysQuote)
{
	char	*dst, c;

	if (!alwaysQuote)
	{
		const char *s;

		s = str;
		while (c = *s++)
			if (c == ' ' || c == '\"' || c == ';') break;
		if (!c) return str;				// line have no chars, which requires quoting
	}

	dst = com_token;
	*dst++ = '\"';
	while (c = *str++)
	{
		*dst++ = c;
		if (c == '\"') *dst++ = c;
	}
	*dst++ = '\"';
	*dst = 0;

	return com_token;
}


/*
=================
SkipRestOfLine
=================
*/
void SkipRestOfLine (char **data)
{
	char *p = *data;
	while (char c = *p++)
	{
		if (c == '\n')
		{
			com_lines++;
			break;
		}
	}

	*data = p;
}



/*
===============
Com_PageInMemory

===============
*/
int	paged_total;

void Com_PageInMemory (void *buffer, int size)
{
	byte	*buf = (byte*)buffer;
	for (int i=size-1 ; i>0 ; i-=4096)
		paged_total += buf[i];
}
