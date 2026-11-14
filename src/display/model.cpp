/*
** model.cpp
**
** DS Overworld addon for mkxp-z. See model.h.
**
** This file is part of mkxp.
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
*/

#include "model.h"

#include "bitmap.h"
#include "sharedstate.h"
#include "glstate.h"
#include "gl-util.h"
#include "gl-meta.h"
#include "shader.h"
#include "vertex.h"
#include "exception.h"
#include "gltf.h"

#include <SDL.h>
#include <SDL_image.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

static const float kPi = 3.14159265358979f;

struct ModelVertex
{
	float x, y, z;
	float u, v;
};

static const VertexAttribute modelAttrs[] =
{
	{ Shader::Position, 3, GL_FLOAT, (const GLvoid*) 0 },
	{ Shader::TexCoord, 2, GL_FLOAT, (const GLvoid*) (3 * sizeof(float)) }
};

struct ModelMaterial
{
	std::string name;
	size_t firstIndex;
	size_t indexCount;
	Bitmap *texture;
	float color[4];
	float uv[4];   /* offset u, offset v, scale u, scale v */
	bool visible;

	ModelMaterial()
	    : firstIndex(0), indexCount(0), texture(0), visible(true)
	{
		uv[0] = uv[1] = 0.f;
		uv[2] = uv[3] = 1.f;
		color[0] = color[1] = color[2] = 0.8f;
		color[3] = 1.f;
	}
};

struct ModelPrivate
{
	std::vector<ModelVertex> verts;
	std::vector<GLushort> indices;
	std::vector<ModelMaterial> mats;

	float bounds[6];

	GLMeta::VAO vao;
	bool glReady;

	/* glTF only */
	bool isGltf;
	gltf::Scene scene;
	std::vector<int> primBase;   /* first vertex of each primitive */
	std::vector<int> matImage;   /* per ModelMaterial: glTF image index or -1 */
	bool poseDirty;

	ModelPrivate()
	    : glReady(false), isGltf(false), poseDirty(false)
	{
		memset(bounds, 0, sizeof(bounds));
		vao = GLMeta::VAO();
	}

	ModelMaterial *find(const char *name)
	{
		for (size_t i = 0; i < mats.size(); ++i)
			if (mats[i].name == name)
				return &mats[i];

		throw Exception(Exception::MKXPError,
		                "Model: unknown material '%s'", name);
	}
};

/*---------------------------------------------------------------------------
 * Offscreen render target shared by all models (color + depth).
 * It is only ever grown, never freed: it lives as long as the GL context.
 *-------------------------------------------------------------------------*/
struct ScratchTarget
{
	TEXFBO tf;
	GLuint depthRb;
	int w, h;
	bool inited;

	ScratchTarget()
	    : depthRb(0), w(0), h(0), inited(false)
	{}

	static ScratchTarget &get(int needW, int needH)
	{
		static ScratchTarget s;

		if (!s.inited)
		{
			TEXFBO::init(s.tf);
			gl.GenRenderbuffers(1, &s.depthRb);
			s.inited = true;
		}

		if (needW > s.w || needH > s.h)
		{
			int nw = needW > s.w ? needW : s.w;
			int nh = needH > s.h ? needH : s.h;

			/* Round up so small differences don't realloc every call */
			nw = (nw + 63) & ~63;
			nh = (nh + 63) & ~63;

			TEXFBO::allocEmpty(s.tf, nw, nh);
			TEXFBO::linkFBO(s.tf);

#ifdef GL_DEPTH_COMPONENT24
			GLenum depthFmt = gl.glsles ? GL_DEPTH_COMPONENT16
			                            : GL_DEPTH_COMPONENT24;
#else
			GLenum depthFmt = GL_DEPTH_COMPONENT16;
#endif

			gl.BindRenderbuffer(GL_RENDERBUFFER, s.depthRb);
			gl.RenderbufferStorage(GL_RENDERBUFFER, depthFmt, nw, nh);
			gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			                           GL_RENDERBUFFER, s.depthRb);
			gl.BindRenderbuffer(GL_RENDERBUFFER, 0);

			s.w = nw;
			s.h = nh;
		}

		return s;
	}
};

/*---------------------------------------------------------------------------
 * OBJ parsing
 *-------------------------------------------------------------------------*/
static int resolveIndex(long idx, size_t count)
{
	if (idx > 0)
		return (int) idx - 1;
	if (idx < 0)
		return (int) count + (int) idx;
	return -1;
}

static void parseFaceToken(const char *s, long &vi, long &ti)
{
	char *e = 0;

	vi = strtol(s, &e, 10);
	ti = 0;

	if (*e == '/')
	{
		s = e + 1;

		if (*s != '/')
			ti = strtol(s, &e, 10);
	}
}

static std::string trim(const std::string &in)
{
	size_t b = 0, e = in.size();

	while (b < e && (in[b] == ' ' || in[b] == '\t'))
		++b;
	while (e > b && (in[e-1] == ' ' || in[e-1] == '\t' || in[e-1] == '\r'))
		--e;

	return in.substr(b, e - b);
}

static void parseObj(const char *data, size_t len, ModelPrivate &mp)
{
	std::vector<float> pos;   /* x y z */
	std::vector<float> uv;    /* u v   */

	std::vector<std::vector<GLushort> > matIdx;
	std::map<std::string, int> matLookup;
	std::map<std::pair<int, int>, GLushort> cache;

	int cur = -1;

	bool haveBounds = false;

	const char *p = data;
	const char *end = data + len;

	while (p < end)
	{
		const char *le = (const char*) memchr(p, '\n', end - p);
		if (!le)
			le = end;

		std::string line = trim(std::string(p, le - p));
		p = le + 1;

		if (line.size() < 2 || line[0] == '#')
			continue;

		if (line.compare(0, 2, "v ") == 0)
		{
			float x = 0, y = 0, z = 0;
			sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z);
			pos.push_back(x); pos.push_back(y); pos.push_back(z);
		}
		else if (line.compare(0, 3, "vt ") == 0)
		{
			float u = 0, v = 0;
			sscanf(line.c_str() + 3, "%f %f", &u, &v);
			uv.push_back(u); uv.push_back(v);
		}
		else if (line.compare(0, 7, "usemtl ") == 0)
		{
			std::string name = trim(line.substr(7));
			std::map<std::string, int>::iterator it = matLookup.find(name);

			if (it == matLookup.end())
			{
				cur = (int) mp.mats.size();
				matLookup[name] = cur;
				mp.mats.push_back(ModelMaterial());
				mp.mats.back().name = name;
				matIdx.push_back(std::vector<GLushort>());
			}
			else
				cur = it->second;
		}
		else if (line.compare(0, 2, "f ") == 0)
		{
			if (cur < 0)
			{
				cur = (int) mp.mats.size();
				matLookup["default"] = cur;
				mp.mats.push_back(ModelMaterial());
				mp.mats.back().name = "default";
				matIdx.push_back(std::vector<GLushort>());
			}

			std::vector<GLushort> face;

			const char *s = line.c_str() + 2;

			while (*s)
			{
				while (*s == ' ' || *s == '\t')
					++s;
				if (!*s)
					break;

				long vi, ti;
				parseFaceToken(s, vi, ti);

				while (*s && *s != ' ' && *s != '\t')
					++s;

				int pi = resolveIndex(vi, pos.size() / 3);
				int tix = resolveIndex(ti, uv.size() / 2);

				if (pi < 0 || (size_t) pi >= pos.size() / 3)
					throw Exception(Exception::MKXPError,
					                "Model: face references missing vertex %ld", vi);

				std::pair<int, int> key(pi, tix);
				std::map<std::pair<int, int>, GLushort>::iterator it = cache.find(key);

				if (it == cache.end())
				{
					if (mp.verts.size() >= 65535)
						throw Exception(Exception::MKXPError,
						                "Model has too many vertices (limit is 65535)");

					ModelVertex v;
					v.x = pos[pi*3]; v.y = pos[pi*3+1]; v.z = pos[pi*3+2];

					if (tix >= 0 && (size_t) tix < uv.size() / 2)
					{
						v.u = uv[tix*2];
						v.v = 1.f - uv[tix*2+1];
					}
					else
						v.u = v.v = 0.f;

					if (!haveBounds)
					{
						mp.bounds[0] = mp.bounds[3] = v.x;
						mp.bounds[1] = mp.bounds[4] = v.y;
						mp.bounds[2] = mp.bounds[5] = v.z;
						haveBounds = true;
					}
					else
					{
						if (v.x < mp.bounds[0]) mp.bounds[0] = v.x;
						if (v.y < mp.bounds[1]) mp.bounds[1] = v.y;
						if (v.z < mp.bounds[2]) mp.bounds[2] = v.z;
						if (v.x > mp.bounds[3]) mp.bounds[3] = v.x;
						if (v.y > mp.bounds[4]) mp.bounds[4] = v.y;
						if (v.z > mp.bounds[5]) mp.bounds[5] = v.z;
					}

					GLushort id = (GLushort) mp.verts.size();
					mp.verts.push_back(v);
					cache[key] = id;
					face.push_back(id);
				}
				else
					face.push_back(it->second);
			}

			/* Triangle fan */
			for (size_t i = 1; i + 1 < face.size(); ++i)
			{
				matIdx[cur].push_back(face[0]);
				matIdx[cur].push_back(face[i]);
				matIdx[cur].push_back(face[i+1]);
			}
		}
	}

	/* Lay triangles out grouped by material */
	for (size_t m = 0; m < mp.mats.size(); ++m)
	{
		mp.mats[m].firstIndex = mp.indices.size();
		mp.mats[m].indexCount = matIdx[m].size();
		mp.indices.insert(mp.indices.end(), matIdx[m].begin(), matIdx[m].end());
	}

	if (mp.indices.empty())
		throw Exception(Exception::MKXPError, "Model has no faces");
}

/*---------------------------------------------------------------------------
 * glTF
 *-------------------------------------------------------------------------*/
static bool looksLikeGltf(const char *data, size_t len)
{
	if (len >= 4 && memcmp(data, "glTF", 4) == 0)
		return true;

	for (size_t i = 0; i < len; ++i)
	{
		char c = data[i];

		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			continue;

		return c == '{';
	}

	return false;
}

static void growBounds(float b[6], bool &have, float x, float y, float z)
{
	if (!have)
	{
		b[0] = b[3] = x; b[1] = b[4] = y; b[2] = b[5] = z;
		have = true;
		return;
	}

	if (x < b[0]) b[0] = x;
	if (y < b[1]) b[1] = y;
	if (z < b[2]) b[2] = z;
	if (x > b[3]) b[3] = x;
	if (y > b[4]) b[4] = y;
	if (z > b[5]) b[5] = z;
}

/* Poses every vertex on the CPU (rigid node transforms and skinning). The
 * models are low-poly, so this is cheap, and the draw shader stays the same
 * as for OBJ files. */
static void applyPose(ModelPrivate &mp, int anim, float time)
{
	std::vector<float> worlds;
	std::vector<float> pos;

	gltf::evalWorlds(mp.scene, anim, time, worlds);

	pos.reserve(mp.verts.size() * 3);

	for (size_t i = 0; i < mp.scene.prims.size(); ++i)
		gltf::posePrimitive(mp.scene, mp.scene.prims[i], worlds, pos);

	size_t n = pos.size() / 3;

	if (n > mp.verts.size())
		n = mp.verts.size();

	for (size_t i = 0; i < n; ++i)
	{
		mp.verts[i].x = pos[i*3];
		mp.verts[i].y = pos[i*3+1];
		mp.verts[i].z = pos[i*3+2];
	}

	mp.poseDirty = true;
}

static std::string cleanName(const std::string &in, const char *fallback, int index)
{
	std::string r;

	for (size_t i = 0; i < in.size(); ++i)
	{
		unsigned char c = (unsigned char) in[i];
		r += (c <= ' ' || c == '|') ? '_' : (char) c;
	}

	if (r.empty())
	{
		char buf[48];
		snprintf(buf, sizeof(buf), "%s_%d", fallback, index);
		r = buf;
	}

	return r;
}

static void buildGltf(const char *data, size_t len,
                      const std::map<std::string, std::string> &externals,
                      ModelPrivate &mp)
{
	std::string err;

	if (!gltf::parse((const unsigned char*) data, len, externals, mp.scene, err))
		throw Exception(Exception::MKXPError, "Model: %s", err.c_str());

	mp.isGltf = true;

	const gltf::Scene &sc = mp.scene;

	/* Materials: all glTF materials, plus a default one if needed */
	std::vector<int> matOf(sc.materials.size(), -1);
	int defaultMat = -1;

	for (size_t i = 0; i < sc.materials.size(); ++i)
	{
		ModelMaterial m;
		std::string name = cleanName(sc.materials[i].name, "material", (int) i);

		/* keep names unique */
		for (int k = 2;; ++k)
		{
			bool dup = false;

			for (size_t j = 0; j < mp.mats.size(); ++j)
				if (mp.mats[j].name == name)
					dup = true;

			if (!dup)
				break;

			char buf[16];
			snprintf(buf, sizeof(buf), "_%d", k);
			name = cleanName(sc.materials[i].name, "material", (int) i) + buf;
		}

		m.name = name;
		memcpy(m.color, sc.materials[i].color, sizeof(m.color));
		matOf[i] = (int) mp.mats.size();
		mp.mats.push_back(m);
		mp.matImage.push_back(sc.materials[i].image);
	}

	/* Vertices (primitive order) and per-material index lists */
	std::vector<std::vector<GLushort> > matIdx(mp.mats.size());

	for (size_t pi = 0; pi < sc.prims.size(); ++pi)
	{
		const gltf::Primitive &pr = sc.prims[pi];
		size_t vc = pr.pos.size() / 3;

		if (mp.verts.size() + vc > 65535)
			throw Exception(Exception::MKXPError,
			                "Model has too many vertices (limit is 65535)");

		mp.primBase.push_back((int) mp.verts.size());

		int mi = pr.material >= 0 && (size_t) pr.material < matOf.size()
		         ? matOf[pr.material] : -1;

		if (mi < 0)
		{
			if (defaultMat < 0)
			{
				ModelMaterial m;
				m.name = "default";
				defaultMat = (int) mp.mats.size();
				mp.mats.push_back(m);
				mp.matImage.push_back(-1);
				matIdx.push_back(std::vector<GLushort>());
			}

			mi = defaultMat;
		}

		GLushort base = (GLushort) mp.verts.size();

		for (size_t v = 0; v < vc; ++v)
		{
			ModelVertex mv;
			mv.x = pr.pos[v*3]; mv.y = pr.pos[v*3+1]; mv.z = pr.pos[v*3+2];

			if (pr.uv.size() >= (v + 1) * 2)
			{
				mv.u = pr.uv[v*2];
				mv.v = pr.uv[v*2+1]; /* glTF: v already points down */
			}
			else
				mv.u = mv.v = 0.f;

			mp.verts.push_back(mv);
		}

		for (size_t k = 0; k < pr.indices.size(); ++k)
			matIdx[mi].push_back((GLushort) (base + pr.indices[k]));
	}

	for (size_t m = 0; m < mp.mats.size(); ++m)
	{
		mp.mats[m].firstIndex = mp.indices.size();
		mp.mats[m].indexCount = matIdx[m].size();
		mp.indices.insert(mp.indices.end(), matIdx[m].begin(), matIdx[m].end());
	}

	if (mp.indices.empty())
		throw Exception(Exception::MKXPError, "Model has no faces");

	/* Bounds: rest pose plus a sampling of every animation, so culling
	 * rectangles stay valid while it plays */
	bool have = false;
	float b[6] = { 0, 0, 0, 0, 0, 0 };

	for (int pass = -1; pass < (int) sc.anims.size(); ++pass)
	{
		int steps = pass < 0 ? 1 : 24;

		for (int k = 0; k < steps; ++k)
		{
			float t = pass < 0 ? 0.f : sc.anims[pass].duration * k / (steps - 1);

			applyPose(mp, pass, t);

			for (size_t i = 0; i < mp.verts.size(); ++i)
				growBounds(b, have, mp.verts[i].x, mp.verts[i].y, mp.verts[i].z);
		}
	}

	memcpy(mp.bounds, b, sizeof(b));

	applyPose(mp, -1, 0.f);
}

/*---------------------------------------------------------------------------
 * Model
 *-------------------------------------------------------------------------*/
Model::Model(const char *objData, size_t objLen,
             const std::map<std::string, std::string> *externals)
{
	p = new ModelPrivate();

	try
	{
		if (looksLikeGltf(objData, objLen))
		{
			std::map<std::string, std::string> none;
			buildGltf(objData, objLen, externals ? *externals : none, *p);
		}
		else
			parseObj(objData, objLen, *p);

		VBO::ID vbo = VBO::gen();
		IBO::ID ibo = IBO::gen();

		VBO::bind(vbo);
		VBO::uploadData(p->verts.size() * sizeof(ModelVertex), &p->verts[0],
		                p->isGltf ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
		IBO::bind(ibo);
		IBO::uploadData(p->indices.size() * sizeof(GLushort), &p->indices[0]);

		p->vao.vbo = vbo;
		p->vao.ibo = ibo;
		p->vao.attr = modelAttrs;
		p->vao.attrCount = 2;
		p->vao.vertSize = sizeof(ModelVertex);

		GLMeta::vaoInit(p->vao);
		p->glReady = true;

		VBO::unbind();
		IBO::unbind();

		p->poseDirty = false;
	}
	catch (...)
	{
		delete p;
		p = 0;
		throw;
	}
}

Model::~Model()
{
	dispose();
}

int Model::vertexCount() const
{
	guardDisposed();
	return (int) p->verts.size();
}

int Model::triangleCount() const
{
	guardDisposed();
	return (int) (p->indices.size() / 3);
}

int Model::materialCount() const
{
	guardDisposed();
	return (int) p->mats.size();
}

const char *Model::materialName(int index) const
{
	guardDisposed();

	if (index < 0 || (size_t) index >= p->mats.size())
		return "";

	return p->mats[index].name.c_str();
}

int Model::materialTriangles(int index) const
{
	guardDisposed();

	if (index < 0 || (size_t) index >= p->mats.size())
		return 0;

	return (int) (p->mats[index].indexCount / 3);
}

void Model::getBounds(float out[6]) const
{
	guardDisposed();
	memcpy(out, p->bounds, sizeof(p->bounds));
}

void Model::setTexture(const char *material, Bitmap *bitmap)
{
	guardDisposed();

	ModelMaterial *m = p->find(material);

	if (bitmap)
		bitmap->ensureNonMega();

	m->texture = bitmap;
}

void Model::setColor(const char *material, float r, float g, float b, float a)
{
	guardDisposed();

	ModelMaterial *m = p->find(material);
	m->color[0] = r; m->color[1] = g; m->color[2] = b; m->color[3] = a;
}

void Model::setUV(const char *material, float ox, float oy, float sx, float sy)
{
	guardDisposed();

	ModelMaterial *m = p->find(material);
	m->uv[0] = ox; m->uv[1] = oy; m->uv[2] = sx; m->uv[3] = sy;
}

void Model::setVisible(const char *material, bool visible)
{
	guardDisposed();
	p->find(material)->visible = visible;
}

bool Model::isGltf() const
{
	guardDisposed();
	return p->isGltf;
}

int Model::animationCount() const
{
	guardDisposed();
	return (int) p->scene.anims.size();
}

const char *Model::animationName(int index) const
{
	guardDisposed();

	if (index < 0 || (size_t) index >= p->scene.anims.size())
		return "";

	return p->scene.anims[index].name.c_str();
}

float Model::animationDuration(int index) const
{
	guardDisposed();

	if (index < 0 || (size_t) index >= p->scene.anims.size())
		return 0.f;

	return p->scene.anims[index].duration;
}

void Model::setPose(int anim, float time)
{
	guardDisposed();

	if (!p->isGltf)
		return;

	if (anim >= (int) p->scene.anims.size())
		throw Exception(Exception::MKXPError, "Model: unknown animation %d", anim);

	applyPose(*p, anim, time);
}

int Model::imageCount() const
{
	guardDisposed();
	return (int) p->scene.images.size();
}

int Model::materialImage(int index) const
{
	guardDisposed();

	if (index < 0 || (size_t) index >= p->matImage.size())
		return -1;

	int img = p->matImage[index];

	if (img < 0 || (size_t) img >= p->scene.images.size())
		return -1;

	return img;
}

void Model::materialColor(int index, float out[4]) const
{
	guardDisposed();

	if (index < 0 || (size_t) index >= p->mats.size())
	{
		out[0] = out[1] = out[2] = 0.8f;
		out[3] = 1.f;
		return;
	}

	memcpy(out, p->mats[index].color, sizeof(float) * 4);
}

void Model::imageRGBA(int image, int &w, int &h, std::string &pixels) const
{
	guardDisposed();

	if (image < 0 || (size_t) image >= p->scene.images.size() ||
	    p->scene.images[image].data.empty())
		throw Exception(Exception::MKXPError, "Model: image %d has no data", image);

	const std::vector<unsigned char> &d = p->scene.images[image].data;

	SDL_RWops *rw = SDL_RWFromConstMem(&d[0], (int) d.size());
	SDL_Surface *surf = rw ? IMG_Load_RW(rw, 1) : 0;

	if (!surf)
		throw Exception(Exception::MKXPError, "Model: cannot decode image %d: %s",
		                image, IMG_GetError());

	SDL_Surface *conv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(surf);

	if (!conv)
		throw Exception(Exception::SDLError, "Model: image conversion failed: %s",
		                SDL_GetError());

	w = conv->w;
	h = conv->h;
	pixels.resize((size_t) w * h * 4);

	for (int y = 0; y < h; ++y)
		memcpy(&pixels[(size_t) y * w * 4],
		       (const unsigned char*) conv->pixels + (size_t) y * conv->pitch,
		       (size_t) w * 4);

	SDL_FreeSurface(conv);
}

std::string Model::toObj() const
{
	guardDisposed();

	std::string out;
	char buf[160];

	out.reserve(p->verts.size() * 64 + p->indices.size() * 12);

	for (size_t i = 0; i < p->verts.size(); ++i)
	{
		snprintf(buf, sizeof(buf), "v %.6f %.6f %.6f\n",
		         p->verts[i].x, p->verts[i].y, p->verts[i].z);
		out += buf;
	}

	/* OBJ's v axis points up; the OBJ loader flips it back */
	for (size_t i = 0; i < p->verts.size(); ++i)
	{
		float v = 1.f - p->verts[i].v;
		snprintf(buf, sizeof(buf), "vt %.6f %.6f\n", p->verts[i].u, v);
		out += buf;
	}

	for (size_t m = 0; m < p->mats.size(); ++m)
	{
		const ModelMaterial &mm = p->mats[m];

		if (mm.indexCount == 0)
			continue;

		out += "usemtl " + mm.name + "\n";

		for (size_t k = 0; k + 2 < mm.indexCount; k += 3)
		{
			unsigned a = p->indices[mm.firstIndex + k] + 1;
			unsigned b = p->indices[mm.firstIndex + k + 1] + 1;
			unsigned c = p->indices[mm.firstIndex + k + 2] + 1;

			snprintf(buf, sizeof(buf), "f %u/%u %u/%u %u/%u\n", a, a, b, b, c, c);
			out += buf;
		}
	}

	return out;
}

void Model::render(Bitmap &target, const RenderParams &r)
{
	guardDisposed();

	if (target.isDisposed())
		throw Exception(Exception::RGSSError, "disposed bitmap");

	target.ensureNonMega();

	const int W = target.width();
	const int H = target.height();

	if (W <= 0 || H <= 0)
		return;

	/* Same camera math as the Ruby DSOverworld::Camera */
	const float pitch = r.pitchDeg * kPi / 180.f;
	const float sinP = sinf(pitch);
	const float cosP = cosf(pitch);
	const float zoom = r.zoom > 0.01f ? r.zoom : 0.01f;
	const float camH = r.focal * sinP / zoom;
	const float g0 = camH / tanf(pitch);
	const float yaw = r.yawDeg * kPi / 180.f;

	ModelShader &shader = shState->shaders().model;
	shader.ensureInit();

	ScratchTarget &s = ScratchTarget::get(W, H);

	FBO::bind(s.tf.fbo);

	glState.scissorTest.pushSet(false);
	glState.viewport.pushSet(IntRect(0, 0, W, H));
	glState.blend.pushSet(false);
	glState.clearColor.pushSet(Vec4(0, 0, 0, 0));

	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gl.Enable(GL_DEPTH_TEST);

	shader.bind();
	shader.setCamera(sinP, cosP, r.focal, camH, g0, r.centerX, r.centerY);
	shader.setTarget(r.targetX, r.targetY, W, H);
	shader.setPlacement(r.posX, r.posY, r.posZ, r.scale, sinf(yaw), cosf(yaw));

	if (p->poseDirty)
	{
		VBO::bind(p->vao.vbo);
		VBO::uploadSubData(0, p->verts.size() * sizeof(ModelVertex), &p->verts[0]);
		VBO::unbind();
		p->poseDirty = false;
	}

	GLMeta::vaoBind(p->vao);

	for (size_t i = 0; i < p->mats.size(); ++i)
	{
		ModelMaterial &m = p->mats[i];

		if (!m.visible || m.indexCount == 0)
			continue;

		bool textured = m.texture && !m.texture->isDisposed();

		if (textured)
		{
			m.texture->bindTex(shader);
			TEX::setRepeat(true);
		}

		shader.setMaterial(textured, m.color);
		shader.setUV(m.uv);

		gl.DrawElements(GL_TRIANGLES, (GLsizei) m.indexCount, GL_UNSIGNED_SHORT,
		                (const GLvoid*) (m.firstIndex * sizeof(GLushort)));

		if (textured)
			TEX::setRepeat(false);
	}

	GLMeta::vaoUnbind(p->vao);

	gl.Disable(GL_DEPTH_TEST);

	glState.clearColor.pop();
	glState.blend.pop();
	glState.viewport.pop();
	glState.scissorTest.pop();

	target.blitFromTexFBO(s.tf, IntRect(0, 0, W, H), 0, 0);
}

void Model::releaseResources()
{
	if (p && p->glReady)
	{
		GLMeta::vaoFini(p->vao);
		VBO::del(p->vao.vbo);
		IBO::del(p->vao.ibo);
	}

	delete p;
	p = 0;
}
