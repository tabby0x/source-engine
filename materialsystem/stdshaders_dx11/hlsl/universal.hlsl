// Universal bring-up shader (migration M3+). Bound for every draw by the DX11
// backend until native per-family shaders land. Matrix convention: row-vector
// (v' = v * M), row_major storage — matching what materialsystem hands the
// dx9 backend.
//
// MODE permutation: 0 = plain (vgui/unlit), 1 = lightmapped world,
// 2 = model rigid (bone0 transform + ambient cube), 3 = model skinned
// (up to 3 bone influences, dx9 cModel layout), 4 = model rigid + baked
// static-prop vertex lighting (color mesh on COLOR1, dx9 STATIC_LIGHT_VERTEX),
// 5 = model rigid PER-PIXEL phong (dx9 skin shader), 6 = skinned phong,
// 7 = Cable (ropes/wires: s0 normal map x uv0, s1 base x uv1, vertex color =
// rope-baked directional light — cable_vs20.fxc/cable_ps2x.fxc),
// 8 = Eyes (sphere normal from $eyeorigin, planar iris/glint projections from
// VS c48-53 — eyes_vs20.fxc/eyes_ps2x.fxc), 9 = WindowImposter (areaportal
// window glass: eye-ray into a cubemap x VS c47 modulation —
// windowimposter_vs20/ps2x.fxc), 10 = EyeRefract (the TF2 player eyeball:
// cornea normal + parallax iris + caustics + cube reflection —
// eye_refract_vs20.fxc/eye_refract_ps2x.fxc ps20b path).
// The phong perms transliterate skin_vs20.fxc/skin_ps20b.fxc driven by the
// dx9 PS register mirror (cbuffer b2 = ps c0..c31, shader_constant_register_map.h).

#ifndef MODE
#define MODE 0
#endif

#define LIGHTMAP	( MODE == 1 )
#define MODELMODE	( ( MODE >= 2 && MODE <= 6 ) || MODE == 11 )
#define SKINNED		( MODE == 3 || MODE == 6 || MODE == 8 || MODE == 10 || MODE == 11 )
#define STATICLIGHT	( MODE == 4 )
#define PHONG		( MODE == 5 || MODE == 6 )
#define CABLE		( MODE == 7 )
#define EYES		( MODE == 8 )
#define WINDOWIMPOSTER	( MODE == 9 )
#define EYEREFRACT	( MODE == 10 )
// 11 = Teeth (teeth_vs20: vertexlit lighting dimmed by $illumfactor x
// saturate(dot(N, $forward)) — both arrive in VS c48 via the mirror)
#define TEETH		( MODE == 11 )
// 12/13 = pyro_vision (pyroland replacement materials): 12 = world/lightmapped,
// 13 = VERTEX_LIT models (rigid OR skinned — rigid formats feed zero weights
// from the slot-1 fallback, which blends pure bone 0). EFFECT 0 (canvas) and
// 1 (colorbar posterize) are runtime branches; the post effects (2/3) stay
// unported until the M7 post chain. Constants ride the PS mirror exactly as
// the dx9 family writes them (c0-c7 params, c1 modulation x lmscale, c12 time).
#define PYROWORLD	( MODE == 12 )
#define PYROMODEL	( MODE == 13 )
#define PYRO		( PYROWORLD || PYROMODEL )
// 14 = SpriteCard (particles: rocket trails, explosions, beams-as-quads).
// The VS does the billboard corner expansion (corner ID + radius/rot/yaw in
// the vertex stream); static combos ride g_SpriteControl as runtime flags.
// spritecard_vsxx.fxc/spritecard_ps2x.fxc.
#define SPRITECARD	( MODE == 14 )
// 15/16 = Water: 15 = expensive (reflect/refract RTs + bump distortion,
// water_vs20/water_ps2x — needs dx11_rt_textures 1), 16 = cheap (envmap cube
// reflection, watercheap_vs20/ps2x). One material draws BOTH passes; the
// routing picks per-pass by the snapshot's sampler set. Flags ride
// g_SpriteControl.w: 1 reflect, 2 refract, 4 abovewater, 8 multitexture,
// 16 cheap-blend, 32 fresnel. BASETEXTURE/BLURRY_REFRACT variants unported.
#define WATER		( MODE == 15 )
#define WATERCHEAP	( MODE == 16 )
// 17/18 = the vertexlitgeneric EXTRA passes for TF2 effects, drawn between
// the base pass and translucents with blend ON + depth writes ON (the
// *_pass_helper.cpp snapshot signature). 17 = spy cloak
// (cloak_blended_pass_vs20/ps2x: framebuffer-copy refraction at s0 warped by
// the projected world normal, fresnel cloak mask in alpha; ps c0/c1 =
// ViewProj rows, c5 eye, c6 = {$cloakfactor, $refractamount}, c7 tint — all
// captured by the PS mirror). 18 = killstreak weapon sheen
// (weapon_sheen_pass_vs20/ps2x: cube reflection at s2 masked by a
// model-space-projected scrolling mask at s3; c6 = mask scale/offset,
// c7 = {$sheendir, $sheenindex}, c8 tint). $bumpmap variants unported — the
// dx9 no-bump path reduces to the vertex normal, which is what TF2's
// (bumpless) player/weapon materials hit anyway.
#define CLOAKPASS	( MODE == 17 )
#define SHEENPASS	( MODE == 18 )
// 19 = Refract (refract_vs20/refract_ps2x): screen-space warp effects — the
// underwater overlay (effects/water_warp*), the ubercharge overlay
// (effects/invuln_overlay_*), teleporter/heat-haze style materials. Samples
// the FB copy (or $basetexture) at s2 warped by the $normalmap at s3.
// Static combos ride g_SpriteControl.w as runtime flags: 1 = BLUR
// ($bluramount/$refractblur, the 4-tap polyphase 3x3), 2 = FADEOUTONSILHOUETTE,
// 4 = CUBEMAP (snapshot s4), 8 = REFRACTTINTTEXTURE (snapshot s5).
// ps c0 envmap tint, c1 refract tint, c2 contrast, c3 saturation,
// c5 = {refractamount,,,time} — all via the mirror. MASKED / COLORMODULATE /
// SECONDARY_NORMAL variants unported; the cubemap reflect uses the world
// normal (the dx9 tangent-space mix is skipped with the TBN).
#define REFRACT		( MODE == 19 )
// 20 = UnlitTwoTexture (unlittwotexture_vs20/ps2x): the control-point
// hologram family (cappoint_logo/beam — $additive, $texture2 = scrolling
// scan-lines via $texture2transform, Sine proxy pulsing $color through the
// c1 LINEAR modulation write our latch already captures). base(uv0) x
// texture2(uv1) x modulation, output alpha forced to 1 like dx9. uv0 rides
// vTexCoord ($basetexturetransform c48/49), uv1 rides vDetailCoord
// ($texture2transform c50/51).
#define UNLITTWOTEX	( MODE == 20 )
// 21 = Modulate (unlitgeneric_vs20 + modulate_ps2x): framebuffer-modulation
// materials — the CP hologram's "dark" backing pass ($mod2x: blend
// DST_COLOR x SRC_COLOR x2, where 0.5 gray = identity). The PS lerps the
// texture toward the c0 gray by (tex.a x $alpha) — the Sine proxy on $alpha
// throbs the darkening. Modulation arrives via VS c47
// (SetModulationVertexShaderDynamicState), gray via ps c0 — both mirrored.
#define MODULATE	( MODE == 21 )
// 22/23/24 = the LDR bloom chain (DoEnginePostProcessing /
// Generate8BitBloomTexture): fullscreen quads whose verts arrive ALREADY in
// clip space (DrawScreenSpaceRectangle) — the VS passes position through
// untransformed like Downsample_vs20/BlurFilter_vs20/screenspaceeffect_vs20.
// 22 = Downsample_nohdr (FB -> quarter, 4 taps via VS c48-51 offsets, ps c0 =
// {lum weights, exponent} bright-pass). 23 = BlurFilterX/Y (13-tap gaussian:
// 7 VS taps via c48-50 +/- offsets, 6 more via ps c0-c2, scale = ps c3 —
// BlurFilterY carries $bloomamount there). 24 = the bloomadd combine
// (screenspace_general + bloomadd_ps20: sample, alpha 1, additive blend).
#define SSDOWNSAMPLE	( MODE == 22 )
#define SSBLUR			( MODE == 23 )
#define SSADD			( MODE == 24 )
// 25 = Engine_Post (engine_post_ps2x, AA off): the bloom+color-correction
// combine the engine switches to when mat_colorcorrection is live. s0 = bloom
// quarter RT, s1 = full FB copy, s2-s5 = 32^3 CC volume LUTs (unbound slots
// stay NULL -> zero samples x zero weights). ps c2 = bloom->FB uv transform
// (wz scale, xy offset), c3.x = default weight, c4 = LUT weights, c5.x =
// bloom factor. The dx9 COL_CORRECT_NUM_LOOKUPS combo becomes weight gating.
#define ENGINEPOST		( MODE == 25 )
// 26 = color_projection (engine FullViewColorAdjustment — view.cpp draws
// dev/red_green_projection over the FINISHED frame, menu included, whenever
// mat_color_projection != 0; the colorblind-assist daltonizer). s4 = the
// _rt_FullFrameFB1 copy of the frame (non-sRGB read+write per the dx9 shadow
// block); ps c1 = (cpu, cpv, am, ayi) confusion-line parms (dx9
// g_vColorParms); the dx9 DYNAMIC combos arrive as g_SpriteControl.w flag
// bits (1 blindMK, 2 monochrome, 4 anomylize) derived from the cvar by the
// backend. Unported, this fell to perm 0 = solid clamped-c1 violet/magenta
// over the whole screen.
#define COLORPROJ		( MODE == 26 )
// 27 = luminance_compare (screenspace_general $pixshader, dev/lumcompare):
// the integer-HDR autoexposure histogram's stencil-marking pass. Samples the
// FB copy (t0 = $basetexture _rt_FullFrameFB), scales by ps c0.z, NTSC
// luminance, outputs 1 inside [c0.x, c0.y] else 0 — the material is
// $alphatested + $disable_color_writes, so out-of-range pixels clip and the
// stencil REPLACE only marks in-range ones. dev/no_pixel_write
// (constant_color) rides MODE 24's passthrough; its color never lands.
#define LUMCOMPARE		( MODE == 27 )
// 28/29/30 = the legacy RTT shadow pipeline (CClientShadowMgr):
// 28 = ShadowBuild — studiorender's forced override renders the CASTER model
// into the _rt_Shadows atlas slot (additive ONE/ONE, white rgb, alpha =
// $basetexture.a x modulation.a; opaque casters bind LIGHTMAP_FULLBRIGHT).
// VS = unlitgeneric_vs20: skinned position + c48/49 uv transform + c47
// modulation color — structurally the MODULATE vertex branch.
// 29 = Shadow (decals/rendershadow + decals/simpleshadow): projects an atlas
// region onto clipped WORLD geometry. Verts arrive pos+COLOR+uv (color.a =
// per-vertex fade); VS adds 4 jittered tap coords (c50/c51 = +/- one texel);
// PS averages 5 ALPHA taps -> coverage, fades by vertex alpha, lerps white
// toward ps c1 ($color, gamma->linear) and pre-compensates the
// (ZERO, SRC_COLOR) modulation blend for fog with a (1-f)^4 white fade.
// 30 = ShadowModel (decals/rendermodelshadow): projective shadow onto MODEL
// geometry — skinned pos+normal, shadow-space texgen via the c48-50 rows
// (SetVertexShaderMatrix3x4), texkill clipping (in-volume + backface), and
// dx9's PS modulates the c47 shadow color at full strength (its lerp factor
// reads an interpolator component D3D9 pads to 1; Valve marks the shader
// unused — ported to match observable dx9 behavior).
#define SHADOWBUILD		( MODE == 28 )
#define SHADOWPROJ		( MODE == 29 )
#define SHADOWMODEL		( MODE == 30 )
// 31 = IntroScreenSpaceEffect (the HL2 G-Man intro, scripted/
// intro_screenspaceeffect): ViewDrawScene_Intro renders the player scene
// into FB copy 0 and the camera scene into FB copy 1, then blends them with
// fullscreen passes — $mode picks one of the dx9 MODE 0..9 combos (negative
// greyscale, luminance ramps, HSV boost, add, passthroughs, overlay), ps
// c0.x = $alpha, blend = SRC_ALPHA/ONE. The combo arrives as
// g_SpriteControl.w like the other per-material mode switches; s0/s1 are the
// FB copies via BindStandardTexture.
#define INTROEFFECT		( MODE == 31 )
// 32 = MotionBlur (engine DoImageSpaceMotionBlur, dev/motion_blur — HL2
// ships mat_motion_blur_enabled 1): blurs the finished-frame FB copy along
// the sum of three vectors — the proxy's global blur vector (ps c1.xy, y
// flipped), a center-dampened "falling" vector (c1.z), and a roll vector
// (c1.w) — clamped to ps c0.x of the screen
// (mat_motion_blur_percent_of_screen_max / 100). The dx9 QUALITY dynamic
// combo (0/1/2/3 -> 1/7/11/15 evenly-weighted line taps) arrives via
// g_SpriteControl.w, computed dx9-style by the backend (FB height; forced 0
// when c1 is all zeros). Unported, this fell to perm 0 = a black fullscreen
// quad over the (by then working) G-Man intro every frame.
#define MOTIONBLUR		( MODE == 32 )
// 33 = MonitorScreen (func_monitor screens, $basetexture = _rt_Camera): the
// dx9 shader is unlittwotexture_vs20 + monitorscreen_ps2x — base x vertex
// color (VS c47 modulation) x optional $texture2 (s1; white fallback when
// absent), then $contrast (ps c1: lerp toward color^2), $saturation (ps c2:
// lerp from NTSC-ish grey), $tint (ps c3). Brush (func_monitor faces) and
// model monitors both route through the skinned branch (zero-weight fallback
// -> bone 0). TONEMAP_SCALE_NONE.
#define MONITORSCREEN	( MODE == 33 )
// 34 = the FLASHLIGHT pass (impulse-100 flashlight / env_projectedtexture
// without depth shadows): while the engine is in flashlight mode
// (SetFlashlightMode scopes CShadowMgr's world re-renders and studiorender's
// DrawShadows), EVERY draw is an additive flashlight pass — dx9 routes all
// families to DrawFlashlight_dx90 (flashlight_ps2x + the family flashlight
// VS). One perm serves world brushes, brush entities and models (the
// zero-weight bone fallback lands on bone 0 = the MODEL stack top).
// s0 = spotlight texture projected by the worldToTexture rows (VS mirror
// c49-52; flashlight pos = c48.xyz, base uv transform = c54/55); s1 = base.
// PS: spot x cFlashlightColor (mirror c28) x base x saturate(NdotL toward
// the light) x distance attenuation (mirror c13 = {const, linear, quad,
// FarZ} against the EYE distance — the dx9 shader attenuates from c11
// eyepos since the flashlight rides the player). TONEMAP_SCALE_LINEAR;
// FogToBlack snapshot. Depth-mapped shadows (s7) are stage 2.
#define FLASHLIGHT		( MODE == 34 )
// 35 = DEPTHWRITE: the flashlight shadow caster pass. The engine re-renders
// the world (gl_rsurf Shader_WorldShadowDepthFill) and studiorender re-draws
// models with the procedural __DepthWrite materials into
// _rt_ShadowDepthTexture_N's DSV — color writes are OFF (the dummy color RT
// absorbs nothing), only depth lands, biased by the SHADOW_BIAS poly offset.
// Position-only skinned transform with the FLASHLIGHT bit-2 discriminator
// (world/brush = folded MVP, studio = bone path); alphatest variants sample
// s0 at the passthrough uv and clip against the mirrored ps c0 threshold
// (depthwrite_ps2x ALPHACLIP — NOT the snapshot alphatest tail; DepthWrite
// never calls EnableAlphaTest).
#define DEPTHWRITE		( MODE == 35 )
// vertexlit/unlit-generic family modes: detail texture rides s2 there
#define VLGENERIC	( MODE == 0 || ( MODE >= 2 && MODE <= 4 ) )
// modes whose vertex streams carry studio flex deltas (dx9 stream 2):
// pos delta + wrinkle in POSITION1.xyzw, normal delta in NORMAL1
#define HASFLEX		( MODELMODE || EYES || EYEREFRACT || CLOAKPASS || SHEENPASS )

cbuffer PerFrame : register( b0 )
{
	row_major float4x4 g_ModelViewProj;
};

// Model path state (dx9 parity: bones are 3 float4 rows per bone, world.x =
// dot(row0, float4(pos,1)) — the frozen cModel contract from Plan.md)
cbuffer PerModel : register( b1 )
{
	row_major float4x4 g_ViewProj;
	float4 g_AmbientCube[6];	// +x -x +y -y +z -z (linear)
	// 4 lights x 5 float4: [0] color.rgb + type (0 off, 1 point, 2 directional,
	// 3 spot); [1] pos.xyz + range; [2] dir.xyz + spot exponent;
	// [3] atten a0,a1,a2 + stopdot; [4] stopdot2
	float4 g_Lights[20];
	float4 g_BoneRows[159];		// 53 bones * 3 rows
	float4 g_EyePosPM;			// world-space camera pos (dx9 cEyePos c2)
};

cbuffer PerDraw : register( b3 )
{
	float4 g_AlphaTest;		// x = enable, y = func (ShaderAlphaFunc_t), z = ref,
							// w = gamma-decode vertex color (dx9 VERTEXCOLOR path)
	float4 g_Modulation;
	float4 g_TintControl;	// x = $blendtintbybasealpha, y = $blendtintcoloroverbase,
							// z = envmap on ($envmap sampler enabled), w = $selfillum
	float4 g_PhongFlags;	// x = $halflambert, y = has $lightwarptexture,
							// z = has $bumpmap, w = has $phongexponenttexture
	float4 g_MiscControl;	// x = $selfillumfresnel, y = base texcoord
							// transform VS register for this family (-1 = off),
							// z = $detailblendmode (TCOMBINE_*, -1 = no detail),
							// w = ps c12 written this pass (lightmapped-family
							// modulation incl. brush-entity fade alpha)
	float4 g_EyeControl;	// x = $raytracesphere, y = $spheretexkillcombo
							// (EyeRefract static combos, sniffed per material),
							// z = pyro_vision $effect (0/1), w = pyro_vision
							// flag bits: 1 vertexcolor, 2 fullbright,
							// 4 basetexture2, 8 fancyblending, 16 colorbar,
							// 32 stripes
	float4 g_SpriteControl;	// SpriteCard: x = flag bits (1 addbasetexture2,
							// 2 addself, 4 animblend, 8 dualsequence,
							// 16 maxlumframeblend1, 32 maxlumframeblend2,
							// 64 colorramp, 128 extractgreenalpha,
							// 256 depthblend), y = $orientation (0-2),
							// z = $sequence_blend_mode (0-2)
	float4 g_FogControl;	// x = fog bits: 1 = water-fog dest-alpha write (dx9
							// WRITEWATERFOGTODESTALPHA, height fog + opaque),
							// 2 = range fog rgb (dx9 fixed-function vertex-fog
							// math), 4 = height fog rgb (underwater murk).
							// y = water Z (height) / maxdensity floor (range),
							// z = 1/(fogEnd - fogStart), w = eye Z.
	float4 g_SceneFogColor;	// rgb = per-pass fog color (ShaderFogMode_t pick,
							// linearized on sRGB-write passes like dx9
							// ApplyFogMode), w = fogEnd * ooFogRange.
	float4 g_EnvmapControl;	// lightmappedgeneric envmap path (perm 1):
							// x = $envmapcontrast, y = $envmapsaturation,
							// z = $fresnelreflection, w = mask mode bits 0-1
							// (0 none / 1 $envmapmask s5 / 2 $basealphaenvmapmask
							// = INVERTED base alpha / 3 $normalmapalphaenvmapmask
							// = bump alpha) + bit 4 = $bumpmap bound (s4,
							// perturb the reflection). Param-fed because the
							// dx9 helper only writes ps c2-c4 on its slow path.
};

// dx9 PS float-constant mirror (c0..c31): the dx9 skin/vertexlitgeneric
// dynamic blocks upload these per pass through SetPixelShaderConstant.
// Indices per shader_constant_register_map.h (c1 modulation, c4-9 ambient
// cube, c11 eyepos+specexp, c13.x rim mask, c14.w rim boost, c19 fresnel
// ranges + spec boost, c20-25 light info, c26 spec tint + rim exp,
// c27 shader controls).
cbuffer PSMirror : register( b2 )
{
	float4 g_PSC[32];
};

// dx9 VS float-constant mirror (c0..c63), VS stage b2: texture transforms
// live at SHADER_SPECIFIC_CONST_0 (engine c48/c49 base, c52/c53 detail —
// every family agrees on the base slot). Stale registers are zero.
cbuffer VSMirror : register( b2 )
{
	float4 g_VSC[64];
};

// Texcoord transforms: u' = u*r0.x + v*r0.y + r0.w, same for v' with r1
// (lightmappedgeneric_vs20.fxc:149). All-zero rows fall back to identity
// (the pass never wrote them).
float2 ApplyTexCoordTransformAt( float2 vUV, int nReg )
{
	float4 vRow0 = g_VSC[nReg];
	float4 vRow1 = g_VSC[nReg + 1];
	if ( dot( abs( vRow0 ), float4( 1, 1, 1, 1 ) ) + dot( abs( vRow1 ), float4( 1, 1, 1, 1 ) ) < 1e-6 )
		return vUV;
	return float2(
		vUV.x * vRow0.x + vUV.y * vRow0.y + vRow0.w,
		vUV.x * vRow1.x + vUV.y * vRow1.y + vRow1.w );
}

// $basetexturetransform: the ROW REGISTER differs per family — most use c48
// (SHADER_SPECIFIC_CONST_0) but Sky keeps texture-size info there and its
// transform at c49 (sky_vs20.fxc:3-4) — so the C++ side resolves the
// register per material (g_MiscControl.y; -1 = no transform).
float2 ApplyBaseTexCoordTransform( float2 vUV )
{
	int nReg = (int)g_MiscControl.y;
	if ( nReg < 0 )
		return vUV;
	return ApplyTexCoordTransformAt( vUV, nReg );
}

// $detailtexturetransform register (with $detailscale folded in by the dx9
// helper): model/unlit/skin families = SHADER_SPECIFIC_CONST_4 (c52);
// lightmappedgeneric = SHADER_SPECIFIC_CONST_2 (c50).
#if LIGHTMAP
#define DETAIL_XFORM_REG 50
#else
#define DETAIL_XFORM_REG 52
#endif

#if WINDOWIMPOSTER
TextureCube g_TexWindowEnv : register( t0 );	// $envmap cube (the only texture)
SamplerState g_Sampler0 : register( s0 );
#elif WATERCHEAP
TextureCube g_TexWaterEnv : register( t0 );		// $envmap cube reflection
SamplerState g_Sampler0 : register( s0 );
#else
Texture2D g_Texture0 : register( t0 );
SamplerState g_Sampler0 : register( s0 );
#endif
#if EYES
// dx9 eyes sampler map (eyes_ps2x.fxc): s0 base/sclera (sRGB), s1 iris (sRGB),
// s2 glint (procedural RT or vgui/black at LOD distance, linear)
Texture2D g_TexIris : register( t1 );
SamplerState g_SampIris : register( s1 );
Texture2D g_TexGlint : register( t2 );
SamplerState g_SampGlint : register( s2 );
#endif
#if EYEREFRACT
// dx9 eye_refract sampler map: s0 cornea normal (linear, in g_Texture0),
// s1 iris (sRGB), s2 reflection cube (sRGB), s3 ambient occlusion (sRGB),
// s4 lightwarp LUT (linear)
Texture2D g_TexIris : register( t1 );
SamplerState g_SampIris : register( s1 );
TextureCube g_TexEyeEnv : register( t2 );
SamplerState g_SampEyeEnv : register( s2 );
Texture2D g_TexEyeAO : register( t3 );
SamplerState g_SampEyeAO : register( s3 );
Texture2D g_TexLightwarp : register( t4 );
SamplerState g_SampLightwarp : register( s4 );
#endif
#if LIGHTMAP
Texture2D g_Lightmap : register( t1 );
SamplerState g_LightmapSampler : register( s1 );
Texture2D g_TexDetail : register( t12 );	// lightmappedgeneric detail (s12)
SamplerState g_SampDetail : register( s12 );
// dx9 lightmappedgeneric envmap map: s2 = $envmap cube (helper:874), s5 =
// $envmapmask (helper:848), s4 = $bumpmap (helper:484/812 — bound for bump OR
// $normalmapalphaenvmapmask). All fall back to WHITE (cube/2D) when disabled.
TextureCube g_EnvmapCube : register( t2 );
SamplerState g_SampEnvmapCube : register( s2 );
Texture2D g_TexEnvMask : register( t5 );
SamplerState g_SampEnvMask : register( s5 );
Texture2D g_TexBump : register( t4 );
SamplerState g_SampBump : register( s4 );
#endif
#if PHONG
// dx9 skin sampler map (skin_ps20b.fxc:99-107)
Texture2D g_TexLightwarp : register( t2 );	// 1D diffuse warp
SamplerState g_SampLightwarp : register( s2 );
Texture2D g_TexNormal : register( t3 );		// normal map, spec mask in alpha
SamplerState g_SampNormal : register( s3 );
Texture2D g_TexSpecExp : register( t7 );	// spec exponent map, rim mask in alpha
SamplerState g_SampSpecExp : register( s7 );
TextureCube g_TexEnvmap : register( t8 );	// cubic environment map
SamplerState g_SampEnvmap : register( s8 );
Texture2D g_TexDetail : register( t13 );	// skin detail (s13)
SamplerState g_SampDetail : register( s13 );
#endif
#if CABLE
Texture2D g_TexCableBase : register( t1 );	// base texture (s1, sRGB read)
SamplerState g_SampCableBase : register( s1 );
#endif
#if SPRITECARD
// s1 = $ramptexture color ramp (sRGB); s2 = frame-buffer depth (depth blend,
// substituted white + flag masked off while RT textures are gated)
Texture2D g_TexColorRamp : register( t1 );
SamplerState g_SampColorRamp : register( s1 );
#endif
#if WATER
// expensive water (water_ps2x): s0 refract RT (sRGB), s2 reflect RT (sRGB),
// s4 normal map (t0 = refract here — g_Texture0 doubles as RefractSampler)
Texture2D g_TexWaterReflect : register( t2 );
SamplerState g_SampWaterReflect : register( s2 );
Texture2D g_TexWaterNormal : register( t4 );
SamplerState g_SampWaterNormal : register( s4 );
#endif
#if WATERCHEAP
// cheap water (watercheap_ps2x): s0 envmap CUBE, s1 normal map, s2 refract RT
// (REFRACTALPHA: bound only when the material also refracts AND blends —
// its alpha feathers the water/land border)
Texture2D g_TexWaterNormal : register( t1 );
SamplerState g_SampWaterNormal : register( s1 );
Texture2D g_TexWaterRefract : register( t2 );
SamplerState g_SampWaterRefract : register( s2 );
#endif
#if SHEENPASS
// weapon_sheen_pass: s2 = sheen CUBE (sRGB), s3 = scrolling mask (sRGB);
// s0 (FB copy) is enabled by the dx9 shadow block but never sampled
TextureCube g_TexSheen : register( t2 );
SamplerState g_SampSheen : register( s2 );
Texture2D g_TexSheenMask : register( t3 );
SamplerState g_SampSheenMask : register( s3 );
#endif
#if UNLITTWOTEX || MONITORSCREEN
Texture2D g_TexTwo : register( t1 );	// $texture2 (sRGB; scan-lines/overlay —
SamplerState g_SampTwo : register( s1 );	// white fallback when unbound)
#endif
#if FLASHLIGHT
// Two dx9 flashlight families share this perm, selected by g_SpriteControl.w
// bit 1: 0 = DrawFlashlight_dx90 (lightmapped world/eyes/teeth: s0 = cookie,
// s1 = base, VS-side projection via c49-52); 1 = the vertexlitgeneric INLINE
// flashlight (models: s0 = base, s7 = cookie, PS-side projection via mirror
// c24-27, atten/pos in c22/c23, c28.w = NoLambert).
Texture2D g_TexFLBase : register( t1 );	// dx90/eyes family $basetexture (sRGB)
SamplerState g_SampFLBase : register( s1 );
Texture2D g_TexFLIris : register( t3 );	// EYES family $iris (sRGB)
SamplerState g_SampFLIris : register( s3 );
// t7 is double-duty across the families: the VLG cookie — OR the dx90
// family's shadow depth map (DrawFlashlight_dx90 binds depth at s7; its
// cookie rides s0). Flag bits pick the interpretation per draw.
Texture2D g_TexFLCookie : register( t7 );	// VLG cookie (sRGB) / dx90 depth map
SamplerState g_SampFLCookie : register( s7 );
// Shadow depth maps for the other two families (flag bit 8; raw 0..1 depth
// in .r, manually compared — no comparison sampler, the registers collide
// across families). VLG binds depth at s8, eyes at s4.
Texture2D g_TexFLDepthVLG : register( t8 );
SamplerState g_SampFLDepthVLG : register( s8 );
Texture2D g_TexFLDepthEyes : register( t4 );
SamplerState g_SampFLDepthEyes : register( s4 );
#endif
#if ENGINEPOST
// engine_post: s1 = full FB copy, s2-s5 = CC volume LUTs (s0 = bloom via
// g_Texture0). All non-sRGB reads — the dx9 shadow block deliberately
// emulates pre-DX10 additive-sRGB behaviour by blending in gamma space.
Texture2D g_TexPostFB : register( t1 );
SamplerState g_SampPostFB : register( s1 );
Texture3D g_TexCCLut0 : register( t2 );
SamplerState g_SampCCLut0 : register( s2 );
Texture3D g_TexCCLut1 : register( t3 );
SamplerState g_SampCCLut1 : register( s3 );
Texture3D g_TexCCLut2 : register( t4 );
SamplerState g_SampCCLut2 : register( s4 );
Texture3D g_TexCCLut3 : register( t5 );
SamplerState g_SampCCLut3 : register( s5 );
#endif
#if COLORPROJ
// color_projection: s4 = _rt_FullFrameFB1 (the finished frame, copied by
// FullViewColorAdjustment right before this draw)
Texture2D g_TexFrame : register( t4 );
SamplerState g_SampFrame : register( s4 );
#endif
#if INTROEFFECT
// intro effect: s0 = FB copy 0 (player scene, rides g_Texture0), s1 = FB
// copy 1 (intro camera scene)
Texture2D g_TexIntro2 : register( t1 );
SamplerState g_SampIntro2 : register( s1 );
#endif
#if REFRACT
// refract_ps2x sampler map: s2 = warp source (FB copy or $basetexture, sRGB),
// s3 = $normalmap (linear), s4 = $envmap cube (sRGB, optional),
// s5 = $refracttinttexture (sRGB, optional)
Texture2D g_TexRefrSource : register( t2 );
SamplerState g_SampRefrSource : register( s2 );
Texture2D g_TexRefrNormal : register( t3 );
SamplerState g_SampRefrNormal : register( s3 );
TextureCube g_TexRefrEnv : register( t4 );
SamplerState g_SampRefrEnv : register( s4 );
Texture2D g_TexRefrTint : register( t5 );
SamplerState g_SampRefrTint : register( s5 );
#endif
#if VLGENERIC
Texture2D g_TexDetail : register( t2 );		// vertexlit/unlit detail (s2)
SamplerState g_SampDetail : register( s2 );
#endif
#if PYRO
// dx9 pyro_vision sampler map (effects 0/1): s1 lightmap (world only),
// s2 canvas (effect 0) or colorbar LUT (effect 1), s3 blend modulation,
// s4 basetexture2, s5 stripes. Unused slots fall back to white.
#if PYROWORLD
Texture2D g_Lightmap : register( t1 );
SamplerState g_LightmapSampler : register( s1 );
#endif
Texture2D g_TexPyroCanvas : register( t2 );
SamplerState g_SampPyroCanvas : register( s2 );
Texture2D g_TexPyroBlendMod : register( t3 );
SamplerState g_SampPyroBlendMod : register( s3 );
Texture2D g_TexPyroBase2 : register( t4 );
SamplerState g_SampPyroBase2 : register( s4 );
Texture2D g_TexPyroStripe : register( t5 );
SamplerState g_SampPyroStripe : register( s5 );
#endif

// dx9 TextureCombine (common_ps_fxc.h:679): $detailblendmode 0-11.
float4 DetailCombine( float4 vBase, float4 vDetail, int nMode, float flBlendFactor )
{
	if ( nMode == 7 )	// MOD2X_SELECT_TWO_PATTERNS
	{
		float3 dc = lerp( vDetail.r, vDetail.a, vBase.a );
		vBase.rgb *= lerp( float3( 1, 1, 1 ), 2.0 * dc, flBlendFactor );
	}
	if ( nMode == 0 )	// RGB_EQUALS_BASE_x_DETAILx2
		vBase.rgb *= lerp( float3( 1, 1, 1 ), 2.0 * vDetail.rgb, flBlendFactor );
	if ( nMode == 1 )	// RGB_ADDITIVE
		vBase.rgb += flBlendFactor * vDetail.rgb;
	if ( nMode == 2 )	// DETAIL_OVER_BASE
		vBase.rgb = lerp( vBase.rgb, vDetail.rgb, flBlendFactor * vDetail.a );
	if ( nMode == 3 )	// FADE
		vBase = lerp( vBase, vDetail, flBlendFactor );
	if ( nMode == 4 )	// BASE_OVER_DETAIL
	{
		vBase.rgb = lerp( vBase.rgb, vDetail.rgb, flBlendFactor * ( 1.0 - vBase.a ) );
		vBase.a = vDetail.a;
	}
	if ( nMode == 8 )	// MULTIPLY
		vBase = lerp( vBase, vBase * vDetail, flBlendFactor );
	if ( nMode == 9 )	// MASK_BASE_BY_DETAIL_ALPHA
		vBase.a = lerp( vBase.a, vBase.a * vDetail.a, flBlendFactor );
	if ( nMode == 11 )	// SSBUMP_NOBUMP
		vBase.rgb = vBase.rgb * dot( vDetail.rgb, 2.0 / 3.0 );
	return vBase;
}

// dx9 TextureCombinePostLighting (common_ps_fxc.h:727): modes 5/6 add the
// detail as self-illumination after lighting.
float3 DetailCombinePostLighting( float3 vLit, float4 vDetail, int nMode, float flBlendFactor )
{
	if ( nMode == 5 )	// RGB_ADDITIVE_SELFILLUM
		vLit += flBlendFactor * vDetail.rgb;
	if ( nMode == 6 )	// RGB_ADDITIVE_SELFILLUM_THRESHOLD_FADE
	{
		float f = flBlendFactor - 0.5;
		float flMult = ( f >= 0.0 ) ? ( 1.0 / flBlendFactor ) : ( 4.0 * flBlendFactor );
		float flAdd = ( f >= 0.0 ) ? ( 1.0 - flMult ) : ( -0.5 * flMult );
		vLit += saturate( flMult * vDetail.rgb + flAdd );
	}
	return vLit;
}

struct VsInput
{
#if SPRITECARD
	// spritecard_vsxx.fxc VS_INPUT: per-corner duplicated particle verts.
	// Texcoords absent from the format read zeros via the slot-1 fallback.
	float3 vPos : POSITION;
	float4 vColor : COLOR0;			// particle tint (gamma)
	float4 vSCTexCoord0 : TEXCOORD0;	// sheet bounding uvs, frame 0
	float4 vSCTexCoord1 : TEXCOORD1;	// sheet bounding uvs, frame 1
	float4 vSCParms : TEXCOORD2;		// frame blend, rot, radius, yaw
	float2 vSCCornerID : TEXCOORD3;	// (0,0) (1,0) (1,1) (0,1)
	float4 vSCTexCoord2 : TEXCOORD4;	// texture 2 bounding uvs
	float4 vSCSeq2TexCoord0 : TEXCOORD5;
	float4 vSCSeq2TexCoord1 : TEXCOORD6;
	float4 vSCParms1 : TEXCOORD7;	// second sequence frame blend
};
#else
	float3 vPos : POSITION;
	float4 vColor : COLOR0;
	float2 vTexCoord : TEXCOORD0;
#if LIGHTMAP
	float2 vLightmapCoord : TEXCOORD1;
#endif
#if CABLE
	float2 vBaseCoord : TEXCOORD1;	// base texture UV (uv0 = normal map UV)
#endif
#if PYROWORLD
	float2 vLightmapCoord : TEXCOORD1;
#endif
#if MODELMODE || PYROMODEL || WATER || WATERCHEAP || CLOAKPASS || SHEENPASS || REFRACT || SHADOWMODEL || FLASHLIGHT || LIGHTMAP
	// LIGHTMAP: the envmap reflection vector needs the world normal; formats
	// without one (decal/overlay verts) read zeros from the slot-1 fallback
	// (the VS safe-normalize lands on +Z).
	float3 vNormal : NORMAL;
#endif
#if WATER || WATERCHEAP || LIGHTMAP
	// LIGHTMAP: the envmapped world formats carry tangents (the dx9 shadow
	// block adds VERTEX_TANGENT_S/T whenever $envmap is set) for the $bumpmap
	// reflection perturbation; formats without them read slot-1 zeros.
	float3 vTangentS : TANGENT;
	float3 vTangentT : BINORMAL;
#endif
#if CLOAKPASS || SHEENPASS || REFRACT || UNLITTWOTEX || MODULATE || SHADOWBUILD || SHADOWMODEL || MONITORSCREEN || FLASHLIGHT || DEPTHWRITE
	// Rigid/world draws (dropped weapons, screen-overlay quads) read zero
	// weights from the slot-1 fallback, which blends to pure bone 0 — one
	// perm covers rigid, skinned, and quad geometry.
	float2 vBoneWeights : BLENDWEIGHT;
	uint4 vBoneIndices : BLENDINDICES;
#endif
#if PYROMODEL
	// Rigid pyro models read zero weights from the slot-1 fallback, which
	// blends to pure bone 0 — one perm covers rigid and skinned.
	float2 vBoneWeights : BLENDWEIGHT;
	uint4 vBoneIndices : BLENDINDICES;
	// Baked static-prop lighting (dx9 v.vSpecular -> DoLighting bStaticLight):
	// streams from IA slot 2 when the prop has a color mesh; the slot stays
	// UNBOUND otherwise and D3D11 defines those reads as zeros (term = 0).
	float4 vSpecular : COLOR1;
#endif
#if SKINNED
	float2 vBoneWeights : BLENDWEIGHT;
	uint4 vBoneIndices : BLENDINDICES;
#endif
#if HASFLEX
	// Studio flex deltas (CPU-morphed by studiorender each frame). When the
	// draw has no flex mesh, these read constant zeros from the slot-1
	// fallback buffer — no cFlexScale gate needed (dx9 used c3 to mask a
	// garbage-aliased stream; our zero fallback makes the add a no-op).
	float4 vPosFlex : POSITION1;	// xyz = position delta, w = wrinkle
	float3 vNormalFlex : NORMAL1;
#endif
#if STATICLIGHT
	float4 vSpecular : COLOR1;	// baked prop lighting, IA slot 2 (dx9 stream 1)
#endif
#if PHONG
	float4 vUserData : TANGENT;	// xyz tangent, w binormal sign (studio USERDATA)
#endif
};
#endif	// !SPRITECARD

struct VsOutput
{
	float4 vProjPos : SV_Position;
	float4 vColor : COLOR0;
	float2 vTexCoord : TEXCOORD0;
	float2 vDetailCoord : TEXCOORD7;	// $detail UV ($detailscale folded in)
	// x = worldPos.z, y = clip-space z (dx9 worldPos_projPosZ) — feeds the
	// water-fog dest-alpha write during the refraction view. Branches without
	// a world transform leave the (VsOutput)0 zeros (their draws never raise
	// g_FogControl.x).
	float2 vFogData : TEXCOORD9;
#if LIGHTMAP
	float2 vLightmapCoord : TEXCOORD1;
	// Envmap term (dx9 lightmappedgeneric CUBEMAP): world vert->eye vector
	// (linear in worldPos, so it interpolates exactly like dx9's per-pixel
	// g_EyePos - worldPos) + the world normal. The .w channels carry the RAW
	// texcoord0 for the $envmapmask sample ($envmapmasktransform unported —
	// identity in shipping content; dx9's default transform is identity too).
	float4 vEnvEye : TEXCOORD2;
	float4 vEnvNormal : TEXCOORD3;
	// World tangent rows for the $bumpmap perturbation — with the normal they
	// form the dx9 tangentSpaceTranspose (rows = tangentS, tangentT, normal).
	float3 vEnvTanS : TEXCOORD4;
	float3 vEnvTanT : TEXCOORD5;
#endif
#if CABLE
	float2 vBaseCoord : TEXCOORD1;
#endif
#if EYES
	float2 vIrisCoord : TEXCOORD1;	// planar projection of worldPos (VS c50/c51)
	float2 vGlintCoord : TEXCOORD2;	// planar projection of worldPos (VS c52/c53)
#endif
#if WINDOWIMPOSTER
	float3 vWorldPosWI : TEXCOORD2;	// eye ray computed per pixel vs ps c11
#endif
#if EYEREFRACT
	// eye_refract_vs20 outputs: world pos + eyeball TBN + per-light
	// atten/cosine pairs; the tangent view vector moves to the PS (computed
	// from ps c4 camera pos — same math, the dx9 VS hoist was an optimization).
	float3 vERWorldPos : TEXCOORD2;
	float3 vERNormal : TEXCOORD3;
	float3 vERTangent : TEXCOORD4;
	float3 vERBinormal : TEXCOORD5;
	float4 vERFalloffCos01 : TEXCOORD6;	// atten0, atten1, cos0, cos1
	float4 vERFalloffCos23 : TEXCOORD8;	// atten2, atten3, cos2, cos3
#endif
#if PYRO
	float2 vPyroSeamlessCoord : TEXCOORD2;	// canvas projection (VS c49 scale)
	float2 vPyroStripeCoord : TEXCOORD3;	// stripe projection (VS c48 scale)
	float3 vPyroBlendFactor : TEXCOORD4;	// xy = $blendmodulate UV, z = vColor.a
#endif
#if PYROWORLD
	float2 vLightmapCoord : TEXCOORD1;
#endif
#if SPRITECARD
	// spritecard_vsxx VS_OUTPUT (vTexCoord above = texCoord0; vColor = tint)
	float2 vSCTex1 : TEXCOORD1;
	float4 vSCBlend0 : TEXCOORD2;	// x = frame blend, z = seq2 blend
	float2 vSCTex2 : TEXCOORD3;		// second texture uvs
	float4 vSCBlend1 : TEXCOORD4;	// extract-green-alpha weights
	float2 vSCSeq2Tex0 : TEXCOORD5;
	float2 vSCSeq2Tex1 : TEXCOORD6;
#endif
#if WATER
	// water_vs20 outputs (vTexCoord = bump uv)
	float3 vWaterTanEye : TEXCOORD2;		// tangent-space eye vector
	float4 vWaterReflRefr : TEXCOORD3;		// reflect.xy / refract.yx (pre-/W)
	float4 vWaterProjPos : TEXCOORD4;		// full projected pos (w = W)
	float4 vWaterExtraBump : TEXCOORD5;		// MULTITEXTURE scroll coords
#endif
#if WATERCHEAP
	// watercheap_vs20 outputs (vTexCoord = normal-map uv)
	float3 vWaterEyeVect : TEXCOORD2;		// world vert -> eye
	float3 vWaterTBN0 : TEXCOORD3;			// tangent-space transpose rows
	float3 vWaterTBN1 : TEXCOORD4;
	float3 vWaterTBN2 : TEXCOORD5;
	float4 vWaterExtraBump : TEXCOORD6;		// MULTITEXTURE scroll coords
	float3 vWaterRefract : TEXCOORD8;		// xy = refract uv (pre-/W), z = W
#endif
#if CLOAKPASS || SHEENPASS || REFRACT
	// cloak/sheen/refract pass outputs (cloak_blended_pass_vs20 /
	// weapon_sheen_pass_vs20 / refract_vs20 share the shape)
	float3 vCSNormal : TEXCOORD2;			// world normal (VS-normalized)
	float3 vCSRefract : TEXCOORD3;			// xy = refract uv (pre-/W), z = W
	float3 vCSView : TEXCOORD4;				// normalized world view vector
#endif
#if SHEENPASS
	float3 vCSModelPos : TEXCOORD5;			// raw model-space position (mask projection)
#endif
#if REFRACT
	float4 vRefrBump : TEXCOORD5;			// $bumptransform uv in xy, transform2 in wz
#endif
#if SSDOWNSAMPLE || SSBLUR || SHADOWPROJ || SHADOWMODEL || FLASHLIGHT
	// downsample: taps 0,1 / 2,3 ; blur: taps 1,2 / 3,1neg / 2neg,3neg
	// (blur tap 0 = vTexCoord); shadowproj: jitter taps +j0,-j0 / +j1,-j1;
	// shadowmodel: (texPos, backface dot) / (1-texPos.xy, 1-fade) texkills;
	// flashlight: spot projection float4 / (posToLight, worldPos.x) /
	// (worldNormal, worldPos.y) — worldPos.z rides vFogData.x
	float4 vSSTapA : TEXCOORD2;
	float4 vSSTapB : TEXCOORD3;
	float4 vSSTapC : TEXCOORD4;
#endif
#if PHONG
	// dx9 skin_vs20 outputs: world pos + per-light attenuation + the
	// tangent-space transpose (rows are (T B N) per world axis).
	float4 vWorldPosAtten3 : TEXCOORD2;	// xyz world pos, w = light 3 atten
	float3 vLightAtten : TEXCOORD3;		// lights 0..2 attenuation
	float3 vTBN0 : TEXCOORD4;
	float3 vTBN1 : TEXCOORD5;
	float3 vTBN2 : TEXCOORD6;
#endif
};

#if MODELMODE || EYES || EYEREFRACT || PYROMODEL
// common_vs_fxc.h AmbientLight: normal-squared weighting of the 6 cube sides
float3 AmbientCubeLight( float3 vWorldNormal )
{
	float3 vSq = vWorldNormal * vWorldNormal;
	int3 vIsNeg = vWorldNormal < 0.0;
	return vSq.x * g_AmbientCube[vIsNeg.x].rgb +
		   vSq.y * g_AmbientCube[2 + vIsNeg.y].rgb +
		   vSq.z * g_AmbientCube[4 + vIsNeg.z].rgb;
}

// common_vs_fxc.h DoLight transliteration. Half-lambert is the dx9
// CosineTermInternal variant: scale-bias the SIGNED dot to 0..1 and square
// (no pre-saturate — the bias already lands in range).
float3 LocalLights( float3 vWorldPos, float3 vWorldNormal, bool bHalfLambert )
{
	float3 vTotal = float3( 0.0, 0.0, 0.0 );
	[unroll]
	for ( int i = 0; i < 4; ++i )
	{
		float4 vColorType = g_Lights[i * 5];
		int nType = (int)vColorType.w;
		if ( nType == 0 )
			continue;

		float flNDotL;
		float flAtten = 1.0;
		if ( nType == 2 )
		{
			// directional: m_Direction points FROM the light
			flNDotL = -dot( vWorldNormal, g_Lights[i * 5 + 2].xyz );
		}
		else
		{
			float3 vDelta = g_Lights[i * 5 + 1].xyz - vWorldPos;
			float flDist = length( vDelta );
			float3 vToLight = ( flDist > 1e-4 ) ? vDelta / flDist : float3( 0.0, 0.0, 1.0 );
			flNDotL = dot( vWorldNormal, vToLight );
			float3 vAtten = g_Lights[i * 5 + 3].xyz;
			flAtten = 1.0 / max( vAtten.x + vAtten.y * flDist + vAtten.z * flDist * flDist, 1e-4 );
			if ( nType == 3 )
			{
				// spot falloff between stopdot (inner) and stopdot2 (outer)
				float flSpotDot = -dot( vToLight, g_Lights[i * 5 + 2].xyz );
				float flStopDot = g_Lights[i * 5 + 3].w;
				float flStopDot2 = g_Lights[i * 5 + 4].x;
				float flSpot = saturate( ( flSpotDot - flStopDot2 ) / max( flStopDot - flStopDot2, 1e-4 ) );
				flAtten *= pow( flSpot, max( g_Lights[i * 5 + 2].w, 1e-4 ) );
			}
		}
		if ( bHalfLambert )
		{
			flNDotL = flNDotL * 0.5 + 0.5;
			flNDotL = flNDotL * flNDotL;
		}
		else
		{
			flNDotL = saturate( flNDotL );
		}
		vTotal += vColorType.rgb * flNDotL * saturate( flAtten );
	}
	return vTotal;
}
#endif

#if EYEREFRACT
// dx9 CosineTermInternal (common_vs_fxc.h:801): per-light N·L (optionally
// half-lambert), light direction by type like LocalLights.
float VSCosineTerm( int n, float3 vWorldPos, float3 vWorldNormal, bool bHalfLambert )
{
	float4 vColorType = g_Lights[n * 5];
	int nType = (int)vColorType.w;
	float flNDotL;
	if ( nType == 2 )
	{
		flNDotL = -dot( vWorldNormal, g_Lights[n * 5 + 2].xyz );
	}
	else
	{
		float3 vDelta = g_Lights[n * 5 + 1].xyz - vWorldPos;
		float flLen = length( vDelta );
		float3 vToLight = ( flLen > 1e-4 ) ? vDelta / flLen : float3( 0.0, 0.0, 1.0 );
		flNDotL = dot( vWorldNormal, vToLight );
	}
	if ( bHalfLambert )
	{
		flNDotL = flNDotL * 0.5 + 0.5;
		flNDotL = flNDotL * flNDotL;
	}
	else
	{
		flNDotL = max( 0.0, flNDotL );
	}
	return flNDotL;
}
#endif

#if PHONG || EYEREFRACT
// dx9 VertexAttenInternal (common_vs_fxc.h:752): distance + spot cone
// attenuation only — N·L happens per pixel. Directionals attenuate to 1.
float VSLightAttenOnly( int n, float3 vWorldPos )
{
	float4 vColorType = g_Lights[n * 5];
	int nType = (int)vColorType.w;
	if ( nType == 0 )
		return 0.0;
	if ( nType == 2 )
		return 1.0;		// directional
	float3 vDelta = g_Lights[n * 5 + 1].xyz - vWorldPos;
	float flDistSq = max( dot( vDelta, vDelta ), 1e-8 );
	float flDist = sqrt( flDistSq );
	float3 vAtt = g_Lights[n * 5 + 3].xyz;
	float flAtten = 1.0 / max( vAtt.x + vAtt.y * flDist + vAtt.z * flDistSq, 1e-4 );
	if ( nType == 3 )
	{
		// spot falloff between stopdot (inner) and stopdot2 (outer)
		float3 vToLight = vDelta / flDist;
		float flSpotDot = -dot( vToLight, g_Lights[n * 5 + 2].xyz );
		float flStopDot = g_Lights[n * 5 + 3].w;
		float flStopDot2 = g_Lights[n * 5 + 4].x;
		float flSpot = saturate( ( flSpotDot - flStopDot2 ) / max( flStopDot - flStopDot2, 1e-4 ) );
		flAtten *= pow( max( flSpot, 1e-4 ), max( g_Lights[n * 5 + 2].w, 1e-4 ) );
	}
	return flAtten;
}
#endif

VsOutput MainVs( VsInput i )
{
	// Zero-init: vFogData (and any branch-skipped field) must still satisfy
	// fxc's complete-initialization rule on every return path.
	VsOutput o = (VsOutput)0;

#if SPRITECARD
	// spritecard_vsxx.fxc: billboard corner expansion. Family constants ride
	// the VS mirror exactly as the dx9 dynamic block writes them: c48-50 =
	// ModelView rows (orientation 0), c51-54 = Projection rows, c55 = zoom
	// parms, c56/c57 = size/fade parms, c58 = viewport transform. cEyePos
	// comes from b1 (g_EyePosPM); bone0 = the MODEL transform.
	float4 vTint = i.vColor;
	vTint.rgb = pow( max( vTint.rgb, 0.0 ), 2.2 );	// GammaToLinear

	float flBlend = i.vSCParms.x;
	float flRot = i.vSCParms.y;
	float flRad = i.vSCParms.z;
	float flYaw = i.vSCParms.w;

	float2 vSCYaw;
	sincos( flYaw, vSCYaw.y, vSCYaw.x );
	float2 vSC;
	sincos( flRot, vSC.y, vSC.x );

	float2 vIX = 2.0 * i.vSCCornerID - 1.0;
	float flX1 = dot( vIX, vSC );
	float flY1 = vSC.x * vIX.y - vSC.y * vIX.x;

	float4 vPos4 = float4( i.vPos, 1.0 );
	float3 vWorldPos = float3( dot( vPos4, g_BoneRows[0] ), dot( vPos4, g_BoneRows[1] ), dot( vPos4, g_BoneRows[2] ) );

	float3 vV2P = vWorldPos - g_EyePosPM.xyz;
	float flLen = length( vV2P );
	// c56 = {min size, max size, start fade, end fade} x screen fraction;
	// c57 = {far fade start, 1/fade interval}
	flRad = max( flRad, g_VSC[56].x * flLen );
	if ( flRad > g_VSC[56].z * flLen )
	{
		if ( flRad > g_VSC[56].w * flLen )
		{
			vTint = 0.0;
			flRad = 0.0;
		}
		else
		{
			vTint *= 1.0 - ( flRad - g_VSC[56].z * flLen ) / max( g_VSC[56].w * flLen - g_VSC[56].z * flLen, 1e-6 );
		}
	}
	float flTScale = 1.0 - min( 1.0, max( 0.0, ( flLen - g_VSC[57].x ) * g_VSC[57].y ) );
	vTint *= flTScale;
	if ( flTScale <= 0.0 )
		flRad = 0.0;
	flRad = min( flRad, g_VSC[56].y * flLen );

	float4 vProjPos;
	int nOrientation = (int)round( g_SpriteControl.y );
	[branch]
	if ( nOrientation == 0 )
	{
		// Screen-aligned: expand in view space (ModelView rows c48-50)
		float3 vViewPos = float3( dot( vPos4, g_VSC[48] ), dot( vPos4, g_VSC[49] ), dot( vPos4, g_VSC[50] ) );
		float3 vDisp = float3( -flX1, flY1, 0.0 );
		float flTmpX = vDisp.x * vSCYaw.x + vDisp.z * vSCYaw.y;
		vDisp.z = vDisp.z * vSCYaw.x - vDisp.x * vSCYaw.y;
		vDisp.x = flTmpX;
		vViewPos += vDisp * flRad;
		float4 vVP4 = float4( vViewPos, 1.0 );
		vProjPos = float4( dot( vVP4, g_VSC[51] ), dot( vVP4, g_VSC[52] ), dot( vVP4, g_VSC[53] ), dot( vVP4, g_VSC[54] ) );
	}
	else if ( nOrientation == 1 )
	{
		// Z-aligned
		if ( flLen > flRad * 0.5 )
		{
			float3 vUp = float3( 0.0, 0.0, 1.0 );
			float3 vRight = normalize( cross( vUp, vV2P ) );
			float flTmpX = vRight.x * vSCYaw.x + vRight.y * vSCYaw.y;
			vRight.y = vRight.y * vSCYaw.x - vRight.x * vSCYaw.y;
			vRight.x = flTmpX;
			vWorldPos += ( flX1 * flRad ) * vRight;
			vWorldPos.z += flY1 * flRad;
			if ( flLen < flRad * 2.0 )
			{
				vTint *= smoothstep( flRad * 0.5, flRad, flLen );
			}
		}
		vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	}
	else
	{
		// Ground-aligned
		float3 vWPos = i.vPos + i.vSCParms.z * float3( flY1, flX1, 0.0 );
		vProjPos = mul( float4( vWPos, 1.0 ), g_ModelViewProj );
	}

	o.vProjPos = vProjPos;
	// Particle center world Z + clip Z for the scene fog blend (the corner
	// offset is negligible at fog scale)
	o.vFogData = float2( i.vPos.z, vProjPos.z );
	o.vSCBlend0 = float4( flBlend, 0.0, 0.0, 0.0 );
	o.vTexCoord.x = lerp( i.vSCTexCoord0.z, i.vSCTexCoord0.x, i.vSCCornerID.x );
	o.vTexCoord.y = lerp( i.vSCTexCoord0.w, i.vSCTexCoord0.y, i.vSCCornerID.y );
	o.vSCTex1.x = lerp( i.vSCTexCoord1.z, i.vSCTexCoord1.x, i.vSCCornerID.x );
	o.vSCTex1.y = lerp( i.vSCTexCoord1.w, i.vSCTexCoord1.y, i.vSCCornerID.y );
	o.vSCTex2.x = lerp( i.vSCTexCoord2.z, i.vSCTexCoord2.x, i.vSCCornerID.x );
	o.vSCTex2.y = lerp( i.vSCTexCoord2.w, i.vSCTexCoord2.y, i.vSCCornerID.y );

	int nSCFlags = (int)round( g_SpriteControl.x );
	float2 vLerpOld = i.vSCCornerID;
	float2 vLerpNew = i.vSCCornerID;
	[branch]
	if ( ( nSCFlags & 8 ) != 0 )	// DUALSEQUENCE
	{
		// ZOOM_ANIMATE_SEQ2 (c55 = {0.5*(1+scale), scale}): scale the corner
		// lerp around the frame center; zero c55 (combo off — the dx9 block
		// only writes it when zooming) degenerates to... guard on nonzero.
		if ( g_VSC[55].x != 0.0 )
		{
			float flTs = i.vSCParms1.x;
			vLerpOld = 0.5 + 0.5 * ( 2.0 * ( i.vSCCornerID - 0.5 ) * lerp( g_VSC[55].x, g_VSC[55].y, flTs ) );
			vLerpNew = 0.5 + 0.5 * ( 2.0 * ( i.vSCCornerID - 0.5 ) * lerp( 1.0, g_VSC[55].x, flTs ) );
		}
		o.vSCSeq2Tex0.x = lerp( i.vSCSeq2TexCoord0.z, i.vSCSeq2TexCoord0.x, vLerpOld.x );
		o.vSCSeq2Tex0.y = lerp( i.vSCSeq2TexCoord0.w, i.vSCSeq2TexCoord0.y, vLerpOld.y );
		o.vSCSeq2Tex1.x = lerp( i.vSCSeq2TexCoord1.z, i.vSCSeq2TexCoord1.x, vLerpNew.x );
		o.vSCSeq2Tex1.y = lerp( i.vSCSeq2TexCoord1.w, i.vSCSeq2TexCoord1.y, vLerpNew.y );
		o.vSCBlend0.z = i.vSCParms1.x;
	}
	else
	{
		o.vSCSeq2Tex0 = float2( 0.0, 0.0 );
		o.vSCSeq2Tex1 = float2( 0.0, 0.0 );
	}

	o.vSCBlend1 = float4( 0.0, 0.0, 0.0, 0.0 );
	[branch]
	if ( ( nSCFlags & 128 ) != 0 )	// EXTRACTGREENALPHA
	{
		if ( flBlend < 0.25 )
		{
			o.vSCBlend0.a = flBlend * 2.0 + 0.5;
			o.vSCBlend0.g = 1.0 - o.vSCBlend0.a;
		}
		else if ( flBlend < 0.75 )
		{
			o.vSCBlend1.g = flBlend * 2.0 - 0.5;
			o.vSCBlend0.a = 1.0 - o.vSCBlend1.g;
		}
		else
		{
			o.vSCBlend1.a = flBlend * 2.0 - 1.5;
			o.vSCBlend1.g = 1.0 - o.vSCBlend1.a;
		}
	}

	o.vColor = vTint;
	o.vDetailCoord = float2( 0.0, 0.0 );
	return o;
#else	// !SPRITECARD: everything below references the standard VsInput

#if WATER
	// water_vs20.fxc (expensive). Bump transform rides VS c49/c50
	// (SHADER_SPECIFIC_CONST_1, two rows), scroll offsets c51; reflect uvs are
	// projection-derived. The dx9 "projected tangent" lines are dead code.
	float4 vPos4 = float4( i.vPos, 1.0 );
	float4 vProjPos = mul( vPos4, g_ModelViewProj );
	o.vProjPos = vProjPos;
	o.vWaterProjPos = vProjPos;

	float2 vReflectPos = ( vProjPos.xy + vProjPos.w ) * 0.5;
	float2 vRefractPos = ( float2( vProjPos.x, -vProjPos.y ) + vProjPos.w ) * 0.5;
	o.vWaterReflRefr = float4( vReflectPos.x, vReflectPos.y, vRefractPos.y, vRefractPos.x );

	float3 vWorldPos = float3( dot( vPos4, g_BoneRows[0] ), dot( vPos4, g_BoneRows[1] ), dot( vPos4, g_BoneRows[2] ) );
	o.vFogData = float2( vWorldPos.z, vProjPos.z );
	float3 vWorldEye = g_EyePosPM.xyz - vWorldPos;
	o.vWaterTanEye = float3(
		dot( vWorldEye, i.vTangentS ), dot( vWorldEye, i.vTangentT ), dot( vWorldEye, i.vNormal ) );

	// dx9 reads texcoord0 as float4 with IA defaults (z=0, w=1): the dot with
	// the transform row picks up the translation via .w
	float4 vBase4 = float4( i.vTexCoord, 0.0, 1.0 );
	o.vTexCoord.x = dot( vBase4, g_VSC[49] );
	o.vTexCoord.y = dot( vBase4, g_VSC[50] );
	float flF45x = i.vTexCoord.x + i.vTexCoord.y;
	float flF45y = i.vTexCoord.y - i.vTexCoord.x;
	o.vWaterExtraBump.x = flF45x * 0.1 + g_VSC[51].x;
	o.vWaterExtraBump.y = flF45y * 0.1 + g_VSC[51].y;
	o.vWaterExtraBump.z = i.vTexCoord.y * 0.45 + g_VSC[51].z;
	o.vWaterExtraBump.w = i.vTexCoord.x * 0.45 + g_VSC[51].w;

	o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
	o.vDetailCoord = float2( 0.0, 0.0 );
	return o;
#elif WATERCHEAP
	// watercheap_vs20.fxc: world TBN transpose to the PS, cube reflection
	// there; scroll offsets at VS c51 (SHADER_SPECIFIC_CONST_3).
	float4 vPos4 = float4( i.vPos, 1.0 );
	float4 vProjPos = mul( vPos4, g_ModelViewProj );
	o.vProjPos = vProjPos;
	// REFRACTALPHA screen uv (watercheap_vs20 BLEND combo — harmless when the
	// refract RT isn't bound; the PS gates the tap on the flag bits)
	o.vWaterRefract.x = ( vProjPos.x + vProjPos.w ) * 0.5;
	o.vWaterRefract.y = ( -vProjPos.y + vProjPos.w ) * 0.5;
	o.vWaterRefract.z = vProjPos.w;

	float3 vWorldPos = float3( dot( vPos4, g_BoneRows[0] ), dot( vPos4, g_BoneRows[1] ), dot( vPos4, g_BoneRows[2] ) );
	o.vFogData = float2( vWorldPos.z, vProjPos.z );
	float3 vWT = float3( dot( i.vTangentS, g_BoneRows[0].xyz ), dot( i.vTangentS, g_BoneRows[1].xyz ), dot( i.vTangentS, g_BoneRows[2].xyz ) );
	float3 vWB = float3( dot( i.vTangentT, g_BoneRows[0].xyz ), dot( i.vTangentT, g_BoneRows[1].xyz ), dot( i.vTangentT, g_BoneRows[2].xyz ) );
	float3 vWN = float3( dot( i.vNormal, g_BoneRows[0].xyz ), dot( i.vNormal, g_BoneRows[1].xyz ), dot( i.vNormal, g_BoneRows[2].xyz ) );
	o.vWaterTBN0 = vWT;
	o.vWaterTBN1 = vWB;
	o.vWaterTBN2 = vWN;
	o.vWaterEyeVect = g_EyePosPM.xyz - vWorldPos;	// VSHADER_VECT_SCALE = 1

	o.vTexCoord = i.vTexCoord;
	float flF45x = i.vTexCoord.x + i.vTexCoord.y;
	float flF45y = i.vTexCoord.y - i.vTexCoord.x;
	o.vWaterExtraBump.x = flF45x * 0.1 + g_VSC[51].x;
	o.vWaterExtraBump.y = flF45y * 0.1 + g_VSC[51].y;
	o.vWaterExtraBump.z = i.vTexCoord.y * 0.45 + g_VSC[51].z;
	o.vWaterExtraBump.w = i.vTexCoord.x * 0.45 + g_VSC[51].w;

	o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
	o.vDetailCoord = float2( 0.0, 0.0 );
	return o;
#endif

#if EYES
	// eyes_vs20.fxc: skinned position; the NORMAL is synthesized — a sphere
	// normal from the eyeball origin (VS c48), flattened toward the eye-up
	// axis (c49 arrives as up * 0.5, hence the extra 0.5 in the fold).
	// ApplyMorph is position-only for eyes.
	float4 vPos4 = float4( i.vPos + i.vPosFlex.xyz, 1.0 );
	float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
	uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
	if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
	{
		vWeights = float3( 1.0, 0.0, 0.0 );
		vIdx = uint3( 0, 0, 0 );
	}
	float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
	float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
	float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
	float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
	o.vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );

	float3 vEyeNormal = vWorldPos - g_VSC[48].xyz;					// cEyeOrigin
	float flNormalDotUp = -dot( vEyeNormal, g_VSC[49].xyz ) * 0.5;	// cHalfEyeballUp
	vEyeNormal = normalize( flNormalDotUp * g_VSC[49].xyz + vEyeNormal );

	// DoLighting (ambient cube + locals); $halflambert rides g_PhongFlags.x
	float3 vLight = AmbientCubeLight( vEyeNormal )
		+ LocalLights( vWorldPos, vEyeNormal, g_PhongFlags.x > 0.5 );
	o.vColor = float4( clamp( vLight, 0.0, 8.0 ), 1.0 );

	// Iris/glint UVs are planar projections of world position (VS c50-c53)
	float4 vWorldPos4 = float4( vWorldPos, 1.0 );
	o.vIrisCoord.x = dot( g_VSC[50], vWorldPos4 );
	o.vIrisCoord.y = dot( g_VSC[51], vWorldPos4 );
	o.vGlintCoord.x = dot( g_VSC[52], vWorldPos4 );
	o.vGlintCoord.y = dot( g_VSC[53], vWorldPos4 );
	o.vFogData = float2( vWorldPos.z, o.vProjPos.z );
#elif WINDOWIMPOSTER
	// windowimposter_vs20.fxc: bone0/model transform, modulation color from
	// VS c47 (SetModulationVertexShaderDynamicState writes it every pass);
	// the eye-to-vertex ray moves to the PS (worldPos - ps c11 eyepos).
	float4 vPos4 = float4( i.vPos, 1.0 );
	float3 vWorldPos = float3( dot( vPos4, g_BoneRows[0] ), dot( vPos4, g_BoneRows[1] ), dot( vPos4, g_BoneRows[2] ) );
	o.vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	o.vWorldPosWI = vWorldPos;
	o.vColor = g_VSC[47];	// cModulationColor
	o.vFogData = float2( vWorldPos.z, o.vProjPos.z );
#elif EYEREFRACT
	// eye_refract_vs20.fxc: skinned position; eyeball-sphere normal from
	// $eyeorigin (VS c48); tangent frame derived from the iris projection
	// vectors (c50 = U/"left", c51 = V/"up", both negated + normalized).
	// ApplyMorph is position-only for eyes.
	float4 vPos4 = float4( i.vPos + i.vPosFlex.xyz, 1.0 );
	float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
	uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
	if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
	{
		vWeights = float3( 1.0, 0.0, 0.0 );
		vIdx = uint3( 0, 0, 0 );
	}
	float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
	float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
	float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
	float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
	o.vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	o.vERWorldPos = vWorldPos;
	o.vFogData = float2( vWorldPos.z, o.vProjPos.z );

	float3 vEyeSocketUp = normalize( -g_VSC[51].xyz );		// -cIrisProjectionV
	float3 vEyeSocketLeft = normalize( -g_VSC[50].xyz );	// -cIrisProjectionU
	float3 vWorldNormal = normalize( vWorldPos - g_VSC[48].xyz );
	float3 vWorldTangent = normalize( cross( vEyeSocketUp, vWorldNormal ) );
	float3 vWorldBinormal = normalize( cross( vWorldNormal, vWorldTangent ) );
	o.vERNormal = vWorldNormal;
	o.vERTangent = vWorldTangent;
	o.vERBinormal = vWorldBinormal;

	// "Bent" normal for vertex lighting: flatten toward the socket-left axis
	float flNDotSide = -dot( vWorldNormal, vEyeSocketLeft ) * 0.5;
	float3 vBentNormal = normalize( flNDotSide * vEyeSocketLeft + vWorldNormal );

	bool bERHalfLambert = g_PhongFlags.x > 0.5;
	// With a lightwarp ($lightwarptexture — TF NPR eyes), the VS keeps ONLY
	// the ambient term; local lights move to the PS warp loop.
	if ( g_PhongFlags.y > 0.5 )
	{
		o.vColor = float4( AmbientCubeLight( vBentNormal ), 1.0 );
	}
	else
	{
		float3 vLight = AmbientCubeLight( vBentNormal )
			+ LocalLights( vWorldPos, vBentNormal, bERHalfLambert );
		o.vColor = float4( clamp( vLight, 0.0, 8.0 ), 1.0 );
	}

	// Per-light attenuation + cosine pairs (cosine vs the UNBENT normal)
	o.vERFalloffCos01 = float4(
		VSLightAttenOnly( 0, vWorldPos ), VSLightAttenOnly( 1, vWorldPos ),
		VSCosineTerm( 0, vWorldPos, vWorldNormal, bERHalfLambert ),
		VSCosineTerm( 1, vWorldPos, vWorldNormal, bERHalfLambert ) );
	o.vERFalloffCos23 = float4(
		VSLightAttenOnly( 2, vWorldPos ), VSLightAttenOnly( 3, vWorldPos ),
		VSCosineTerm( 2, vWorldPos, vWorldNormal, bERHalfLambert ),
		VSCosineTerm( 3, vWorldPos, vWorldNormal, bERHalfLambert ) );
#elif PYRO
	// pyro_vision_vs20.fxc (effects 0/1). World geometry transforms by bone 0
	// (= the MODEL stack / dx9 cModel[0]); models additionally blend real
	// weights — rigid formats feed constant zero weights from the slot-1
	// fallback, which degenerates to pure bone 0 (w2 = 1 - 0 - 0).
	float4 vPos4 = float4( i.vPos, 1.0 );
#if PYROMODEL
	float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
	uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
	if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
	{
		vWeights = float3( 1.0, 0.0, 0.0 );
		vIdx = uint3( 0, 0, 0 );
	}
	float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
	float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
	float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
#else
	float4 vRow0 = g_BoneRows[0];
	float4 vRow1 = g_BoneRows[1];
	float4 vRow2 = g_BoneRows[2];
#endif
	float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
	o.vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	o.vFogData = float2( vWorldPos.z, o.vProjPos.z );

	// Seamless canvas projection (VS c49 = $canvas_scale; effect 1 leaves it
	// zero — the coord goes unused there)
	float flZFactor = vWorldPos.z * g_VSC[49].z;
	o.vPyroSeamlessCoord = float2( vWorldPos.x + flZFactor, vWorldPos.y - flZFactor ) * g_VSC[49].xy;
	// Stripe projection (VS c48 = $stripe_scale)
	float3 vStripeWS = vWorldPos * g_VSC[48].xyz;
	o.vPyroStripeCoord = vStripeWS.yz + vStripeWS.xz + vStripeWS.xy;
	// $blendmodulatetexture transform rows at VS c58/c59
	// (SHADER_SPECIFIC_CONST_10 — "not contiguous with the rest!")
	o.vPyroBlendFactor.x = dot( i.vTexCoord, g_VSC[58].xy ) + g_VSC[58].w;
	o.vPyroBlendFactor.y = dot( i.vTexCoord, g_VSC[59].xy ) + g_VSC[59].w;
	o.vPyroBlendFactor.z = i.vColor.a;

#if PYROWORLD
	o.vLightmapCoord = i.vLightmapCoord;
#endif

	int nPyroFlags = (int)round( g_EyeControl.w );
	if ( ( nPyroFlags & 1 ) != 0 )			// VERTEXCOLOR
	{
		o.vColor = i.vColor;
	}
#if PYROMODEL
	else if ( ( nPyroFlags & 2 ) == 0 )		// VERTEX_LIT && !FULLBRIGHT
	{
		float3 vNRot = float3(
			dot( i.vNormal, vRow0.xyz ), dot( i.vNormal, vRow1.xyz ), dot( i.vNormal, vRow2.xyz ) );
		float flNLenSq = dot( vNRot, vNRot );
		float3 vWorldNormal = ( flNLenSq > 1e-8 && !isnan( flNLenSq ) )
			? vNRot * rsqrt( flNLenSq ) : float3( 0.0, 0.0, 1.0 );
		float3 vLight = AmbientCubeLight( vWorldNormal )
			+ LocalLights( vWorldPos, vWorldNormal, g_PhongFlags.x > 0.5 );
		// dx9 DoLighting bStaticLight: baked prop colors (COLOR1, slot 2 —
		// zeros when the prop has no color mesh). Without this, static props
		// went near-black in pyrovision (their pass has a zeroed ambient cube).
		vLight += pow( i.vSpecular.rgb * 2.0, 2.2 );
		// dx9 outputs this through a COLOR semantic, which D3D9 CLAMPS to
		// [0,1] at the interpolator — the effect-1 gray math depends on it
		// (unclamped light saturated flGray at 1.0 = solid colorbar-end pink
		// across every prop).
		o.vColor = float4( saturate( vLight ), 1.0 );
	}
#endif
	else
	{
		o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
	}
#elif SSDOWNSAMPLE
	// Downsample_vs20: clip-space passthrough + 4 tap coords (VS c48-51 .xy)
	o.vProjPos = float4( i.vPos, 1.0 );
	o.vTexCoord = i.vTexCoord;
	o.vSSTapA = float4( i.vTexCoord + g_VSC[48].xy, i.vTexCoord + g_VSC[49].xy );
	o.vSSTapB = float4( i.vTexCoord + g_VSC[50].xy, i.vTexCoord + g_VSC[51].xy );
	return o;
#elif SSBLUR
	// BlurFilter_vs20: clip-space passthrough + 7 tap coords (c48-50 +/-)
	o.vProjPos = float4( i.vPos, 1.0 );
	o.vTexCoord = i.vTexCoord;	// tap 0
	o.vSSTapA = float4( i.vTexCoord + g_VSC[48].xy, i.vTexCoord + g_VSC[49].xy );
	o.vSSTapB = float4( i.vTexCoord + g_VSC[50].xy, i.vTexCoord - g_VSC[48].xy );
	o.vSSTapC = float4( i.vTexCoord - g_VSC[49].xy, i.vTexCoord - g_VSC[50].xy );
	return o;
#elif SSADD || ENGINEPOST || COLORPROJ || LUMCOMPARE || INTROEFFECT || MOTIONBLUR
	// screenspaceeffect_vs20 / color_projection_vs20 / motion_blur_vs20:
	// clip-space + uv passthrough (DrawScreenSpaceRectangle verts arrive
	// pre-projected)
	o.vProjPos = float4( i.vPos, 1.0 );
	o.vTexCoord = i.vTexCoord;
	o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
	return o;
#elif SHADOWPROJ
	// shadow_vs20: clipped world-geometry decal verts through the full MVP
	// (the engine drives MODEL to the receiver transform/identity). uv via
	// the c48/49 transform; 4 jittered taps at +/- one atlas texel (c50.xy =
	// (1/w, 1/h), c51.xy = (1/w, -1/h)); vertex COLOR passes through
	// (a = per-vertex shadow fade).
	float4 vProjPos = mul( float4( i.vPos, 1.0 ), g_ModelViewProj );
	o.vProjPos = vProjPos;
	float4 vUV4 = float4( i.vTexCoord, 0.0, 1.0 );
	float2 vUV = float2( dot( vUV4, g_VSC[48] ), dot( vUV4, g_VSC[49] ) );
	o.vTexCoord = vUV;
	o.vSSTapA = float4( vUV + g_VSC[50].xy, vUV - g_VSC[50].xy );
	o.vSSTapB = float4( vUV + g_VSC[51].xy, vUV - g_VSC[51].xy );
	o.vColor = i.vColor;
	o.vFogData = float2( i.vPos.z, vProjPos.z );
	return o;
#elif SHADOWMODEL
	// shadowmodel_vs20: skinned position + normal; shadow-space texgen via
	// the c48-50 matrix rows (SetVertexShaderMatrix3x4 BASETEXTURETRANSFORM);
	// texkill fields: in-volume (T1 = texPos, T2 = 1-texPos with z replaced
	// by 1-fade) and backface (T3 = dot(normal, -row2)); c47 = shadow color.
	float4 vPos4 = float4( i.vPos, 1.0 );
	float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
	uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
	if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
	{
		vWeights = float3( 1.0, 0.0, 0.0 );
		vIdx = uint3( 0, 0, 0 );
	}
	float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
	float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
	float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
	float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
	float3 vWorldNormal = float3( dot( i.vNormal, vRow0.xyz ), dot( i.vNormal, vRow1.xyz ), dot( i.vNormal, vRow2.xyz ) );
	float4 vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	o.vProjPos = vProjPos;
	float3 vTexPos = float3(
		dot( vWorldPos, g_VSC[48].xyz ),
		dot( vWorldPos, g_VSC[49].xyz ),
		dot( vWorldPos, g_VSC[50].xyz ) );
	float flShadowFade = ( vTexPos.z - g_VSC[53].x ) * g_VSC[53].y;
	o.vSSTapA = float4( vTexPos, dot( vWorldNormal, -g_VSC[50].xyz ) );
	o.vSSTapB = float4( 1.0 - vTexPos.x, 1.0 - vTexPos.y, 1.0 - flShadowFade, 0.0 );
	o.vColor = float4( g_VSC[47].rgb, 1.0 );
	o.vFogData = float2( vWorldPos.z, vProjPos.z );
	return o;
#elif MODULATE || SHADOWBUILD
	// unlitgeneric_vs20 (the Modulate family's VS, and ShadowBuild's): skinned
	// position, uv via $basetexturetransform (c48/49), vColor = the gamma
	// modulation (VS c47, SetModulationVertexShaderDynamicState — the $alpha
	// Sine rides it; for ShadowBuild c47.a = the shadow strength).
	float4 vPos4 = float4( i.vPos, 1.0 );
	float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
	uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
	if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
	{
		vWeights = float3( 1.0, 0.0, 0.0 );
		vIdx = uint3( 0, 0, 0 );
	}
	float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
	float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
	float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
	float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
	float4 vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	o.vProjPos = vProjPos;
	float4 vBase4 = float4( i.vTexCoord, 0.0, 1.0 );
	o.vTexCoord = float2( dot( vBase4, g_VSC[48] ), dot( vBase4, g_VSC[49] ) );
	o.vColor = g_VSC[47];	// cModulationColor
	o.vFogData = float2( vWorldPos.z, vProjPos.z );
	return o;
#elif FLASHLIGHT
	// lightmappedgeneric_flashlight_vs20 / vertexlitgeneric_flashlight:
	// skinned position + normal (zero-weight fallback -> bone 0 = the MODEL
	// stack top: identity for world geometry, the entity transform for brush
	// models). Spot projection = worldPos through the worldToTexture rows
	// (VS c49-52, uploaded by SetFlashlightVertexShaderConstants); flashlight
	// origin = c48.xyz; base uv via the c54/55 transform (SHADER_SPECIFIC_
	// CONST_6).
	{
		float4 vPos4 = float4( i.vPos, 1.0 );
		float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
		uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
		if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
		{
			vWeights = float3( 1.0, 0.0, 0.0 );
			vIdx = uint3( 0, 0, 0 );
		}
		float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
		float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
		float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
		float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
		float3 vWorldNormal = float3( dot( i.vNormal, vRow0.xyz ), dot( i.vNormal, vRow1.xyz ), dot( i.vNormal, vRow2.xyz ) );
		int nFLVS = (int)round( g_SpriteControl.w );
		float4 vProjPos;
		if ( ( nFLVS & 2 ) != 0 )
		{
			// Rigid no-bone-format draw (world geometry, brush entities like
			// the trainstation tracktrain/turnstile): the BASE pass transforms
			// through the folded MVP in one step — going (pos x bone0) x
			// ViewProj here rounds differently and z-fights the additive
			// flashlight pass. Use the bit-identical base transform for depth;
			// vWorldPos (bone0 = the MODEL matrix) still feeds the lighting.
			vProjPos = mul( vPos4, g_ModelViewProj );
		}
		else
		{
			vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
		}
		o.vProjPos = vProjPos;
		float4 vBase4 = float4( i.vTexCoord, 0.0, 1.0 );
		if ( ( nFLVS & 1 ) != 0 )
		{
			// VLG inline family: base uv via the standard c48/49 transform
			// (c54-57 hold the worldToTexture rows there — CONST_6 x4)
			o.vTexCoord = float2( dot( vBase4, g_VSC[48] ), dot( vBase4, g_VSC[49] ) );
		}
		else
		{
			// DrawFlashlight_dx90 family: base uv via CONST_6 (c54/55)
			o.vTexCoord = float2( dot( vBase4, g_VSC[54] ), dot( vBase4, g_VSC[55] ) );
		}
		float4 vWP4 = float4( vWorldPos, 1.0 );
		o.vSSTapA = float4( dot( vWP4, g_VSC[49] ), dot( vWP4, g_VSC[50] ),
			dot( vWP4, g_VSC[51] ), dot( vWP4, g_VSC[52] ) );
		if ( ( nFLVS & 4 ) != 0 )
		{
			// eyes_flashlight_vs20 (the EYES family, bit 4): iris uv via the
			// c56/c57 projections (CONST_8/9) and a PER-VERTEX attenuation —
			// endFalloff x dot({1,1/d,1/d^2}, c53.xyz atten) x UNSATURATED
			// NdotL toward the c48 light origin (dx9 keeps the sign).
			o.vDetailCoord = float2( dot( g_VSC[56], vWP4 ), dot( g_VSC[57], vWP4 ) );
			float3 vFLToLight = g_VSC[48].xyz - vWorldPos;
			float flFLDistSq = max( dot( vFLToLight, vFLToLight ), 0.0001 );
			float flFLDist = sqrt( flFLDistSq );
			float flFLFar = g_VSC[53].w;
			float flFLEnd = saturate( ( flFLDist - flFLFar ) / ( 0.6 * flFLFar - flFLFar ) );
			float flFLVertAtten = flFLEnd *
				dot( float3( 1.0, 1.0 / flFLDist, 1.0 / flFLDistSq ), g_VSC[53].xyz );
			flFLVertAtten *= dot( vFLToLight / flFLDist, vWorldNormal );
			o.vSSTapB = float4( flFLVertAtten, 0.0, 0.0, vWorldPos.x );
		}
		else
		{
			o.vSSTapB = float4( g_VSC[48].xyz - vWorldPos, vWorldPos.x );
		}
		o.vSSTapC = float4( vWorldNormal, vWorldPos.y );
		o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
		o.vFogData = float2( vWorldPos.z, vProjPos.z );
		return o;
	}
#elif DEPTHWRITE
	// depthwrite_vs20: position-only depth fill into the shadow map. Same
	// transform discriminator as FLASHLIGHT bit 2 — world/brush depth fills
	// ride the folded MVP (their base pass does), studio casters the bone
	// path (zero-weight fallback -> bone 0 = the MODEL stack top). Texcoord
	// passes through untransformed for the alphatest variants' s0 clip.
	{
		float4 vPos4 = float4( i.vPos, 1.0 );
		int nDWVS = (int)round( g_SpriteControl.w );
		if ( ( nDWVS & 2 ) != 0 )
		{
			o.vProjPos = mul( vPos4, g_ModelViewProj );
		}
		else
		{
			float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
			uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
			if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
			{
				vWeights = float3( 1.0, 0.0, 0.0 );
				vIdx = uint3( 0, 0, 0 );
			}
			float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
			float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
			float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
			float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
			o.vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
		}
		o.vTexCoord = i.vTexCoord;
		o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
		return o;
	}
#elif UNLITTWOTEX || MONITORSCREEN
	// unlittwotexture_vs20 (shared by MonitorScreen): skinned position only
	// (the format's NORMAL is ignored); texcoord0 through TWO transforms —
	// $basetexturetransform (c48/49) into vTexCoord, $texture2transform
	// (c50/51, the hologram scan-line scroll) into vDetailCoord.
	float4 vPos4 = float4( i.vPos, 1.0 );
	float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
	uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
	if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
	{
		vWeights = float3( 1.0, 0.0, 0.0 );
		vIdx = uint3( 0, 0, 0 );
	}
	float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
	float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
	float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
	float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
	float4 vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	o.vProjPos = vProjPos;
	float4 vBase4 = float4( i.vTexCoord, 0.0, 1.0 );
	o.vTexCoord = float2( dot( vBase4, g_VSC[48] ), dot( vBase4, g_VSC[49] ) );
	o.vDetailCoord = float2( dot( vBase4, g_VSC[50] ), dot( vBase4, g_VSC[51] ) );
#if MONITORSCREEN
	// dx9 SetModulationVertexShaderDynamicState: vColor = cModulationColor
	// (VS c47) — the PS multiplies base x vColor like unlittwotexture's
	// modulation, but MonitorScreen's ps c1 carries $contrast, so the c1
	// modulation latch must not be used here.
	o.vColor = g_VSC[47];
#else
	o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
#endif
	o.vFogData = float2( vWorldPos.z, vProjPos.z );
	return o;
#elif CLOAKPASS || SHEENPASS || REFRACT
	// cloak_blended_pass_vs20 / weapon_sheen_pass_vs20 / refract_vs20: skinned
	// position + normal, refract uv from the projection, normalized view
	// vector. Cloak/sheen bump transform = VS c48/49 (two rows, .xy dots);
	// refract = c49..c52 (four rows, float4 dots — $bumptransform scrolls the
	// underwater warp). Unwritten registers upload as zeros via the mirror.
	// Refract has no flex stream (dx9 refract_vs20 has no ApplyMorph either).
#if HASFLEX
	float4 vPos4 = float4( i.vPos + i.vPosFlex.xyz, 1.0 );
	float3 vNormalIn = i.vNormal + i.vNormalFlex;
#else
	float4 vPos4 = float4( i.vPos, 1.0 );
	float3 vNormalIn = i.vNormal;
#endif
	float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
	uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
	if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
	{
		vWeights = float3( 1.0, 0.0, 0.0 );
		vIdx = uint3( 0, 0, 0 );
	}
	float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
	float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
	float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
	float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
	float3 vRotNormal = float3(
		dot( vNormalIn, vRow0.xyz ), dot( vNormalIn, vRow1.xyz ), dot( vNormalIn, vRow2.xyz ) );
	float flLenSq = dot( vRotNormal, vRotNormal );
	float3 vWorldNormal = ( flLenSq > 1e-8 && !isnan( flLenSq ) && !isinf( flLenSq ) )
		? vRotNormal * rsqrt( flLenSq ) : float3( 0.0, 0.0, 1.0 );

	float4 vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	o.vProjPos = vProjPos;
	o.vCSNormal = vWorldNormal;
	o.vCSRefract = float3( ( vProjPos.x + vProjPos.w ) * 0.5,
		( -vProjPos.y + vProjPos.w ) * 0.5, vProjPos.w );
	o.vCSView = normalize( vWorldPos - g_EyePosPM.xyz );
#if REFRACT
	float4 vBase4 = float4( i.vTexCoord, 0.0, 1.0 );
	o.vRefrBump.x = dot( vBase4, g_VSC[49] );
	o.vRefrBump.y = dot( vBase4, g_VSC[50] );
	o.vRefrBump.w = dot( vBase4, g_VSC[51] );	// dx9 packs transform2 as wz
	o.vRefrBump.z = dot( vBase4, g_VSC[52] );
	o.vTexCoord = o.vRefrBump.xy;
#else
	o.vTexCoord = float2( dot( i.vTexCoord, g_VSC[48].xy ), dot( i.vTexCoord, g_VSC[49].xy ) );
#endif
#if SHEENPASS
	o.vCSModelPos = i.vPos;
#endif
	o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
	o.vFogData = float2( vWorldPos.z, vProjPos.z );
	return o;
#elif MODELMODE
	float4 vPos4 = float4( i.vPos + i.vPosFlex.xyz, 1.0 );
	float3 vNormalIn = i.vNormal + i.vNormalFlex;
#if SKINNED
	float3 vWeights = float3( i.vBoneWeights.xy, 1.0 - i.vBoneWeights.x - i.vBoneWeights.y );
	uint3 vIdx = min( i.vBoneIndices.xyz, 52 ) * 3;
	// Guard: meshes whose weight fields were never filled (NaN/garbage from
	// the staging build — see Plan.md M5 status) fall back to rigid bone 0
	// instead of exploding across clip space.
	if ( any( isnan( vWeights ) ) || vWeights.x < 0.0 || vWeights.x > 1.0 )
	{
		vWeights = float3( 1.0, 0.0, 0.0 );
		vIdx = uint3( 0, 0, 0 );
	}
	float4 vRow0 = vWeights.x * g_BoneRows[vIdx.x]     + vWeights.y * g_BoneRows[vIdx.y]     + vWeights.z * g_BoneRows[vIdx.z];
	float4 vRow1 = vWeights.x * g_BoneRows[vIdx.x + 1] + vWeights.y * g_BoneRows[vIdx.y + 1] + vWeights.z * g_BoneRows[vIdx.z + 1];
	float4 vRow2 = vWeights.x * g_BoneRows[vIdx.x + 2] + vWeights.y * g_BoneRows[vIdx.y + 2] + vWeights.z * g_BoneRows[vIdx.z + 2];
#else
	float4 vRow0 = g_BoneRows[0];
	float4 vRow1 = g_BoneRows[1];
	float4 vRow2 = g_BoneRows[2];
#endif
	float3 vWorldPos = float3( dot( vPos4, vRow0 ), dot( vPos4, vRow1 ), dot( vPos4, vRow2 ) );
	// Safe normalize: degenerate blended normals (partial-influence verts)
	// must not reach normalize() — INF rgb here paints black splotches.
	float3 vRotNormal = float3(
		dot( vNormalIn, vRow0.xyz ), dot( vNormalIn, vRow1.xyz ), dot( vNormalIn, vRow2.xyz ) );
	float flLenSq = dot( vRotNormal, vRotNormal );
	float3 vWorldNormal = ( flLenSq > 1e-8 && !isnan( flLenSq ) && !isinf( flLenSq ) )
		? vRotNormal * rsqrt( flLenSq ) : float3( 0.0, 0.0, 1.0 );

	o.vProjPos = mul( float4( vWorldPos, 1.0 ), g_ViewProj );
	o.vFogData = float2( vWorldPos.z, o.vProjPos.z );
#if PHONG
	// dx9 skin_vs20.fxc: lighting moves to the pixel shader — the VS hands
	// over world pos, per-light attenuation, and the tangent-space transpose
	// (rows = (T B N) per world axis; binormal = cross(N, T) * sign, the
	// SkinPositionNormalAndTangentSpace convention, common_vs_fxc.h:730).
	// Tangents flex with the same deltas as normals (common_vs_fxc ApplyMorph)
	float3 vTangentIn = i.vUserData.xyz + i.vNormalFlex;
	float3 vWorldTangent = float3(
		dot( vTangentIn, vRow0.xyz ), dot( vTangentIn, vRow1.xyz ), dot( vTangentIn, vRow2.xyz ) );
	float flTanLenSq = dot( vWorldTangent, vWorldTangent );
	vWorldTangent = ( flTanLenSq > 1e-8 && !isnan( flTanLenSq ) ) ? vWorldTangent * rsqrt( flTanLenSq ) : float3( 1.0, 0.0, 0.0 );
	float3 vWorldBinormal = normalize( cross( vWorldNormal, vWorldTangent ) * i.vUserData.w );
	o.vTBN0 = float3( vWorldTangent.x, vWorldBinormal.x, vWorldNormal.x );
	o.vTBN1 = float3( vWorldTangent.y, vWorldBinormal.y, vWorldNormal.y );
	o.vTBN2 = float3( vWorldTangent.z, vWorldBinormal.z, vWorldNormal.z );
	o.vWorldPosAtten3 = float4( vWorldPos, VSLightAttenOnly( 3, vWorldPos ) );
	o.vLightAtten = float3( VSLightAttenOnly( 0, vWorldPos ),
		VSLightAttenOnly( 1, vWorldPos ), VSLightAttenOnly( 2, vWorldPos ) );
	o.vColor = float4( 1.0, 1.0, 1.0, 1.0 );
#else
	// Vertex lighting: ambient cube + local lights (dx9 vertexlitgeneric vs20
	// baseline); computed linear, re-encoded by the sRGB write at the OM stage.
	// Clamped: corrupt inputs must not propagate INF into interpolation.
	float3 vLight = AmbientCubeLight( vWorldNormal ) + LocalLights( vWorldPos, vWorldNormal, false );
#if STATICLIGHT
	// dx9 DoLighting bStaticLight (common_vs_fxc.h:858): baked colors arrive
	// gamma-space premultiplied by 1/overbright — GammaToLinear(spec * 2.0).
	// Ambient cube + locals stay in the sum: DYNAMIC_LIGHT=0 coincides with a
	// zeroed cube and no lights, so the extra terms are zero exactly when dx9
	// would skip them.
	vLight += pow( i.vSpecular.rgb * 2.0, 2.2 );
#endif
#if TEETH
	// teeth_vs20.fxc:119 — darken by $illumfactor x saturate(N . $forward);
	// both ride VS c48 ({forward.xyz, illumfactor}, teeth.cpp:216).
	vLight *= g_VSC[48].w * saturate( dot( vWorldNormal, g_VSC[48].xyz ) );
#endif
	o.vColor = float4( clamp( vLight, 0.0, 8.0 ), 1.0 );
#endif
#else
	o.vProjPos = mul( float4( i.vPos, 1.0 ), g_ModelViewProj );
	o.vColor = i.vColor;
	// World Z via bone 0 (= the MODEL stack top, identity for the static
	// world) — b1 is bound for every perm so the refraction view's brushes
	// and displacements can write water-fog depth to dest alpha.
	o.vFogData = float2( dot( float4( i.vPos, 1.0 ), g_BoneRows[2] ), o.vProjPos.z );
#if LIGHTMAP
	// Envmap term inputs: world pos/normal via bone 0 (identity for the
	// static world, the live transform for brush entities — doors/trains
	// must reflect correctly while moving, dx9 cModel[0] semantics).
	{
		float4 vPos4LM = float4( i.vPos, 1.0 );
		float3 vWorldPosLM = float3( dot( vPos4LM, g_BoneRows[0] ),
			dot( vPos4LM, g_BoneRows[1] ), dot( vPos4LM, g_BoneRows[2] ) );
		float3 vRotNLM = float3( dot( i.vNormal, g_BoneRows[0].xyz ),
			dot( i.vNormal, g_BoneRows[1].xyz ), dot( i.vNormal, g_BoneRows[2].xyz ) );
		float flNLenSqLM = dot( vRotNLM, vRotNLM );
		vRotNLM = ( flNLenSqLM > 1e-8 && !isnan( flNLenSqLM ) )
			? vRotNLM * rsqrt( flNLenSqLM ) : float3( 0.0, 0.0, 1.0 );
		o.vEnvEye = float4( g_EyePosPM.xyz - vWorldPosLM, i.vTexCoord.x );
		o.vEnvNormal = float4( vRotNLM, i.vTexCoord.y );
		// Tangent rows rotate like the normal (rigid transform, no normalize
		// — dx9 lightmappedgeneric_vs20 TANGENTSPACE passes them raw too)
		o.vEnvTanS = float3( dot( i.vTangentS, g_BoneRows[0].xyz ),
			dot( i.vTangentS, g_BoneRows[1].xyz ), dot( i.vTangentS, g_BoneRows[2].xyz ) );
		o.vEnvTanT = float3( dot( i.vTangentT, g_BoneRows[0].xyz ),
			dot( i.vTangentT, g_BoneRows[1].xyz ), dot( i.vTangentT, g_BoneRows[2].xyz ) );
	}
#endif
#if CABLE
	// cable_vs20.fxc passes the rope's directional-light vertex color through
	// raw — no gamma decode (its tangent-space setup is dead code; the model
	// transform folds into g_ModelViewProj exactly like dx9's cModel[0]+cViewProj).
	o.vBaseCoord = i.vBaseCoord;
#else
	// dx9 parity: vertexlit_and_unlit_generic_vs20 runs GammaToLinear (pow 2.2)
	// on vertex color so the sRGB write at the OM stage re-encodes to identity.
	// Alpha stays raw. Gated on the snapshot's sRGB-write state.
	[branch]
	if ( g_AlphaTest.w > 0.5 )
	{
		o.vColor.rgb = pow( i.vColor.rgb, 2.2 );
	}
#endif
#endif
	o.vTexCoord = ApplyBaseTexCoordTransform( i.vTexCoord );
	// ($separatedetailuvs unported — detail always derives from texcoord0)
	o.vDetailCoord = ApplyTexCoordTransformAt( i.vTexCoord, DETAIL_XFORM_REG );
#if LIGHTMAP
	o.vLightmapCoord = i.vLightmapCoord;
#endif
	return o;
#endif	// !SPRITECARD
}

#if PHONG || EYEREFRACT
// PixelShaderGetLightColor/Vector unpacking (light 3 lives in the w channels)
float3 PSLightColor( int n )
{
	return ( n == 3 ) ? float3( g_PSC[20].w, g_PSC[21].w, g_PSC[22].w ) : g_PSC[20 + n * 2].rgb;
}

float3 PSLightPos( int n )
{
	return ( n == 3 ) ? float3( g_PSC[23].w, g_PSC[24].w, g_PSC[25].w ) : g_PSC[21 + n * 2].xyz;
}
#endif

#if PHONG
// ---- dx9 skin_ps20b.fxc transliteration (no flashlight/wrinkle/detail/
// selfillum yet; envmap lands with cube-SRV support) ----

// PixelShaderAmbientLight (common_vertexlitgeneric_dx9.h:41)
float3 PSAmbient( float3 vN )
{
	float3 vSq = vN * vN;
	float3 vIsNeg = ( vN >= 0.0 ) ? float3( 0, 0, 0 ) : vSq;
	float3 vIsPos = ( vN >= 0.0 ) ? vSq : float3( 0, 0, 0 );
	return vIsPos.x * g_PSC[4].rgb + vIsNeg.x * g_PSC[5].rgb +
		   vIsPos.y * g_PSC[6].rgb + vIsNeg.y * g_PSC[7].rgb +
		   vIsPos.z * g_PSC[8].rgb + vIsNeg.z * g_PSC[9].rgb;
}

// DiffuseTerm (common_vertexlitgeneric_dx9.h:86): $halflambert + lightwarp
float3 PSDiffuseTerm( float3 vN, float3 vL )
{
	float flNDotL = dot( vN, vL );
	float flResult;
	if ( g_PhongFlags.x > 0.5 )			// $halflambert: scale-bias to 0..1
	{
		flResult = saturate( flNDotL * 0.5 + 0.5 );
		if ( g_PhongFlags.y < 0.5 )
			flResult *= flResult;		// square only without a lightwarp
	}
	else
	{
		flResult = saturate( flNDotL );
	}
	float3 vOut = float3( flResult, flResult, flResult );
	if ( g_PhongFlags.y > 0.5 )
	{
		// dx9 tex1D semantics: the second coordinate is 0 (row 0 of the LUT).
		// SampleLevel: this runs inside the per-light branch and gradient
		// samples there are an X4014 error; warp LUTs are mip-less anyway.
		vOut = 2.0 * g_TexLightwarp.SampleLevel( g_SampLightwarp, float2( flResult, 0.0 ), 0 ).rgb;
	}
	return vOut;
}

float4 PhongShade( VsOutput i, float4 vBase )
{
	// $detail (skin: sampler s13, blend factor c0.w, no tint) combines into
	// the base right after sampling — every later term sees the combined
	// base (skin_ps20b.fxc:174-177). Sampled outside the branch (X4014).
	float4 vDetail = g_TexDetail.Sample( g_SampDetail, i.vDetailCoord );
	int nDetailMode = (int)g_MiscControl.z;
	[branch]
	if ( nDetailMode >= 0 )
	{
		vBase = DetailCombine( vBase, vDetail, nDetailMode, g_PSC[0].w );
	}

	// Normal: tangent-space map (flat-normal fallback when no $bumpmap, the
	// dx9 TEXTURE_NORMALMAP_FLAT bind) lerped against (0,0,1) by the
	// basemap-alpha phong mask (g_ShaderControls.x, skin_ps20b.fxc:195).
	float4 vNormalTexel = ( g_PhongFlags.z > 0.5 )
		? g_TexNormal.Sample( g_SampNormal, i.vTexCoord )
		: float4( 0.5, 0.5, 1.0, 1.0 );
	float flBaseAlphaPhongMask = g_PSC[27].x;
	float3 vTSN = lerp( 2.0 * vNormalTexel.xyz - 1.0, float3( 0, 0, 1 ), flBaseAlphaPhongMask );
	float flSpecMask = lerp( vNormalTexel.a, vBase.a, flBaseAlphaPhongMask );

	float3x3 mTBN = float3x3( i.vTBN0, i.vTBN1, i.vTBN2 );
	float3 vN = normalize( mul( mTBN, vTSN ) );
	float3 vWorldPos = i.vWorldPosAtten3.xyz;
	float4 vLightAtten = float4( i.vLightAtten, i.vWorldPosAtten3.w );
	float3 vEyeDir = normalize( g_PSC[11].xyz - vWorldPos );	// PSREG_EYEPOS

	// Fresnel ranges arrive pre-encoded as ((mid-min)*2, mid, (max-mid)*2)
	// (common_vertexlitgeneric_dx9.h:229); Fresnel4 for rim.
	float flF = saturate( 1.0 - dot( vN, vEyeDir ) );
	float flF2 = flF * flF;
	float flFresnelRanges = g_PSC[19].y + ( ( flF2 - 0.5 >= 0.0 ) ? g_PSC[19].z : g_PSC[19].x ) * ( flF2 - 0.5 );
	float flRimFresnel = flF2 * flF2;

	// Diffuse: ambient cube + up to 4 per-pixel lights with VS attenuation
	float3 vDiffuse = PSAmbient( vN );
	float3 vSpec = float3( 0, 0, 0 );
	float3 vRim = float3( 0, 0, 0 );

	// Spec exponent map (white fallback = dx9 TEXTURE_WHITE bind); constant
	// exponent >= 0 wins, else 1+149*map.r (skin_ps20b.fxc:261).
	float4 vSpecExpMap = ( g_PhongFlags.w > 0.5 )
		? g_TexSpecExp.Sample( g_SampSpecExp, i.vTexCoord )
		: float4( 1, 1, 1, 1 );
	// max() insurance: pow(0, 0) is NaN in HLSL — a zero exponent can only
	// reach here through a pass that didn't write c11 (routing already gates
	// on c11 validity, this is the belt to that suspender).
	float flSpecExp = max( ( g_PSC[11].w >= 0.0 ) ? g_PSC[11].w : ( 1.0 + 149.0 * vSpecExpMap.r ), 1e-4 );
	float3 vSpecTint = lerp( float3( 1, 1, 1 ), vBase.rgb, vSpecExpMap.g );
	vSpecTint = ( g_PSC[26].x >= 0.0 ) ? g_PSC[26].rgb : vSpecTint;
	float flRimExp = g_PSC[26].w;		// PSREG_SPEC_RIM_PARAMS.w
	float flRimMask = lerp( 1.0, vSpecExpMap.a, g_PSC[13].x );	// rim mask control
	bool bDoRim = flRimExp > 0.0;

	// Reflect view through normal — shared by the envmap and per-light specular
	float3 vReflect = 2.0 * vN * dot( vN, vEyeDir ) - vEyeDir;

	[unroll]
	for ( int l = 0; l < 4; ++l )
	{
		float flAtten = vLightAtten[l];
		[branch]
		if ( flAtten > 0.0 )
		{
			float3 vL = normalize( PSLightPos( l ) - vWorldPos );
			float3 vColorAtten = PSLightColor( l ) * flAtten;
			vDiffuse += vColorAtten * PSDiffuseTerm( vN, vL );

			// SpecularAndRimTerms (common_vertexlitgeneric_dx9.h:167)
			float flLdotR = saturate( dot( vReflect, vL ) );
			float flNdotL = saturate( dot( vN, vL ) );
			vSpec += pow( flLdotR, flSpecExp ) * flNdotL * vColorAtten;
			if ( bDoRim )
				vRim += pow( flLdotR, flRimExp ) * flNdotL * vColorAtten;
		}
	}

	// Envmap (skin_ps20b.fxc:224-238, non-SELFILLUMFRESNEL variant) — uses the
	// spec mask BEFORE the fresnel fold below. The gold of australiums lives
	// here: $envmaptint (c2.rgb) × the cubemap reflection. ENV_MAP_SCALE is
	// cLightScale.z (c30) — unwritten in LDR, so default 1.
	// Cube sample outside the branch (X4014); white-cube fallback when off.
	float3 vEnvSample = g_TexEnvmap.Sample( g_SampEnvmap, vReflect ).rgb;
	float3 vEnvMapColor = float3( 0, 0, 0 );
	[branch]
	if ( g_TintControl.z > 0.5 )
	{
		float flEnvScale = ( g_PSC[30].z > 0.0 ) ? g_PSC[30].z : 1.0;
		// With SELFILLUMFRESNEL the base alpha is the emissive mask, so the
		// envmap mask select uses the invert-phong-mask constant instead
		// (skin_ps20b.fxc:227-231).
		float flEnvMapMask = ( g_MiscControl.x > 0.5 )
			? lerp( vBase.a, g_PSC[27].w, g_PSC[2].w )
			: lerp( vBase.a, flSpecMask, g_PSC[2].w );
		vEnvMapColor = ( flEnvScale *
			lerp( 1.0, flFresnelRanges, g_PSC[10].x ) *
			lerp( flEnvMapMask, 1.0 - flEnvMapMask, g_PSC[27].w ) ) *
			vEnvSample *
			g_PSC[2].rgb;
	}

	// No phong warp in phase 1 → fresnel folds into the spec mask
	flSpecMask *= flFresnelRanges;
	vSpec *= flSpecMask * g_PSC[19].w;	// spec boost

	// Albedo modulation incl. the paint-mask path (skin_ps20b.fxc:307-316)
	float3 vAlbedo = vBase.rgb;
	float4 vMod = g_PSC[1];				// PSREG_DIFFUSE_MODULATION
	if ( g_TintControl.x > 0.5 )
	{
		float3 vTinted = vAlbedo * vMod.rgb;
		vTinted = lerp( vTinted, vMod.rgb, g_PSC[27].z );
		vAlbedo = lerp( vAlbedo, vTinted, vBase.a );
	}
	else
	{
		vAlbedo = vAlbedo * vMod.rgb;
	}
	float3 vDiffuseComp = vAlbedo * vDiffuse;

	// dx9 $selfillum (skin_ps20b.fxc:320-334, tint in c0; the s14 mask
	// texture variant is unported — mask control 0 selects base alpha)
	[branch]
	if ( g_TintControl.w > 0.5 && g_TintControl.x < 0.5 )
	{
		if ( g_MiscControl.x > 0.5 )
		{
			// SELFILLUMFRESNEL (skin_ps20b.fxc:322-326): fresnel from the
			// VERTEX normal (the TBN's N column, not the per-pixel normal);
			// c3 = (scale, bias, exp, brightness) packed by the helper from
			// $selfillumfresnelminmaxexp (skin_dx9_helper.cpp:752-764).
			float3 vVertexNormal = normalize( float3( i.vTBN0.z, i.vTBN1.z, i.vTBN2.z ) );
			float flSIFresnel = pow( saturate( dot( vVertexNormal, vEyeDir ) ), g_PSC[3].z )
				* g_PSC[3].x + g_PSC[3].y;
			vDiffuseComp = lerp( vDiffuseComp, g_PSC[0].rgb * vAlbedo * g_PSC[3].w,
				vBase.a * saturate( flSIFresnel ) );
		}
		else
		{
			vDiffuseComp = lerp( vDiffuseComp, g_PSC[0].rgb * vAlbedo, vBase.a );
		}
		vDiffuseComp = max( float3( 0, 0, 0 ), vDiffuseComp );
	}

	// $detail post-lighting combine (skin_ps20b.fxc:336-339)
	[branch]
	if ( nDetailMode >= 0 )
		vDiffuseComp = DetailCombinePostLighting( vDiffuseComp, vDetail, nDetailMode, g_PSC[0].w );

	if ( bDoRim )
	{
		float flRimMul = flRimMask * flRimFresnel;
		vRim *= flRimMul;
		vSpec = max( vSpec, vRim );
		// View-ray ambient cube term folded into specular (skin_ps20b.fxc:352)
		vSpec += ( PSAmbient( vEyeDir ) * g_PSC[14].w ) * saturate( flRimMul * vN.z );
	}

	float3 vResult = vSpec * vSpecTint + vEnvMapColor + vDiffuseComp;
	float flAlpha = vMod.a;
	if ( g_TintControl.x < 0.5 && g_TintControl.w < 0.5 )	// !blendtint && !selfillum
		flAlpha = lerp( vBase.a * flAlpha, flAlpha, flBaseAlphaPhongMask );
	return float4( vResult, flAlpha );
}
#endif

#if INTROEFFECT
// IntroScreenSpaceEffect_ps2x.fxc helpers (MODE 5's HSV boost)
float3 IntroRGBtoHSV( float3 rgb )
{
	float3 hsv;
	float fmin = min( min( rgb.r, rgb.g ), rgb.b );
	float fmax = max( max( rgb.r, rgb.g ), rgb.b );
	hsv.b = fmax;
	float delta = fmax - fmin;
	if ( delta != 0.0 )
	{
		hsv.g = delta / fmax;
		if ( rgb.r == fmax )
			hsv.r = ( rgb.g - rgb.b ) / delta;
		else if ( rgb.g == fmax )
			hsv.r = 2.0 + ( rgb.b - rgb.r ) / delta;
		else
			hsv.r = 4.0 + ( rgb.r - rgb.g ) / delta;
		hsv.r *= 60.0;
		if ( hsv.r < 0.0 )
			hsv.r += 360.0;
	}
	else
	{
		hsv.g = 0.0;
		hsv.r = -1.0;
	}
	return hsv;
}

float3 IntroHSVtoRGB( float3 hsv )
{
	float3 rgb;
	float h = hsv.r, s = hsv.g, v = hsv.b;
	if ( s == 0.0 )
	{
		rgb = float3( v, v, v );
	}
	else
	{
		h /= 60.0;
		float i = floor( h );
		float f = h - i;
		float p = v * ( 1.0 - s );
		float q = v * ( 1.0 - s * f );
		float t = v * ( 1.0 - s * ( 1.0 - f ) );
		if ( h < 1.0 )			rgb = float3( v, t, p );
		else if ( h < 2.0 )		rgb = float3( q, v, p );
		else if ( h < 3.0 )		rgb = float3( p, v, t );
		else if ( h < 4.0 )		rgb = float3( p, q, v );
		else if ( h < 5.0 )		rgb = float3( t, p, v );
		else					rgb = float3( v, p, q );
	}
	return rgb;
}
#endif

#if COLORPROJ
// color_projection_ps2x.fxc (kaioa.com/node/91 daltonization): project the
// color onto the confusion line of the cone deficiency described by
// (cpu, cpv, am, ayi) = ps c1, then shift so white stays white. Ported
// verbatim including the div-by-zero hazards dx9 ps_2_0 carried (the cmp
// selects discard the NaN side exactly like the vector ternaries here).
float3 CPRgbFromXyz( float3 v )
{
	return float3(
		dot( v, float3(  3.063218, -1.393325, -0.475802 ) ),
		dot( v, float3( -0.969243,  1.875966,  0.041555 ) ),
		dot( v, float3(  0.067871, -0.228834,  1.069251 ) ) );
}

float3 CPBlindMK( float3 vColor, float4 vParms )
{
	const float3 w_xyz = float3( 0.312713, 0.329016, 0.358271 );
	float3 c_xyz = float3(
		dot( vColor, float3( 0.430574, 0.341550, 0.178325 ) ),
		dot( vColor, float3( 0.222015, 0.706655, 0.071330 ) ),
		dot( vColor, float3( 0.020183, 0.129553, 0.939180 ) ) );
	float flSumXyz = c_xyz.x + c_xyz.y + c_xyz.z;
	float2 c_uv = ( flSumXyz != 0.0 ) ? c_xyz.xy / flSumXyz : float2( 0.0, 0.0 );
	float2 n_xz = w_xyz.xz * c_xyz.y / w_xyz.y;
	float clm = ( c_uv.x < vParms.x )
		? ( vParms.y - c_uv.y ) / ( vParms.x - c_uv.x )
		: ( c_uv.y - vParms.y ) / ( c_uv.x - vParms.x );
	float clyi = c_uv.y - c_uv.x * clm;
	float2 d_uv;
	d_uv.x = ( vParms.w - clyi ) / ( clm - vParms.z );
	d_uv.y = ( clm * d_uv.x ) + clyi;
	float3 s_xyz;
	s_xyz.x = d_uv.x * c_xyz.y / d_uv.y;
	s_xyz.y = c_xyz.y;
	s_xyz.z = ( 1.0 - ( d_uv.x + d_uv.y ) ) * c_xyz.y / d_uv.y;
	float3 s_rgb = CPRgbFromXyz( s_xyz );
	float3 d_xyz = float3( n_xz.x - s_xyz.x, 0.0, n_xz.y - s_xyz.z );
	float3 d_rgb = CPRgbFromXyz( d_xyz );
	float3 vStep = ( s_rgb < float3( 0, 0, 0 ) ) ? float3( 0, 0, 0 ) : float3( 1, 1, 1 );
	float3 vAdj = ( d_rgb != float3( 0, 0, 0 ) ) ? ( vStep - s_rgb / d_rgb ) : float3( 0, 0, 0 );
	vAdj = ( vAdj < float3( 0, 0, 0 ) ) ? float3( 0, 0, 0 ) : vAdj;
	vAdj = ( vAdj > float3( 1, 1, 1 ) ) ? float3( 0, 0, 0 ) : vAdj;
	float flAdjust = max( max( vAdj.r, vAdj.g ), vAdj.b );
	return s_rgb + flAdjust * d_rgb;
}
#endif

// ShaderAlphaFunc_t: 0=NEVER 1=LESS 2=EQUAL 3=LEQUAL 4=GREATER 5=NOTEQUAL 6=GEQUAL 7=ALWAYS
float4 MainPs( VsOutput i ) : SV_Target0
{
#if WINDOWIMPOSTER || WATERCHEAP || CLOAKPASS || SHEENPASS || REFRACT || SSDOWNSAMPLE || SSBLUR || COLORPROJ || SHADOWMODEL || FLASHLIGHT
	// t0 is the cube (windowimposter/watercheap), the FB copy sampled at the
	// warped uv below (cloak), enabled-but-unused (sheen), absent (refract
	// rides s2/s3, color_projection rides s4, shadowmodel never samples),
	// tap-sampled below (downsample/blur), or the spot texture sampled at
	// the projected uv below (flashlight)
	float4 vTex = float4( 1.0, 1.0, 1.0, 1.0 );
#else
	float4 vTex = g_Texture0.Sample( g_Sampler0, i.vTexCoord );
#endif
	// Base alpha BEFORE the detail combine (some combine modes rewrite vTex.a)
	// — the dx9 $basealphaenvmapmask reads the raw baseColor.a.
	float flBaseAlphaPreDetail = vTex.a;
#if VLGENERIC || LIGHTMAP
	// $detail pre-lighting combine (dx9 applies it right after the base
	// sample, so every later term sees the combined base). Tint/factor are
	// family registers: lightmapped c8.rgb/c8.w, vertexlit c10.rgb/c4.w.
	// The sample stays OUTSIDE the branch: gradient ops inside flow control
	// are an error (X4014) on the runtime d3dcompiler; the white fallback
	// makes the unconditional sample harmless.
	float4 vDetail = g_TexDetail.Sample( g_SampDetail, i.vDetailCoord );
	int nDetailMode = (int)g_MiscControl.z;
	[branch]
	if ( nDetailMode >= 0 )
	{
#if LIGHTMAP
		vDetail.rgb *= g_PSC[8].rgb;
		vTex = DetailCombine( vTex, vDetail, nDetailMode, g_PSC[8].w );
#else
		vDetail.rgb *= g_PSC[10].rgb;
		vTex = DetailCombine( vTex, vDetail, nDetailMode, g_PSC[4].w );
#endif
	}
#endif
	float4 c = vTex;
#if SPRITECARD
	// spritecard_ps2x.fxc with the static combos as runtime flags
	// (g_SpriteControl.x bits; ps c0 = {add2nd weight, overbright, addself}).
	// Depth blend is masked off until RT textures exist. All samples hoisted
	// (X4014); vTex = frame 0 sample at texCoord0.
	int nSCFlags = (int)round( g_SpriteControl.x );
	int nSeqBlendMode = (int)round( g_SpriteControl.z );
	float4 vFrame0 = vTex;
	float4 vFrame1 = g_Texture0.Sample( g_Sampler0, i.vSCTex1 );
	float4 vTex2Sample = g_Texture0.Sample( g_Sampler0, i.vSCTex2 );
	float4 vSeq2Frame0 = g_Texture0.Sample( g_Sampler0, i.vSCSeq2Tex0 );
	float4 vSeq2Frame1 = g_Texture0.Sample( g_Sampler0, i.vSCSeq2Tex1 );

	float4 vBlended = ( ( nSCFlags & 4 ) != 0 )	// ANIMBLEND
		? lerp( vFrame0, vFrame1, i.vSCBlend0.x ) : vFrame0;

	if ( ( nSCFlags & 16 ) != 0 )	// MAXLUMFRAMEBLEND1
	{
		float flLum0 = dot( float3( 0.3, 0.59, 0.11 ), vFrame0.rgb * ( 1.0 - i.vSCBlend0.x ) );
		float flLum1 = dot( float3( 0.3, 0.59, 0.11 ), vFrame1.rgb * i.vSCBlend0.x );
		vBlended = ( flLum0 > flLum1 ) ? vFrame0 : vFrame1;
	}
	else if ( ( nSCFlags & 128 ) != 0 )	// EXTRACTGREENALPHA
	{
		vBlended = dot( vFrame0, i.vSCBlend0 ) + dot( vFrame1, i.vSCBlend1 );
	}

	[branch]
	if ( ( nSCFlags & 8 ) != 0 )	// DUALSEQUENCE
	{
		float4 vRGB2 = lerp( vSeq2Frame0, vSeq2Frame1, i.vSCBlend0.z );
		if ( ( nSCFlags & 32 ) != 0 )	// MAXLUMFRAMEBLEND2
		{
			float flTL0 = dot( float3( 0.3, 0.59, 0.11 ), vSeq2Frame0.rgb * ( 1.0 - i.vSCBlend0.x ) );
			float flTL1 = dot( float3( 0.3, 0.59, 0.11 ), vSeq2Frame1.rgb * i.vSCBlend0.x );
			vRGB2 = ( flTL0 > flTL1 ) ? vSeq2Frame0 : vSeq2Frame1;
		}
		if ( nSeqBlendMode == 0 )		// average
			vBlended = 0.5 * ( vBlended + vRGB2 );
		else if ( nSeqBlendMode == 1 )	// first as alpha mask on second
			vBlended.rgb = vRGB2.rgb;
		else							// first over second
			vBlended.rgb = lerp( vBlended.rgb, vRGB2.rgb, vRGB2.a );
	}

	// $ramptexture: red/green index the ramp (SampleLevel: LUT, branch-safe)
	float3 vRamp = g_TexColorRamp.SampleLevel( g_SampColorRamp, float2( vBlended.r, vBlended.g ), 0 ).rgb;
	if ( ( nSCFlags & 64 ) != 0 )	// COLORRAMP
	{
		vBlended.rgb = vRamp;
	}

	vBlended.rgb *= g_PSC[0].y;	// overbright

	[branch]
	if ( ( nSCFlags & 1 ) != 0 )	// ADDBASETEXTURE2 (premultiplied ONE:INVSRCALPHA)
	{
		vBlended.a *= i.vColor.a;
		if ( ( nSCFlags & 64 ) == 0 )
			vBlended.rgb *= vBlended.a;
		if ( ( nSCFlags & 128 ) != 0 )
			vBlended.rgb += g_PSC[0].x * i.vColor.a * vBlended.rgb;
		else
			vBlended.rgb += g_PSC[0].y * g_PSC[0].x * i.vColor.a * vTex2Sample.rgb;
		vBlended.rgb *= i.vColor.rgb;
	}
	else if ( ( nSCFlags & 2 ) != 0 )	// ADDSELF
	{
		vBlended.a *= i.vColor.a;
		vBlended.rgb *= vBlended.a;
		vBlended.rgb += g_PSC[0].y * g_PSC[0].z * i.vColor.a * vBlended.rgb;
		vBlended.rgb *= i.vColor.rgb;
	}
	else
	{
		vBlended *= i.vColor;
	}
	c = vBlended;
#elif PHONG
	c = PhongShade( i, vTex );
#elif CABLE
	// cable_ps2x.fxc: s0 = normal map (uv0, in vTex), s1 = base (uv1, sRGB).
	// Tangent-space light dir is the constant (0,0,1), so the lighting term is
	// just the expanded normal's z through half-lambert squared, times base
	// color times the rope-baked per-vertex directional light color. No
	// modulation/tint/detail — the dx9 PS reads none of them.
	float4 vCableBase = g_TexCableBase.Sample( g_SampCableBase, i.vBaseCoord );
	float flCableDot = vTex.z * 2.0 - 1.0;
	flCableDot = flCableDot * 0.5 + 0.5;
	flCableDot = flCableDot * flCableDot;
	c.rgb = flCableDot * ( vCableBase.rgb * i.vColor.rgb );
	c.a = vCableBase.a * i.vColor.a;
#elif EYES
	// eyes_ps2x.fxc: iris composites over the sclera by iris alpha, the vertex
	// lighting attenuates, and the glint adds (damped by ambient luminance in
	// ps c0.y — the dx9 dynamic block computes it; dilation c0.x is dead code
	// in the dx9 shader too).
	float4 vIris = g_TexIris.Sample( g_SampIris, i.vIrisCoord );
	float3 vGlint = g_TexGlint.Sample( g_SampGlint, i.vGlintCoord ).rgb;
	c.rgb = lerp( vTex.rgb, vIris.rgb, vIris.a );
	c.rgb *= i.vColor.rgb;
	c.rgb += vGlint * g_PSC[0].y;
	c.a = vTex.a;
#elif WINDOWIMPOSTER
	// windowimposter_ps2x.fxc: cube sample along the eye-to-vertex ray times
	// the modulation color; alpha 1 pre-modulation. ENV_MAP_SCALE = 1 in LDR.
	float3 vEyeRay = i.vWorldPosWI - g_PSC[11].xyz;	// PSREG_EYEPOS
	c.rgb = g_TexWindowEnv.Sample( g_Sampler0, vEyeRay ).rgb;
	c.a = 1.0;
	c *= i.vColor;
#elif EYEREFRACT
	// eye_refract_ps2x.fxc ps20b transliteration. Constants per the dx9
	// dynamic block: c0 = {dilation, glossiness, avgAmbient, corneaBumpStr},
	// c1 = eye origin, c2/c3 = iris projection U/V, c4 = camera pos,
	// c5 = AO color, c6.y = eyeball radius, c6.w = parallax strength.
	// Lights live at c20-25 (CommitPixelShaderLighting), atten/cos pairs come
	// interpolated — terms for absent lights carry zero attenuation.
	float3 vERN = i.vERNormal;
	float3 vERT = i.vERTangent;
	float3 vERB = i.vERBinormal;
	float3 vWorldPos = i.vERWorldPos;
	float3 vViewVec = normalize( vWorldPos - g_PSC[4].xyz );
	// Tangent-space view vector (dx9 computes this in the VS — same math)
	float3 vTanView = float3( dot( vViewVec, vERT ), dot( vViewVec, vERB ), dot( vViewVec, vERN ) );
	float4 vVertexLight = float4( i.vColor.rgb, 0.0 );

	// TF NPR lightwarp: local lights accumulate here instead of the VS
	[branch]
	if ( g_PhongFlags.y > 0.5 )
	{
		float4 vWarp;
		vWarp.x = i.vERFalloffCos01.z; vWarp.y = i.vERFalloffCos01.w;
		vWarp.z = i.vERFalloffCos23.z; vWarp.w = i.vERFalloffCos23.w;
		// SampleLevel: gradient ops in flow control are X4014 on the runtime
		// compiler; the warp LUT is mip-less (dx9 tex1D row 0).
		float3 cWarp0 = 2.0 * g_TexLightwarp.SampleLevel( g_SampLightwarp, float2( vWarp.x, 0.0 ), 0 ).rgb;
		float3 cWarp1 = 2.0 * g_TexLightwarp.SampleLevel( g_SampLightwarp, float2( vWarp.y, 0.0 ), 0 ).rgb;
		float3 cWarp2 = 2.0 * g_TexLightwarp.SampleLevel( g_SampLightwarp, float2( vWarp.z, 0.0 ), 0 ).rgb;
		float3 cWarp3 = 2.0 * g_TexLightwarp.SampleLevel( g_SampLightwarp, float2( vWarp.w, 0.0 ), 0 ).rgb;
		vVertexLight.rgb += i.vERFalloffCos01.x * PSLightColor( 0 ) * cWarp0;
		vVertexLight.rgb += i.vERFalloffCos01.y * PSLightColor( 1 ) * cWarp1;
		vVertexLight.rgb += i.vERFalloffCos23.x * PSLightColor( 2 ) * cWarp2;
		vVertexLight.rgb += i.vERFalloffCos23.y * PSLightColor( 3 ) * cWarp3;
	}

	// Raycast the eyeball sphere ($raytracesphere) to stabilize the iris on
	// morphed/non-spherical eye geometry
	[branch]
	if ( g_EyeControl.x > 0.5 )
	{
		float3 vDst = g_PSC[4].xyz - g_PSC[1].xyz;
		float flB = dot( vDst, vViewVec );
		float flC = dot( vDst, vDst ) - g_PSC[6].y * g_PSC[6].y;
		float flD = flB * flB - flC;
		float flDist = ( flD > 0.0 ) ? ( -flB - sqrt( flD ) ) : 0.0;
		if ( flDist > 0.0 )
		{
			vWorldPos = g_PSC[4].xyz + vViewVec * flDist;
		}
		else
		{
			if ( g_EyeControl.y > 0.5 )
				clip( -1.0 );	// $spheretexkillcombo silhouette
			vWorldPos = g_PSC[1].xyz + vERN * g_PSC[6].y;
		}
	}

	// Cornea/sphere UVs from the iris planar projection
	float2 vCorneaUv;
	vCorneaUv.x = dot( g_PSC[2], float4( vWorldPos, 1.0 ) );
	vCorneaUv.y = dot( g_PSC[3], float4( vWorldPos, 1.0 ) );
	float2 vSphereUv = vCorneaUv * 0.5 + 0.25;

	// Parallax iris (cornea blue channel = height)
	float flIrisOffset = g_Texture0.Sample( g_Sampler0, vCorneaUv ).b;
	float2 vParallax = ( vTanView.xy * flIrisOffset * g_PSC[6].w ) / ( 1.0 - vTanView.z );
	vParallax.x = -vParallax.x;
	float2 vIrisUv = vSphereUv - vParallax;
	float2 vCorneaNoiseUv = vSphereUv + vParallax * 0.5;
	float flCorneaNoise = g_TexIris.Sample( g_SampIris, vCorneaNoiseUv ).a;

	// Cornea tangent normal (rg biased at 50% strength) + noise
	float4 vCorneaSample = g_Texture0.Sample( g_Sampler0, vCorneaUv );
	float3 vCorneaTanN = float3( ( vCorneaSample.rg - 0.5 ) * g_PSC[0].w, 1.0 );
	vCorneaTanN.xy += flCorneaNoise * 0.1;
	vCorneaTanN = normalize( vCorneaTanN );
	float3 vCorneaWorldN = normalize(
		vCorneaTanN.x * vERT + vCorneaTanN.y * vERB + vCorneaTanN.z * vERN );

	// Pupil dilation
	vIrisUv -= 0.5;
	float flCenterToBorder = saturate( length( vIrisUv ) / 0.2 );
	vIrisUv *= lerp( 1.0, flCenterToBorder, saturate( g_PSC[0].x ) * 2.5 - 1.25 );
	vIrisUv += 0.5;

	float4 cIris = g_TexIris.Sample( g_SampIris, vIrisUv );

	// Iris caustic highlights (mask in cornea alpha)
	float flIrisMask = vCorneaSample.a;
	float3 vIrisTanN = float3( vCorneaTanN.xy * -2.5, vCorneaTanN.z );
	float3 cIrisLighting = float3( 0.0, 0.0, 0.0 );
	float4 vFalloffs = float4( i.vERFalloffCos01.xy, i.vERFalloffCos23.xy );
	[unroll]
	for ( int nL = 0; nL < 4; ++nL )
	{
		float flFalloff = vFalloffs[nL];
		[branch]
		if ( flFalloff > 0.0 )
		{
			float3 vWorldLight = normalize( PSLightPos( nL ) - vWorldPos );
			float3 vTanLight = float3( dot( vWorldLight, vERT ), dot( vWorldLight, vERB ), dot( vWorldLight, vERN ) );
			float3 vTmp = -vTanLight;
			vTmp.xy *= -0.5;
			vTmp.z = max( vTmp.z, 0.5 );
			vTmp = normalize( vTmp );
			float flIrisFacing = pow( abs( dot( vIrisTanN, vTmp ) ), 6.0 ) * 0.5;
			float flCone = pow( 1.0 - saturate( ( -vTanLight.z - 0.25 ) / 0.75 ), 4.0 );
			cIrisLighting += flIrisFacing * flIrisMask * flCone * flFalloff * PSLightColor( nL );
		}
	}
	// View-dependent ambient iris term (c0.z = average ambient luminance)
	cIrisLighting += saturate( dot( vIrisTanN, -vTanView ) ) * g_PSC[0].z * flIrisMask * 0.5;

	// Ambient occlusion (texcoord0, colored by c5)
	float3 cAO = g_TexEyeAO.Sample( g_SampEyeAO, i.vTexCoord ).rgb;
	vVertexLight.rgb *= lerp( g_PSC[5].rgb, float3( 1.0, 1.0, 1.0 ), cAO );

	// Cube reflection scaled by $glossiness (c0.y)
	float3 vReflect = reflect( vViewVec, vCorneaWorldN );
	float3 cReflection = g_PSC[0].y * g_TexEyeEnv.Sample( g_SampEyeEnv, vReflect ).rgb;

	// Local-light glints (exponent 128 vs the cornea reflection vector)
	float3 cGlints = float3( 0.0, 0.0, 0.0 );
	[unroll]
	for ( int nG = 0; nG < 4; ++nG )
	{
		float flFalloff = vFalloffs[nG];
		[branch]
		if ( flFalloff > 0.0 )
		{
			float3 vWorldLight = normalize( PSLightPos( nG ) - vWorldPos );
			cGlints += pow( saturate( dot( vReflect, vWorldLight ) ), 128.0 ) * flFalloff * PSLightColor( nG );
		}
	}

	c.rgb = cIris.rgb + flCorneaNoise * 0.1;
	c.rgb *= vVertexLight.rgb + cIrisLighting;
	c.rgb += cReflection * vVertexLight.rgb;
	c.rgb += cGlints;
	c.a = 1.0;
#elif PYRO
	// pyro_vision_ps2x.fxc effects 0/1 (vTex = s0 base sample). Constants per
	// the dx9 blocks: c0 = effect params, c1 = modulation x lmscale
	// (+ diffuse_white in w for effect 1), c2 = canvas step range (e0) /
	// {diffuse_base, selfillumtint} (e1), c3/c4 canvas colors, c5/c6 stripes.
	// All samples hoisted out of the flag branches (X4014); unused slots are
	// white fallbacks.
	int nPyroFlags = (int)round( g_EyeControl.w );
	float4 vPyroBase = vTex;

	// $basetexture2 blend (WorldVertexTransition-style, vColor.a factor)
	float4 vPyroBase2 = g_TexPyroBase2.Sample( g_SampPyroBase2, i.vTexCoord );
	float4 vBlendMod = g_TexPyroBlendMod.Sample( g_SampPyroBlendMod, i.vPyroBlendFactor.xy );
	[branch]
	if ( ( nPyroFlags & 4 ) != 0 )
	{
		float flBlend = i.vPyroBlendFactor.z;
		if ( ( nPyroFlags & 8 ) != 0 )	// FANCY_BLENDING
		{
			float flMin = saturate( vBlendMod.g - vBlendMod.r );
			float flMax = saturate( vBlendMod.g + vBlendMod.r );
			flBlend = smoothstep( flMin, flMax, flBlend );
		}
		vPyroBase = lerp( vPyroBase, vPyroBase2, flBlend );
	}

	float4 vPyroCanvas = g_TexPyroCanvas.Sample( g_SampPyroCanvas, i.vPyroSeamlessCoord );
	float4 vPyroStripe = g_TexPyroStripe.Sample( g_SampPyroStripe, i.vPyroStripeCoord );
#if PYROWORLD
	float3 vPyroLM = g_Lightmap.Sample( g_LightmapSampler, i.vLightmapCoord ).rgb * g_PSC[1].rgb;
#endif

	[branch]
	if ( (int)round( g_EyeControl.z ) == 1 )
	{
		// EFFECT 1: gray posterize through the colorbar LUT
		float4 vE1Base = vPyroBase;
		vE1Base.rgb = lerp( vE1Base.rgb, float3( 1.0, 1.0, 1.0 ), g_PSC[2].x );	// diffuse_base
		float4 vResult = vE1Base * i.vColor;
#if PYROWORLD
		vResult.rgb = lerp( vResult.rgb, float3( 0.5, 0.5, 0.5 ), g_PSC[1].w );	// diffuse_white
		vResult.rgb *= vPyroLM;
#endif
		if ( g_TintControl.w > 0.5 )	// SELFILLUM
		{
			vResult.rgb = lerp( vResult.rgb, g_PSC[2].yzw * vE1Base.rgb, vE1Base.a );
		}
		float flGray = dot( vResult.rgb, float3( 0.30, 0.59, 0.11 ) );
		flGray = pow( max( flGray, 1e-6 ), max( g_PSC[0].x, 1e-4 ) );	// gray_power
		flGray = smoothstep( g_PSC[0].y, g_PSC[0].z, flGray );			// gray_step
		flGray = ceil( flGray * g_PSC[0].w ) / max( g_PSC[0].w, 1.0 );	// lightmap_gradients
		if ( ( nPyroFlags & 16 ) != 0 )	// COLOR_BAR
		{
			float3 vBar = g_TexPyroCanvas.SampleLevel( g_SampPyroCanvas, float2( flGray, 0.0 ), 0 ).rgb;
			vResult.rgb = vBar * flGray;
		}
		if ( ( nPyroFlags & 32 ) != 0 )	// STRIPES (normal fade = dx9 dead code)
		{
			float3 vStripeC = vPyroStripe.rgb * g_PSC[5].rgb;
			vStripeC *= lerp( float3( 1.0, 1.0, 1.0 ), flGray.xxx, g_PSC[5].w );
			vResult.rgb = lerp( vResult.rgb, vStripeC, vPyroStripe.a );
		}
		c = vResult;
	}
	else
	{
		// EFFECT 0: rgb posterize + smoothstep ranges + canvas color ramp
		float4 vE0Base = vPyroBase;
		vE0Base.rgb *= i.vColor.rgb;
		vE0Base.rgb = ceil( vE0Base.rgb * 16.0 ) / 16.0;
		float4 vResult;
		vResult.rgb = smoothstep( g_PSC[0].xxx, g_PSC[0].yyy, vE0Base.rgb );	// base_step_range
		vResult.a = vE0Base.a;
#if PYROWORLD
		float3 vLMStep = smoothstep( g_PSC[0].zzz, g_PSC[0].www, vPyroLM );		// lightmap_step_range
		vResult.rgb *= vLMStep;
#endif
		float flCanvasGray = dot( vPyroCanvas.rgb, float3( 0.30, 0.59, 0.11 ) );
		flCanvasGray = smoothstep( g_PSC[2].x, g_PSC[2].y, flCanvasGray );		// canvas_step_range
		vResult.rgb *= lerp( g_PSC[3].rgb, g_PSC[4].rgb, flCanvasGray );		// canvas color ramp
		vResult.a *= 1.0;
		c = vResult;
	}
#elif WATER
	// water_ps2x_helper.h DrawWater (no BASETEXTURE/BLURRY_REFRACT). t0 here =
	// the refract RT (vTex was sampled at the bump uv — resample properly
	// below at the warped refract uv). Constants: c1 refract tint, c4 reflect
	// tint, c5 reflect/refract scale, c6 water fog color, c7 fog params.
	int nWFlags = (int)round( g_SpriteControl.w );
	bool bReflect = ( nWFlags & 1 ) != 0;
	bool bRefract = ( nWFlags & 2 ) != 0;
	bool bAboveWater = ( nWFlags & 4 ) != 0;

	float4 vWNormal;
	float4 vWN0 = g_TexWaterNormal.Sample( g_SampWaterNormal, i.vTexCoord );
	float4 vWN1 = g_TexWaterNormal.Sample( g_SampWaterNormal, i.vWaterExtraBump.xy );
	float4 vWN2 = g_TexWaterNormal.Sample( g_SampWaterNormal, i.vWaterExtraBump.zw );
	if ( ( nWFlags & 8 ) != 0 )	// MULTITEXTURE
	{
		vWNormal = 0.33 * ( vWN0 + vWN1 + vWN2 );
		vWNormal.xyz = 2.0 * vWNormal.xyz - 1.0;
	}
	else
	{
		vWNormal = float4( 2.0 * vWN0.xyz - 1.0, vWN0.a );
	}

	float flOOW = 1.0 / i.vWaterProjPos.w;
	float2 vUnwarpedRefract = i.vWaterReflRefr.wz * flOOW;
	float flWaterFogDepth = bAboveWater
		? g_Texture0.Sample( g_Sampler0, vUnwarpedRefract ).a : 1.0;

	float4 vScale = g_PSC[5];
	vScale *= flWaterFogDepth;	// !BASETEXTURE !BLURRY path

	float4 vNQ;
	vNQ.xy = vWNormal.xy;
	vNQ.w = vWNormal.x;
	vNQ.z = vWNormal.y;
	float4 vDepUV = vNQ * vWNormal.a * vScale + i.vWaterReflRefr * flOOW;

	float4 vReflectColor = g_TexWaterReflect.Sample( g_SampWaterReflect, vDepUV.xy ) * g_PSC[4];
	float4 vRefractColor = g_Texture0.Sample( g_Sampler0, vDepUV.wz );
	if ( bAboveWater )
		flWaterFogDepth = vRefractColor.a;
	vRefractColor *= g_PSC[1];

	float3 vWEye = normalize( i.vWaterTanEye );
	float flNdotV = saturate( dot( vWEye, vWNormal.xyz ) );
	float flWFresnel = pow( 1.0 - flNdotV, 5.0 );
	flWFresnel *= saturate( ( flWaterFogDepth - 0.05 ) * 20.0 );

	if ( bAboveWater )
	{
		vRefractColor.rgb = lerp( vRefractColor.rgb, g_PSC[6].rgb, saturate( flWaterFogDepth - 0.05 ) );
	}
	else
	{
		float flWFog = saturate( ( i.vWaterProjPos.z - g_PSC[7].x ) / max( g_PSC[7].y, 1e-4 ) );
		vRefractColor.rgb = lerp( vRefractColor.rgb, g_PSC[6].rgb, flWFog );
	}
	vReflectColor.rgb *= ( g_PSC[7].z > 0.0 ) ? g_PSC[7].z : 1.0;	// overbright

	if ( bReflect && bRefract )
		c = float4( lerp( vRefractColor.rgb, vReflectColor.rgb, flWFresnel ), 1.0 );
	else if ( bReflect )
		c = float4( vReflectColor.rgb, 1.0 );
	else if ( bRefract )
		c = float4( vRefractColor.rgb, 1.0 );
	else
		c = float4( 0.0, 0.0, 0.0, 0.0 );
#elif WATERCHEAP
	// watercheap_ps2x: cube reflection along the bumped normal + fresnel,
	// blended toward the water fog color (c0); cheap params c1, tint c2.
	int nWFlags = (int)round( g_SpriteControl.w );
	bool bCheapBlend = ( nWFlags & 16 ) != 0;

	float3 vWNormal;
	float3 vWN0 = g_TexWaterNormal.Sample( g_SampWaterNormal, i.vTexCoord ).xyz;
	float3 vWN1 = g_TexWaterNormal.Sample( g_SampWaterNormal, i.vWaterExtraBump.xy ).xyz;
	float3 vWN2 = g_TexWaterNormal.Sample( g_SampWaterNormal, i.vWaterExtraBump.zw ).xyz;
	if ( ( nWFlags & 8 ) != 0 )	// MULTITEXTURE
		vWNormal = 2.0 * ( 0.33 * ( vWN0 + vWN1 + vWN2 ) ) - 1.0;
	else
		vWNormal = 2.0 * vWN0 - 1.0;

	float3 vWSNormal = vWNormal.x * i.vWaterTBN0 + vWNormal.y * i.vWaterTBN1 + vWNormal.z * i.vWaterTBN2;
	float flWSDist = length( i.vWaterEyeVect );
	float3 vWSEye = i.vWaterEyeVect / max( flWSDist, 1e-4 );

	float3 vWReflect = 2.0 * vWSNormal * dot( vWSNormal, vWSEye ) - vWSEye;
	float3 vSpec = g_TexWaterEnv.Sample( g_Sampler0, vWReflect ).rgb * g_PSC[2].rgb;

	float flWFresnel;
	if ( ( nWFlags & 32 ) != 0 )	// FRESNEL
	{
		float flDot = 1.0 - max( 0.0, dot( vWSEye, vWSNormal ) );
		flWFresnel = flDot * flDot;
		flWFresnel *= flWFresnel;
		flWFresnel *= flDot;
	}
	else
	{
		flWFresnel = g_PSC[2].a;
	}

	float flWAlpha;
	if ( bCheapBlend )
	{
		float flReflectAmount = saturate( flWSDist * g_PSC[1].z - g_PSC[1].w );
		flWAlpha = saturate( flWFresnel + flReflectAmount );
		if ( ( nWFlags & 2 ) != 0 )	// REFRACTALPHA: water/land border feather
		{
			float2 vUnwarped = i.vWaterRefract.xy / i.vWaterRefract.z;
			float flBorderDepth = g_TexWaterRefract.Sample( g_SampWaterRefract, vUnwarped ).a;
			flWAlpha *= saturate( ( flBorderDepth - 0.05 ) * 20.0 );
		}
	}
	else
	{
		flWAlpha = 1.0;
		vSpec = lerp( g_PSC[0].rgb, vSpec, flWFresnel );
	}
	c = float4( vSpec, flWAlpha );
#elif CLOAKPASS
	// cloak_blended_pass_ps2x (no-bump path): FB-copy refraction warped by the
	// world normal projected through ViewProj rows (ps c0/c1, transposed 3x3
	// from the helper); fresnel cloak mask in alpha. 8 2D Poisson offsets,
	// .xy/.wz swizzles like dx9.
	const float4 vPoisson0 = float4( -0.0876,  0.9703,  0.5651,  0.4802 );
	const float4 vPoisson1 = float4(  0.1851,  0.1580, -0.0617, -0.2616 );
	const float4 vPoisson2 = float4( -0.5477, -0.6603,  0.0711, -0.5325 );
	const float4 vPoisson3 = float4( -0.0751, -0.8954,  0.4054,  0.6384 );
	float3 vCSN = normalize( i.vCSNormal );
	float2 vUnwarped = i.vCSRefract.xy / i.vCSRefract.z;
	float flCloak = saturate( g_PSC[6].x );		// $cloakfactor
	float flScale = lerp( g_PSC[6].y, 0.0, flCloak );	// $refractamount
	float2 vUV = float2( dot( vCSN, g_PSC[0].xyz ), dot( vCSN, g_PSC[1].xyz ) ) * flScale + vUnwarped;
	float flBlur = lerp( 0.05, 0.0, flCloak );
	float3 vRefr = g_Texture0.Sample( g_Sampler0, vUV ).rgb;
	vRefr += g_Texture0.Sample( g_Sampler0, vUV + vPoisson0.xy * flBlur ).rgb;
	vRefr += g_Texture0.Sample( g_Sampler0, vUV + vPoisson0.wz * flBlur ).rgb;
	vRefr += g_Texture0.Sample( g_Sampler0, vUV + vPoisson1.xy * flBlur ).rgb;
	vRefr += g_Texture0.Sample( g_Sampler0, vUV + vPoisson1.wz * flBlur ).rgb;
	vRefr += g_Texture0.Sample( g_Sampler0, vUV + vPoisson2.xy * flBlur ).rgb;
	vRefr += g_Texture0.Sample( g_Sampler0, vUV + vPoisson2.wz * flBlur ).rgb;
	vRefr += g_Texture0.Sample( g_Sampler0, vUV + vPoisson3.xy * flBlur ).rgb;
	vRefr += g_Texture0.Sample( g_Sampler0, vUV + vPoisson3.wz * flBlur ).rgb;
	vRefr /= 9.0;
	// fresnel uses the RAW interpolated normal (dx9 does too)
	float flFres = 1.0 - saturate( dot( i.vCSNormal, normalize( -i.vCSView ) ) );
	float flMask = saturate( lerp( 1.0, flFres - 1.35, flCloak ) );
	flMask = 1.0 - smoothstep( 0.4, 0.425, flMask );
	vRefr *= lerp( flFres * 0.4 + 0.8, 1.0, flCloak * flCloak );
	float flTintStr = saturate( ( flCloak - 0.75 ) * 4.0 );
	vRefr *= lerp( g_PSC[7].rgb, float3( 1.0, 1.0, 1.0 ), flTintStr );	// $cloakcolortint
	c = float4( vRefr, flMask );
#elif SHEENPASS
	// weapon_sheen_pass_ps2x: hard cube reflection along the vertex normal
	// (the dx9 no-bump TBN x (0,0,1) IS the normal), masked by a model-space
	// projected scrolling mask. ENV_MAP_SCALE = cLightScale.z = 1 in LDR.
	float3 vSN = normalize( i.vCSNormal );
	float3 vEyeDir = -normalize( i.vCSView );
	float3 vRefl = 2.0 * vSN * dot( vSN, vEyeDir ) - vEyeDir;
	float3 vEnv = g_TexSheen.Sample( g_SampSheen, vRefl ).rgb * g_PSC[8].rgb * 10.0;	// $sheenmaptint
	int nSheenDir = (int)round( g_PSC[7].x );	// $sheenmapmaskdirection
	float3 vMPos = i.vCSModelPos;
	float2 vMaskUV = ( nSheenDir == 0 ) ? vMPos.zy : ( ( nSheenDir == 1 ) ? vMPos.zx : vMPos.yx );
	vMaskUV -= g_PSC[6].zw;						// mask offset (scroll)
	vMaskUV /= max( g_PSC[6].xy, 1e-6 );		// mask scale
	vMaskUV.y = 1.0 - vMaskUV.y;
	float4 vMask = g_TexSheenMask.Sample( g_SampSheenMask, vMaskUV );
	float flEnvMax = max( max( vEnv.x, vEnv.y ), vEnv.z );
	c = float4( vEnv * vMask.xyz, flEnvMax * vMask.x );
	int nSheenEffect = (int)round( g_PSC[7].y );	// $sheenindex
	if ( nSheenEffect == 1 )
		c *= 1.8;
	else if ( nSheenEffect == 2 )
		c = float4( 0.0, 0.0, 0.0, 0.0 );
#elif REFRACT
	// refract_ps2x: warp the s2 source by the normal map, lerp toward the
	// unwarped sample by the silhouette blend; alpha = normal-map alpha
	// (drives the material's translucency blend). Flags: 1 BLUR (4-tap
	// polyphase 3x3, 1/512 kernel), 2 FADEOUTONSILHOUETTE, 4 CUBEMAP,
	// 8 REFRACTTINTTEXTURE.
	int nRFlags = (int)round( g_SpriteControl.w );
	float4 vRNormal = g_TexRefrNormal.Sample( g_SampRefrNormal, i.vRefrBump.xy );
	vRNormal.xyz = 2.0 * vRNormal.xyz - 1.0;

	float flRBlend = 1.0;
	if ( ( nRFlags & 2 ) != 0 )		// FADEOUTONSILHOUETTE
	{
		flRBlend = saturate( dot( -i.vCSView, i.vCSNormal ) );
		flRBlend = flRBlend * flRBlend * flRBlend;
	}

	float3 vRTint = g_PSC[1].rgb;	// $refracttint (gamma-to-linear by the helper)
	if ( ( nRFlags & 8 ) != 0 )		// REFRACTTINTTEXTURE
		vRTint = 2.0 * vRTint * g_TexRefrTint.Sample( g_SampRefrTint, i.vRefrBump.xy ).rgb;

	float flOOW = 1.0 / i.vCSRefract.z;
	float2 vNoWarp = i.vCSRefract.xy * flOOW;
	float2 vWarp = vRNormal.xy * ( vRNormal.a * g_PSC[5].x ) + vNoWarp;

	float3 vRResult;
	if ( ( nRFlags & 1 ) != 0 )		// BLUR == 1 polyphase
	{
		const float flBlurFrac = 1.0 / 512.0;
		const float flHalfBlurFrac = 0.5 / 512.0;
		vRResult  = g_TexRefrSource.Sample( g_SampRefrSource, vWarp - float2( flHalfBlurFrac, flHalfBlurFrac ) ).rgb * 0.4444444;
		vRResult += g_TexRefrSource.Sample( g_SampRefrSource, vWarp + float2( flBlurFrac, -flHalfBlurFrac ) ).rgb * 0.2222222;
		vRResult += g_TexRefrSource.Sample( g_SampRefrSource, vWarp + float2( -flHalfBlurFrac, flBlurFrac ) ).rgb * 0.2222222;
		vRResult += g_TexRefrSource.Sample( g_SampRefrSource, vWarp + float2( flBlurFrac, flBlurFrac ) ).rgb * 0.1111111;
		float3 vUnblurred = g_TexRefrSource.Sample( g_SampRefrSource, vNoWarp ).rgb;
		vRResult = lerp( vUnblurred, vRResult * vRTint, flRBlend );
	}
	else
	{
		float3 vWarpColor = g_TexRefrSource.Sample( g_SampRefrSource, vWarp ).rgb * vRTint;
		float3 vNoWarpColor = g_TexRefrSource.Sample( g_SampRefrSource, vNoWarp ).rgb;
		vRResult = lerp( vNoWarpColor, vWarpColor, flRBlend );
	}

	if ( ( nRFlags & 4 ) != 0 )		// CUBEMAP (world-normal reflect approximation)
	{
		float3 vRN = normalize( i.vCSNormal );
		float3 vREye = -i.vCSView;
		float3 vRRefl = 2.0 * vRN * dot( vRN, vREye ) - vREye;
		float3 vRSpec = g_TexRefrEnv.Sample( g_SampRefrEnv, vRRefl ).rgb * vRNormal.a * g_PSC[0].rgb;
		vRSpec = lerp( vRSpec, vRSpec * vRSpec, g_PSC[2].rgb );	// $envmapcontrast
		float flRGrey = dot( vRSpec, float3( 0.299, 0.587, 0.114 ) );
		vRSpec = lerp( flRGrey.xxx, vRSpec, g_PSC[3].rgb );		// $envmapsaturation
		vRResult += vRSpec;
	}

	c = float4( vRResult, vRNormal.a );
#elif UNLITTWOTEX
	// unlittwotexture_ps2x: base x texture2 x linear modulation (the Sine
	// $color pulse rides the c1 write -> g_Modulation latch). Output alpha is
	// FORCED to 1 like dx9 (the additive blend doesn't read it).
	c = float4( vTex.rgb * g_TexTwo.Sample( g_SampTwo, i.vDetailCoord ).rgb * g_Modulation.rgb, 1.0 );
#elif MODULATE
	// modulate_ps2x: lerp toward the c0 gray by (tex.a x modulation.a) — low
	// alpha = mod2x identity (0.5 x dst x 2 = dst). The blend (DST_COLOR x
	// SRC_COLOR or x ZERO) rides the snapshot.
	float4 vModC = saturate( vTex * i.vColor );
	c.rgb = lerp( g_PSC[0].rgb, vModC.rgb, vModC.a );
	c.a = vModC.a;
#elif SSDOWNSAMPLE
	// Downsample_nohdr_ps2x (CSTRIKE=0, SRGB_ADAPTER=0): per-tap bright-pass
	// Shape — luminance (ps c0.xyz weights) x pow(rgb, c0.w) — then average.
	float4 vDS0 = g_Texture0.Sample( g_Sampler0, i.vSSTapA.xy );
	float4 vDS1 = g_Texture0.Sample( g_Sampler0, i.vSSTapA.zw );
	float4 vDS2 = g_Texture0.Sample( g_Sampler0, i.vSSTapB.xy );
	float4 vDS3 = g_Texture0.Sample( g_Sampler0, i.vSSTapB.zw );
	vDS0.rgb = pow( max( vDS0.rgb, 0.0 ), g_PSC[0].w ) * dot( vDS0.rgb, g_PSC[0].xyz );
	vDS1.rgb = pow( max( vDS1.rgb, 0.0 ), g_PSC[0].w ) * dot( vDS1.rgb, g_PSC[0].xyz );
	vDS2.rgb = pow( max( vDS2.rgb, 0.0 ), g_PSC[0].w ) * dot( vDS2.rgb, g_PSC[0].xyz );
	vDS3.rgb = pow( max( vDS3.rgb, 0.0 ), g_PSC[0].w ) * dot( vDS3.rgb, g_PSC[0].xyz );
	c = ( vDS0 + vDS1 + vDS2 + vDS3 ) * 0.25;
#elif SSBLUR
	// BlurFilter_ps2x: 13-tap gaussian — 7 VS taps + 6 more from ps c0-c2
	// offsets, scaled by ps c3 (BlurFilterY carries $bloomamount there).
	float4 vBl = g_Texture0.Sample( g_Sampler0, i.vTexCoord ) * 0.2013;
	vBl += ( g_Texture0.Sample( g_Sampler0, i.vSSTapA.xy )
		   + g_Texture0.Sample( g_Sampler0, i.vSSTapB.zw ) ) * 0.2185;
	vBl += ( g_Texture0.Sample( g_Sampler0, i.vSSTapA.zw )
		   + g_Texture0.Sample( g_Sampler0, i.vSSTapC.xy ) ) * 0.0821;
	vBl += ( g_Texture0.Sample( g_Sampler0, i.vSSTapB.xy )
		   + g_Texture0.Sample( g_Sampler0, i.vSSTapC.zw ) ) * 0.0461;
	vBl += ( g_Texture0.Sample( g_Sampler0, i.vTexCoord + g_PSC[0].xy )
		   + g_Texture0.Sample( g_Sampler0, i.vTexCoord - g_PSC[0].xy ) ) * 0.0262;
	vBl += ( g_Texture0.Sample( g_Sampler0, i.vTexCoord + g_PSC[1].xy )
		   + g_Texture0.Sample( g_Sampler0, i.vTexCoord - g_PSC[1].xy ) ) * 0.0162;
	vBl += ( g_Texture0.Sample( g_Sampler0, i.vTexCoord + g_PSC[2].xy )
		   + g_Texture0.Sample( g_Sampler0, i.vTexCoord - g_PSC[2].xy ) ) * 0.0102;
	vBl.rgb *= g_PSC[3].rgb;
	c = vBl;
#elif SSADD
	// bloomadd_ps20: sample, force alpha 1; the additive blend rides the
	// snapshot ($additive in dev/bloomadd.vmt)
	c = float4( vTex.rgb, 1.0 );
#elif ENGINEPOST
	// engine_post_ps2x (AA off, PC LDR path): FB sample at the transformed
	// uv, bloom add, then up to 4 weighted CC volume-LUT lookups. The LUT
	// coordinate maps [0,1] onto texel centers of the 32^3 volume.
	float2 vFBUV = i.vTexCoord * g_PSC[2].wz + g_PSC[2].xy;
	float3 vPost = g_TexPostFB.Sample( g_SampPostFB, vFBUV ).rgb;
	vPost += g_PSC[5].x * vTex.rgb;		// bloom (t0 sampled at the bloom uv)
	float3 vCCCoord = vPost * ( 31.0 / 32.0 ) + ( 0.5 / 32.0 );
	float3 vCC = vPost * g_PSC[3].x;	// default (passthrough) weight
	vCC += g_TexCCLut0.Sample( g_SampCCLut0, vCCCoord ).rgb * g_PSC[4].x;
	vCC += g_TexCCLut1.Sample( g_SampCCLut1, vCCCoord ).rgb * g_PSC[4].y;
	vCC += g_TexCCLut2.Sample( g_SampCCLut2, vCCCoord ).rgb * g_PSC[4].z;
	vCC += g_TexCCLut3.Sample( g_SampCCLut3, vCCCoord ).rgb * g_PSC[4].w;
	c = float4( vCC, 1.0 );
#elif COLORPROJ
	// color_projection_ps2x: daltonize the finished frame. The dx9 DYNAMIC
	// combo trio rides g_SpriteControl.w (1 blindMK, 2 monochrome,
	// 4 anomylize); cpu/cpv/am/ayi ride ps c1 (set every draw by the dx9
	// DYNAMIC_STATE block, captured by the mirror).
	float4 vFrame = g_TexFrame.Sample( g_SampFrame, i.vTexCoord );
	int nCPFlags = (int)round( g_SpriteControl.w );
	float3 vCPResult = vFrame.rgb;
	if ( ( nCPFlags & 1 ) != 0 )
		vCPResult = CPBlindMK( vCPResult, g_PSC[1] );
	if ( ( nCPFlags & 2 ) != 0 )
		vCPResult = dot( vCPResult, float3( 0.299, 0.587, 0.114 ) ).xxx;
	if ( ( nCPFlags & 4 ) != 0 )
		vCPResult = ( 1.75 * vCPResult + vFrame.rgb ) / 2.75;
	c = float4( vCPResult, vFrame.a );
#elif LUMCOMPARE
	// luminance_compare_ps2x: NTSC luminance range test against ps c0
	// (min, max, scale). All channels get the step result; the snapshot's
	// alpha test clips out-of-range pixels so stencil REPLACE marks only
	// the in-range ones (color writes are off in dev/lumcompare.vmt).
	float3 vLumColor = vTex.rgb * g_PSC[0].z;
	float flLum = dot( vLumColor, float3( 0.2125, 0.7154, 0.0721 ) );
	float flInRange = step( g_PSC[0].x, flLum ) * step( flLum, g_PSC[0].y );
	c = float4( flInRange, flInRange, flInRange, flInRange );
#elif SHADOWBUILD
	// shadowbuildtexture_ps2x: white rgb, alpha = $basetexture.a x
	// modulation.a, accumulated ONE/ONE into the _rt_Shadows atlas slot.
	c = float4( 1.0, 1.0, 1.0, vTex.a * i.vColor.a );
#elif SHADOWPROJ
	// shadow_ps2x: average 5 jittered ALPHA taps of the atlas -> coverage,
	// fade by vertex alpha, lerp white toward the $color shadow color
	// (ps c1, gamma->linear). The (ZERO, SRC_COLOR) blend modulates the
	// frame; fog compensation pre-fades the factor to white with a ^4 curve
	// (the pass also takes the standard white FF-fog tail, like dx9).
	float flShadowCoverage = vTex.a;
	flShadowCoverage += g_Texture0.Sample( g_Sampler0, i.vSSTapA.xy ).a;
	flShadowCoverage += g_Texture0.Sample( g_Sampler0, i.vSSTapA.zw ).a;
	flShadowCoverage += g_Texture0.Sample( g_Sampler0, i.vSSTapB.xy ).a;
	flShadowCoverage += g_Texture0.Sample( g_Sampler0, i.vSSTapB.zw ).a;
	flShadowCoverage *= 0.2;
	flShadowCoverage = saturate( flShadowCoverage - i.vColor.a );
	c.rgb = 1.0 + flShadowCoverage * g_PSC[1].rgb - flShadowCoverage;
	c.a = 1.0;
	{
		// dx9 compensates the modulation blend on already-fogged pixels:
		// result = 1 - (1-result) * (1-fogAmount)^4. Our range-fog factor is
		// (1 = no fog), i.e. exactly (1-fogAmount).
		int nSPFogBits = (int)round( g_FogControl.x );
		if ( ( nSPFogBits & 2 ) != 0 )
		{
			float flSPFactor = saturate( max( g_FogControl.y, g_SceneFogColor.w - i.vFogData.y * g_FogControl.z ) );
			float flSPFactor4 = flSPFactor * flSPFactor;
			flSPFactor4 *= flSPFactor4;
			c.rgb = 1.0 - ( 1.0 - c.rgb ) * flSPFactor4;
		}
	}
#elif SHADOWMODEL
	// shadowmodel_ps20: texkill clipping (shadow volume + backface), then
	// modulate the frame by the shadow color (the dx9 lerp factor reads an
	// interpolator component D3D9 pads to 1 — full-strength color).
	clip( i.vSSTapA.xyz );
	clip( i.vSSTapB.xyz );
	clip( i.vSSTapA.w );
	c = float4( i.vColor.rgb, 1.0 );
#elif INTROEFFECT
	// IntroScreenSpaceEffect_ps2x MODE 0..9 as a runtime switch
	// (g_SpriteControl.w = $mode). s0 = player scene FB copy (vTex),
	// s1 = intro camera FB copy; ps c0.x = $alpha; SRC_ALPHA/ONE blend.
	{
		float3 vScene = vTex.rgb;
		float3 vGman = g_TexIntro2.Sample( g_SampIntro2, i.vTexCoord ).rgb;
		int nIntroMode = (int)round( g_SpriteControl.w );
		float flIntroAlpha = g_PSC[0].x;
		float3 vIntro = vScene;
		if ( nIntroMode == 0 )
		{
			// negative greyscale of scene * gman
			float flLum = dot( float3( 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 ), vScene );
			vIntro = ( 1.0 - flLum.xxx ) * vGman;
		}
		else if ( nIntroMode == 1 )
		{
			float flGmanLum = dot( float3( 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 ), vGman );
			float flSceneLum = dot( float3( 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 ), vScene );
			vIntro = ( flGmanLum < 0.3 ) ? ( 1.0 - vGman ) : ( ( 1.0 - vGman ) * flSceneLum.xxx );
		}
		else if ( nIntroMode == 2 )
		{
			float flGmanLum = dot( float3( 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 ), vGman );
			vIntro = min( flGmanLum.xxx, vScene );
		}
		else if ( nIntroMode == 3 || nIntroMode == 4 )
		{
			// luminance ramp blend between the gman layer and the scene
			float flGmanLum = dot( float3( 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 ), vGman );
			float flBlend;
			if ( flGmanLum < 0.4 )
				flBlend = flGmanLum / 0.4;
			else if ( flGmanLum > 0.7 )
				flBlend = 1.0 - ( ( flGmanLum - 0.7 ) / 0.3 );
			else
				flBlend = 1.0;
			flBlend = saturate( flBlend );
			float3 vBase = ( nIntroMode == 3 ) ? flGmanLum.xxx : vGman;
			vIntro = vBase * ( 1.0 - flBlend ) + vScene * flBlend;
		}
		else if ( nIntroMode == 5 )
		{
			if ( vScene.r > 0.0 )
			{
				vIntro = vScene;
			}
			else
			{
				float3 vHSV = IntroRGBtoHSV( vGman );
				float flBoost = vHSV.b - 0.5;
				vHSV.b *= 1.0 + flBoost;
				vHSV.g *= 1.0 - flBoost;
				vIntro = IntroHSVtoRGB( vHSV );
			}
		}
		else if ( nIntroMode == 6 )
		{
			vIntro = vScene + vGman;
		}
		else if ( nIntroMode == 7 )
		{
			vIntro = vScene;
		}
		else if ( nIntroMode == 8 )
		{
			vIntro = vGman;
		}
		else // 9: contrasty overlay
		{
			float flL1 = saturate( dot( vScene, float3( 0.333, 0.334, 0.333 ) ) );
			float3 vLayer1 = saturate( vScene * vScene * 2.0 );
			vIntro = vLayer1 + vGman * saturate( 1.0 - flL1 * 2.0 );
		}
		c = float4( vIntro, flIntroAlpha );
	}
#elif MOTIONBLUR
	// motion_blur_ps2x: blur the FB copy along the sum of three vectors — the
	// proxy's global blur vector (c1.xy, y flipped), a falling vector pointed
	// inward and dampened toward screen center, and a roll vector (cross with
	// z) — clamped to c0.x screen fraction; evenly-weighted line taps.
	// SampleLevel: the tap count is cbuffer-driven (dynamic flow control),
	// where gradient samples are illegal (X4014); the FB copy has one mip.
	{
		float2 vFallingVec = ( i.vTexCoord * 2.0 ) - 1.0;
		float2 vRollVec = cross( float3( vFallingVec, 0.0 ), float3( 0.0, 0.0, 1.0 ) ).xy;
		float2 vGlobalVec = g_PSC[1].xy;
		vGlobalVec.y = -vGlobalVec.y;
		vFallingVec *= dot( vFallingVec, vFallingVec );	// dampen mid-screen
		vFallingVec *= -abs( g_PSC[1].z );	// point inward: taps stay on screen
		vRollVec *= g_PSC[1].w;
		float2 vBlurVec = vGlobalVec + vFallingVec + vRollVec;
		if ( length( vBlurVec ) > g_PSC[0].x )
			vBlurVec = normalize( vBlurVec ) * g_PSC[0].x;
		int nMBQuality = (int)round( g_SpriteControl.w );
		int nMBSamples = ( nMBQuality == 0 ) ? 1 : ( 4 * nMBQuality + 3 );	// 1/7/11/15
		if ( nMBSamples == 1 )
		{
			// dx9 QUALITY 0 divides the (all-zero) vector by (1-1) here;
			// ps_2_0 made inf*0 read back 0, IEEE makes the uv NaN — branch.
			c = float4( vTex.rgb, 1.0 );
		}
		else
		{
			float2 vUvStep = vBlurVec / ( nMBSamples - 1 );
			float3 vBlurred = float3( 0.0, 0.0, 0.0 );
			for ( int nTap = 0; nTap < nMBSamples; ++nTap )
				vBlurred += g_Texture0.SampleLevel( g_Sampler0, i.vTexCoord + vUvStep * nTap, 0 ).rgb;
			c = float4( vBlurred / nMBSamples, 1.0 );
		}
	}
#elif FLASHLIGHT
	// Two dx9 flashlight families (g_SpriteControl.w bit 1):
	// 0 = flashlight_ps2x / DrawFlashlight_dx90 (world lightmapped, eyes,
	//     teeth): s0 = cookie at the VS-projected uv, s1 = base; atten =
	//     mirror c13 against the c11 EYE distance (dx9 verbatim — the
	//     flashlight rides the player).
	// 1 = vertexlit_and_unlit_generic_ps2x FLASHLIGHT / DoFlashlight (model
	//     family): s0 = base, s7 = cookie projected IN-PS via mirror c24-27;
	//     atten = c22 against the c23 LIGHT-ORIGIN distance; NdotL gets the
	//     c28.w NoLambert bias. Both: x cFlashlightColor (c28.rgb), additive
	//     blend + alpha test from the snapshot.
	{
		float3 vFLWorldPos = float3( i.vSSTapB.w, i.vSSTapC.w, i.vFogData.x );
		float3 vFLNormal = normalize( i.vSSTapC.xyz );
		// All samples hoisted out of the family branch (gradient ops are
		// illegal inside flow control on the runtime compiler — X4014).
		float3 vFLProj90 = i.vSSTapA.xyz / ( ( abs( i.vSSTapA.w ) > 0.0001 ) ? i.vSSTapA.w : 0.0001 );
		float4 vFLWP4 = float4( vFLWorldPos, 1.0 );
		float4 vFLSpotVLG = float4( dot( vFLWP4, g_PSC[24] ), dot( vFLWP4, g_PSC[25] ),
			dot( vFLWP4, g_PSC[26] ), dot( vFLWP4, g_PSC[27] ) );
		float3 vFLProjVLG = vFLSpotVLG.xyz / ( ( abs( vFLSpotVLG.w ) > 0.0001 ) ? vFLSpotVLG.w : 0.0001 );
		float4 vFLTex0AtUV = g_Texture0.Sample( g_Sampler0, i.vTexCoord );		// VLG base
		float3 vFLTex0AtProj = g_Texture0.Sample( g_Sampler0, vFLProj90.xy ).rgb;	// dx90/eyes cookie
		float4 vFLTex1AtUV = g_TexFLBase.Sample( g_SampFLBase, i.vTexCoord );	// dx90/eyes base
		float3 vFLCookieVLG = g_TexFLCookie.Sample( g_SampFLCookie, vFLProjVLG.xy ).rgb;
		float4 vFLIris = g_TexFLIris.Sample( g_SampFLIris, i.vDetailCoord );	// eyes $iris

		int nFLFlags = (int)round( g_SpriteControl.w );
		// Depth-mapped shadows (bit 8): the dx9 family blocks bound the
		// flashlight depth map at their family's depth sampler — dx90 s7
		// (the register the VLG cookie rides), VLG s8, eyes s4. The caster
		// pass wrote raw 0..1 depth through the SAME worldToTexture frustum
		// (biased by the SHADOW_BIAS poly offset), so compare the spot
		// projection's z against the stored value. SampleLevel (one mip) is
		// legal inside the divergent branch; unbound slots read white = 1.0
		// = never shadowed.
		float flFLShadow = 1.0;
		if ( ( nFLFlags & 8 ) != 0 )
		{
			float3 vFLShadowPos = ( ( nFLFlags & 1 ) != 0 ) ? vFLProjVLG : vFLProj90;
			float flFLMapDepth;
			if ( ( nFLFlags & 4 ) != 0 )
				flFLMapDepth = g_TexFLDepthEyes.SampleLevel( g_SampFLDepthEyes, vFLShadowPos.xy, 0 ).r;
			else if ( ( nFLFlags & 1 ) != 0 )
				flFLMapDepth = g_TexFLDepthVLG.SampleLevel( g_SampFLDepthVLG, vFLShadowPos.xy, 0 ).r;
			else
				flFLMapDepth = g_TexFLCookie.SampleLevel( g_SampFLCookie, vFLShadowPos.xy, 0 ).r;
			flFLShadow = ( vFLShadowPos.z <= flFLMapDepth + 0.0001 ) ? 1.0 : 0.0;
		}
		if ( ( nFLFlags & 4 ) != 0 )
		{
			// eyes_flashlight_inc: spot x cFlashlightColor x albedo x the
			// VS-computed vertAtten, where albedo composites the iris over
			// the whites DIMMED x0.5 ("dim down the iris in HDR"); the
			// w<=0 guard blacks the mirrored rear cone.
			float3 vFLEyeAlbedo = lerp( vFLTex1AtUV.rgb, vFLIris.rgb * 0.5, vFLIris.a );
			float3 vFLEyeOut = vFLTex0AtProj * g_PSC[28].rgb * vFLEyeAlbedo * i.vSSTapB.x * flFLShadow;
			c.rgb = ( i.vSSTapA.w > 0.0 ) ? vFLEyeOut : float3( 0.0, 0.0, 0.0 );
			c.a = 1.0;
		}
		else
		{
		float3 vFLSpot, vFLDelta;
		float4 vFLBase;
		float3 vFLAttenF;
		float flFLFarZ, flFLNoLambert, flFLBehind;
		if ( ( nFLFlags & 1 ) != 0 )
		{
			vFLSpot = vFLCookieVLG;
			vFLBase = vFLTex0AtUV;
			vFLDelta = g_PSC[23].xyz - vFLWorldPos;	// light origin
			vFLAttenF = g_PSC[22].xyz;
			flFLFarZ = g_PSC[22].w;
			flFLNoLambert = g_PSC[28].w;
			flFLBehind = ( vFLSpotVLG.w > 0.0 ) ? 1.0 : 0.0;
		}
		else
		{
			vFLSpot = vFLTex0AtProj;
			vFLBase = vFLTex1AtUV;
			vFLDelta = g_PSC[11].xyz - vFLWorldPos;	// eye pos (dx9 verbatim)
			vFLAttenF = g_PSC[13].xyz;
			flFLFarZ = g_PSC[13].w;
			flFLNoLambert = 0.0;
			flFLBehind = ( i.vSSTapA.w > 0.0 ) ? 1.0 : 0.0;
		}
		vFLSpot *= g_PSC[28].rgb;
		float flFLDistSq = max( dot( vFLDelta, vFLDelta ), 0.0001 );
		float flFLDist = sqrt( flFLDistSq );
		// RemapValClamped( dist, farZ, 0.6*farZ, 0, 1 )
		float flFLEndFalloff = saturate( ( flFLDist - flFLFarZ ) / ( 0.6 * flFLFarZ - flFLFarZ ) );
		float flFLAtten = saturate( flFLEndFalloff *
			dot( vFLAttenF, float3( 1.0, 1.0 / flFLDist, 1.0 / flFLDistSq ) ) );
		// NdotL toward the light (VLG adds the NoLambert bias; the dx90
		// family's light vector arrives from the VS in vSSTapB)
		float3 vFLLightVec = ( ( nFLFlags & 1 ) != 0 ) ? vFLDelta : i.vSSTapB.xyz;
		float flFLNdotL = saturate( dot( normalize( vFLLightVec ), vFLNormal ) + flFLNoLambert );
		// Back-projection guard: dx9 hides the mirrored rear cone via the
		// cookie's black clamp border; kill w<=0 pixels outright.
		c.rgb = vFLSpot * vFLBase.rgb * flFLNdotL * flFLAtten * flFLBehind * flFLShadow;
		c.a = vFLBase.a;
		}
	}
#elif DEPTHWRITE
	// Color writes are off — only depth lands. depthwrite_ps2x ALPHACLIP
	// parity: clip against the MIRRORED ps c0 threshold — the dx9 DYNAMIC
	// block writes c0 = {$alphatestreference or 0.7} ONLY for the $alphatest
	// variants (__DepthWrite1x, where the engine copies the caster's
	// $basetexture/$AlphaTestReference into the override per surface).
	// Non-alphatest variants leave c0 unwritten (the mirror uploads stale
	// regs as 0) AND have s0 disabled (white fallback, a=1), so the clip
	// can never fire there. NOTE the global g_AlphaTest tail below is NOT
	// the signal for this perm: DepthWrite never calls EnableAlphaTest (its
	// dx9 PS does its own kill), so the snapshot bit is 0 — relying on it
	// shadowed chain-link fences as solid quads.
	clip( vTex.a - g_PSC[0].x );
#elif MONITORSCREEN
	// monitorscreen_ps2x: base x vColor (VS c47 modulation), x $texture2
	// (white fallback when unbound), then $contrast = lerp(c, c*c, ps c1),
	// $saturation = lerp(grey(c), c, ps c2), x $tint (ps c3). Alpha stays
	// base.a x modulation.a (feeds the translucent/additive snapshot blends).
	c = vTex * i.vColor;
	c *= g_TexTwo.Sample( g_SampTwo, i.vDetailCoord );
	{
		float3 vMSContrast = c.rgb * c.rgb;
		c.rgb = lerp( c.rgb, vMSContrast, g_PSC[1].rgb );
		float flMSGrey = dot( c.rgb, float3( 0.33333, 0.33333, 0.33333 ) );
		c.rgb = lerp( flMSGrey.xxx, c.rgb, g_PSC[2].rgb );
		c.rgb *= g_PSC[3].rgb;
	}
#elif LIGHTMAP
	// dx9 lightmappedgeneric: albedo * lightmap * c12 modulation. The family
	// writes ps c12 = $color2 tint x lightmap scale, ALPHA = the material's
	// alpha modulation — which carries brush-ENTITY fades (engine
	// ModulateMaterial -> AlphaModulate(r_blend) -> ComputeModulationColor):
	// func_areaportalwindow's distance fade painted koth_king's toolsblack
	// portal boxes solid black until this alpha landed. Fallback (c12 not
	// written this pass — non-lightmappedgeneric families on the heuristic
	// path) keeps the PerDraw lightmap scale with alpha 1.
	float4 vLMMod = ( g_MiscControl.w > 0.5 ) ? g_PSC[12] : float4( g_Modulation.rgb, 1.0 );
	c *= i.vColor * vLMMod;
	// $basealphaenvmapmask: base alpha is the (inverted) envmap mask, so it
	// leaves the translucency product (ps2_3_x.h only multiplies baseColor.a
	// into alpha without the combo).
	[branch]
	if ( ( (int)round( g_EnvmapControl.w ) & 3 ) == 2 )
	{
		c.a = i.vColor.a * vLMMod.a;
	}
	c.rgb *= g_Lightmap.Sample( g_LightmapSampler, i.vLightmapCoord ).rgb;
	// dx9 $selfillum (lightmappedgeneric_ps2_3_x.h:515, tint in c7): emissive
	// = tint × albedo lerped in by BASE alpha — it ignores the lightmap, and
	// base alpha leaves the translucency product (line 415).
	[branch]
	if ( g_TintControl.w > 0.5 )
	{
		c.rgb = lerp( c.rgb, g_PSC[7].rgb * vTex.rgb, vTex.a );
		c.a = i.vColor.a * vLMMod.a;
	}
#else
	// dx9 vertexlit_and_unlit_generic_ps2x.fxc:339-355: with
	// BLENDTINTBYBASEALPHA the base alpha is a paint MASK — the $color/$color2
	// modulation lerps in by it ($tintreplacesbasecolor replaces instead of
	// multiplies) and does NOT feed translucency; alpha = modulation alpha.
	[branch]
	if ( g_TintControl.x > 0.5 )
	{
		float3 vTinted = c.rgb * g_Modulation.rgb;
		vTinted = lerp( vTinted, g_Modulation.rgb, g_TintControl.y );
		c.rgb = lerp( c.rgb, vTinted, c.a );
		c.a = g_Modulation.a;
	}
	else
	{
		c *= g_Modulation;
	}
	c *= i.vColor;
	// dx9 $selfillum (ps2x.fxc:416-421, tint in c4): emissive = tint × albedo
	// (incl. modulation) lerped in by BASE alpha over the lit result; base
	// alpha leaves the translucency product (ps2x.fxc:352). Incompatible with
	// blendtint (SKIP rule) — base alpha can't be two masks at once.
	[branch]
	if ( g_TintControl.w > 0.5 && g_TintControl.x < 0.5 )
	{
		c.rgb = lerp( c.rgb, g_PSC[4].rgb * ( vTex.rgb * g_Modulation.rgb ), vTex.a );
		c.a = g_Modulation.a * i.vColor.a;
	}
#endif

#if VLGENERIC || LIGHTMAP
	// $detail post-lighting combine (modes 5/6 add detail as self-illum)
	[branch]
	if ( nDetailMode >= 0 )
	{
#if LIGHTMAP
		c.rgb = DetailCombinePostLighting( c.rgb, vDetail, nDetailMode, g_PSC[8].w );
#else
		c.rgb = DetailCombinePostLighting( c.rgb, vDetail, nDetailMode, g_PSC[4].w );
#endif
	}
#endif

#if LIGHTMAP
	// dx9 lightmappedgeneric CUBEMAP term (lightmappedgeneric_ps2_3_x.h:522-548),
	// unbumped: N = the vertex world normal (the TANGENTSPACE transpose row 2).
	// spec = ENV_MAP_SCALE x texCUBE(s2, 2(N.E)N - (N.N)E) x mask x c0 tint,
	// contrast lerp toward spec^2, saturation lerp from luma grey, x fresnel
	// ((1-N.Ê)^5 x (1-R) + R). Tint rides mirror c0 (the semi-static block
	// always writes it — incl. mat_fullbright 2 zeroing); contrast/saturation/
	// fresnel/mask ride PerDraw g_EnvmapControl (the dx9 helper only uploads
	// ps c2-c4 on its slow path — fastpath materials like the trainstation
	// windows never write them). ENV_MAP_SCALE = cLightScale.z (c30, 16 in
	// integer HDR, seeded 1). Gate = g_TintControl.z (snapshot s2 enable =
	// $envmap set); samples hoisted out of the branch (X4014).
	{
		// $bumpmap (s4) perturbs the reflection; its uv shares VS c50 with
		// the detail transform (helper:661-673): with a detail texture the
		// bump coords are the BASE coords, else c50 holds $bumptransform —
		// which is exactly what vDetailCoord carries for this family.
		float2 vEnvBumpUV = ( nDetailMode >= 0 ) ? i.vTexCoord : i.vDetailCoord;
		float4 vEnvBumpTexel = g_TexBump.Sample( g_SampBump, vEnvBumpUV );
		int nEnvCtl = (int)round( g_EnvmapControl.w );
		float3 vEnvN = i.vEnvNormal.xyz;
		if ( ( nEnvCtl & 4 ) != 0 )
		{
			// worldSpaceNormal = mul(tangentNormal, tangentSpaceTranspose)
			// (ps2_3_x.h:479; transpose rows = tangentS, tangentT, normal).
			// Raw (non-normalized) like dx9 — the reflect formula divides by
			// dot(N,N) and the fresnel dot uses it unnormalized too.
			float3 vEnvTanN = vEnvBumpTexel.xyz * 2.0 - 1.0;
			vEnvN = vEnvTanN.x * i.vEnvTanS + vEnvTanN.y * i.vEnvTanT
				+ vEnvTanN.z * i.vEnvNormal.xyz;
		}
		float3 vEnvEyeVec = i.vEnvEye.xyz;
		float3 vEnvReflect = 2.0 * dot( vEnvN, vEnvEyeVec ) * vEnvN
			- dot( vEnvN, vEnvN ) * vEnvEyeVec;
		float3 vEnvCube = g_EnvmapCube.Sample( g_SampEnvmapCube, vEnvReflect ).rgb;
		float3 vEnvMaskTexel = g_TexEnvMask.Sample( g_SampEnvMask,
			float2( i.vEnvEye.w, i.vEnvNormal.w ) ).rgb;
		[branch]
		if ( g_TintControl.z > 0.5 )
		{
			int nEnvMaskMode = nEnvCtl & 3;
			float3 vEnvSpec = ( ( g_PSC[30].z > 0.0 ) ? g_PSC[30].z : 1.0 ) * vEnvCube;
			if ( nEnvMaskMode == 1 )
				vEnvSpec *= vEnvMaskTexel;
			else if ( nEnvMaskMode == 2 )
				vEnvSpec *= 1.0 - flBaseAlphaPreDetail;	// "Reversing alpha blows!" (:410)
			else if ( nEnvMaskMode == 3 )
				vEnvSpec *= vEnvBumpTexel.a;	// $normalmapalphaenvmapmask
			vEnvSpec *= g_PSC[0].rgb;	// g_EnvmapTint
			vEnvSpec = lerp( vEnvSpec, vEnvSpec * vEnvSpec, g_EnvmapControl.x );
			float flEnvGrey = dot( vEnvSpec, float3( 0.299, 0.587, 0.114 ) );
			vEnvSpec = lerp( flEnvGrey.xxx, vEnvSpec, g_EnvmapControl.y );
			// abs = the dx9 log-of-|x| pow behavior for backfacing normals
			float flEnvFresnel = pow( abs( 1.0 - dot( vEnvN, normalize( vEnvEyeVec ) ) ), 5.0 );
			flEnvFresnel = flEnvFresnel * ( 1.0 - g_EnvmapControl.z ) + g_EnvmapControl.z;
			c.rgb += vEnvSpec * flEnvFresnel;
		}
	}
#endif

	[branch]
	if ( g_AlphaTest.x > 0.5 )
	{
		int nFunc = (int)g_AlphaTest.y;
		float flRef = g_AlphaTest.z;
		bool bPass =
			( nFunc == 7 ) ||
			( nFunc == 4 && c.a >  flRef ) ||
			( nFunc == 6 && c.a >= flRef ) ||
			( nFunc == 1 && c.a <  flRef ) ||
			( nFunc == 3 && c.a <= flRef ) ||
			( nFunc == 2 && c.a == flRef ) ||
			( nFunc == 5 && c.a != flRef );
		clip( bPass ? 1.0 : -1.0 );
	}

	// dx9 FinalOutput TONEMAP_SCALE_LINEAR: lit and emissive passes scale rgb
	// by cLightScale.x (ps c30, SetToneMappingScaleLinear — the integer-HDR
	// autoexposure scale; 1.0 in LDR, and the engine resets it to 1 around UI)
	// BEFORE fog blends in (the fog color is pre-scaled CPU-side to match).
	// TONEMAP_SCALE_NONE families per the dx9 fxc sweep: expensive water,
	// cloak, sheen, refract, modulate, the post family (downsample/blur/
	// bloomadd/engine_post), color_projection, lumcompare. Everything else we
	// port — vertexlit/skin, lightmapped, cable, eyes, windowimposter,
	// eyerefract, teeth, pyro, spritecard, CHEAP water, unlittwotexture — is
	// TONEMAP_SCALE_LINEAR. The >0 guard covers draws before the first
	// SetToneMappingScaleLinear lands in the mirror.
#if !( WATER || CLOAKPASS || SHEENPASS || REFRACT || MODULATE || SSDOWNSAMPLE || SSBLUR || SSADD || ENGINEPOST || COLORPROJ || LUMCOMPARE || SHADOWBUILD || SHADOWPROJ || SHADOWMODEL || INTROEFFECT || MOTIONBLUR || MONITORSCREEN || DEPTHWRITE )
	c.rgb *= ( g_PSC[30].x > 0.0 ) ? g_PSC[30].x : 1.0;
#endif

	int nFogBits = (int)round( g_FogControl.x );
#if !( WATER || WATERCHEAP )
	// Height fog (CalcWaterFogAlpha, common_ps_fxc.h:212) — the refraction
	// and underwater views. Bit 1 = dx9 WRITEWATERFOGTODESTALPHA
	// (lightmappedgeneric_ps2_3_x.h:575): opaque draws replace dest alpha
	// with the factor; the water surface reads it back as per-pixel fog depth
	// (fog lerp, bump-warp scale, edge feather). Runs AFTER the alpha test:
	// dx9 disables the combo on alpha-tested materials, leaving junk alpha;
	// clipping on material alpha first then writing fog depth is strictly
	// closer to the intent. Bit 4 = BlendPixelFog HEIGHT rgb lerp (the
	// underwater murk). The water perms keep their own fog math; spritecard
	// keeps its alpha but takes the rgb lerp.
	[branch]
	if ( ( nFogBits & 5 ) != 0 )
	{
		float flDepthFromWater = g_FogControl.y - i.vFogData.x;	// waterZ - worldZ
		float flDepthFromEye = g_FogControl.w - i.vFogData.x;	// eyeZ - worldZ
		float flUnderFrac = saturate( flDepthFromWater / flDepthFromEye );
		float flHeightFog = saturate( flUnderFrac * i.vFogData.y * g_FogControl.z );
#if !SPRITECARD
		if ( ( nFogBits & 1 ) != 0 )
			c.a = flHeightFog;
#endif
		if ( ( nFogBits & 4 ) != 0 )
			c.rgb = lerp( c.rgb, g_SceneFogColor.rgb, flHeightFog );
	}
#endif
	// Range fog — dx9 PC does MATERIAL_FOG_LINEAR with FIXED-FUNCTION vertex
	// fog (no D3D11 equivalent), so the same math runs here per pixel:
	// factor = max(1 - maxdensity, fogEndOverRange - projZ * ooRange), 1 = no
	// fog (UpdateVertexShaderFogParams + common_vs_fxc CalcRangeFog). Applies
	// to every perm; the per-pass ShaderFogMode_t color (FOGCOLOR/BLACK for
	// additive/GREY/WHITE) rides g_SceneFogColor.
	[branch]
	if ( ( nFogBits & 2 ) != 0 )
	{
		float flFogFactor = saturate( max( g_FogControl.y, g_SceneFogColor.w - i.vFogData.y * g_FogControl.z ) );
		c.rgb = lerp( g_SceneFogColor.rgb, c.rgb, flFogFactor );
	}

	return c;
}
