/*
** gltf.h
**
** DS Overworld addon for mkxp-z: a small, dependency-free glTF 2.0 / GLB
** reader plus skeleton and node animation evaluation. No OpenGL or SDL in
** here, so it can be tested on its own.
**
** Supported: .glb and .gltf (buffers as GLB chunk, data: URIs or supplied
** external files), triangle meshes, TEXCOORD_0, JOINTS_0/WEIGHTS_0, node
** hierarchy, skins, baseColor material (factor + texture), animations with
** translation/rotation/scale channels (LINEAR, STEP, CUBICSPLINE).
** Not supported: sparse accessors, morph targets, cameras, lights, KHR_*
** extensions (they are ignored).
**
** This file is part of mkxp.
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
*/

#ifndef GLTF_H
#define GLTF_H

#include <map>
#include <string>
#include <vector>

namespace gltf
{

struct Primitive
{
	std::vector<float> pos;             /* x y z per vertex */
	std::vector<float> uv;              /* u v per vertex (empty if none) */
	std::vector<unsigned short> joints; /* 4 per vertex (empty if unskinned) */
	std::vector<float> weights;         /* 4 per vertex */
	std::vector<unsigned int> indices;  /* triangle list */
	int material;                       /* index into Scene::materials or -1 */
	int node;                           /* node that carries the mesh */
	int skin;                           /* index into Scene::skins or -1 */

	Primitive() : material(-1), node(-1), skin(-1) {}
};

struct Material
{
	std::string name;
	float color[4];
	int image; /* index into Scene::images or -1 */

	Material() : image(-1)
	{
		color[0] = color[1] = color[2] = 0.8f;
		color[3] = 1.f;
	}
};

struct Node
{
	std::string name;
	int parent;
	float t[3];
	float r[4]; /* x y z w */
	float s[3];
	bool hasMatrix;
	float m[16]; /* column-major, only if hasMatrix */

	Node() : parent(-1), hasMatrix(false)
	{
		t[0] = t[1] = t[2] = 0.f;
		r[0] = r[1] = r[2] = 0.f; r[3] = 1.f;
		s[0] = s[1] = s[2] = 1.f;
	}
};

struct Skin
{
	std::vector<int> joints;
	std::vector<float> ibm; /* 16 floats per joint, column-major */
};

enum ChannelPath { PathTranslation = 0, PathRotation = 1, PathScale = 2 };
enum Interp { InterpLinear = 0, InterpStep = 1, InterpCubic = 2 };

struct Channel
{
	int node;
	int path;
	int interp;
	std::vector<float> times;
	std::vector<float> values; /* 3 or 4 per key (x3 for cubic spline) */
};

struct Animation
{
	std::string name;
	float duration;
	std::vector<Channel> channels;

	Animation() : duration(0.f) {}
};

struct Image
{
	std::string mime;
	std::vector<unsigned char> data;
};

struct Scene
{
	std::vector<Node> nodes;
	std::vector<Primitive> prims;
	std::vector<Material> materials;
	std::vector<Skin> skins;
	std::vector<Animation> anims;
	std::vector<Image> images;
};

/* Parses a .glb (binary) or .gltf (JSON) file held in memory.
 * 'externals' maps a uri (as written in the file) to its bytes.
 * Returns false and fills 'err' on failure. */
bool parse(const unsigned char *data, size_t len,
           const std::map<std::string, std::string> &externals,
           Scene &out, std::string &err);

/* World matrices (column-major, 16 floats per node) for the given animation
 * (anim < 0 = rest pose) at 'time' seconds (clamped to the duration). */
void evalWorlds(const Scene &sc, int anim, float time,
                std::vector<float> &worlds);

/* Appends the posed positions (x y z per vertex) of one primitive. */
void posePrimitive(const Scene &sc, const Primitive &pr,
                   const std::vector<float> &worlds, std::vector<float> &out);

}

#endif // GLTF_H
