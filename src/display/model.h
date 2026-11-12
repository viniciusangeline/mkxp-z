/*
** model.h
**
** DS Overworld addon for mkxp-z: renders simple textured 3D models
** (Wavefront OBJ) into a Bitmap with a depth buffer, using the same
** perspective camera as the "tilted floor" renderer written in Ruby.
**
** The result is an ordinary Bitmap, so it can be shown with a regular
** Sprite (z-ordering, tone, opacity, viewports all keep working).
**
** This file is part of mkxp.
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
*/

#ifndef MODEL_H
#define MODEL_H

#include "disposable.h"

#include <stddef.h>

#include <map>
#include <string>

class Bitmap;
struct ModelPrivate;

class Model : public Disposable
{
public:
	struct RenderParams
	{
		/* Camera (same meaning as the Ruby DSOverworld::Camera) */
		float pitchDeg;
		float focal;
		float zoom;
		/* Screen center the camera looks at */
		float centerX, centerY;
		/* Position of the target bitmap on screen */
		int targetX, targetY;
		/* Model origin relative to the focus point, in map pixels:
		 * x = east, y = up, z = south */
		float posX, posY, posZ;
		/* Map pixels per model unit */
		float scale;
		/* Rotation around the up axis, in degrees */
		float yawDeg;
	};

	/* Accepts Wavefront OBJ text, or a glTF 2.0 file (.glb bytes, or .gltf
	 * JSON text plus its external files in 'externals': uri -> bytes). */
	Model(const char *objData, size_t objLen,
	      const std::map<std::string, std::string> *externals = 0);
	~Model();

	int vertexCount() const;
	int triangleCount() const;
	int materialCount() const;
	const char *materialName(int index) const;
	int materialTriangles(int index) const;
	/* minX, minY, minZ, maxX, maxY, maxZ */
	void getBounds(float out[6]) const;

	void setTexture(const char *material, Bitmap *bitmap);
	void setColor(const char *material, float r, float g, float b, float a);
	void setVisible(const char *material, bool visible);

	/* glTF extras. Animations are evaluated on the CPU: setPose() poses the
	 * model (anim < 0 = rest pose) and the next render() shows it. */
	bool isGltf() const;
	int animationCount() const;
	const char *animationName(int index) const;
	float animationDuration(int index) const;
	void setPose(int anim, float time);

	/* Embedded/linked texture images of a glTF file, decoded to RGBA bytes */
	int imageCount() const;
	int materialImage(int materialIndex) const;
	void materialColor(int materialIndex, float out[4]) const;
	void imageRGBA(int image, int &w, int &h, std::string &pixels) const;

	/* Current pose as Wavefront OBJ text (used to merge static tile models) */
	std::string toObj() const;

	/* Renders the model into 'target' (clearing it to transparent first). */
	void render(Bitmap &target, const RenderParams &params);

private:
	ModelPrivate *p;

	void releaseResources();
	const char *klassName() const { return "model"; }
};

#endif // MODEL_H
