#ifndef PCSIM_GU_H
#define PCSIM_GU_H
void guMtxIdentity(Mtx m); void guOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
void guMtxConcat(Mtx a, Mtx b, Mtx ab); void guMtxTrans(Mtx m, f32 x, f32 y, f32 z); void guMtxScale(Mtx m, f32 x, f32 y, f32 z);
#endif
