//
//  node.h
//  c-ray
//
//  Created by Valtteri Koskivuori on 19/11/2023.
//  Copyright © 2023 Valtteri Koskivuori. All rights reserved.
//

#pragma once

// These are ripped off here:
// https://docs.blender.org/manual/en/latest/render/shader_nodes/converter/math.html
// TODO: Commented ones need to be implemented to reach parity with Cycles. Feel free to do so! :^)
enum cr_math_op {
	Add,
	Subtract,
	Multiply,
	Divide,
	//MultiplyAdd,
	Power,
	Log,
	SquareRoot,
	InvSquareRoot,
	Absolute,
	//Exponent,
	Min,
	Max,
	LessThan,
	GreaterThan,
	Sign,
	Compare,
	//SmoothMin,
	//SmoothMax,
	Round,
	Floor,
	Ceil,
	Truncate,
	Fraction,
	Modulo,
	//Wrap,
	//Snap,
	//PingPong,
	Sine,
	Cosine,
	Tangent,
	//ArcSine,
	//ArcCosine,
	//ArcTangent,
	//ArcTan2,
	//HyperbolicSine,
	//HyperbolicCosine,
	//HyperbolicTangent,
	ToRadians,
	ToDegrees,
};

struct cr_value_node {
	enum cr_value_node_type {
		cr_vn_unknown = 0,
		cr_vn_constant,
		cr_vn_fresnel,
		cr_vn_map_range,
		cr_vn_light_path,
		cr_vn_alpha,
		cr_vn_vec_to_value,
		cr_vn_math,
		cr_vn_grayscale,
	} type;

	union {
		double constant;

		struct cr_fresnel_params {
			struct cr_value_node *IOR;
			struct cr_vector_node *normal;
		} fresnel;

		struct cr_map_range_params {
			struct cr_value_node *input_value;
			struct cr_value_node *from_min;
			struct cr_value_node *from_max;
			struct cr_value_node *to_min;
			struct cr_value_node *to_max;
		} map_range;

		struct cr_light_path_params {
			enum cr_light_path_info {
				cr_is_camera_ray = 0,
				cr_is_shadow_ray,
				cr_is_diffuse_ray,
				cr_is_glossy_ray,
				cr_is_singular_ray,
				cr_is_reflection_ray,
				cr_is_transmission_ray,
				cr_ray_length,
			} query;
		} light_path;

		struct cr_alpha_params {
			struct cr_color_node *color;
		} alpha;

		struct cr_vec_to_value_params {
			enum cr_vec_to_value_component {
				X = 0, Y, Z, U, V, F
			} comp;
			struct cr_vector_node *vec;
		} vec_to_value;

		struct cr_math_params {
			struct cr_value_node *A;
			struct cr_value_node *B;
			enum cr_math_op op;
		} math;

		struct cr_grayscale_params {
			struct cr_color_node *color;
		} grayscale;

	} arg;
};

// ------

struct cr_color_node {
	enum color_node_type {
		cr_cn_unknown = 0,
		cr_cn_constant,
		cr_cn_image,
		cr_cn_checkerboard,
		cr_cn_blackbody,
		cr_cn_split,
		cr_cn_rgb,
		cr_cn_hsl,
		cr_cn_hsv,
		cr_cn_hsv_tform,
		cr_cn_vec_to_color,
		cr_cn_gradient,
		cr_cn_color_mix,
		cr_cn_color_ramp,
	} type;

	union {
		struct cr_color constant;

		struct cr_image_texture_params {
			char *full_path;
			uint8_t options;
		} image;

		struct cr_checkerboard_params {
			struct cr_color_node *a;
			struct cr_color_node *b;
			struct cr_value_node *scale;
		} checkerboard;

		struct cr_blackbody_params {
			struct cr_value_node *degrees;
		} blackbody;

		struct cr_split_params {
			struct cr_value_node *node;
		} split;

		struct cr_rgb_params {
			struct cr_value_node *red;
			struct cr_value_node *green;
			struct cr_value_node *blue;
			// Alpha?
		} rgb;

		struct cr_hsl_params {
			struct cr_value_node *H;
			struct cr_value_node *S;
			struct cr_value_node *L;
		} hsl;

		struct cr_hsv_params {
			struct cr_value_node *H;
			struct cr_value_node *S;
			struct cr_value_node *V;
		} hsv;

		struct cr_hsv_tform_params {
			struct cr_color_node *tex;
			struct cr_value_node *H;
			struct cr_value_node *S;
			struct cr_value_node *V;
			struct cr_value_node *f;
		} hsv_tform;

		struct cr_vec_to_color_params {
			struct cr_vector_node *vec;
		} vec_to_color;

		struct cr_gradient_params {
			struct cr_color_node *a;
			struct cr_color_node *b;
		} gradient;

		struct cr_color_mix_params {
			struct cr_color_node *a;
			struct cr_color_node *b;
			struct cr_value_node *factor;
		} color_mix;

		struct cr_color_ramp_params {
			struct cr_value_node *factor;
			enum cr_color_mode {
				cr_mode_rgb,
				cr_mode_hsv,
				cr_mode_hsl,
			} color_mode;

			enum cr_interpolation {
				cr_ease,     // TODO
				cr_cardinal, // TODO
				cr_linear,
				cr_b_spline, // TODO
				cr_constant,
			} interpolation;

			struct ramp_element {
				struct cr_color color;
				float position; // [0.0,1.0]
			} *elements;

			int element_count;
		} color_ramp;
	} arg;
};

// -----

// These are ripped off here:
// https://docs.blender.org/manual/en/latest/render/shader_nodes/converter/vector_math.html
// TODO: Commented ones need to be implemented to reach parity with Cycles. Feel free to do so! :^)
enum cr_vec_op {
	VecAdd,
	VecSubtract,
	VecMultiply,
	VecDivide,
	//VecMultiplyAdd,
	VecCross,
	//VecProject,
	VecReflect,
	VecRefract,
	//VecFaceforward,
	VecDot,
	VecDistance,
	VecLength,
	VecScale,
	VecNormalize,
	VecWrap,
	//VecSnap,
	VecFloor,
	VecCeil,
	VecModulo,
	//VecFraction,
	VecAbs,
	VecMin,
	VecMax,
	VecSin,
	VecCos,
	VecTan,
};

struct cr_vector_node {
	enum cr_vector_node_type {
		cr_vec_unknown = 0,
		cr_vec_constant,
		cr_vec_normal,
		cr_vec_uv,
		cr_vec_vecmath,
		cr_vec_mix,
	} type;

	// TODO: Maybe express vectorValue vec/coord/float union a bit better here?
	union {
		struct cr_vector constant;

		struct cr_vecmath_params {
			struct cr_vector_node *A;
			struct cr_vector_node *B;
			struct cr_vector_node *C;
			struct cr_value_node *f;
			enum cr_vec_op op;
		} vecmath;

		struct cr_vec_mix_params {
			struct cr_vector_node *A;
			struct cr_vector_node *B;
			struct cr_value_node *factor;
		} vec_mix;

	} arg;
};

struct cr_shader_node {
	enum cr_shader_node_type {
		cr_bsdf_unknown = 0,
		cr_bsdf_diffuse,
		cr_bsdf_metal,
		cr_bsdf_glass,
		cr_bsdf_plastic,
		cr_bsdf_mix,
		cr_bsdf_add,
		cr_bsdf_transparent,
		cr_bsdf_emissive,
		cr_bsdf_translucent,
		cr_bsdf_background,
	} type;

	union {
		struct cr_diffuse_args {
			struct cr_color_node *color;
		} diffuse;

		struct cr_metal_args {
			struct cr_color_node *color;
			struct cr_value_node *roughness;
		} metal;

		struct cr_glass_args {
			struct cr_color_node *color;
			struct cr_value_node *roughness;
			struct cr_value_node *IOR;
		} glass;

		struct cr_plastic_args {
			struct cr_color_node *color;
			struct cr_value_node *roughness;
			struct cr_value_node *IOR;
		} plastic;

		struct cr_mix_args {
			struct cr_shader_node *A;
			struct cr_shader_node *B;
			struct cr_value_node *factor;
		} mix;

		struct cr_add_args {
			struct cr_shader_node *A;
			struct cr_shader_node *B;
		} add;

		struct cr_transparent_args {
			struct cr_color_node *color;
		} transparent;

		struct cr_emissive_args {
			struct cr_color_node *color;
			struct cr_value_node *strength;
		} emissive;

		struct cr_translucent_args {
			struct cr_color_node *color;
		} translucent;

		struct cr_background_args {
			struct cr_color_node *color;
			struct cr_vector_node *pose;
			struct cr_value_node *strength;
		} background;

	} arg;
};
