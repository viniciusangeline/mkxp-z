// Vertex shader for the Model class (DS-style 3D buildings).
//
// World space: X = east, Y = up, Z = south (towards the camera).
// The camera sits at (0, camH, g0) and looks north with a downward pitch.
// This is exactly the same projection used by the Ruby ground renderer,
// so models line up perfectly with the perspective floor.
//
// Output is written directly in homogeneous clip space (no divisions),
// so hardware clipping and perspective-correct interpolation work.

uniform vec4 camA;    // sin(pitch), cos(pitch), focal length (px), camera height
uniform vec4 camB;    // g0, screen center x, screen center y, unused
uniform vec4 tgt;     // target origin on screen (ox, oy), target size (w, h)
uniform vec4 modelA;  // model origin in world: x, y (up), z (south); scale
uniform vec2 yaw;     // sin, cos of the model rotation around the up axis

attribute vec3 position;
attribute vec2 texCoord;

varying vec2 v_texCoord;

const float NEAR = 16.0;
const float FAR  = 16384.0;

void main()
{
	vec3 p = position * modelA.w;

	vec3 w;
	w.x =  p.x * yaw.y + p.z * yaw.x;
	w.y =  p.y;
	w.z = -p.x * yaw.x + p.z * yaw.y;
	w += modelA.xyz;

	float sinP = camA.x;
	float cosP = camA.y;
	float focal = camA.z;

	vec3 rel = vec3(w.x, w.y - camA.w, w.z - camB.x);

	float depth = -sinP * rel.y - cosP * rel.z;   // distance along the view axis
	float up    =  cosP * rel.y - sinP * rel.z;   // component along camera "up"

	float cx = (2.0 / tgt.z) * (focal * rel.x + (camB.y - tgt.x) * depth) - depth;
	float cy = (2.0 / tgt.w) * (-focal * up   + (camB.z - tgt.y) * depth) - depth;

	float A = (FAR + NEAR) / (FAR - NEAR);
	float B = -2.0 * FAR * NEAR / (FAR - NEAR);
	float cz = A * depth + B;

	gl_Position = vec4(cx, cy, cz, depth);

	v_texCoord = texCoord;
}
