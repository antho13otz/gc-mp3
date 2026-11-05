/* GC Station sur PC : GX (processeur graphique) -> moteur logiciel (../pcsim_gx.c) */
#ifndef PCSIM_GX_H
#define PCSIM_GX_H
#define GX_FALSE 0
#define GX_TRUE 1
#define GX_DISABLE 0
#define GX_ENABLE 1
#define GX_FIFO_SIZE (256 * 1024)
typedef struct { u8 r, g, b, a; } GXColor;
typedef struct { u32 val[8]; } GXTexObj;
typedef struct { u32 val[3]; } GXTlutObj;
typedef struct { u8 pad[128]; } GXFifoObj;
typedef f32 Mtx[3][4];
typedef f32 (*MtxP)[4];
typedef f32 Mtx44[4][4];
typedef f32 (*Mtx44P)[4];
typedef struct { f32 x, y, z; } guVector;
/* primitives */
#define GX_POINTS 0xB8
#define GX_LINES 0xA8
#define GX_LINESTRIP 0xB0
#define GX_TRIANGLES 0x90
#define GX_TRIANGLESTRIP 0x98
#define GX_TRIANGLEFAN 0xA0
#define GX_QUADS 0x80
#define GX_VTXFMT0 0
/* attributs */
#define GX_VA_PTNMTXIDX 0
#define GX_VA_POS 9
#define GX_VA_NRM 10
#define GX_VA_CLR0 11
#define GX_VA_CLR1 12
#define GX_VA_TEX0 13
#define GX_NONE 0
#define GX_DIRECT 1
#define GX_INDEX8 2
#define GX_INDEX16 3
#define GX_POS_XY 0
#define GX_POS_XYZ 1
#define GX_CLR_RGB 0
#define GX_CLR_RGBA 1
#define GX_TEX_S 0
#define GX_TEX_ST 1
#define GX_U8 0
#define GX_S8 1
#define GX_U16 2
#define GX_S16 3
#define GX_F32 4
#define GX_RGB565 0
#define GX_RGB8 1
#define GX_RGBX8 2
#define GX_RGBA4 3
#define GX_RGBA6 4
#define GX_RGBA8 5
/* textures */
#define GX_TF_I4 0x0
#define GX_TF_I8 0x1
#define GX_TF_IA4 0x2
#define GX_TF_IA8 0x3
#define GX_TF_RGB565 0x4
#define GX_TF_RGB5A3 0x5
#define GX_TF_RGBA8 0x6
#define GX_TF_CMPR 0xE
#define GX_CLAMP 0
#define GX_REPEAT 1
#define GX_MIRROR 2
#define GX_NEAR 0
#define GX_LINEAR 1
#define GX_NEAR_MIP_NEAR 2
#define GX_LIN_MIP_NEAR 3
#define GX_NEAR_MIP_LIN 4
#define GX_LIN_MIP_LIN 5
#define GX_TEXMAP0 0
#define GX_TEXMAP1 1
#define GX_TEXMAP2 2
#define GX_TEXMAP3 3
#define GX_TEXMAP7 7
#define GX_TEXMAP_NULL 0xff
#define GX_TEXCOORD0 0
#define GX_TEXCOORD1 1
#define GX_TEXCOORDNULL 0xff
#define GX_TG_MTX3x4 0
#define GX_TG_MTX2x4 1
#define GX_TG_POS 0
#define GX_TG_TEX0 4
#define GX_IDENTITY 60
#define GX_PNMTX0 0
#define GX_ANISO_1 0
/* TEV */
#define GX_TEVSTAGE0 0
#define GX_TEVSTAGE1 1
#define GX_MODULATE 0
#define GX_DECAL 1
#define GX_BLEND 2
#define GX_REPLACE 3
#define GX_PASSCLR 4
#define GX_CC_CPREV 0
#define GX_CC_APREV 1
#define GX_CC_C0 2
#define GX_CC_A0 3
#define GX_CC_TEXC 8
#define GX_CC_TEXA 9
#define GX_CC_RASC 10
#define GX_CC_RASA 11
#define GX_CC_ONE 12
#define GX_CC_HALF 13
#define GX_CC_KONST 14
#define GX_CC_ZERO 15
#define GX_CA_APREV 0
#define GX_CA_A0 1
#define GX_CA_TEXA 4
#define GX_CA_RASA 5
#define GX_CA_KONST 6
#define GX_CA_ZERO 7
#define GX_TEV_ADD 0
#define GX_TEV_SUB 1
#define GX_TB_ZERO 0
#define GX_TB_ADDHALF 1
#define GX_TB_SUBHALF 2
#define GX_CS_SCALE_1 0
#define GX_CS_SCALE_2 1
#define GX_CS_SCALE_4 2
#define GX_CS_DIVIDE_2 3
#define GX_TEVPREV 0
#define GX_TEVREG0 1
#define GX_COLOR0 0
#define GX_ALPHA0 2
#define GX_COLOR0A0 4
#define GX_COLORNULL 0xff
#define GX_SRC_REG 0
#define GX_SRC_VTX 1
#define GX_DF_NONE 0
#define GX_LIGHTNULL 0
#define GX_AF_NONE 2
/* pixels */
#define GX_BM_NONE 0
#define GX_BM_BLEND 1
#define GX_BM_LOGIC 2
#define GX_BM_SUBTRACT 3
#define GX_BL_ZERO 0
#define GX_BL_ONE 1
#define GX_BL_SRCCLR 2
#define GX_BL_INVSRCCLR 3
#define GX_BL_SRCALPHA 4
#define GX_BL_INVSRCALPHA 5
#define GX_BL_DSTALPHA 6
#define GX_BL_INVDSTALPHA 7
#define GX_LO_CLEAR 0
#define GX_LO_COPY 3
#define GX_NEVER 0
#define GX_LESS 1
#define GX_EQUAL 2
#define GX_LEQUAL 3
#define GX_ALWAYS 7
#define GX_CULL_NONE 0
#define GX_CULL_FRONT 1
#define GX_CULL_BACK 2
#define GX_CULL_ALL 3
#define GX_PF_RGB8_Z24 0
#define GX_PF_RGBA6_Z24 1
#define GX_PF_RGB565_Z16 2
#define GX_ZC_LINEAR 0
#define GX_GM_1_0 0
#define GX_ORTHOGRAPHIC 1
#define GX_PERSPECTIVE 0
typedef void (*GXDrawDoneCallback)(void);
GXFifoObj *GX_Init(void *base, u32 size);
void GX_SetCopyClear(GXColor c, u32 z); void GX_SetViewport(f32 x, f32 y, f32 w, f32 h, f32 n, f32 f);
f32 GX_GetYScaleFactor(u16 efb, u16 xfb); u32 GX_SetDispCopyYScale(f32 s);
void GX_SetScissor(u32 x, u32 y, u32 w, u32 h); void GX_SetDispCopySrc(u16 l, u16 t, u16 w, u16 h); void GX_SetDispCopyDst(u16 w, u16 h);
void GX_SetCopyFilter(u8 aa, u8 sp[12][2], u8 vf, u8 vfilter[7]); void GX_SetFieldMode(u8 f, u8 h);
void GX_SetPixelFmt(u8 pix, u8 z); void GX_SetCullMode(u8 m); void GX_CopyDisp(void *dst, u8 clear); void GX_SetDispCopyGamma(u8 g);
void GX_ClearVtxDesc(void); void GX_SetVtxDesc(u8 attr, u8 type); void GX_SetVtxAttrFmt(u8 fmt, u32 attr, u32 comp, u32 type, u32 frac);
void GX_SetNumTexGens(u32 n); void GX_SetTexCoordGen(u16 tc, u32 type, u32 src, u32 mtx);
void GX_SetTevOrder(u8 stage, u8 tc, u32 map, u8 color); void GX_SetTevOp(u8 stage, u8 mode);
void GX_SetTevColorIn(u8 st, u8 a, u8 b, u8 c, u8 d); void GX_SetTevAlphaIn(u8 st, u8 a, u8 b, u8 c, u8 d);
void GX_SetTevColorOp(u8 st, u8 op, u8 bias, u8 scale, u8 clamp, u8 reg); void GX_SetTevAlphaOp(u8 st, u8 op, u8 bias, u8 scale, u8 clamp, u8 reg);
void GX_SetNumChans(u8 n); void GX_SetChanCtrl(s32 ch, u8 en, u8 amb, u8 mat, u8 lit, u8 diff, u8 att);
void GX_SetNumTevStages(u8 n); void GX_SetBlendMode(u8 type, u8 src, u8 dst, u8 op);
void GX_SetAlphaUpdate(u8 e); void GX_SetColorUpdate(u8 e); void GX_SetZMode(u8 e, u8 f, u8 u);
void GX_LoadProjectionMtx(Mtx44 m, u8 type); void GX_LoadPosMtxImm(Mtx m, u32 idx); void GX_SetCurrentMtx(u32 idx);
void GX_InvVtxCache(void); void GX_InvalidateTexAll(void);
void GX_InitTexObj(GXTexObj *o, void *img, u16 w, u16 h, u8 fmt, u8 ws, u8 wt, u8 mip);
void GX_InitTexObjFilterMode(GXTexObj *o, u8 minf, u8 magf);
static inline void GX_InitTexObjLOD(GXTexObj *o, u8 a, u8 b, f32 c, f32 d, f32 e, u8 f, u8 g, u8 h) { (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; GX_InitTexObjFilterMode(o, a, b); }
void GX_InitTexObjFilterMode(GXTexObj *o, u8 minf, u8 magf); void GX_LoadTexObj(GXTexObj *o, u8 map);
void GX_DrawDone(void); void GX_SetDrawDone(void); GXDrawDoneCallback GX_SetDrawDoneCallback(GXDrawDoneCallback cb);
void GX_GetGPStatus(u8 *ovh, u8 *undh, u8 *rdy, u8 *ci, u8 *brkpt);
void GX_Flush(void);
/* envoi des sommets (sur la vraie console : ecritures dans la FIFO) */
void GX_Begin(u8 prim, u8 fmt, u16 n); void GX_End(void);
void pcsim_gx_pos(f32 x, f32 y, f32 z); void pcsim_gx_clr(u8 r, u8 g, u8 b, u8 a); void pcsim_gx_tex(f32 s, f32 t);
static inline void GX_Position2f32(f32 x, f32 y) { pcsim_gx_pos(x, y, 0); }
static inline void GX_Position3f32(f32 x, f32 y, f32 z) { pcsim_gx_pos(x, y, z); }
static inline void GX_Position2s16(s16 x, s16 y) { pcsim_gx_pos(x, y, 0); }
static inline void GX_Position3s16(s16 x, s16 y, s16 z) { pcsim_gx_pos(x, y, z); }
static inline void GX_Color4u8(u8 r, u8 g, u8 b, u8 a) { pcsim_gx_clr(r, g, b, a); }
static inline void GX_Color3u8(u8 r, u8 g, u8 b) { pcsim_gx_clr(r, g, b, 255); }
static inline void GX_Color1u32(u32 c) { pcsim_gx_clr(c >> 24, c >> 16, c >> 8, c); }
static inline void GX_TexCoord2f32(f32 s, f32 t) { pcsim_gx_tex(s, t); }
#endif
