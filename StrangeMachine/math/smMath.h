#ifndef SM_MATH_H
#define SM_MATH_H

// #define CGLM_FORCE_LEFT_HANDED
#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include "cglm/cglm.h"
#include "core/smBase.h"

/* https://stackoverflow.com/a/11398748 */
static const u32 log2_tab32[32] = {0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30, 8, 12, 20, 28, 15, 17, 24,
    7, 19, 27, 23, 6, 26, 5, 4, 31};

sm__force_inline u32
fast_log2_32(u32 value)
{
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	return (log2_tab32[(u32)(value * 0x07C4ACDD) >> 27]);
}

static const u32 log_tab64[64] = {63, 0, 58, 1, 59, 47, 53, 2, 60, 39, 48, 27, 54, 33, 42, 3, 61, 51, 37, 40, 49, 18,
    28, 20, 55, 30, 34, 11, 43, 14, 22, 4, 62, 57, 46, 52, 38, 26, 32, 41, 50, 36, 17, 19, 29, 10, 13, 21, 56, 45, 25,
    31, 35, 16, 9, 12, 44, 24, 15, 8, 23, 7, 6, 5};

sm__force_inline u32
fast_log2_64(u64 value)
{
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	value |= value >> 32;
	return (log_tab64[((u64)((value - (value >> 1)) * 0x07EDD5E59A4E28C2)) >> 58]);
}

/* Use the same memory for the anonynous structure and the byte array field `data` */
typedef union v2
{
	struct // Coordinate
	{
		f32 x, y;
	};

	struct // imaginary
	{
		f32 i;
		f32 j;
	};

	struct
	{
		f32 width, height;
	};

	vec2 data;
} v2;

#define v2_print(V)  printf("%s\n\t%f, %f\n", #V, (V).x, (V).y);
#define v2_new(X, Y) ((union v2){.x = (X), .y = (Y)})
#define v2_zero()    v2_new(0.0f, 0.0f)
#define v2_one()     v2_new(1.0f, 1.0f)
#define v2_fill(V)   v2_new(V, V)

/* A vector with 2 int32_t components */
typedef union iv2
{
	struct
	{
		i32 x;
		i32 y;
	};

	ivec2 data;
} iv2;

#define iv2_print(V)  printf("%s\n\t%d, %d\n", #V, (V).x, (V).y);
#define iv2_new(X, Y) ((union iv2){.x = (X), .y = (Y)})
#define iv2_zero()    iv2_new(0, 0)
#define iv2_one()     iv2_new(1, 1)
#define iv2_fill(V)   iv2_new(V, V)

/* A vector with 2 u32 components */
typedef union uiv2
{
	struct
	{
		u32 x;
		u32 y;
	};

	u32 data[2];

} uiv2;

#define uiv2_print(V)  printf("%s\n\t%d, %d\n", #V, (V).x, (V).y);
#define uiv2_new(X, Y) ((union uiv2){.x = (X), .y = (Y)})
#define uiv2_zero()    uiv2_new(0, 0)
#define uiv2_one()     uiv2_new(1, 1)
#define uiv2_fill(V)   uiv2_new(V, V)

/* A vector with 2 b8 components */
typedef union bv2
{
	struct
	{
		b8 x;
		b8 y;
	};

	b8 data[2];

} bv2;

#define bv2_print(V)  printf("%s\n\t%d, %d\n", #V, ((V).x) ? "true" : "false", ((V).y) ? "true" : "false");
#define bv2_new(X, Y) ((union bv2){.x = (X), .y = (Y)})
#define bv2_false()   bv2_new(false, false)
#define bv2_true()    bv2_new(true, true)
#define bv2_fill(V)   bv2_new(V, V)

/* Use the same memory for the anonymous structure and the byte array field `data` */

/* A vector with 3 f32 components */
typedef union v3
{
	struct // 3D coordinates
	{
		f32 x;
		f32 y;
		f32 z;
	};

	struct // RGB channels
	{
		f32 r;
		f32 g;
		f32 b;
	};

	struct // angles
	{
		f32 pitch;
		f32 yaw;
		f32 roll;
	};

	struct // imaginary
	{
		f32 i;
		f32 j;
		f32 k;
	};

	struct
	{
		f32 width;
		f32 height;
		f32 length;
	};

	v2 v2;

	vec3 data;
} v3;

#define v3_print(V)	printf("%s\n\t%f, %f, %f\n", #V, (V).x, (V).y, (V).z)
#define v3_new(X, Y, Z) ((union v3){.x = (X), .y = (Y), .z = (Z)})
#define v3_zero()	v3_new(0.0f, 0.0f, 0.0f)
#define v3_one()	v3_new(1.0f, 1.0f, 1.0f)
#define v3_fill(V)	v3_new(V, V, V)

#define v3_right()    v3_new(1.0f, 0.0f, 0.0f)
#define v3_left()     v3_new(-1.0f, 0.0f, 0.0f)
#define v3_up()	      v3_new(0.0f, 1.0f, 0.0f)
#define v3_down()     v3_new(0.0f, -1.0f, 0.0f)
#define v3_forward()  v3_new(0.0f, 0.0f, 1.0f)
#define v3_backward() v3_new(0.0f, 0.0f, -1.0f)

/* A vector with 3 i32 components */
typedef union iv3
{
	struct // 3D coordinates
	{
		i32 x;
		i32 y;
		i32 z;
	};

	struct
	{
		i32 width;
		i32 height;
		i32 length;
	};

	iv2 iv2;

	ivec3 data;
} iv3;

#define iv3_print(V)	 printf("%s\n\t%d, %d, %d\n", #V, (V).x, (V).y, (V).z)
#define iv3_new(X, Y, Z) ((union iv3){.x = (X), .y = (Y), .z = (Z)})
#define iv3_zero()	 iv3_new(0, 0, 0)
#define iv3_one()	 iv3_new(1, 1, 1)
#define iv3_fill(V)	 iv3_new(V, V, V)

/* A vector with 3 i32 components */
typedef union uiv3
{
	struct // 3D coordinates
	{
		u32 x;
		u32 y;
		u32 z;
	};

	struct
	{
		u32 width;
		u32 height;
		u32 length;
	};

	uiv2 uiv2;

	u32 data[3];
} uiv3;

#define uiv3_print(V)	  printf("%s\n\t%u, %u, %u\n", #V, (V).x, (V).y, (V).z)
#define uiv3_new(X, Y, Z) ((union uiv3){.x = (X), .y = (Y), .z = (Z)})
#define uiv3_zero()	  uiv3_new(0, 0, 0)
#define uiv3_one()	  uiv3_new(1, 1, 1)
#define uiv3_fill(V)	  uiv3_new(V, V, V)

/* A vector with 3 b8 components */
typedef union bv3
{
	struct
	{
		b8 x;
		b8 y;
		b8 z;
	};

	bv2 bv2;

	b8 data[3];

} bv3;

#define bv3_print(V)                                                                             \
	printf("%s\n\t%s, %s, %s\n", #V, ((V).x) ? "true" : "false", ((V).y) ? "true" : "false", \
	    ((V).z) ? "true" : "false");
#define bv3_new(X, Y, Z) ((union bv3){.x = (X), .y = (Y), .z = (Z)})
#define bv3_false()	 bv3_new(false, false, false)
#define bv3_true()	 bv3_new(true, true, true)
#define bv3_fill(V)	 bv3_new(V, V, V)

/* A vector with 4 f32 components */
typedef CGLM_ALIGN_IF(16) union v4
{
	struct // coordinates
	{
		f32 x;
		f32 y;
		f32 z;
		f32 w;
	};

	struct // RGBA channels
	{
		f32 r;
		f32 g;
		f32 b;
		f32 a;
	};

	struct // viewport
	{
		f32 vpx;
		f32 vpy;
		f32 width;
		f32 height;
	};

	v3 v3;
	v2 v2;

	vec4 data;
} v4;

#define v4_print(V)	   printf("%s\n\t%f, %f, %f, %f\n", #V, (V).x, (V).y, (V).z, (V).w);
#define v4_new(X, Y, Z, W) ((union v4){.x = (X), .y = (Y), .z = (Z), .w = (W)})
#define v4_zero()	   v4_new(0.0f, 0.0f, 0.0f, 0.0f)
#define v4_one()	   v4_new(1.0f, 1.0f, 1.0f, 1.0f)
#define v4_fill(V)	   v4_new(V, V, V, V)

/* A vector with 4 int32_t components */
typedef union iv4
{
	struct
	{
		i32 x;
		i32 y;
		i32 z;
		i32 w;
	};

	ivec4 data;
} iv4;

#define iv4_print(V)	    printf("%s\n\t%d, %d, %d, %d\n", #V, (V).x, (V).y, (V).z, (V).w);
#define iv4_new(X, Y, Z, W) ((union iv4){.x = (X), .y = (Y), .z = (Z), .w = (W)})
#define iv4_zero()	    iv4_new(0, 0, 0, 0)
#define iv4_one()	    iv4_new(1, 1, 1, 1)
#define iv4_fill(V)	    iv4_new(V, V, V, V)

/* A vector with 4 u32 components */
typedef union uiv4
{
	struct
	{
		u32 x;
		u32 y;
		u32 z;
		u32 w;
	};

	u32 data[4];
} uiv4;

#define uiv4_print(V)	     printf("%s\n\t%u, %u, %u, %u\n", #V, (V).x, (V).y, (V).z, (V).w);
#define uiv4_new(X, Y, Z, W) ((union uiv4){.x = (X), .y = (Y), .z = (Z), .w = (W)})
#define uiv4_zero()	     uiv4_new(0, 0, 0, 0)
#define uiv4_one()	     uiv4_new(1, 1, 1, 1)
#define uiv4_fill(V)	     uiv4_new(V, V, V, V)

/* A vector with 4 u32 components */
typedef union bv4
{
	struct
	{
		b8 x;
		b8 y;
		b8 z;
		b8 w;
	};

	b8 data[4];
} bv4;

#define bv4_print(V)                                                                                 \
	printf("%s\n\t%s, %s, %s, %s\n", #V, ((V).x) ? "true" : "false", ((V).y) ? "true" : "false", \
	    ((V).z) ? "true" : "false", ((V).w) ? "true" : "false");
#define bv4_new(X, Y, Z, W) ((union uiv4){.x = (X), .y = (Y), .z = (Z), .w = (W)})
#define bv4_false()	    bv4_new(false, false, false, false)
#define bv4_true()	    bv4_new(true, true, true, true)
#define bv4_fill(V)	    bv4_new(V, V, V, V)

/* A matrix with 4x4 f32 components */
typedef CGLM_ALIGN_MAT union m4
{
	struct
	{
		v4 right;
		v4 up;
		v4 forward;
		v4 position;
	} v4;

	struct
	{
		v3 right;	     /* first column */
		f32 padding_rigth;   /* w value of right */
		v3 up;		     /* second column */
		f32 padding_up;	     /* w value of up */
		v3 forward;	     /* third column */
		f32 padding_forward; /* w value of forward */
		v3 position;	     /* fourth column */
		f32 w_position;

	} v3;

	/* basis vector notation */
	struct
	{
		/* col 1 */ f32 xx, xy, xz, xw; /* right */
		/* col 2 */ f32 yx, yy, yz, yw; /* up */
		/* col 3 */ f32 zx, zy, zz, zw; /* forward */
		/* col 4 */ f32 tx, ty, tz, tw; /* position */
	};

	struct // column-row notation
	{
		f32 c0r0, c0r1, c0r2, c0r3;
		f32 c1r0, c1r1, c1r2, c1r3;
		f32 c2r0, c2r1, c2r2, c2r3;
		f32 c3r0, c3r1, c3r2, c3r3;
	};

	struct // row-column notation
	{
		f32 r0c0, r1c0, r2c0, r3c0;
		f32 r0c1, r1c1, r2c1, r3c1;
		f32 r0c2, r1c2, r2c2, r3c2;
		f32 r0c3, r1c3, r2c3, r3c3;
	};

	struct
	{
		f32 m0, m4, m8, m12;  // Matrix first row (4 components)
		f32 m1, m5, m9, m13;  // Matrix second row (4 components)
		f32 m2, m6, m10, m14; // Matrix third row (4 components)
		f32 m3, m7, m11, m15; // Matrix fourth row (4 components)
	};

	mat4 data;
	f32 float16[16];
} m4;

// clang-format off
#define m4_print(M)                                                                  \
	printf("\t%s---------\n"                                                     \
               "\t|%7.3f|%7.3f|%7.3f|%7.3f|\n"                                       \
               "\t|%7.3f|%7.3f|%7.3f|%7.3f|\n"                                       \
	       "\t|%7.3f|%7.3f|%7.3f|%7.3f|\n"                                       \
	       "\t|%7.3f|%7.3f|%7.3f|%7.3f|\n"                                       \
               "\t---------------------------------\n",                              \
	    #M, (M).float16[0],  (M).float16[1],  (M).float16[2],  (M).float16[3],   \
	        (M).float16[4],  (M).float16[5],  (M).float16[6],  (M).float16[7],   \
	        (M).float16[8],  (M).float16[9],  (M).float16[10], (M).float16[11],  \
	        (M).float16[12], (M).float16[13], (M).float16[14], (M).float16[15])

#define m4_new(inXX, inXY, inXZ, inXW, inYX, inYY, inYZ, inYW, inZX, inZY, inZZ, inZW, inTX, inTY, inTZ, inTW) \
	((union m4){                                                                                           \
	    .xx = inXX, .xy = inXY, .xz = inXZ, .xw = inXW,  \
	    .yx = inYX, .yy = inYY, .yz = inYZ, .yw = inYW,  \
	    .zx = inZX, .zy = inZY, .zz = inZZ, .zw = inZW,  \
	    .tx = inTX, .ty = inTY, .tz = inTZ, .tw = inTW,  \
	})

#define m4_zero()                                           \
	((union m4){                                        \
	    .xx = 0.0f, .xy = 0.0f, .xz = 0.0f, .xw = 0.0f, \
	    .yx = 0.0f, .yy = 0.0f, .yz = 0.0f, .yw = 0.0f, \
	    .zx = 0.0f, .zy = 0.0f, .zz = 0.0f, .zw = 0.0f, \
	    .tx = 0.0f, .ty = 0.0f, .tz = 0.0f, .tw = 0.0f, \
	})

#define m4_identity()                                       \
	((union m4){                                        \
	    .xx = 1.0f, .xy = 0.0f, .xz = 0.0f, .xw = 0.0f, \
	    .yx = 0.0f, .yy = 1.0f, .yz = 0.0f, .yw = 0.0f, \
	    .zx = 0.0f, .zy = 0.0f, .zz = 1.0f, .zw = 0.0f, \
	    .tx = 0.0f, .ty = 0.0f, .tz = 0.0f, .tw = 1.0f, \
	})

#define m4_fill(V)                              \
	((union m4){                            \
	    .xx = V, .xy = V, .xz = V, .xw = V, \
	    .yx = V, .yy = V, .yz = V, .yw = V, \
	    .zx = V, .zy = V, .zz = V, .zw = V, \
	    .tx = V, .ty = V, .tz = V, .tw = V, \
	})

// clang-format on

sm__force_inline void
m4_decompose(m4 m, v3 *position, v4 *rotation, v3 *scale)
{
	glm_vec3_copy(m.v3.position.data, position->data);

	m4 rot_matrix;
	glm_decompose_rs(m.data, rot_matrix.data, scale->data);
	glm_mat4_quat(rot_matrix.data, rotation->data);
}

sm__force_inline m4
m4_compose(v3 translation, v4 rotation, v3 scale)
{
	m4 result;

	v4 x, y, z;
	glm_quat_rotatev(rotation.data, v3_right().data, x.data);
	glm_quat_rotatev(rotation.data, v3_up().data, y.data);
	glm_quat_rotatev(rotation.data, v3_forward().data, z.data);

	/* scale the basis vectors */
	glm_vec4_scale(x.data, scale.x, x.data);
	glm_vec4_scale(y.data, scale.y, y.data);
	glm_vec4_scale(z.data, scale.z, z.data);

	result = m4_new(x.x, x.y, x.z, 0.0f,		      // X basis (and scale)
	    y.x, y.y, y.z, 0.0f,			      // Y basis (and scale)
	    z.x, z.y, z.z, 0.0f,			      // Z basis (and scale)
	    translation.x, translation.y, translation.z, 1.0f // Position
	);

	return (result);
}

typedef union trs
{
	struct
	{
		v4 translation;
		v4 rotation;
		v3 scale;
	};

	struct
	{
		v4 T;
		v4 R;
		v3 S;
	};
} trs;

#define trs_identity() \
	((union trs){.translation = v4_zero(), .rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f), .scale = v3_one()})
#define trs_print(T)                                                                                              \
	(printf("%s\n\tT: %f, %f, %f\n\tR: %f, %f, %f, %f\n\tS: %f, %f, %f\n", #T, (T).translation.x,             \
	    (T).translation.y, (T).translation.z, (T).rotation.x, (T).rotation.y, (T).rotation.z, (T).rotation.w, \
	    (T).scale.x, (T).scale.y, (T).scale.z))

sm__force_inline m4
trs_to_m4(trs transform)
{
	m4 result;

	result = m4_compose(transform.translation.v3, transform.rotation, transform.scale);

	return (result);
}

sm__force_inline trs
trs_from_m4(m4 m)
{
	trs result;

	m4_decompose(m, &result.translation.v3, &result.rotation, &result.scale);

	return (result);
}

sm__force_inline v3
m4_v3(m4 m, v3 v)
{
	v4 result;

	result.x = (v.x * m.r0c0) + (v.y * m.r1c0) + (v.z * m.r2c0) + m.r3c0;
	result.y = (v.x * m.r0c1) + (v.y * m.r1c1) + (v.z * m.r2c1) + m.r3c1;
	result.z = (v.x * m.r0c2) + (v.y * m.r1c2) + (v.z * m.r2c2) + m.r3c2;
	result.w = 1 / ((v.x * m.r0c3) + (v.y * m.r1c3) + (v.z * m.r2c3) + m.r3c3);

	return v3_new(result.x * result.w, result.y * result.w, result.z * result.w);
}

sm__force_inline trs
trs_combine(trs a, trs b)
{
	trs result;

	glm_vec3_mul(a.scale.data, b.scale.data, result.scale.data);
	glm_quat_mul(a.rotation.data, b.rotation.data, result.rotation.data);

	v3 pos;
	glm_vec3_mul(a.scale.data, b.translation.v3.data, pos.data);
	glm_quat_rotatev(a.rotation.data, pos.data, result.translation.data);
	glm_vec3_add(a.translation.data, result.translation.data, result.translation.data);

	return (result);
}

sm__force_inline trs
trs_mix(trs a, trs b, f32 t)
{
	trs result;

	glm_vec3_lerp(a.translation.v3.data, b.translation.v3.data, t, result.translation.v3.data);
	glm_quat_nlerp(a.rotation.data, b.rotation.data, t, result.rotation.data);
	glm_vec3_lerp(a.scale.data, b.scale.data, t, result.scale.data);

	return (result);
}

sm__force_inline trs
trs_inverse(trs t)
{
	trs result;

	glm_quat_inv(t.rotation.data, result.rotation.data);

	result.scale.x = fabsf(t.scale.x) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.x;
	result.scale.y = fabsf(t.scale.y) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.y;
	result.scale.z = fabsf(t.scale.z) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.z;

	v3 inv_trans;
	glm_vec3_scale(t.translation.data, -1.0f, inv_trans.data);
	glm_vec3_mul(result.scale.data, inv_trans.data, inv_trans.data);
	glm_quat_rotatev(result.rotation.data, inv_trans.data, result.translation.v3.data);

	return (result);
}

sm__force_inline trs
trs_lookat(v3 position, v3 target, v3 up)
{
	trs result;

	result.translation.v3 = position;
	glm_quat_forp(position.data, target.data, up.data, result.rotation.data);
	result.scale = v3_one();

	return (result);
}

// sm__force_inline trs
// trs_lookat_trs(trs t)
// {
// 	trs result;
//
// 	return (result);
// }

sm__force_inline v3
trs_point(trs t, v3 p)
{
	v3 result;

	// Scale -> Rotation -> Translation

	glm_vec3_mul(t.scale.data, p.data, result.data);
	glm_quat_rotatev(t.rotation.data, result.data, result.data);
	glm_vec3_add(t.translation.data, result.data, result.data);

	return (result);
}

sm__force_inline v3
trs_v3(trs t, v3 v)
{
	v3 result;

	glm_vec3_mul(t.scale.data, v.data, result.data);
	glm_quat_rotatev(t.rotation.data, result.data, result.data);

	return (result);
}

sm__force_inline v3
trs_get_right(trs t)
{
	v3 result;

	glm_quat_rotatev(t.rotation.data, v3_right().data, result.data);

	return (result);
}

sm__force_inline v3
trs_get_left(trs t)
{
	v3 result;

	glm_quat_rotatev(t.rotation.data, v3_left().data, result.data);

	return (result);
}

sm__force_inline v3
trs_get_up(trs t)
{
	v3 result;

	glm_quat_rotatev(t.rotation.data, v3_up().data, result.data);

	return (result);
}

sm__force_inline v3
trs_get_down(trs t)
{
	v3 result;

	glm_quat_rotatev(t.rotation.data, v3_down().data, result.data);

	return (result);
}

sm__force_inline v3
trs_get_forward(trs t)
{
	v3 result;

	glm_quat_rotatev(t.rotation.data, v3_forward().data, result.data);

	return (result);
}

sm__force_inline v3
trs_get_backward(trs t)
{
	v3 result;

	glm_quat_rotatev(t.rotation.data, v3_backward().data, result.data);

	return (result);
}

sm__force_inline v4
quat_from_euler_angles(f32 pitch, f32 yaw, f32 roll)
{
	v4 result;

#if 0
	v4 qx, qy, qz;
	glm_quatv(qz.data, roll, v3_forward().data);
	glm_quatv(qx.data, pitch, v3_right().data);
	glm_quatv(qy.data, yaw, v3_up().data);

	glm_quat_mul(qy.data, qx.data, result.data);
	glm_quat_mul(result.data, qz.data, result.data);

	return (result);
#endif

	f32 h_roll = roll * 0.5f;
	f32 h_pitch = pitch * 0.5f;
	f32 h_yaw = yaw * 0.5f;

	f32 s_roll = sin(h_roll);
	f32 c_roll = cos(h_roll);
	f32 s_pitch = sin(h_pitch);
	f32 c_pitch = cos(h_pitch);
	f32 s_yaw = sin(h_yaw);
	f32 c_yaw = cos(h_yaw);

	result = v4_new(c_yaw * s_pitch * c_roll + s_yaw * c_pitch * s_roll,
	    s_yaw * c_pitch * c_roll - c_yaw * s_pitch * s_roll, c_yaw * c_pitch * s_roll - s_yaw * s_pitch * c_roll,
	    c_yaw * c_pitch * c_roll + s_yaw * s_pitch * s_roll);

	return (result);
}

// http://www.geometrictools.com/Documentation/EulerAngles.pdf
sm__force_inline v3
quat_to_euler_angles(v4 q)
{
	// ZXY
	v3 result;
	f32 r21 = 2.0f * (-q.y * q.z + q.w * q.x);

	if (r21 < 1.0f)
	{
		if (r21 > -1.0f)
		{
			result = v3_new(asinf(r21),
			    atan2f(2.0f * (q.x * q.z + q.w * q.y), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)),
			    atan2f(2.0f * (q.x * q.y + q.w * q.z), 1.0f - 2.0f * (q.x * q.x + q.z * q.z)));
		}
		else
		{
			result = v3_new(-GLM_PI_2f, 0.0f,
			    -atan2f(2.0f * (q.x * q.z - q.w * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z)));
		}
	}
	else
	{
		result = v3_new(
		    GLM_PI_2f, 0.0f, atan2f(2.0f * (q.x * q.z - q.w * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z)));
	}

	return (result);
}

typedef union color
{
	u32 hex;
	u8 rgba[4];

	struct
	{
		u8 r, g, b, a;
	};
} color;

#define color_print(V)	      printf("%s\n\t0x%x\n", #V, V.hex);
#define color_new(R, G, B, A) ((union color){.r = R, .g = G, .b = B, .a = A})

#define color_from_v4(V4)            \
	((union color){              \
	    .r = (u8)((V4).r * 255), \
	    .g = (u8)((V4).g * 255), \
	    .b = (u8)((V4).b * 255), \
	    .a = (u8)((V4).a * 255), \
	})

#define color_from_v3(V3)            \
	((union color){              \
	    .r = (u8)((V3).r * 255), \
	    .g = (u8)((V3).g * 255), \
	    .b = (u8)((V3).b * 255), \
	    .a = 255,                \
	})

#define color_to_v4(COLOR) (v4_new((COLOR).r / 255.f, (COLOR).g / 255.f, (COLOR).b / 255.f, (COLOR).a / 255.f))
#define color_to_v3(COLOR) (v3_new((COLOR).r / 255.f, (COLOR).g / 255.f, (COLOR).b / 255.f))

#define color_from_hex(HEX)                  \
	((union color){                      \
	    .r = (u8)(((HEX) >> 24) & 0xFF), \
	    .g = (u8)(((HEX) >> 16) & 0xFF), \
	    .b = (u8)(((HEX) >> 8) & 0xFF),  \
	    .a = (u8)((HEX)&0xFF),           \
	})

sm__force_inline color
color_lerp(color color1, color color2, f32 frac)
{
	return (color){
	    .r = (u8)((color2.r - color1.r) * frac) + color1.r,
	    .g = (u8)((color2.g - color1.g) * frac) + color1.g,
	    .b = (u8)((color2.b - color1.b) * frac) + color1.b,
	    .a = (u8)((color2.a - color1.a) * frac) + color1.a,
	};
}

sm__force_inline u32
color_lerp_hex(u32 color1, u32 color2, f32 frac)
{
	u8 r1 = (u8)(color1 >> 24) & 0xFF;
	u8 r2 = (u8)(color2 >> 24) & 0xFF;
	u8 g1 = (u8)(color1 >> 16) & 0xFF;
	u8 g2 = (u8)(color2 >> 16) & 0xFF;
	u8 b1 = (u8)(color1 >> 8) & 0xFF;
	u8 b2 = (u8)(color2 >> 8) & 0xFF;
	u8 a1 = (u8)(color1)&0xFF;
	u8 a2 = (u8)(color2)&0xFF;

	u8 R = (u8)((r2 - r1) * frac) + r1;
	u8 G = (u8)((g2 - g1) * frac) + g1;
	u8 B = (u8)((b2 - b1) * frac) + b1;
	u8 A = (u8)((a2 - a1) * frac) + a1;

	return (u32)R << 24 | (u32)G << 16 | (u32)B << 8 | (u32)A;
}

#define cWHITE		    ((union color){.r = 255, .g = 255, .b = 255, .a = 255})
#define cRED		    ((union color){.r = 255, .g = 0, .b = 0, .a = 255})
#define cGREEN		    ((union color){.r = 0, .g = 255, .b = 0, .a = 255})
#define cBLUE		    ((union color){.r = 0, .g = 0, .b = 255, .a = 255})
#define cMAGENTA	    ((union color){.r = 255, .g = 0, .b = 255, .a = 255})
#define cCYAN		    ((union color){.r = 0, .g = 255, .b = 255, .a = 255})
#define cYELLOW		    ((union color){.r = 255, .g = 255, .b = 0, .a = 255})
#define cBLACK		    ((union color){.r = 0, .g = 0, .b = 0, .a = 255})
#define cGRAY		    ((union color){.r = 127, .g = 127, .b = 127, .a = 255})
#define cAQUAMARINE	    ((union color){.r = 112, .g = 219, .b = 147, .a = 255})
#define cBAKERCHOCOLATE	    ((union color){.r = 92, .g = 51, .b = 23, .a = 255})
#define cBLUEVIOLET	    ((union color){.r = 159, .g = 95, .b = 159, .a = 255})
#define cBRASS		    ((union color){.r = 181, .g = 166, .b = 66, .a = 255})
#define cBRIGHTGOLD	    ((union color){.r = 217, .g = 217, .b = 25, .a = 255})
#define cBROWN		    ((union color){.r = 166, .g = 42, .b = 42, .a = 255})
#define cBRONZE		    ((union color){.r = 140, .g = 120, .b = 83, .a = 255})
#define cBRONZEII	    ((union color){.r = 166, .g = 125, .b = 61, .a = 255})
#define cCADETBLUE	    ((union color){.r = 95, .g = 159, .b = 159, .a = 255})
#define cCOOLCOPPER	    ((union color){.r = 217, .g = 135, .b = 25, .a = 255})
#define cCOPPER		    ((union color){.r = 184, .g = 115, .b = 51, .a = 255})
#define cCORAL		    ((union color){.r = 255, .g = 127, .b = 0, .a = 255})
#define cCORNFLOWERBLUE	    ((union color){.r = 66, .g = 66, .b = 111, .a = 255})
#define cDARKBROWN	    ((union color){.r = 92, .g = 64, .b = 51, .a = 255})
#define cDARKGREEN	    ((union color){.r = 47, .g = 79, .b = 47, .a = 255})
#define cDARKGREENCOPPER    ((union color){.r = 74, .g = 118, .b = 110, .a = 255})
#define cDARKOLIVEGREEN	    ((union color){.r = 79, .g = 79, .b = 47, .a = 255})
#define cDARKORCHID	    ((union color){.r = 153, .g = 50, .b = 205, .a = 255})
#define cDARKPURPLE	    ((union color){.r = 135, .g = 31, .b = 120, .a = 255})
#define cDARKSLATEBLUE	    ((union color){.r = 107, .g = 35, .b = 142, .a = 255})
#define cDARKSLATEGREY	    ((union color){.r = 47, .g = 79, .b = 79, .a = 255})
#define cDARKTAN	    ((union color){.r = 151, .g = 105, .b = 79, .a = 255})
#define cDARKTURQUOISE	    ((union color){.r = 112, .g = 147, .b = 219, .a = 255})
#define cDARKWOOD	    ((union color){.r = 133, .g = 94, .b = 66, .a = 255})
#define cDIMGREY	    ((union color){.r = 84, .g = 84, .b = 84, .a = 255})
#define cDUSTYROSE	    ((union color){.r = 133, .g = 99, .b = 99, .a = 255})
#define cFELDSPAR	    ((union color){.r = 209, .g = 146, .b = 117, .a = 255})
#define cFIREBRICK	    ((union color){.r = 142, .g = 35, .b = 35, .a = 255})
#define cFORESTGREEN	    ((union color){.r = 35, .g = 142, .b = 35, .a = 255})
#define cGOLD		    ((union color){.r = 205, .g = 127, .b = 50, .a = 255})
#define cGOLDENROD	    ((union color){.r = 219, .g = 219, .b = 112, .a = 255})
#define cGREY		    ((union color){.r = 192, .g = 192, .b = 192, .a = 255})
#define cGREENCOPPER	    ((union color){.r = 82, .g = 127, .b = 118, .a = 255})
#define cGREENYELLOW	    ((union color){.r = 147, .g = 219, .b = 112, .a = 255})
#define cHUNTERGREEN	    ((union color){.r = 33, .g = 94, .b = 33, .a = 255})
#define cINDIANRED	    ((union color){.r = 78, .g = 47, .b = 47, .a = 255})
#define cKHAKI		    ((union color){.r = 159, .g = 159, .b = 95, .a = 255})
#define cLIGHTBLUE	    ((union color){.r = 192, .g = 217, .b = 217, .a = 255})
#define cLIGHTGREY	    ((union color){.r = 168, .g = 168, .b = 168, .a = 255})
#define cLIGHTSTEELBLUE	    ((union color){.r = 143, .g = 143, .b = 189, .a = 255})
#define cLIGHTWOOD	    ((union color){.r = 233, .g = 194, .b = 166, .a = 255})
#define cLIMEGREEN	    ((union color){.r = 50, .g = 205, .b = 50, .a = 255})
#define cMANDARIANORANGE    ((union color){.r = 228, .g = 120, .b = 51, .a = 255})
#define cMAROON		    ((union color){.r = 142, .g = 35, .b = 107, .a = 255})
#define cMEDIUMAQUAMARINE   ((union color){.r = 50, .g = 205, .b = 153, .a = 255})
#define cMEDIUMBLUE	    ((union color){.r = 50, .g = 50, .b = 205, .a = 255})
#define cMEDIUMFORESTGREEN  ((union color){.r = 107, .g = 142, .b = 35, .a = 255})
#define cMEDIUMGOLDENROD    ((union color){.r = 234, .g = 234, .b = 174, .a = 255})
#define cMEDIUMORCHID	    ((union color){.r = 147, .g = 112, .b = 219, .a = 255})
#define cMEDIUMSEAGREEN	    ((union color){.r = 66, .g = 111, .b = 66, .a = 255})
#define cMEDIUMSLATEBLUE    ((union color){.r = 127, .g = 0, .b = 255, .a = 255})
#define cMEDIUMSPRINGGREEN  ((union color){.r = 127, .g = 255, .b = 0, .a = 255})
#define cMEDIUMTURQUOISE    ((union color){.r = 112, .g = 219, .b = 219, .a = 255})
#define cMEDIUMVIOLETRED    ((union color){.r = 219, .g = 112, .b = 147, .a = 255})
#define cMEDIUMWOOD	    ((union color){.r = 166, .g = 128, .b = 100, .a = 255})
#define cMIDNIGHTBLUE	    ((union color){.r = 47, .g = 47, .b = 79, .a = 255})
#define cNAVYBLUE	    ((union color){.r = 35, .g = 35, .b = 142, .a = 255})
#define cNEONBLUE	    ((union color){.r = 77, .g = 77, .b = 255, .a = 255})
#define cNEONPINK	    ((union color){.r = 255, .g = 110, .b = 199, .a = 255})
#define cNEWMIDNIGHTBLUE    ((union color){.r = 0, .g = 0, .b = 156, .a = 255})
#define cNEWTAN		    ((union color){.r = 235, .g = 199, .b = 158, .a = 255})
#define cOLDGOLD	    ((union color){.r = 207, .g = 181, .b = 59, .a = 255})
#define cORANGE		    ((union color){.r = 255, .g = 127, .b = 0, .a = 255})
#define cORANGERED	    ((union color){.r = 255, .g = 36, .b = 0, .a = 255})
#define cORCHID		    ((union color){.r = 219, .g = 112, .b = 219, .a = 255})
#define cPALEGREEN	    ((union color){.r = 143, .g = 188, .b = 143, .a = 255})
#define cPINK		    ((union color){.r = 188, .g = 143, .b = 143, .a = 255})
#define cPLUM		    ((union color){.r = 234, .g = 173, .b = 234, .a = 255})
#define cQUARTZ		    ((union color){.r = 217, .g = 217, .b = 243, .a = 255})
#define cRICHBLUE	    ((union color){.r = 89, .g = 89, .b = 171, .a = 255})
#define cSALMON		    ((union color){.r = 111, .g = 66, .b = 66, .a = 255})
#define cSCARLET	    ((union color){.r = 140, .g = 23, .b = 23, .a = 255})
#define cSEAGREEN	    ((union color){.r = 35, .g = 142, .b = 104, .a = 255})
#define cSEMISWEETCHOCOLATE ((union color){.r = 107, .g = 66, .b = 38, .a = 255})
#define cSIENNA		    ((union color){.r = 142, .g = 107, .b = 35, .a = 255})
#define cSILVER		    ((union color){.r = 230, .g = 232, .b = 250, .a = 255})
#define cSKYBLUE	    ((union color){.r = 50, .g = 153, .b = 204, .a = 255})
#define cSLATEBLUE	    ((union color){.r = 0, .g = 127, .b = 255, .a = 255})
#define cSPICYPINK	    ((union color){.r = 255, .g = 28, .b = 174, .a = 255})
#define cSPRINGGREEN	    ((union color){.r = 0, .g = 255, .b = 127, .a = 255})
#define cSTEELBLUE	    ((union color){.r = 35, .g = 107, .b = 142, .a = 255})
#define cSUMMERSKY	    ((union color){.r = 56, .g = 176, .b = 222, .a = 255})
#define cTAN		    ((union color){.r = 219, .g = 147, .b = 112, .a = 255})
#define cTHISTLE	    ((union color){.r = 216, .g = 191, .b = 216, .a = 255})
#define cTURQUOISE	    ((union color){.r = 173, .g = 234, .b = 234, .a = 255})
#define cVERYDARKBROWN	    ((union color){.r = 92, .g = 64, .b = 51, .a = 255})
#define cVERYLIGHTGREY	    ((union color){.r = 205, .g = 205, .b = 205, .a = 255})
#define cVIOLET		    ((union color){.r = 79, .g = 47, .b = 79, .a = 255})
#define cVIOLETRED	    ((union color){.r = 204, .g = 50, .b = 153, .a = 255})
#define cWHEAT		    ((union color){.r = 216, .g = 216, .b = 191, .a = 255})
#define cYELLOWGREEN	    ((union color){.r = 153, .g = 204, .b = 50, .a = 255})

sm__static_assert(sizeof(v2) == sizeof(vec2), "v2 and vec2 mismatch size");
sm__static_assert(sizeof(v3) == sizeof(vec3), "v3 and vec3 mismatch size");
sm__static_assert(sizeof(v4) == sizeof(vec4), "v4 and vec4 mismatch size");
sm__static_assert(sizeof(m4) == sizeof(mat4), "m4 and mat4 mismatch size");

#endif // SM_MATH_H
