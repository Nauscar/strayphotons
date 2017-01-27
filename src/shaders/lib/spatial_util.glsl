#ifndef SPATIAL_UTIL_GLSL_INCLUDED
#define SPATIAL_UTIL_GLSL_INCLUDED

// Inverse perspective divide to produce linear depth in (0, 1) relative to the
// camera and far plane.
float LinearDepth(float depth, vec2 clip) {
	depth = 2 * depth - 1;
	return 2 * clip.x / (clip.y + clip.x - depth * (clip.y - clip.x));
}

// Projects v onto a normalized vector u.
vec3 ProjectVec3(vec3 v, vec3 u) {
	return u * dot(v, u);
}

// Returns a ray from the camera to the far plane.
vec3 FarPlaneRay(vec2 viewSpacePosition, float tanHalfFov, float aspectRatio) {
	return vec3(
		viewSpacePosition.x * tanHalfFov * aspectRatio,
		viewSpacePosition.y * tanHalfFov,
		1.0
	);
}

// Returns the homogeneous view-space position of clip-space position.
vec4 ClipPosToHViewPos(vec3 clipPos, mat4 invProj) {
	return invProj * vec4(clipPos, 1.0);
}

// Returns the view-space position of clip-space position.
vec3 ClipPosToViewPos(vec3 clipPos, mat4 invProj) {
	vec4 hpos = ClipPosToHViewPos(clipPos, invProj);
	return hpos.xyz / hpos.w; // Inverse perspective divide.
}

// Returns the homogenous view-space position of a screen-space texcoord and depth.
vec4 ScreenPosToHViewPos(vec2 texCoord, float depth, mat4 invProj) {
	vec3 clip = vec3(texCoord, depth) * 2.0 - 1.0;
	return ClipPosToHViewPos(clip, invProj);
}

// Returns the view-space position of a screen-space texcoord and depth.
vec3 ScreenPosToViewPos(vec2 texCoord, float depth, mat4 invProj) {
	vec3 clip = vec3(texCoord, depth) * 2.0 - 1.0;
	return ClipPosToViewPos(clip, invProj);
}

// Returns the screen-space texcoord of a view-space position.
vec3 ViewPosToScreenPos(vec3 viewPos, mat4 projMat) {
	vec4 clip = projMat * vec4(viewPos, 1.0);
	return clip.xyz / clip.w * vec3(0.5) + vec3(0.5);
}

// Gradient noise in [-1, 1] as in [Jimenez 2014]
float InterleavedGradientNoise(vec2 position) {
	const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return 2 * fract(magic.z * fract(dot(position, magic.xy))) - 1;
}

// Some nice sample coordinates around a spiral.
const vec2[8] SpiralOffsets = vec2[](
	vec2(-0.7071,  0.7071),
	vec2(-0.0000, -0.8750),
	vec2( 0.5303,  0.5303),
	vec2(-0.6250, -0.0000),
	vec2( 0.3536, -0.3536),
	vec2(-0.0000,  0.3750),
	vec2(-0.1768, -0.1768),
	vec2( 0.1250,  0.0000)
);

// Returns vector with angles phi, tht in the hemisphere defined by the input normal.
vec3 OrientByNormal(float phi, float tht, vec3 normal) {
	float sintht = sin(tht);
	float xs = sintht * cos(phi);
	float ys = cos(tht);
	float zs = sintht * sin(phi);

	vec3 up = abs(normal.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
	vec3 tangent1 = normalize(up - normal * dot(up, normal));
	vec3 tangent2 = normalize(cross(tangent1, normal));
	return normalize(xs * tangent1 + ys * normal + zs * tangent2);
}

#endif
