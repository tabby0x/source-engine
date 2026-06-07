# Plan.md - DX11 Replacement Roadmap

## Long-term goal

Replace the current DX9 renderer with a true Direct3D 11 backend while preserving DX9 visual and behavioral parity first. DX11 must become the default renderer once it can render the game correctly. DX9 can remain as a reference during migration, but it must not be used as a hidden fallback for DX11 rendering.

The migration must be piece-by-piece. Each phase below should be implemented, compiled, validated, and recorded before the next phase starts.

## Definition of done for the full migration

The migration is complete only when all of these are true:

- The game boots using DX11 without constructing an `IDirect3DDevice9` in the render path.
- The material system creates the DX11 shader device and shader API through the same public engine-facing interfaces used by the current DX9 path.
- VGUI/menu/console/debug overlay render correctly in DX11.
- Static and dynamic meshes render correctly.
- Core Source material passes, textures, samplers, fog, culling, alpha, depth, stencil, and sRGB behavior match DX9.
- Render targets, postprocessing, shadow/depth passes, water/refraction-style effects, and screenshot paths are implemented or explicitly documented if not applicable to this branch.
- DX11 shader compilation and reflection are robust enough to diagnose broken shaders without vague failures.
- No DX11 TODO placeholder remains in normal gameplay rendering paths.
- DX9 is disabled/deprecated from default builds after parity is proven.

## Repository strategy

Prefer adding a new implementation area:

```text
materialsystem/shaderapidx11/
```

Keep `materialsystem/shaderapidx9/` intact during migration as the behavioral reference. Only remove or disable DX9 after DX11 parity has been proven.

Expected initial file layout:

```text
materialsystem/shaderapidx11/
  dx11_todo.h
  dx11_com_ptr.h                 # optional if no project COM RAII helper exists
  dx11_format.h
  dx11_format.cpp
  dx11_device_resources.h
  dx11_device_resources.cpp
  dx11_state_cache.h
  dx11_state_cache.cpp
  dx11_buffers.h
  dx11_buffers.cpp
  dx11_constant_buffers.h
  dx11_constant_buffers.cpp
  dx11_shader.h
  dx11_shader.cpp
  dx11_texture.h
  dx11_texture.cpp
  dx11_mesh.h
  dx11_mesh.cpp
  dx11_vgui.h
  dx11_vgui.cpp
  shadershadowdx11.h
  shadershadowdx11.cpp
  shaderdevicedx11.h
  shaderdevicedx11.cpp
  shaderapidx11.h
  shaderapidx11.cpp
```

If the build system makes a separate `shaderapidx11` project difficult at first, create the classes in a temporary compile-safe location but keep class names, file names, and compile flags clearly DX11-specific.

## Phase 0 - Inventory and build hygiene

### Goal

Understand the DX9 renderer surface area and add DX11 scaffolding without changing runtime behavior.

### Tasks

- Verify the current DX9-only state compiles and launches.
- Identify the exact solution/project/VPC files that compile `materialsystem/shaderapidx9`.
- List every file that includes `d3d9.h`, `locald3dtypes.h`, `IDirect3DDevice9`, or `Dx9Device()`.
- List factory/export points for the current shader device manager and shader API.
- Decide how the engine will select DX11 initially: build flag, command line flag, separate DLL, or replacing the existing shader API DLL.
- Add empty DX11 files and build entries only if they can compile without behavior changes.

### Validation gate

- Existing DX9 build still compiles and runs.
- No runtime behavior changed.
- `Plan.md` is updated with the discovered build commands and project files.

### Do not proceed if

- The build system path is still unknown.
- Adding empty DX11 files breaks DX9.

## Phase 1 - Robust DX11 backend skeleton

### Goal

Create a DX11 device, context, swap chain, main render target, depth/stencil buffer, viewport, clear, and present path. No real game rendering yet.

### Primary files/classes

- `dx11_device_resources.*`
- `shaderdevicedx11.*`
- `shaderapidx11.*`
- build/project files

### Required implementation

- Dynamically or statically access `D3D11CreateDeviceAndSwapChain` according to project style.
- Request a clear feature-level list. Prefer `11_0` first; decide whether `10_0`/`9_3` are allowed compatibility levels.
- Create the swap chain for the engine HWND.
- Create back-buffer RTV.
- Create depth/stencil texture and DSV.
- Set viewport to back-buffer dimensions.
- Clear color and depth every frame.
- Present every frame.
- Expose `Dx11Device()` and `Dx11Context()` accessors for DX11 implementation files only.
- Add debug layer support behind a command-line flag or ConVar such as `-dx11debug` or `mat_dx11_debug`.
- Add debug object names where supported.
- Implement one-time TODO logging for unimplemented renderer functions.

### Placeholder behavior

- `CShaderAPIDx11::DrawMesh` may skip or draw a debug placeholder, but it must log once.
- Shader and mesh creation may return invalid handles only if callers remain safe.
- Any unsupported call must use `DX11_TODO_ONCE` or equivalent.

### Validation gate

- The game creates a DX11 device and swap chain.
- The window clears to a known color and presents.
- Resizing the window recreates swap-chain-dependent RTV/DSV resources without leaks.
- Shutting down releases all DX11 COM objects without debug-layer live-object spam where the debug layer is available.
- DX9 is not constructed in the DX11 path.

### Do not proceed if

- Resize/present is unstable.
- Device creation failures are opaque.
- The backend leaks core COM resources.

## Phase 2 - Format, state, and resource translation layer

### Goal

Build the shared translation and cache layer that later VGUI/world rendering will use.

### Primary files/classes

- `dx11_format.*`
- `dx11_state_cache.*`
- `dx11_texture.*`
- `dx11_buffers.*`

### Required implementation

- Implement `ImageFormat` to `DXGI_FORMAT` mapping.
- Add explicit linear/sRGB pairing for textures and render targets.
- Add depth/stencil format mapping, including typeless resource formats when SRV access is needed later.
- Add primitive topology mapping.
- Add rasterizer-state cache.
- Add blend-state cache.
- Add depth/stencil-state cache.
- Add sampler-state cache.
- Add viewport/scissor binding through the state cache.
- Add static vertex buffer and index buffer creation.
- Add dynamic vertex buffer and index buffer creation with `D3D11_USAGE_DYNAMIC` and `D3D11_CPU_ACCESS_WRITE`.
- Implement initial dynamic buffer locking with `WRITE_DISCARD` first; add `WRITE_NO_OVERWRITE` only once allocation tracking is safe.
- Add texture creation for the minimum formats required by VGUI: RGBA8/BGRA8 and font/atlas textures.
- Add texture update path via `UpdateSubresource` or dynamic/staging path depending on usage.

### Validation gate

- The backend can create and destroy buffers/textures without leaks.
- The state cache avoids redundant state object creation in hot paths.
- A simple internal test draw can bind a vertex buffer, index buffer, texture, sampler, blend state, raster state, and depth state without crashing.

### Do not proceed if

- Formats are guessed ad hoc in multiple places.
- State objects are created per draw.
- Dynamic buffers can overwrite in-flight data.

## Phase 3 - Shader compiler, reflection, and constant-buffer bootstrap

### Goal

Compile and bind minimal DX11 shaders, then provide a compatibility path for existing Source shader constants and samplers.

### Primary files/classes

- `dx11_shader.*`
- `dx11_constant_buffers.*`
- `shaderdevicedx11.*`
- `shaderapidx11.*`

### Required implementation

- Add a shader compiler wrapper around `D3DCompile`.
- Include source path, entry point, shader target, defines, and compiler messages in failures.
- Add `D3DReflect` wrapper.
- Reflect input signature for input-layout creation.
- Reflect cbuffers, variable offsets/sizes, textures, samplers, and bind points.
- Define DX11 shader register slots in one header.
- Implement dedicated dynamic constant buffers first, updated with `WRITE_DISCARD`.
- Add constant-buffer data structures that obey HLSL 16-byte packing rules.
- Add a compatibility layer for DX9-style vertex/pixel constants so existing shader code can be brought up incrementally.
- Add a simple placeholder shader pair for debug drawing.
- Add a simple textured UI shader pair for VGUI phase.

### Shader target policy

Start with the smallest target that allows existing shaders to come up safely:

- `vs_4_0_level_9_3` / `ps_4_0_level_9_3` if feature-level 9.3 compatibility is required.
- `vs_4_0` / `ps_4_0` if DX10-class hardware is the baseline.
- `vs_5_0` / `ps_5_0` only after compatibility shader issues are known.

Do not rewrite all shaders to modern HLSL before the compatibility path works.

### Validation gate

- Minimal vertex/pixel shader compiles and binds.
- Reflection returns expected inputs, cbuffers, textures, and samplers.
- A simple colored triangle or quad renders through DX11.
- Compile failures are understandable.

### Do not proceed if

- Constants use magic offsets instead of reflected offsets or documented layouts.
- Shader compile errors are swallowed.
- Input layouts are hardcoded in a way that cannot support Source vertex formats.

## Phase 4 - VGUI/UI rendering first real feature

### Goal

Render menus, console, overlays, text, and simple textured quads through DX11 after the backend is stable.

### Primary files/classes

- `dx11_vgui.*`
- `dx11_buffers.*`
- `dx11_texture.*`
- `dx11_shader.*`
- `shaderapidx11.*`

### Required implementation

- Implement orthographic projection using top-left pixel coordinates.
- Implement dynamic quad batching for UI vertices.
- Implement per-vertex color.
- Implement textured quads.
- Implement VGUI font/atlas texture upload and binding.
- Implement alpha blending parity with DX9 UI.
- Disable depth/stencil for UI draws.
- Implement scissor/clipping rectangles.
- Handle viewport changes and resize.
- Add a debug placeholder texture for missing UI textures.

### Validation gate

- Main menu draws.
- Console draws.
- Text draws.
- UI clipping works.
- UI alpha blending matches DX9 closely enough for menu/console use.
- UI rendering does not depend on world rendering.

### Do not proceed if

- The backend is still unstable.
- UI works only by bypassing the material/shader API in a way that cannot coexist with later rendering.

## Phase 5 - Mesh and material world rendering

### Goal

Bring up the core Source mesh draw path and enough material shader support to render simple world geometry.

### Primary files/classes

- `dx11_mesh.*`
- `dx11_buffers.*`
- `dx11_shader.*`
- `dx11_texture.*`
- `shadershadowdx11.*`
- `shaderapidx11.*`

### Required implementation

- Implement Source vertex format to DX11 input-layout mapping.
- Cache input layouts by vertex declaration plus vertex shader bytecode signature.
- Implement static mesh creation/destruction.
- Implement dynamic mesh creation/destruction.
- Implement `DrawMesh` and indexed draw paths.
- Implement material texture binding for pixel shader SRVs and samplers.
- Implement cull mode.
- Implement depth test/write.
- Implement alpha blend and alpha test compatibility behavior.
- Implement fog constants/mode compatibility.
- Implement z-bias/depth-bias mapping.
- Implement color modulation/tint constants.
- Implement wireframe/debug draw modes if present in the DX9 path.

### Recommended shader bring-up order

1. Unlit textured material.
2. Vertex-color material.
3. Lightmapped world material.
4. Basic model material.
5. Transparent material.
6. Decals.
7. Sprite/particle path.

### Validation gate

- A simple map renders opaque world geometry.
- Basic textures bind correctly.
- Vertex colors work.
- Basic transparency works.
- Visual differences from DX9 are listed in this file.

### Do not proceed if

- Mesh rendering relies on a single hardcoded vertex layout.
- Material passes bypass the existing material system in a way that prevents parity.

## Phase 6 - Lighting, depth, stencil, and shadows

### Goal

Implement the depth/stencil and lighting behavior required for real gameplay scenes.

### Required implementation

- Verify depth precision and clip-space conventions.
- Implement depth-only and shadow-map render paths.
- Implement stencil operations used by the engine.
- Implement light constants and lightmap binding.
- Implement normal/specular/environment-map resource binding as required by current materials.
- Implement alpha-to-coverage if current DX9 path relies on it.
- Implement sRGB read/write parity.
- Validate gamma behavior and hardware gamma ramp expectations.

### Validation gate

- Lit world surfaces render correctly.
- Models render with expected lighting.
- Shadow/depth passes do not corrupt main pass state.
- sRGB/gamma behavior is documented and compared against DX9.

## Phase 7 - Render targets, postprocessing, water/refraction, and screenshots

### Goal

Support non-backbuffer render targets and full-screen passes.

### Required implementation

- Render-target texture creation with RTV/SRV.
- Depth target creation with DSV and optional SRV.
- MRT support if used by this branch.
- MSAA creation and resolve if required.
- Full-screen triangle/quad draw helper.
- Postprocess material support.
- Bloom/tone/color-correction paths if present.
- Water/refraction/reflection paths if present.
- Screenshot/front-buffer capture path.

### Validation gate

- Render-to-texture materials work.
- Full-screen postprocessing works or is explicitly disabled with a visible TODO.
- Water/refraction-style scenes do not crash and have documented status.
- Screenshot capture works.

## Phase 8 - Performance, ring buffers, queries, and polish

### Goal

Move from correct to robust and performant.

### Required implementation

- Replace naive dynamic buffer usage with a frame/ring-buffer allocator where beneficial.
- Add safe `WRITE_NO_OVERWRITE` append behavior for dynamic VB/IB.
- Add D3D11.1 constant-buffer-view ring-buffer path if `ID3D11DeviceContext1` is available.
- Keep fallback dedicated constant-buffer path for systems without offset-view support.
- Add GPU timestamp/occlusion queries if needed by engine systems.
- Add debug markers compatible with RenderDoc/PIX where possible.
- Audit redundant state changes.
- Audit resource lifetime and live-object output.
- Add device-removed diagnostics.

### Validation gate

- Rendering is stable for long sessions.
- Dynamic UI and mesh buffers do not stall heavily or corrupt geometry.
- GPU capture is readable because object names and markers exist.
- No major live-object leaks remain in normal shutdown.

## Phase 9 - DX9 deprecation and cleanup

### Goal

Make DX11 the renderer and remove DX9 from the default path.

### Required implementation

- Change default renderer selection to DX11.
- Remove or disable DX9 device creation from normal runtime.
- Keep DX9 code only if explicitly needed as a separate legacy/reference build target.
- Remove temporary compatibility shims that hide missing DX11 work.
- Remove stale TODO placeholders from normal gameplay paths.
- Update README/build docs with DX11 requirements.
- Document supported Windows/feature-level baseline.

### Validation gate

- Clean build from fresh checkout.
- Game boots into DX11 by default.
- VGUI, world, materials, shadows/depth, render targets, and postprocessing meet parity targets.
- DX9 fallback is not silently used.

## DX9-to-DX11 mapping checklist

Use this mapping table while implementing parity.

| DX9 concept | DX11 replacement | Notes |
| --- | --- | --- |
| `Direct3DCreate9` / `CreateDevice` | `D3D11CreateDeviceAndSwapChain` plus DXGI swap chain | Create RTV/DSV explicitly. |
| `D3DPRESENT_PARAMETERS` | `DXGI_SWAP_CHAIN_DESC` plus render-target/depth descriptors | Resize through `ResizeBuffers`. |
| Lost device / reset | Resize resources and device-removed handling | Do not port DX9 lost-device model literally. |
| `SetRenderState` | Cached rasterizer/blend/depth-stencil state objects | Immutable state objects should be reused. |
| `SetSamplerState` | Cached `ID3D11SamplerState` | Bind per shader stage. |
| `SetTexture` | SRV binding with `PSSetShaderResources`/`VSSetShaderResources` | Avoid SRV/RTV hazards by unbinding when needed. |
| FVF / vertex declaration | `ID3D11InputLayout` | Key by Source vertex format and shader bytecode signature. |
| `DrawPrimitive` | `Draw` | Topology must be set first. |
| `DrawIndexedPrimitive` | `DrawIndexed` | Track base vertex/index offsets correctly. |
| Dynamic VB/IB lock discard | `Map(... WRITE_DISCARD ...)` | First write after flush/wrap. |
| Dynamic VB/IB lock no-overwrite | `Map(... WRITE_NO_OVERWRITE ...)` | Only when allocator guarantees safety. |
| Vertex/pixel constants | D3D11 constant buffers | Align to 16 bytes and use reflection/layout structs. |
| Render target surface | Texture/resource plus RTV/SRV | Use typeless formats where depth SRV is needed. |
| Depth/stencil surface | Texture/resource plus DSV | Optional SRV for depth sampling. |
| D3D9 fixed-function remnants | Compatibility HLSL shaders and explicit constants | No silent fixed-function emulation. |

## Initial skeleton contracts

These signatures are not final API design. They define the first stable backend shape Codex should aim for.

```cpp
struct Dx11BackBufferDesc_t
{
    int m_nWidth;
    int m_nHeight;
    DXGI_FORMAT m_ColorFormat;
    DXGI_FORMAT m_DepthFormat;
    bool m_bWindowed;
};

struct Dx11BufferAllocation_t
{
    ID3D11Buffer *m_pBuffer;
    UINT m_nOffsetBytes;
    UINT m_nSizeBytes;
    void *m_pMapped;
    bool m_bDiscarded;
};

struct Dx11ConstantBufferView_t
{
    ID3D11Buffer *m_pBuffer;
    UINT m_nOffsetBytes;
    UINT m_nSizeBytes;
    bool m_bUsesOffsetView;
};

class CDx11DeviceResources
{
public:
    CDx11DeviceResources();
    ~CDx11DeviceResources();

    bool Create( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &info );
    void Destroy();
    bool Resize( int width, int height );
    bool BeginFrame();
    void Clear( const float color[4], float depth, UINT8 stencil );
    void Present();

    ID3D11Device *Device() const;
    ID3D11DeviceContext *Context() const;
    ID3D11DeviceContext1 *Context1() const;
    IDXGISwapChain *SwapChain() const;

    int Width() const;
    int Height() const;
    D3D_FEATURE_LEVEL FeatureLevel() const;

private:
    bool CreateDeviceAndSwapChain( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &info );
    bool CreateBackBufferResources();
    void ReleaseBackBufferResources();
    void ReportDeviceRemoved( HRESULT hr );
};

class CDx11StateCache
{
public:
    explicit CDx11StateCache( CDx11DeviceResources *resources );
    void ResetCache();
    void InvalidateAll();
    void ApplyDefaultFrameState();

    void SetViewport( int x, int y, int width, int height, float minDepth, float maxDepth );
    void SetScissor( bool enabled, const RECT &rect );
    void SetCullMode( D3DCULL cullMode );
    void SetDepthStateFromShadowState( const ShadowState_t &state );
    void SetBlendStateFromShadowState( const ShadowState_t &state );
    void SetSamplerState( ShaderStage_t stage, int sampler, const Dx11SamplerKey_t &key );
};

class CDx11ShaderManager
{
public:
    bool Init( CDx11DeviceResources *resources );
    IShaderBuffer *CompileShader( const char *program, size_t len, const char *shaderVersion );
    VertexShaderHandle_t CreateVertexShader( IShaderBuffer *buffer );
    PixelShaderHandle_t CreatePixelShader( IShaderBuffer *buffer );
    bool ReflectShader( const void *bytecode, size_t bytecodeSize, Dx11ShaderReflection_t &out );
    void BindVertexShader( VertexShaderHandle_t handle );
    void BindPixelShader( PixelShaderHandle_t handle );
};

class CDx11VGuiRenderer
{
public:
    bool Init( CDx11DeviceResources *resources, CDx11ShaderManager *shaders );
    void Begin( int width, int height );
    void DrawTexturedQuadBatch( const VGuiVertex_t *vertices, int vertexCount, ITexture *texture );
    void SetScissorRect( const RECT &rect );
    void End();
};
```

## Placeholder policy

Until a phase implements real rendering, use explicit placeholders:

- Missing texture: bind magenta/black checker texture.
- Missing shader: bind debug shader and draw magenta if vertex layout is known; otherwise skip draw and warn once.
- Missing material feature: render the pass with a clear debug tint only if this does not recurse into the missing material feature.
- Missing render target feature: fail the render-target creation call and warn once.
- Missing query/perf feature: return safe default values and warn once.

Never return success for a missing feature unless the caller can observe the placeholder result or warning.

## Running status log

Codex should update this section as work lands.

### Current status

- DX9-only renderer is assumed to work.
- DX11 migration plan and agent rules have been added.
- No DX11 implementation code has been added by this planning step.

### Next concrete task

Phase 0: inventory the build system and renderer factory/export path, then add compile-only DX11 skeleton files with TODO placeholders.
