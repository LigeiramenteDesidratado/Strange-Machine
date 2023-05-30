#ifndef SM_MATH_H
#define SM_MATH_H

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
	struct
	{
		f32 x, y;
	};

	struct
	{
		f32 width, height;
	};

	vec2 data;
} v2;

#define v2_print(V)  printf("%s\n\t%f, %f\n", #V, V.x, V.y);
#define v2_new(X, Y) ((union v2){.x = X, .y = Y})
#define v2_zero()    v2_new(0.0f, 0.0f)
#define v2_one()     v2_new(1.0f, 1.0f)

/* A vector with 2 int32_t components */
typedef union iv2
{
	struct
	{
		i32 x;
		i32 y;
	};

	i32 data[2];
} iv2;

#define iv2_print(V)  printf("%s\n\t%d, %d\n", #V, V.x, V.y);
#define iv2_new(X, Y) ((union iv2){.x = X, .y = Y})
#define iv2_zero()    iv2_new(0, 0)
#define iv2_one()     iv2_new(1, 1)

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

#define uiv2_print(V)  printf("%s\n\t%d, %d\n", #V, V.x, V.y);
#define uiv2_new(X, Y) ((union uiv2){.x = X, .y = Y})
#define uiv2_zero()    uiv2_new(0, 0)
#define uiv2_one()     uiv2_new(1, 1)

/* Use the same memory for the anonymous structure and the byte array field `data` */

/* A vector with 3 f32 components */
typedef union v3
{
	struct
	{
		f32 x;
		f32 y;
		f32 z;
	};

	struct
	{
		f32 r;
		f32 g;
		f32 b;
	};

	struct
	{
		f32 pitch;
		f32 yaw;
		f32 roll;
	};

	v2 v2;

	vec3 data;
} v3;

#define v3_print(V)	printf("%s\n\t%f, %f, %f\n", #V, V.x, V.y, V.z)
#define v3_new(X, Y, Z) ((union v3){.x = X, .y = Y, .z = Z})
#define v3_zero()	v3_new(0.0f, 0.0f, 0.0f)
#define v3_one()	v3_new(1.0f, 1.0f, 1.0f)
#define v3_right()	v3_new(1.0f, 0.0f, 0.0f)
#define v3_up()		v3_new(0.0f, 1.0f, 0.0f)
#define v3_forward()	v3_new(0.0f, 0.0f, 1.0f)

/* A vector with 4 f32 components */
typedef CGLM_ALIGN_IF(16) union v4
{
	struct
	{
		f32 x;
		f32 y;
		f32 z;
		f32 w;
	};

	struct
	{
		f32 r;
		f32 g;
		f32 b;
		f32 a;
	};

	v2 v2;
	v3 v3;

	vec4 data;
} v4;

#define v4_print(V)	   printf("%s\n\t%f, %f, %f, %f\n", #V, V.x, V.y, V.z, V.w);
#define v4_new(X, Y, Z, W) ((union v4){.x = X, .y = Y, .z = Z, .w = W})
#define v4_zero()	   v4_new(0.0f, 0.0f, 0.0f, 0.0f)
#define v4_one()	   v4_new(1.0f, 1.0f, 1.0f, 1.0f)

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

	i32 data[4];
} iv4;

#define iv4_print(V)	    printf("%s\n\t%d, %d, %d, %d\n", #V, V.x, V.y, V.z, V.w);
#define iv4_new(X, Y, Z, W) ((union iv4){.x = X, .y = Y, .z = Z, .w = W})
#define iv4_zero()	    iv4_new(0, 0, 0, 0)
#define iv4_one()	    iv4_new(1, 1, 1, 1)

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

#define uiv4_print(V)	     printf("%s\n\t%d, %d, %d, %d\n", #V, V.x, V.y, V.z, V.w);
#define uiv4_new(X, Y, Z, W) ((union uiv4){.x = X, .y = Y, .z = Z, .w = W})
#define uiv4_zero()	     uiv4_new(0, 0, 0, 0)
#define uiv4_one()	     uiv4_new(1, 1, 1, 1)

/* A matrix with 4x4 f32 components */
typedef CGLM_ALIGN_MAT union m4
{
	struct
	{
		v4 right;    /* first column */
		v4 up;	     /* second column */
		v4 forward;  /* third column */
		v4 position; /* fourth column */
	} vec4;

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

	} vec3;

	/* basis vector notation */
	struct
	{
		/* col 1 */ f32 xx, xy, xz, xw; /* right */
		/* col 2 */ f32 yx, yy, yz, yw; /* up */
		/* col 3 */ f32 zx, zy, zz, zw; /* forward */
		/* col 4 */ f32 tx, ty, tz, tw; /* position */
	};

	/* column-row notation */
	struct
	{
		f32 c0r0, c0r1, c0r2, c0r3;
		f32 c1r0, c1r1, c1r2, c1r3;
		f32 c2r0, c2r1, c2r2, c2r3;
		f32 c3r0, c3r1, c3r2, c3r3;
	};

	/* row-column notation */
	struct
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
#define m4_print(M)                                                       \
	printf("\t%s---------\n"                                          \
               "\t|%7.3f|%7.3f|%7.3f|%7.3f|\n"                            \
               "\t|%7.3f|%7.3f|%7.3f|%7.3f|\n"                            \
	       "\t|%7.3f|%7.3f|%7.3f|%7.3f|\n"                            \
	       "\t|%7.3f|%7.3f|%7.3f|%7.3f|\n"                            \
               "\t---------------------------------\n",                   \
	    #M, M.float16[0], M.float16[1], M.float16[2], M.float16[3],   \
	        M.float16[4], M.float16[5], M.float16[6], M.float16[7],   \
	        M.float16[8], M.float16[9], M.float16[10], M.float16[11], \
	        M.float16[12], M.float16[13], M.float16[14], M.float16[15])

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

// clang-format on

struct trs
{
	v4 position;
	v4 rotation;
	v3 scale;
};

#define trs_identity() \
	((struct trs){.position = v4_zero(), .rotation = v4_new(0.0f, 0.0f, 0.0f, 1.0f), .scale = v3_one()})
#define trs_print(T)                                                                                                 \
	(printf("%s\n\tposition: %f, %f, %f\n\trotation: %f, %f, %f, %f\n\tscale: %f, %f, %f\n", #T, (T).position.x, \
	    (T).position.y, (T).position.z, (T).rotation.x, (T).rotation.y, (T).rotation.z, (T).rotation.w,          \
	    (T).scale.x, (T).scale.y, (T).scale.z))

sm__force_inline m4
trs_to_m4(struct trs transform)
{
	m4 result;

	/* first, extract the rotation basis of the transform */
	versor q = {transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w};

	v4 x, y, z;
	glm_quat_rotatev(q, v3_right().data, x.data);
	glm_quat_rotatev(q, v3_up().data, y.data);
	glm_quat_rotatev(q, v3_forward().data, z.data);

	/* next, scale the basis vectors */
	glm_vec4_scale(x.data, transform.scale.x, x.data);
	glm_vec4_scale(y.data, transform.scale.y, y.data);
	glm_vec4_scale(z.data, transform.scale.z, z.data);

	result = m4_new(x.x, x.y, x.z, 0.0f,					   // X basis (and scale)
	    y.x, y.y, y.z, 0.0f,						   // Y basis (and scale)
	    z.x, z.y, z.z, 0.0f,						   // Z basis (and scale)
	    transform.position.x, transform.position.y, transform.position.z, 1.0f // Position
	);

	return (result);
}

sm__force_inline v4
quat_look_rotation(v3 direction, v3 up)
{
	// find orhonormal basis vectors
	v3 f, u, r;
	glm_vec3_normalize_to(direction.data, f.data); // object forward
	glm_vec3_normalize_to(up.data, u.data);	       // desired up
	glm_vec3_cross(u.data, f.data, r.data);	       // object right
						       //
	glm_vec3_cross(f.data, r.data, u.data);	       // object up

	glm_vec3_normalize(f.data);

	// from world forward to object forward
	v4 f2d;
	glm_quat_from_vecs(v3_new(0, 0, 1).data, f.data, f2d.data);

	// what direction is the new object up?
	v3 object_up;
	glm_quat_rotatev(f2d.data, v3_up().data, object_up.data);
	glm_vec3_normalize(object_up.data);

	// from object up to desired up
	// quat u2u = quat_from_to(object_up, u);

	v4 u2u;
	glm_quat_from_vecs(object_up.data, u.data, u2u.data);

	// rotate to forward direction first
	// then twist to correct up
	v4 result;
	glm_quat_mul(u2u.data, f2d.data, result.data);

	// don't forget to normalize the result
	glm_quat_normalize(result.data);

	return (result);
}

sm__force_inline v4
m4_to_quat(m4 m)
{
	v3 up, forward, right;
	glm_vec3_normalize_to(m.vec3.up.data, up.data);
	glm_vec3_normalize_to(m.vec3.forward.data, forward.data);

	glm_vec3_cross(up.data, forward.data, right.data);
	glm_vec3_cross(forward.data, right.data, up.data);

	return quat_look_rotation(forward, up);
}

sm__force_inline struct trs
trs_from_m4(m4 m)
{
	struct trs result;
	glm_vec3_copy(m.vec3.position.data, result.position.data); // rotation = first column of mat
	result.rotation = m4_to_quat(m);

	/* get the rotate scale matrix, then estimate the scale from that */
	m4 rot_scale_mat;
	glm_mat4_identity(rot_scale_mat.data);

	rot_scale_mat = m4_new(m.float16[0], m.float16[1], m.float16[2], 0, m.float16[4], m.float16[5], m.float16[6], 0,
	    m.float16[8], m.float16[9], m.float16[10], 0, 0, 0, 0, 1);

	versor q;
	glm_quat_inv(result.rotation.data, q);
	m4 inv_rot_mat;
	glm_quat_mat4(q, inv_rot_mat.data);
	m4 scale_mat;
	glm_mat4_mul(rot_scale_mat.data, inv_rot_mat.data, scale_mat.data);

	/* the diagonal of the scale matrix is the scale */
	result.scale.data[0] = scale_mat.c0r0;
	result.scale.data[1] = scale_mat.c1r1;
	result.scale.data[2] = scale_mat.c2r2;

	/* out.position.data[3] = 1.0f; */
	/* out.scale.data[3] = 1.0f; */

	return (result);
}

sm__force_inline struct trs
trs_combine(struct trs a, struct trs b)
{
	struct trs result;

	glm_vec3_mul(a.scale.data, b.scale.data, result.scale.data);
	glm_quat_mul(a.rotation.data, b.rotation.data, result.rotation.data);

	v3 pos;
	glm_vec3_mul(a.scale.data, b.position.v3.data, pos.data);
	glm_quat_rotatev(a.rotation.data, pos.data, result.position.data);
	glm_vec3_add(a.position.data, result.position.data, result.position.data);

	// transform_set_dirty(&result, true);

	return (result);
}

sm__force_inline struct trs
trs_mix(struct trs a, struct trs b, f32 t)
{
	struct trs result;

	glm_vec3_lerp(a.position.v3.data, b.position.v3.data, t, result.position.v3.data);
	glm_quat_nlerp(a.rotation.data, b.rotation.data, t, result.rotation.data);
	glm_vec3_lerp(a.scale.data, b.scale.data, t, result.scale.data);

	return (result);
}

sm__force_inline struct trs
trs_inverse(struct trs t)
{
	struct trs result;

	glm_quat_inv(t.rotation.data, result.rotation.data);

	result.scale.x = fabsf(t.scale.x) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.x;
	result.scale.y = fabsf(t.scale.y) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.y;
	result.scale.z = fabsf(t.scale.z) < GLM_FLT_EPSILON ? 0.0f : 1.0f / t.scale.z;

	v3 inv_trans;
	glm_vec3_scale(t.position.data, -1.0f, inv_trans.data);
	glm_vec3_mul(result.scale.data, inv_trans.data, inv_trans.data);
	glm_quat_rotatev(result.rotation.data, inv_trans.data, result.position.v3.data);

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

#define color_from_hex(HEX)              \
	((union color){                  \
	    .r = (((HEX) >> 24) & 0xFF), \
	    .g = (((HEX) >> 16) & 0xFF), \
	    .b = (((HEX) >> 8) & 0xFF),  \
	    .a = ((HEX)&0xFF),           \
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
	u8 r1 = (color1 >> 24) & 0xFF;
	u8 r2 = (color2 >> 24) & 0xFF;
	u8 g1 = (color1 >> 16) & 0xFF;
	u8 g2 = (color2 >> 16) & 0xFF;
	u8 b1 = (color1 >> 8) & 0xFF;
	u8 b2 = (color2 >> 8) & 0xFF;
	u8 a1 = (color1)&0xFF;
	u8 a2 = (color2)&0xFF;

	u8 R = (u8)((r2 - r1) * frac) + r1;
	u8 G = (u8)((g2 - g1) * frac) + g1;
	u8 B = (u8)((b2 - b1) * frac) + b1;
	u8 A = (u8)((a2 - a1) * frac) + a1;

	return (u32)R << 24 | (u32)G << 16 | (u32)B << 8 | (u32)A;
}

#define BLACK color_from_hex(0x00000000)
#define WHITE color_from_hex(0xFFFFFFFF)

#define BLACK1 color_from_hex(0x4B475CFF)
#define WHITE1 color_from_hex(0xD7DEDCFF)

#define COLOR0	color_from_hex(0x28282EFF)
#define COLOR1	color_from_hex(0x6C5671FF)
#define COLOR2	color_from_hex(0xD9C8BFFF)
#define COLOR3	color_from_hex(0xF98284FF)
#define COLOR4	color_from_hex(0xB0A9E4FF)
#define COLOR5	color_from_hex(0xACCCE4FF)
#define COLOR6	color_from_hex(0xB3E3DAFF)
#define COLOR7	color_from_hex(0xFEAAE4FF)
#define COLOR8	color_from_hex(0x87A889FF)
#define COLOR9	color_from_hex(0xB0EB93FF)
#define COLOR10 color_from_hex(0xE9F59DFF)
#define COLOR11 color_from_hex(0xFFE6C6FF)
#define COLOR12 color_from_hex(0xDEA38BFF)
#define COLOR13 color_from_hex(0xFFC384FF)
#define COLOR14 color_from_hex(0xFFF7A0FF)
#define COLOR15 color_from_hex(0xFFF7E4FF)

sm__static_assert(sizeof(v2) == sizeof(vec2), "v2 and vec2 mismatch size");
sm__static_assert(sizeof(v3) == sizeof(vec3), "v3 and vec3 mismatch size");
sm__static_assert(sizeof(v4) == sizeof(vec4), "v4 and vec4 mismatch size");
sm__static_assert(sizeof(m4) == sizeof(mat4), "m4 and mat4 mismatch size");

#endif // SM_MATH_H
