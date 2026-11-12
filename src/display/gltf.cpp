/*
** gltf.cpp
**
** DS Overworld addon for mkxp-z. See gltf.h.
**
** This file is part of mkxp.
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
*/

#include "gltf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdexcept>
#include <utility>

namespace gltf
{

/*---------------------------------------------------------------------------
 * Minimal JSON
 *-------------------------------------------------------------------------*/
struct JV
{
	enum Type { Null, Bool, Num, Str, Arr, Obj };

	Type t;
	double n;
	bool b;
	std::string s;
	std::vector<JV> a;
	std::vector<std::pair<std::string, JV> > o;

	JV() : t(Null), n(0), b(false) {}

	const JV *get(const char *key) const
	{
		if (t != Obj)
			return 0;

		for (size_t i = 0; i < o.size(); ++i)
			if (o[i].first == key)
				return &o[i].second;

		return 0;
	}

	double num(double def) const { return t == Num ? n : def; }
	int integer(int def) const { return t == Num ? (int) n : def; }
	size_t size() const { return t == Arr ? a.size() : 0; }
};

class JsonParser
{
public:
	JsonParser(const char *d, size_t l) : p(d), end(d + l) {}

	JV parse()
	{
		JV v = value(0);
		ws();
		return v;
	}

private:
	const char *p;
	const char *end;

	void fail(const char *msg)
	{
		throw std::runtime_error(std::string("JSON: ") + msg);
	}

	void ws()
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			++p;
	}

	bool lit(const char *w)
	{
		size_t n = strlen(w);
		if ((size_t) (end - p) >= n && memcmp(p, w, n) == 0)
		{
			p += n;
			return true;
		}
		return false;
	}

	static void putUtf8(std::string &s, unsigned cp)
	{
		if (cp < 0x80)
			s += (char) cp;
		else if (cp < 0x800)
		{
			s += (char) (0xC0 | (cp >> 6));
			s += (char) (0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			s += (char) (0xE0 | (cp >> 12));
			s += (char) (0x80 | ((cp >> 6) & 0x3F));
			s += (char) (0x80 | (cp & 0x3F));
		}
		else
		{
			s += (char) (0xF0 | (cp >> 18));
			s += (char) (0x80 | ((cp >> 12) & 0x3F));
			s += (char) (0x80 | ((cp >> 6) & 0x3F));
			s += (char) (0x80 | (cp & 0x3F));
		}
	}

	unsigned hex4()
	{
		if (end - p < 4)
			fail("bad \\u escape");

		unsigned v = 0;

		for (int i = 0; i < 4; ++i)
		{
			char c = *p++;
			v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0';
			else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
			else fail("bad \\u escape");
		}

		return v;
	}

	std::string str()
	{
		std::string r;

		if (p >= end || *p != '"')
			fail("string expected");
		++p;

		while (p < end && *p != '"')
		{
			if (*p == '\\')
			{
				++p;
				if (p >= end)
					fail("bad escape");

				char c = *p++;
				switch (c)
				{
				case 'n': r += '\n'; break;
				case 't': r += '\t'; break;
				case 'r': r += '\r'; break;
				case 'b': r += '\b'; break;
				case 'f': r += '\f'; break;
				case 'u':
				{
					unsigned cp = hex4();
					if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 &&
					    p[0] == '\\' && p[1] == 'u')
					{
						p += 2;
						unsigned lo = hex4();
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					}
					putUtf8(r, cp);
					break;
				}
				default: r += c; break;
				}
			}
			else
				r += *p++;
		}

		if (p >= end)
			fail("unterminated string");
		++p;

		return r;
	}

	JV value(int depth)
	{
		if (depth > 64)
			fail("nesting too deep");

		ws();
		if (p >= end)
			fail("unexpected end");

		JV v;

		if (*p == '{')
		{
			++p;
			v.t = JV::Obj;
			ws();
			if (p < end && *p == '}') { ++p; return v; }

			for (;;)
			{
				ws();
				std::string k = str();
				ws();
				if (p >= end || *p != ':')
					fail("':' expected");
				++p;
				v.o.push_back(std::make_pair(k, value(depth + 1)));
				ws();
				if (p < end && *p == ',') { ++p; continue; }
				if (p < end && *p == '}') { ++p; break; }
				fail("',' or '}' expected");
			}
		}
		else if (*p == '[')
		{
			++p;
			v.t = JV::Arr;
			ws();
			if (p < end && *p == ']') { ++p; return v; }

			for (;;)
			{
				v.a.push_back(value(depth + 1));
				ws();
				if (p < end && *p == ',') { ++p; continue; }
				if (p < end && *p == ']') { ++p; break; }
				fail("',' or ']' expected");
			}
		}
		else if (*p == '"')
		{
			v.t = JV::Str;
			v.s = str();
		}
		else if (lit("true")) { v.t = JV::Bool; v.b = true; }
		else if (lit("false")) { v.t = JV::Bool; v.b = false; }
		else if (lit("null")) { v.t = JV::Null; }
		else
		{
			char *e = 0;
			v.n = strtod(p, &e);
			if (e == p)
				fail("unexpected character");
			v.t = JV::Num;
			p = e;
		}

		return v;
	}
};

/*---------------------------------------------------------------------------
 * Helpers
 *-------------------------------------------------------------------------*/
static std::vector<unsigned char> base64Decode(const char *s, size_t n)
{
	std::vector<unsigned char> out;
	unsigned acc = 0;
	int bits = 0;

	for (size_t i = 0; i < n; ++i)
	{
		char c = s[i];
		int v;

		if (c >= 'A' && c <= 'Z') v = c - 'A';
		else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
		else if (c >= '0' && c <= '9') v = c - '0' + 52;
		else if (c == '+' || c == '-') v = 62;
		else if (c == '/' || c == '_') v = 63;
		else continue; /* '=' padding, whitespace */

		acc = (acc << 6) | (unsigned) v;
		bits += 6;

		if (bits >= 8)
		{
			bits -= 8;
			out.push_back((unsigned char) ((acc >> bits) & 0xFF));
		}
	}

	return out;
}

static std::string baseName(const std::string &uri)
{
	size_t i = uri.find_last_of("/\\");
	return i == std::string::npos ? uri : uri.substr(i + 1);
}

static std::string urlDecode(const std::string &s)
{
	std::string r;

	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '%' && i + 2 < s.size())
		{
			char h[3] = { s[i+1], s[i+2], 0 };
			r += (char) strtol(h, 0, 16);
			i += 2;
		}
		else
			r += s[i];
	}

	return r;
}

struct Loader
{
	JV root;
	std::vector<std::vector<unsigned char> > buffers;
	const std::map<std::string, std::string> &externals;

	Loader(const std::map<std::string, std::string> &ext) : externals(ext) {}

	static void fail(const std::string &msg)
	{
		throw std::runtime_error("glTF: " + msg);
	}

	/* uri -> bytes (data: URI or supplied external file) */
	std::vector<unsigned char> loadUri(const std::string &uri)
	{
		if (uri.compare(0, 5, "data:") == 0)
		{
			size_t comma = uri.find(',');
			if (comma == std::string::npos)
				fail("bad data URI");

			if (uri.substr(0, comma).find("base64") == std::string::npos)
				fail("only base64 data URIs are supported");

			return base64Decode(uri.c_str() + comma + 1, uri.size() - comma - 1);
		}

		std::map<std::string, std::string>::const_iterator it = externals.find(uri);

		if (it == externals.end())
			it = externals.find(urlDecode(uri));
		if (it == externals.end())
			it = externals.find(baseName(urlDecode(uri)));

		if (it == externals.end())
			fail("missing external file '" + uri + "'");

		return std::vector<unsigned char>(it->second.begin(), it->second.end());
	}

	const JV &arr(const char *name) const
	{
		static JV empty;
		const JV *v = root.get(name);
		return v && v->t == JV::Arr ? *v : empty;
	}

	/* Bytes of a bufferView (returns pointer, size and stride) */
	const unsigned char *view(int idx, size_t &size, int &stride)
	{
		const JV &views = arr("bufferViews");

		if (idx < 0 || (size_t) idx >= views.size())
			fail("bad bufferView index");

		const JV &bv = views.a[idx];
		int bi = bv.get("buffer") ? bv.get("buffer")->integer(-1) : -1;

		if (bi < 0 || (size_t) bi >= buffers.size())
			fail("bad buffer index");

		size_t off = bv.get("byteOffset") ? (size_t) bv.get("byteOffset")->num(0) : 0;
		size_t len = bv.get("byteLength") ? (size_t) bv.get("byteLength")->num(0) : 0;

		if (off + len > buffers[bi].size())
			fail("bufferView out of range");

		size = len;
		stride = bv.get("byteStride") ? bv.get("byteStride")->integer(0) : 0;

		return buffers[bi].empty() ? 0 : &buffers[bi][0] + off;
	}

	static int compCount(const std::string &type)
	{
		if (type == "SCALAR") return 1;
		if (type == "VEC2") return 2;
		if (type == "VEC3") return 3;
		if (type == "VEC4") return 4;
		if (type == "MAT2") return 4;
		if (type == "MAT3") return 9;
		if (type == "MAT4") return 16;
		return 0;
	}

	static int compSize(int ct)
	{
		switch (ct)
		{
		case 5120: case 5121: return 1;
		case 5122: case 5123: return 2;
		case 5125: case 5126: return 4;
		}
		return 0;
	}

	/* Reads an accessor as floats (normalized ints are converted to 0..1 /
	 * -1..1 when 'normalized' is set in the file). */
	void readFloats(int idx, std::vector<float> &out, int &comps)
	{
		const JV &accs = arr("accessors");

		if (idx < 0 || (size_t) idx >= accs.size())
			fail("bad accessor index");

		const JV &ac = accs.a[idx];

		if (ac.get("sparse"))
			fail("sparse accessors are not supported");

		int ct = ac.get("componentType") ? ac.get("componentType")->integer(0) : 0;
		size_t count = ac.get("count") ? (size_t) ac.get("count")->num(0) : 0;
		std::string type = ac.get("type") ? ac.get("type")->s : "";
		bool norm = ac.get("normalized") && ac.get("normalized")->b;

		comps = compCount(type);
		int cs = compSize(ct);

		if (comps == 0 || cs == 0)
			fail("unsupported accessor type");

		out.assign(count * comps, 0.f);

		if (!ac.get("bufferView"))
			return; /* all zeros */

		size_t vsize;
		int stride;
		const unsigned char *base = view(ac.get("bufferView")->integer(-1), vsize, stride);
		size_t aoff = ac.get("byteOffset") ? (size_t) ac.get("byteOffset")->num(0) : 0;

		size_t elem = (size_t) comps * cs;
		size_t step = stride > 0 ? (size_t) stride : elem;

		if (count > 0 && aoff + (count - 1) * step + elem > vsize)
			fail("accessor out of range");

		for (size_t i = 0; i < count; ++i)
		{
			const unsigned char *src = base + aoff + i * step;

			for (int c = 0; c < comps; ++c)
			{
				const unsigned char *q = src + c * cs;
				float f = 0.f;

				switch (ct)
				{
				case 5126: { float v; memcpy(&v, q, 4); f = v; break; }
				case 5121: f = norm ? q[0] / 255.f : (float) q[0]; break;
				case 5120: { int8_t v = (int8_t) q[0];
				             f = norm ? (v / 127.f < -1.f ? -1.f : v / 127.f) : (float) v; break; }
				case 5123: { uint16_t v; memcpy(&v, q, 2);
				             f = norm ? v / 65535.f : (float) v; break; }
				case 5122: { int16_t v; memcpy(&v, q, 2);
				             f = norm ? (v / 32767.f < -1.f ? -1.f : v / 32767.f) : (float) v; break; }
				case 5125: { uint32_t v; memcpy(&v, q, 4); f = (float) v; break; }
				}

				out[i * comps + c] = f;
			}
		}
	}

	/* Integer data (indices, joints) read without float rounding issues */
	void readUInts(int idx, std::vector<unsigned int> &out, int &comps)
	{
		const JV &accs = arr("accessors");

		if (idx < 0 || (size_t) idx >= accs.size())
			fail("bad accessor index");

		const JV &ac = accs.a[idx];

		if (ac.get("sparse"))
			fail("sparse accessors are not supported");

		int ct = ac.get("componentType") ? ac.get("componentType")->integer(0) : 0;
		size_t count = ac.get("count") ? (size_t) ac.get("count")->num(0) : 0;
		std::string type = ac.get("type") ? ac.get("type")->s : "";

		comps = compCount(type);
		int cs = compSize(ct);

		if (comps == 0 || cs == 0 || ct == 5126)
			fail("unsupported integer accessor");

		out.assign(count * comps, 0);

		if (!ac.get("bufferView"))
			return;

		size_t vsize;
		int stride;
		const unsigned char *base = view(ac.get("bufferView")->integer(-1), vsize, stride);
		size_t aoff = ac.get("byteOffset") ? (size_t) ac.get("byteOffset")->num(0) : 0;
		size_t elem = (size_t) comps * cs;
		size_t step = stride > 0 ? (size_t) stride : elem;

		if (count > 0 && aoff + (count - 1) * step + elem > vsize)
			fail("accessor out of range");

		for (size_t i = 0; i < count; ++i)
		{
			const unsigned char *src = base + aoff + i * step;

			for (int c = 0; c < comps; ++c)
			{
				const unsigned char *q = src + c * cs;
				unsigned int v = 0;

				if (cs == 1) v = q[0];
				else if (cs == 2) { uint16_t t; memcpy(&t, q, 2); v = t; }
				else { uint32_t t; memcpy(&t, q, 4); v = t; }

				out[i * comps + c] = v;
			}
		}
	}
};

static void readVec(const JV *v, float *dst, int n)
{
	if (!v || v->t != JV::Arr)
		return;

	for (int i = 0; i < n && (size_t) i < v->a.size(); ++i)
		dst[i] = (float) v->a[i].num(dst[i]);
}

/*---------------------------------------------------------------------------
 * 4x4 matrices, column-major: m[col*4 + row]
 *-------------------------------------------------------------------------*/
static void matIdentity(float *m)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.f;
}

static void matMul(const float *a, const float *b, float *out)
{
	float r[16];

	for (int c = 0; c < 4; ++c)
		for (int row = 0; row < 4; ++row)
		{
			float s = 0.f;
			for (int k = 0; k < 4; ++k)
				s += a[k * 4 + row] * b[c * 4 + k];
			r[c * 4 + row] = s;
		}

	memcpy(out, r, sizeof(r));
}

static void matCompose(const float *t, const float *q, const float *s, float *m)
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float n = sqrtf(x*x + y*y + z*z + w*w);

	if (n > 0.f) { x /= n; y /= n; z /= n; w /= n; }
	else { x = y = z = 0.f; w = 1.f; }

	float xx = x*x, yy = y*y, zz = z*z;
	float xy = x*y, xz = x*z, yz = y*z;
	float wx = w*x, wy = w*y, wz = w*z;

	/* rotation (row r, column c) scaled by s[c] */
	m[0]  = (1.f - 2.f*(yy + zz)) * s[0];
	m[1]  = (2.f*(xy + wz)) * s[0];
	m[2]  = (2.f*(xz - wy)) * s[0];
	m[3]  = 0.f;

	m[4]  = (2.f*(xy - wz)) * s[1];
	m[5]  = (1.f - 2.f*(xx + zz)) * s[1];
	m[6]  = (2.f*(yz + wx)) * s[1];
	m[7]  = 0.f;

	m[8]  = (2.f*(xz + wy)) * s[2];
	m[9]  = (2.f*(yz - wx)) * s[2];
	m[10] = (1.f - 2.f*(xx + yy)) * s[2];
	m[11] = 0.f;

	m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1.f;
}

static void xformPoint(const float *m, float x, float y, float z, float *out)
{
	out[0] = m[0]*x + m[4]*y + m[8]*z  + m[12];
	out[1] = m[1]*x + m[5]*y + m[9]*z  + m[13];
	out[2] = m[2]*x + m[6]*y + m[10]*z + m[14];
}

/*---------------------------------------------------------------------------
 * Parsing
 *-------------------------------------------------------------------------*/
static void buildScene(Loader &L, Scene &sc)
{
	/* Images */
	const JV &images = L.arr("images");

	for (size_t i = 0; i < images.size(); ++i)
	{
		const JV &im = images.a[i];
		Image img;

		if (im.get("mimeType"))
			img.mime = im.get("mimeType")->s;

		if (im.get("bufferView"))
		{
			size_t size;
			int stride;
			const unsigned char *d = L.view(im.get("bufferView")->integer(-1), size, stride);
			img.data.assign(d, d + size);
		}
		else if (im.get("uri"))
			img.data = L.loadUri(im.get("uri")->s);

		sc.images.push_back(img);
	}

	/* Materials (texture -> image resolved here) */
	const JV &textures = L.arr("textures");
	const JV &mats = L.arr("materials");

	for (size_t i = 0; i < mats.size(); ++i)
	{
		const JV &mj = mats.a[i];
		Material m;

		if (mj.get("name"))
			m.name = mj.get("name")->s;

		const JV *pbr = mj.get("pbrMetallicRoughness");

		if (pbr)
		{
			readVec(pbr->get("baseColorFactor"), m.color, 4);

			const JV *bt = pbr->get("baseColorTexture");

			if (bt && bt->get("index"))
			{
				int ti = bt->get("index")->integer(-1);

				if (ti >= 0 && (size_t) ti < textures.size() &&
				    textures.a[ti].get("source"))
					m.image = textures.a[ti].get("source")->integer(-1);
			}
		}

		sc.materials.push_back(m);
	}

	/* Nodes */
	const JV &nodes = L.arr("nodes");
	sc.nodes.resize(nodes.size());

	for (size_t i = 0; i < nodes.size(); ++i)
	{
		const JV &nj = nodes.a[i];
		Node &n = sc.nodes[i];

		if (nj.get("name"))
			n.name = nj.get("name")->s;

		if (nj.get("matrix"))
		{
			n.hasMatrix = true;
			matIdentity(n.m);
			readVec(nj.get("matrix"), n.m, 16);
		}

		readVec(nj.get("translation"), n.t, 3);
		readVec(nj.get("rotation"), n.r, 4);
		readVec(nj.get("scale"), n.s, 3);
	}

	for (size_t i = 0; i < nodes.size(); ++i)
	{
		const JV *ch = nodes.a[i].get("children");

		if (!ch)
			continue;

		for (size_t k = 0; k < ch->size(); ++k)
		{
			int c = ch->a[k].integer(-1);

			if (c < 0 || (size_t) c >= sc.nodes.size() || c == (int) i)
				Loader::fail("bad node child index");

			sc.nodes[c].parent = (int) i;
		}
	}

	/* Skins */
	const JV &skins = L.arr("skins");

	for (size_t i = 0; i < skins.size(); ++i)
	{
		const JV &sj = skins.a[i];
		Skin sk;

		const JV *js = sj.get("joints");

		for (size_t k = 0; js && k < js->size(); ++k)
		{
			int j = js->a[k].integer(-1);

			if (j < 0 || (size_t) j >= sc.nodes.size())
				Loader::fail("bad skin joint");

			sk.joints.push_back(j);
		}

		if (sj.get("inverseBindMatrices"))
		{
			int comps;
			L.readFloats(sj.get("inverseBindMatrices")->integer(-1), sk.ibm, comps);

			if (comps != 16 || sk.ibm.size() < sk.joints.size() * 16)
				Loader::fail("bad inverseBindMatrices");
		}
		else
		{
			sk.ibm.resize(sk.joints.size() * 16);
			for (size_t k = 0; k < sk.joints.size(); ++k)
				matIdentity(&sk.ibm[k * 16]);
		}

		sc.skins.push_back(sk);
	}

	/* Meshes: one Primitive per (node using the mesh, primitive) */
	const JV &meshes = L.arr("meshes");

	for (size_t ni = 0; ni < nodes.size(); ++ni)
	{
		const JV *mi = nodes.a[ni].get("mesh");

		if (!mi)
			continue;

		int mesh = mi->integer(-1);

		if (mesh < 0 || (size_t) mesh >= meshes.size())
			Loader::fail("bad mesh index");

		int skin = nodes.a[ni].get("skin") ? nodes.a[ni].get("skin")->integer(-1) : -1;

		const JV *prims = meshes.a[mesh].get("primitives");

		for (size_t pi = 0; prims && pi < prims->size(); ++pi)
		{
			const JV &pj = prims->a[pi];
			int mode = pj.get("mode") ? pj.get("mode")->integer(4) : 4;

			if (mode != 4)
				continue; /* only triangle lists */

			const JV *attrs = pj.get("attributes");

			if (!attrs || !attrs->get("POSITION"))
				continue;

			Primitive pr;
			pr.node = (int) ni;
			pr.skin = skin;
			pr.material = pj.get("material") ? pj.get("material")->integer(-1) : -1;

			int comps;
			L.readFloats(attrs->get("POSITION")->integer(-1), pr.pos, comps);

			if (comps != 3)
				Loader::fail("POSITION must be VEC3");

			size_t vcount = pr.pos.size() / 3;

			if (attrs->get("TEXCOORD_0"))
			{
				L.readFloats(attrs->get("TEXCOORD_0")->integer(-1), pr.uv, comps);

				if (comps != 2 || pr.uv.size() / 2 != vcount)
					pr.uv.clear();
			}

			if (skin >= 0 && attrs->get("JOINTS_0") && attrs->get("WEIGHTS_0"))
			{
				std::vector<unsigned int> j;
				L.readUInts(attrs->get("JOINTS_0")->integer(-1), j, comps);

				if (comps == 4 && j.size() / 4 == vcount)
				{
					pr.joints.resize(j.size());
					for (size_t k = 0; k < j.size(); ++k)
						pr.joints[k] = (unsigned short) j[k];

					L.readFloats(attrs->get("WEIGHTS_0")->integer(-1), pr.weights, comps);

					if (comps != 4 || pr.weights.size() / 4 != vcount)
					{
						pr.joints.clear();
						pr.weights.clear();
					}
				}
			}

			if (pr.joints.empty())
				pr.skin = -1;

			if (pj.get("indices"))
			{
				std::vector<unsigned int> idx;
				L.readUInts(pj.get("indices")->integer(-1), idx, comps);
				pr.indices = idx;
			}
			else
			{
				pr.indices.resize(vcount);
				for (size_t k = 0; k < vcount; ++k)
					pr.indices[k] = (unsigned int) k;
			}

			pr.indices.resize(pr.indices.size() - pr.indices.size() % 3);

			for (size_t k = 0; k < pr.indices.size(); ++k)
				if (pr.indices[k] >= vcount)
					Loader::fail("index out of range");

			sc.prims.push_back(pr);
		}
	}

	/* Animations */
	const JV &anims = L.arr("animations");

	for (size_t ai = 0; ai < anims.size(); ++ai)
	{
		const JV &aj = anims.a[ai];
		Animation an;

		an.name = aj.get("name") ? aj.get("name")->s : "";

		if (an.name.empty())
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "animation_%d", (int) ai);
			an.name = buf;
		}

		const JV *samplers = aj.get("samplers");
		const JV *channels = aj.get("channels");

		for (size_t ci = 0; channels && ci < channels->size(); ++ci)
		{
			const JV &cj = channels->a[ci];
			const JV *target = cj.get("target");

			if (!target || !target->get("node") || !target->get("path"))
				continue;

			const std::string &path = target->get("path")->s;
			Channel ch;

			if (path == "translation") ch.path = PathTranslation;
			else if (path == "rotation") ch.path = PathRotation;
			else if (path == "scale") ch.path = PathScale;
			else continue; /* weights etc. */

			ch.node = target->get("node")->integer(-1);

			if (ch.node < 0 || (size_t) ch.node >= sc.nodes.size())
				continue;

			int si = cj.get("sampler") ? cj.get("sampler")->integer(-1) : -1;

			if (!samplers || si < 0 || (size_t) si >= samplers->size())
				Loader::fail("bad animation sampler");

			const JV &sj = samplers->a[si];

			std::string interp = sj.get("interpolation") ? sj.get("interpolation")->s : "LINEAR";
			ch.interp = interp == "STEP" ? InterpStep
			          : interp == "CUBICSPLINE" ? InterpCubic : InterpLinear;

			int comps;
			L.readFloats(sj.get("input") ? sj.get("input")->integer(-1) : -1, ch.times, comps);
			L.readFloats(sj.get("output") ? sj.get("output")->integer(-1) : -1, ch.values, comps);

			int per = ch.path == PathRotation ? 4 : 3;
			size_t need = ch.times.size() * per * (ch.interp == InterpCubic ? 3 : 1);

			if (ch.times.empty() || ch.values.size() < need)
				Loader::fail("animation sampler data size mismatch");

			if (ch.times.back() > an.duration)
				an.duration = ch.times.back();

			an.channels.push_back(ch);
		}

		sc.anims.push_back(an);
	}
}

bool parse(const unsigned char *data, size_t len,
           const std::map<std::string, std::string> &externals,
           Scene &out, std::string &err)
{
	try
	{
		Loader L(externals);
		const unsigned char *bin = 0;
		size_t binLen = 0;
		const char *json = (const char*) data;
		size_t jsonLen = len;

		if (len >= 12 && memcmp(data, "glTF", 4) == 0)
		{
			uint32_t version, total;
			memcpy(&version, data + 4, 4);
			memcpy(&total, data + 8, 4);

			if (version != 2)
				Loader::fail("only glTF 2.0 is supported");

			if (total > len)
				total = (uint32_t) len;

			json = 0;
			jsonLen = 0;

			size_t off = 12;

			while (off + 8 <= total)
			{
				uint32_t clen, ctype;
				memcpy(&clen, data + off, 4);
				memcpy(&ctype, data + off + 4, 4);
				off += 8;

				if (off + clen > total)
					Loader::fail("truncated GLB chunk");

				if (ctype == 0x4E4F534A && !json)
				{
					json = (const char*) data + off;
					jsonLen = clen;
				}
				else if (ctype == 0x004E4942 && !bin)
				{
					bin = data + off;
					binLen = clen;
				}

				off += (clen + 3) & ~3u;
			}

			if (!json)
				Loader::fail("GLB has no JSON chunk");
		}

		JsonParser jp(json, jsonLen);
		L.root = jp.parse();

		if (L.root.t != JV::Obj)
			Loader::fail("root is not an object");

		const JV &bufs = L.arr("buffers");

		for (size_t i = 0; i < bufs.size(); ++i)
		{
			const JV *uri = bufs.a[i].get("uri");

			if (uri)
				L.buffers.push_back(L.loadUri(uri->s));
			else if (i == 0 && bin)
				L.buffers.push_back(std::vector<unsigned char>(bin, bin + binLen));
			else
				Loader::fail("buffer without data");
		}

		out = Scene();
		buildScene(L, out);

		if (out.prims.empty())
			Loader::fail("file has no triangle meshes");
	}
	catch (const std::exception &e)
	{
		err = e.what();
		return false;
	}

	return true;
}

/*---------------------------------------------------------------------------
 * Animation
 *-------------------------------------------------------------------------*/
static void sampleChannel(const Channel &ch, float time, float *out)
{
	const int per = ch.path == PathRotation ? 4 : 3;
	const size_t n = ch.times.size();
	const size_t stride = ch.interp == InterpCubic ? per * 3 : per;
	/* cubic layout per key: in-tangent, value, out-tangent */
	const size_t valOff = ch.interp == InterpCubic ? per : 0;

	if (time <= ch.times[0] || n == 1)
	{
		for (int c = 0; c < per; ++c)
			out[c] = ch.values[valOff + c];
		return;
	}

	if (time >= ch.times[n - 1])
	{
		for (int c = 0; c < per; ++c)
			out[c] = ch.values[(n - 1) * stride + valOff + c];
		return;
	}

	size_t lo = 0, hi = n - 1;

	while (hi - lo > 1)
	{
		size_t mid = (lo + hi) / 2;
		if (ch.times[mid] <= time) lo = mid; else hi = mid;
	}

	const float t0 = ch.times[lo], t1 = ch.times[lo + 1];
	const float dt = t1 - t0;
	const float u = dt > 0.f ? (time - t0) / dt : 0.f;

	const float *v0 = &ch.values[lo * stride + valOff];
	const float *v1 = &ch.values[(lo + 1) * stride + valOff];

	if (ch.interp == InterpStep)
	{
		for (int c = 0; c < per; ++c)
			out[c] = v0[c];
		return;
	}

	if (ch.interp == InterpCubic)
	{
		const float *b0 = &ch.values[lo * stride + per * 2];       /* out-tangent of lo */
		const float *a1 = &ch.values[(lo + 1) * stride];           /* in-tangent of lo+1 */
		const float u2 = u * u, u3 = u2 * u;
		const float h00 = 2*u3 - 3*u2 + 1, h10 = u3 - 2*u2 + u;
		const float h01 = -2*u3 + 3*u2,    h11 = u3 - u2;

		for (int c = 0; c < per; ++c)
			out[c] = h00 * v0[c] + h10 * dt * b0[c] + h01 * v1[c] + h11 * dt * a1[c];

		if (ch.path == PathRotation)
		{
			float l = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
			if (l > 0.f) for (int c = 0; c < 4; ++c) out[c] /= l;
		}
		return;
	}

	if (ch.path == PathRotation)
	{
		/* slerp (shortest path) */
		float dot = v0[0]*v1[0] + v0[1]*v1[1] + v0[2]*v1[2] + v0[3]*v1[3];
		float b[4] = { v1[0], v1[1], v1[2], v1[3] };

		if (dot < 0.f)
		{
			dot = -dot;
			for (int c = 0; c < 4; ++c) b[c] = -b[c];
		}

		float s0, s1;

		if (dot > 0.9995f)
		{
			s0 = 1.f - u;
			s1 = u;
		}
		else
		{
			float th = acosf(dot);
			float sn = sinf(th);
			s0 = sinf((1.f - u) * th) / sn;
			s1 = sinf(u * th) / sn;
		}

		for (int c = 0; c < 4; ++c)
			out[c] = s0 * v0[c] + s1 * b[c];

		float l = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
		if (l > 0.f) for (int c = 0; c < 4; ++c) out[c] /= l;
		return;
	}

	for (int c = 0; c < per; ++c)
		out[c] = v0[c] + (v1[c] - v0[c]) * u;
}

void evalWorlds(const Scene &sc, int anim, float time, std::vector<float> &worlds)
{
	const size_t n = sc.nodes.size();

	std::vector<float> t(n * 3), r(n * 4), s(n * 3);

	for (size_t i = 0; i < n; ++i)
	{
		memcpy(&t[i*3], sc.nodes[i].t, sizeof(float) * 3);
		memcpy(&r[i*4], sc.nodes[i].r, sizeof(float) * 4);
		memcpy(&s[i*3], sc.nodes[i].s, sizeof(float) * 3);
	}

	if (anim >= 0 && (size_t) anim < sc.anims.size())
	{
		const Animation &an = sc.anims[anim];

		if (time < 0.f) time = 0.f;
		if (time > an.duration) time = an.duration;

		for (size_t c = 0; c < an.channels.size(); ++c)
		{
			const Channel &ch = an.channels[c];
			float v[4];

			sampleChannel(ch, time, v);

			if (ch.path == PathTranslation) memcpy(&t[ch.node*3], v, sizeof(float) * 3);
			else if (ch.path == PathRotation) memcpy(&r[ch.node*4], v, sizeof(float) * 4);
			else memcpy(&s[ch.node*3], v, sizeof(float) * 3);
		}
	}

	worlds.assign(n * 16, 0.f);
	std::vector<char> done(n, 0);

	/* parents are resolved on demand (no ordering assumption) */
	for (size_t start = 0; start < n; ++start)
	{
		if (done[start])
			continue;

		std::vector<int> chain;
		int cur = (int) start;

		while (cur >= 0 && !done[cur] && chain.size() <= n)
		{
			chain.push_back(cur);
			cur = sc.nodes[cur].parent;
		}

		for (size_t k = chain.size(); k-- > 0;)
		{
			int i = chain[k];
			float local[16];

			if (sc.nodes[i].hasMatrix)
				memcpy(local, sc.nodes[i].m, sizeof(local));
			else
				matCompose(&t[i*3], &r[i*4], &s[i*3], local);

			int p = sc.nodes[i].parent;

			if (p >= 0 && done[p])
				matMul(&worlds[p*16], local, &worlds[i*16]);
			else
				memcpy(&worlds[i*16], local, sizeof(local));

			done[i] = 1;
		}
	}
}

void posePrimitive(const Scene &sc, const Primitive &pr,
                   const std::vector<float> &worlds, std::vector<float> &out)
{
	const size_t vc = pr.pos.size() / 3;

	if (pr.skin >= 0 && !pr.joints.empty())
	{
		const Skin &sk = sc.skins[pr.skin];
		const size_t jc = sk.joints.size();
		std::vector<float> jm(jc * 16);

		for (size_t k = 0; k < jc; ++k)
			matMul(&worlds[sk.joints[k] * 16], &sk.ibm[k * 16], &jm[k * 16]);

		for (size_t v = 0; v < vc; ++v)
		{
			const float x = pr.pos[v*3], y = pr.pos[v*3+1], z = pr.pos[v*3+2];
			float acc[3] = { 0.f, 0.f, 0.f };
			float wsum = 0.f;

			for (int i = 0; i < 4; ++i)
			{
				const float w = pr.weights[v*4 + i];
				const size_t j = pr.joints[v*4 + i];

				if (w <= 0.f || j >= jc)
					continue;

				float p[3];
				xformPoint(&jm[j * 16], x, y, z, p);
				acc[0] += w * p[0]; acc[1] += w * p[1]; acc[2] += w * p[2];
				wsum += w;
			}

			if (wsum > 0.f)
			{
				acc[0] /= wsum; acc[1] /= wsum; acc[2] /= wsum;
			}
			else
			{
				acc[0] = x; acc[1] = y; acc[2] = z;
			}

			out.push_back(acc[0]); out.push_back(acc[1]); out.push_back(acc[2]);
		}
	}
	else
	{
		const float *m = &worlds[pr.node * 16];

		for (size_t v = 0; v < vc; ++v)
		{
			float p[3];
			xformPoint(m, pr.pos[v*3], pr.pos[v*3+1], pr.pos[v*3+2], p);
			out.push_back(p[0]); out.push_back(p[1]); out.push_back(p[2]);
		}
	}
}

}
