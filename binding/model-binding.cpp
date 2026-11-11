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

#if RAPI_FULL > 187
DEF_TYPE(Model);
#else
DEF_ALLOCFUNC(Model);
#endif

RB_METHOD(modelInitialize) {
  const char *data;
  int len;
  rb_get_args(argc, argv, "s", &data, &len RB_ARG_END);

  Model *m = 0;
  GFX_GUARD_EXC(m = new Model(data, (size_t) len););

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

RB_METHOD(modelSetVisible) {
  const char *name;
  bool visible;
  rb_get_args(argc, argv, "zb", &name, &visible RB_ARG_END);

  Model *m = getPrivateData<Model>(self);
  GFX_GUARD_EXC(m->setVisible(name, visible););

  return self;
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
  _rb_define_method(klass, "render", modelRender);
}
