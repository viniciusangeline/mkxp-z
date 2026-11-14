/*
** model-binding.cpp
**
** DS Overworld addon for mkxp-z: Ruby binding for the Model class.
**
**   m = Model.new(File.read("Graphics/Models/house.obj"))
**   m.materials                       # => ["wall", "roof", ...]
**   m.set_texture("wall", Bitmap.new("Graphics/Models/wall.png"))
**   m.set_color("roof", 0.8, 0.2, 0.2)       # untextured material
**   m.set_visible("shadow", false)
**   m.triangles; m.vertices; m.bounds         # polygon budget helpers
**   m.render(bitmap, [pitch, focal, zoom, cx, cy, tx, ty,
**                     px, py, pz, scale, yaw])
**
** glTF (.glb bytes, or .gltf text + {uri => bytes}) is detected automatically:
**   m = Model.new(File.binread("x.glb"))
**   m.animations   # => [["idle", 2.0], ...]
**   m.set_pose("idle", 0.5)   # CPU pose; shown by the next render
**   w, h, rgba = m.image_rgba(m.material_image("mat"))
**
** This file is part of mkxp.
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
*/

#include "binding-types.h"
#include "binding-util.h"
#include "disposable-binding.h"
#include "model.h"
#include "bitmap.h"

#include <string.h>

#include <map>
#include <string>

#if RAPI_FULL > 187
DEF_TYPE(Model);
#else
DEF_ALLOCFUNC(Model);
#endif

static int findMaterial(Model *m, const char *name) {
  int n = 0;
  GFX_GUARD_EXC(n = m->materialCount(););

  for (int i = 0; i < n; ++i)
    if (strcmp(m->materialName(i), name) == 0)
      return i;

  rb_raise(rb_eArgError, "unknown material '%s'", name);
  return -1;
}

RB_METHOD(modelInitialize) {
  VALUE str = Qnil;
  VALUE files = Qnil;
  /* "o|o": no mkxp-z, 's' e 'S' NAO avancam o ponteiro de argumentos (so 'o',
     'z', 'i', 'f', 'b'), entao o 2o argumento era lido como a mesma string.
     Pegamos os dois como objetos e validamos na mao. */
  rb_get_args(argc, argv, "o|o", &str, &files RB_ARG_END);
  Check_Type(str, T_STRING);
  const char *data = RSTRING_PTR(str);
  int len = (int) RSTRING_LEN(str);

  /* Optional: { "uri" => bytes } with the external files of a .gltf */
  std::map<std::string, std::string> externals;

  if (!NIL_P(files)) {
    Check_Type(files, T_HASH);

    VALUE keys = rb_funcall(files, rb_intern("keys"), 0);

    for (long i = 0; i < RARRAY_LEN(keys); ++i) {
      VALUE k = rb_ary_entry(keys, i);
      VALUE v = rb_hash_aref(files, k);
      StringValue(k);
      StringValue(v);
      externals[std::string(RSTRING_PTR(k), RSTRING_LEN(k))] =
          std::string(RSTRING_PTR(v), RSTRING_LEN(v));
    }
  }

  Model *m = 0;
  GFX_GUARD_EXC(m = new Model(data, (size_t) len, &externals););

  setPrivateData(self, m);

  return self;
}

RB_METHOD(modelMaterials) {
  RB_UNUSED_PARAM;

  Model *m = getPrivateData<Model>(self);

  VALUE ary = rb_ary_new();
  int n = 0;

  GFX_GUARD_EXC(n = m->materialCount(););

  for (int i = 0; i < n; ++i)
    rb_ary_push(ary, rb_str_new_cstr(m->materialName(i)));

  return ary;
}

RB_METHOD(modelTriangles) {
  RB_UNUSED_PARAM;

  Model *m = getPrivateData<Model>(self);
  int n = 0;
  GFX_GUARD_EXC(n = m->triangleCount(););
  return INT2NUM(n);
}

RB_METHOD(modelVertices) {
  RB_UNUSED_PARAM;

  Model *m = getPrivateData<Model>(self);
  int n = 0;
  GFX_GUARD_EXC(n = m->vertexCount(););
  return INT2NUM(n);
}

RB_METHOD(modelMaterialTriangles) {
  const char *name;
  rb_get_args(argc, argv, "z", &name RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  int result = -1;

  GFX_GUARD_EXC(
    int n = m->materialCount();
    for (int i = 0; i < n; ++i)
      if (strcmp(m->materialName(i), name) == 0)
        result = m->materialTriangles(i);
  );

  if (result < 0)
    rb_raise(rb_eArgError, "unknown material '%s'", name);

  return INT2NUM(result);
}

RB_METHOD(modelBounds) {
  RB_UNUSED_PARAM;

  Model *m = getPrivateData<Model>(self);
  float b[6] = {0, 0, 0, 0, 0, 0};

  GFX_GUARD_EXC(m->getBounds(b););

  VALUE ary = rb_ary_new();
  for (int i = 0; i < 6; ++i)
    rb_ary_push(ary, rb_float_new(b[i]));

  return ary;
}

RB_METHOD(modelSetTexture) {
  const char *name;
  VALUE bmpObj;
  rb_get_args(argc, argv, "zo", &name, &bmpObj RB_ARG_END);

  Model *m = getPrivateData<Model>(self);

  Bitmap *bmp = 0;
  if (!NIL_P(bmpObj))
    bmp = getPrivateDataCheck<Bitmap>(bmpObj, BitmapType);

  GFX_GUARD_EXC(m->setTexture(name, bmp););

  /* Keep the Ruby Bitmap alive for as long as this model references it */
  VALUE hash = rb_iv_get(self, "textures_");
  if (NIL_P(hash)) {
    hash = rb_hash_new();
    rb_iv_set(self, "textures_", hash);
  }
  rb_hash_aset(hash, rb_str_new_cstr(name), bmpObj);

  return self;
}

RB_METHOD(modelSetColor) {
  const char *name;
  /* rb_get_args' 'f' format writes a double */
  double r, g, b;
  double a = 1.0;
  rb_get_args(argc, argv, "zfff|f", &name, &r, &g, &b, &a RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  GFX_GUARD_EXC(m->setColor(name, (float) r, (float) g, (float) b, (float) a););

  return self;
}

RB_METHOD(modelSetUV) {
  const char *name;
  double ox, oy;
  double sx = 1.0, sy = 1.0;
  rb_get_args(argc, argv, "zff|ff", &name, &ox, &oy, &sx, &sy RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  GFX_GUARD_EXC(m->setUV(name, (float) ox, (float) oy, (float) sx, (float) sy););

  return self;
}

RB_METHOD(modelSetVisible) {
  const char *name;
  bool visible;
  rb_get_args(argc, argv, "zb", &name, &visible RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  GFX_GUARD_EXC(m->setVisible(name, visible););

  return self;
}

RB_METHOD(modelIsGltf) {
  RB_UNUSED_PARAM;

  Model *m = getPrivateData<Model>(self);
  bool v = false;
  GFX_GUARD_EXC(v = m->isGltf(););
  return v ? Qtrue : Qfalse;
}

/* => [["idle", 2.0], ["open", 0.8], ...] */
RB_METHOD(modelAnimations) {
  RB_UNUSED_PARAM;

  Model *m = getPrivateData<Model>(self);
  int n = 0;
  GFX_GUARD_EXC(n = m->animationCount(););

  VALUE ary = rb_ary_new();

  for (int i = 0; i < n; ++i) {
    VALUE pair = rb_ary_new();
    rb_ary_push(pair, rb_str_new_cstr(m->animationName(i)));
    rb_ary_push(pair, rb_float_new(m->animationDuration(i)));
    rb_ary_push(ary, pair);
  }

  return ary;
}

/* set_pose("walk", 0.5)  /  set_pose(nil, 0) for the rest pose */
RB_METHOD(modelSetPose) {
  VALUE anim;
  double time;
  rb_get_args(argc, argv, "of", &anim, &time RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  int index = -1;

  if (!NIL_P(anim)) {
    if (RB_TYPE_P(anim, T_STRING)) {
      const char *name = StringValueCStr(anim);
      int n = 0;
      GFX_GUARD_EXC(n = m->animationCount(););
      index = -2;

      for (int i = 0; i < n; ++i)
        if (strcmp(m->animationName(i), name) == 0)
          index = i;

      if (index == -2)
        rb_raise(rb_eArgError, "unknown animation '%s'", name);
    } else
      index = NUM2INT(anim);
  }

  GFX_GUARD_EXC(m->setPose(index, (float) time););

  return self;
}

RB_METHOD(modelImageCount) {
  RB_UNUSED_PARAM;

  Model *m = getPrivateData<Model>(self);
  int n = 0;
  GFX_GUARD_EXC(n = m->imageCount(););
  return INT2NUM(n);
}

/* Index of the texture image used by a material, or -1 */
RB_METHOD(modelMaterialImage) {
  const char *name;
  rb_get_args(argc, argv, "z", &name RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  int mi = findMaterial(m, name);
  int img = -1;
  GFX_GUARD_EXC(img = m->materialImage(mi););
  return INT2NUM(img);
}

RB_METHOD(modelMaterialColor) {
  const char *name;
  rb_get_args(argc, argv, "z", &name RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  int mi = findMaterial(m, name);
  float c[4] = {0, 0, 0, 1};
  GFX_GUARD_EXC(m->materialColor(mi, c););

  VALUE ary = rb_ary_new();
  for (int i = 0; i < 4; ++i)
    rb_ary_push(ary, rb_float_new(c[i]));
  return ary;
}

/* => [width, height, rgba_bytes]; feed it to Bitmap#raw_data= */
RB_METHOD(modelImageRGBA) {
  int index;
  rb_get_args(argc, argv, "i", &index RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  int w = 0, h = 0;
  std::string pixels;
  GFX_GUARD_EXC(m->imageRGBA(index, w, h, pixels););

  VALUE ary = rb_ary_new();
  rb_ary_push(ary, INT2NUM(w));
  rb_ary_push(ary, INT2NUM(h));
  rb_ary_push(ary, rb_str_new(pixels.data(), (long) pixels.size()));
  return ary;
}

RB_METHOD(modelToObj) {
  RB_UNUSED_PARAM;

  Model *m = getPrivateData<Model>(self);
  std::string text;
  GFX_GUARD_EXC(text = m->toObj(););
  return rb_str_new(text.data(), (long) text.size());
}

RB_METHOD(modelRender) {
  VALUE bmpObj;
  VALUE params;
  rb_get_args(argc, argv, "oo", &bmpObj, &params RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  Bitmap *bmp = getPrivateDataCheck<Bitmap>(bmpObj, BitmapType);

  Check_Type(params, T_ARRAY);

  if (RARRAY_LEN(params) < 12)
    rb_raise(rb_eArgError,
             "render parameters need 12 numbers: pitch, focal, zoom, "
             "center_x, center_y, target_x, target_y, x, y, z, scale, yaw");

  double v[12];
  for (int i = 0; i < 12; ++i)
    v[i] = NUM2DBL(rb_ary_entry(params, i));

  Model::RenderParams rp;
  rp.pitchDeg = (float) v[0];
  rp.focal    = (float) v[1];
  rp.zoom     = (float) v[2];
  rp.centerX  = (float) v[3];
  rp.centerY  = (float) v[4];
  rp.targetX  = (int) v[5];
  rp.targetY  = (int) v[6];
  rp.posX     = (float) v[7];
  rp.posY     = (float) v[8];
  rp.posZ     = (float) v[9];
  rp.scale    = (float) v[10];
  rp.yawDeg   = (float) v[11];

  GFX_GUARD_EXC(m->render(*bmp, rp););

  return self;
}

void modelBindingInit() {
  VALUE klass = rb_define_class("Model", rb_cObject);
#if RAPI_FULL > 187
  rb_define_alloc_func(klass, classAllocate<&ModelType>);
#else
  rb_define_alloc_func(klass, ModelAllocate);
#endif

  disposableBindingInit<Model>(klass);

  _rb_define_method(klass, "initialize", modelInitialize);
  _rb_define_method(klass, "materials", modelMaterials);
  _rb_define_method(klass, "triangles", modelTriangles);
  _rb_define_method(klass, "vertices", modelVertices);
  _rb_define_method(klass, "material_triangles", modelMaterialTriangles);
  _rb_define_method(klass, "bounds", modelBounds);
  _rb_define_method(klass, "set_texture", modelSetTexture);
  _rb_define_method(klass, "set_color", modelSetColor);
  _rb_define_method(klass, "set_visible", modelSetVisible);
  _rb_define_method(klass, "set_uv", modelSetUV);
  _rb_define_method(klass, "render", modelRender);
  _rb_define_method(klass, "gltf?", modelIsGltf);
  _rb_define_method(klass, "animations", modelAnimations);
  _rb_define_method(klass, "set_pose", modelSetPose);
  _rb_define_method(klass, "image_count", modelImageCount);
  _rb_define_method(klass, "material_image", modelMaterialImage);
  _rb_define_method(klass, "material_color", modelMaterialColor);
  _rb_define_method(klass, "image_rgba", modelImageRGBA);
  _rb_define_method(klass, "to_obj", modelToObj);
}
