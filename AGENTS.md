# AGENTS.md - DX11 Migration Rules for `source-engine`

## Mission

This repository is being migrated from the reset, DX9-only Source renderer to a true Direct3D 11 renderer. Treat DX11 as the target renderer, not as an optional side path and not as a thin compatibility wrapper around `IDirect3DDevice9`.

The required end state is:

1. The game boots and renders through DX11 by default.
2. DX11 reaches full visual and behavioral parity with the current DX9 path.
3. DX9 is deprecated after parity is proven, with no hidden fallback that still renders through D3D9.
4. Migration happens piece by piece, with each phase compiling and having an observable validation target.

Do not attempt a one-pass renderer rewrite. Preserve the existing Source renderer interfaces and migrate the implementation behind them in controlled steps.

## Scope and precedence

These instructions apply to the whole repository. If a more specific `AGENTS.md` is added inside a subdirectory, follow the closest file for files under that directory, but keep this DX11 migration strategy intact unless the user explicitly changes it.

User prompts override this document. Existing code style and build conventions override personal preferences.

## Current repository assumptions

- The active renderer is DX9-only after a force reset.
- The main renderer seam is under `materialsystem/shaderapidx9/`.
- The current DX9 device manager/device implementation is the behavioral reference for adapter enumeration, mode setup, resource lifecycle, mesh creation, shader creation, dynamic buffers, presentation, and render-state behavior.
- `Trunk2016/Rendering/GfxCore/D3D11/` is reference material only. Use it to understand robust DX11 patterns such as device/context ownership, state caches, shader reflection, constant buffers, dynamic buffers, and ring buffers. Do not copy it wholesale into the Source renderer.

## Important local references

Inspect these before editing renderer code:

- `materialsystem/shaderapidx9/shaderdevicedx8.h`
- `materialsystem/shaderapidx9/shaderdevicedx8.cpp`
- `materialsystem/shaderapidx9/shaderapidx8.h`
- `materialsystem/shaderapidx9/shaderapidx8.cpp`
- `materialsystem/shaderapidx9/shadershadowdx8.*`
- `materialsystem/shaderapidx9/imeshdx8.*`
- `materialsystem/shaderapidx9/dynamicvb.h`
- `materialsystem/shaderapidx9/dynamicib.h`
- `materialsystem/shaderapidx9/locald3dtypes.h`
- `materialsystem/shaderapidx9/shaderapidx8_global.h`

Reference-only Trunk2016 files:

- `Trunk2016/Rendering/GfxCore/D3D11/DeviceD3D11.h`
- `Trunk2016/Rendering/GfxCore/D3D11/DeviceD3D11.cpp`
- `Trunk2016/Rendering/GfxCore/D3D11/DeviceD3D11Win32.cpp`
- `Trunk2016/Rendering/GfxCore/D3D11/DeviceContextD3D11.cpp`
- `Trunk2016/Rendering/GfxCore/D3D11/ShaderD3D11.h`
- `Trunk2016/Rendering/GfxCore/D3D11/ShaderD3D11.cpp`
- `Trunk2016/Rendering/GfxCore/D3D11/GeometryD3D11.*`
- `Trunk2016/Rendering/GfxCore/D3D11/BufferD3D11.*`
- `Trunk2016/Rendering/GfxCore/D3D11/TextureD3D11.*`
- `Trunk2016/Rendering/GfxCore/D3D11/FramebufferD3D11.*`

## External references to keep open

Use these sources when implementing DX11 behavior:

- AGENTS.md format: https://agents.md/
- Microsoft D3D11 device and swap chain creation: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdeviceandswapchain
- Microsoft D3D11 dynamic resources: https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources
- Microsoft D3D11_MAP, including `WRITE_DISCARD` and `WRITE_NO_OVERWRITE`: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map
- Microsoft HLSL constant-buffer packing rules: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
- Microsoft `D3DCompile`: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile
- Microsoft `D3DReflect`: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dreflect
- Microsoft `ID3D11DeviceContext1::VSSetConstantBuffers1`: https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-vssetconstantbuffers1
- Microsoft Direct3D feature levels: https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro
- Valve Source SDK Base 2013 notes: https://developer.valvesoftware.com/wiki/Source_SDK_2013
- Valve material docs: https://developer.valvesoftware.com/wiki/Material
- Valve shader docs: https://developer.valvesoftware.com/wiki/Shader

## Non-negotiable migration rules

### 1. Build the DX11 backend before feature work

Do not start VGUI, world rendering, shadows, or shader-port work until the DX11 backend can reliably:

- create an `ID3D11Device`, immediate context, and swap chain;
- create and bind a back-buffer RTV and depth/stencil DSV;
- set viewport and scissor state;
- clear and present every frame;
- handle window resize without leaking resources;
- report device-removed/device-reset failures clearly;
- tear down COM resources cleanly;
- expose debug-layer and object-name support in debug builds or debug modes.

A clear screen is not feature parity, but it is the first backend milestone.

### 2. Preserve Source interfaces; replace implementations behind them

The first viable DX11 path should expose the same material-system/device interfaces that the DX9 implementation exposes. Do not force the engine to understand a new renderer architecture before DX11 is stable.

Expected new implementation names:

- `CShaderDeviceMgrDx11`
- `CShaderDeviceDx11`
- `CShaderAPIDx11`
- `CShaderShadowDx11`
- `CMeshDx11`
- `CVertexBufferDx11`
- `CIndexBufferDx11`
- `CTextureDx11`
- `CDx11DeviceResources`
- `CDx11StateCache`
- `CDx11ShaderManager`
- `CDx11ConstantBufferAllocator`
- `CDx11DynamicBufferAllocator`
- `CDx11VGuiRenderer`

If the branch still uses legacy names like `IShaderAPIDX8` for the public ABI, keep the ABI stable and name only the concrete implementation `Dx11`.

### 3. Never fake support silently

Unimplemented functions must not pretend to succeed. Each unfinished method must do one of these:

- return an invalid/null handle that callers already understand;
- return `false`/failure and log a one-time `DX11_UNSUPPORTED` warning;
- render a clear placeholder, such as a magenta checker material or debug triangle, when skipping the draw would hide the issue;
- assert in debug builds only if the call is not expected in the current migration phase.

Use a helper pattern like this, adjusted to repository style:

```cpp
#define DX11_TODO_ONCE( feature )                                      \
    do                                                                  \
    {                                                                   \
        static bool s_bWarned = false;                                  \
        if ( !s_bWarned )                                               \
        {                                                               \
            Warning( "[DX11 TODO] %s is not implemented yet.\n", feature ); \
            s_bWarned = true;                                           \
        }                                                               \
    } while ( 0 )
```

Do not spam logs every frame. Do not crash release builds for normal unsupported migration-phase calls.

### 4. No hidden DX9 fallback

During migration, DX9 may remain as a reference backend and emergency manual runtime option. It must not be used as an invisible fallback in the DX11 renderer. A DX11 mode that calls into D3D9 for actual rendering is not acceptable.

Allowed temporarily:

- keep DX9 files untouched for reference;
- compile both backends while DX11 comes online;
- use DX9 screenshots/output as parity targets;
- compare DX9 state behavior while writing DX11 translation tables.

Not allowed:

- creating an `IDirect3DDevice9` in the DX11 path;
- returning DX9 resources through DX11 APIs;
- wrapping DX9 state calls instead of translating them into D3D11 state objects;
- declaring DX11 complete while any frame rendering still depends on D3D9.

### 5. Map DX9 concepts explicitly to DX11 concepts

Create centralized translation helpers instead of scattering conversions across the renderer.

Minimum required maps:

- `ImageFormat` to `DXGI_FORMAT`, including linear and sRGB variants;
- D3D9-style cull modes to `D3D11_RASTERIZER_DESC`;
- depth/stencil state to `D3D11_DEPTH_STENCIL_DESC`;
- blend state to `D3D11_BLEND_DESC`;
- sampler state to `D3D11_SAMPLER_DESC`;
- primitive types to `D3D11_PRIMITIVE_TOPOLOGY`;
- Source vertex formats/FVF/declarations to D3D11 input layouts;
- render-target/depth formats to typeless resource/SRV/RTV/DSV combinations where needed.

All state-object creation must go through caches. Never create blend/rasterizer/depth/sampler states inside hot draw loops without caching.

### 6. Respect the difference between DX9 lost devices and DX11 devices

Do not port DX9 lost-device code literally. D3D11 does not require the same normal alt-tab lost-device handling model. Implement these behaviors instead:

- release and recreate swap-chain-dependent resources on resize;
- handle `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, and `DXGI_ERROR_DEVICE_HUNG` from present/creation paths;
- keep immutable/static resources alive across normal window events;
- keep the old `ReleaseResources`/`ReacquireResources` entry points wired so the engine stays compatible, but make their DX11 semantics explicit.

### 7. Dynamic vertex/index buffers must preserve DX9 DISCARD/NOOVERWRITE behavior

The DX9 dynamic buffers use discard and no-overwrite locking semantics. The DX11 version must mirror that behavior using dynamic buffers and `Map` modes:

- create frequently updated VB/IB buffers with `D3D11_USAGE_DYNAMIC` and `D3D11_CPU_ACCESS_WRITE`;
- first write after flush/wrap uses `D3D11_MAP_WRITE_DISCARD`;
- append writes use `D3D11_MAP_WRITE_NO_OVERWRITE` only when the allocator can guarantee it will not overwrite data still referenced by in-flight draws;
- when unsure, use `WRITE_DISCARD` rather than corrupting geometry;
- add ring-buffer/fence discipline after the simple dynamic path is working.

### 8. Constant buffers are not raw DX9 constant arrays

Initial DX11 shader parity may emulate DX9-style vertex/pixel constant arrays, but the implementation must store them in D3D11 constant buffers with explicit layout rules.

Rules:

- keep constant data 16-byte aligned;
- respect HLSL packing rules;
- avoid hand-written offsets where reflection can provide offsets;
- define register slots in one header, not spread across random files;
- begin with simple dedicated dynamic constant buffers using `WRITE_DISCARD`;
- add a D3D11.1 constant-buffer-view/ring-buffer path only after the dedicated path is validated;
- if using `VSSetConstantBuffers1`/`PSSetConstantBuffers1`, offsets and sizes must be in 16-byte shader constants and must obey the required 256-byte window alignment rules for offset views.

### 9. Shader strategy: make existing shaders work first

The first shader goal is not a beautiful new shader library. The first goal is to compile and bind enough of the existing Source shader set to reproduce DX9 behavior.

Rules:

- use `D3DCompile` or dynamically load `D3DCompiler_47.dll` consistently with the project style;
- use `D3DReflect` to discover cbuffers, samplers, textures, input/output signatures, and uniform offsets;
- introduce `DX11` shader macros where needed;
- prefer `vs_4_0_level_9_3`/`ps_4_0_level_9_3` or `vs_4_0`/`ps_4_0` as compatibility stepping stones if full `5_0` shader targets require too much rewrite;
- keep row-major/column-major decisions explicit and consistent with the existing matrix upload code;
- build a shader compile smoke test before claiming shader parity;
- include readable compile errors with source path, entry point, target, defines, and compiler messages.

### 10. VGUI/UI comes after the backend is robust

Do not start UI rendering until the backend can clear, present, resize, create buffers, create textures, set state, and bind a minimal shader.

The VGUI phase must include:

- orthographic top-left pixel coordinate rendering;
- dynamic quad batching;
- font/atlas texture upload and binding;
- alpha blending equivalent to DX9 UI;
- no-depth/no-stencil UI draw state;
- scissor/clipping rectangles;
- color modulation and per-vertex color;
- support for the console/menu/debug overlay before 3D world rendering.

### 11. Parity must be observable

For every phase, update `Plan.md` with:

- files changed;
- functionality implemented;
- what still falls back to placeholder;
- exact validation performed;
- known visual differences from DX9.

Do not mark a phase done because code compiles. Mark it done because it compiles and the phase validation target works.

### 12. Keep commits narrow

Each commit or Codex task should have one goal. Good examples:

- add DX11 skeleton classes and build files;
- create DX11 device/swap chain and clear/present;
- add DX11 format translation helpers;
- add dynamic VB/IB allocation skeleton;
- add shader compiler wrapper;
- bring up VGUI textured quads.

Bad examples:

- rewrite all shaders;
- replace all material-system classes at once;
- add backend, UI, world rendering, postprocessing, and cleanup in one patch;
- delete DX9 before DX11 parity is proven.

## Proposed initial DX11 skeleton

Create a new renderer area. Prefer `materialsystem/shaderapidx11/` unless the current build system makes a temporary colocated implementation substantially safer.

```text
materialsystem/shaderapidx11/
  dx11_todo.h
  dx11_com_ptr.h                 # only if no existing COM smart pointer is suitable
  dx11_format.h/.cpp
  dx11_device_resources.h/.cpp
  dx11_state_cache.h/.cpp
  dx11_buffers.h/.cpp
  dx11_constant_buffers.h/.cpp
  dx11_shader.h/.cpp
  dx11_texture.h/.cpp
  dx11_mesh.h/.cpp
  dx11_vgui.h/.cpp
  shadershadowdx11.h/.cpp
  shaderdevicedx11.h/.cpp
  shaderapidx11.h/.cpp
```

Minimum class outline:

```cpp
class CDx11DeviceResources
{
public:
    bool Create( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &info );
    void Destroy();
    bool Resize( int width, int height );
    bool BeginFrame();
    void Present();
    void SetDebugName( ID3D11DeviceChild *pObject, const char *pName );

    ID3D11Device *Device() const;
    ID3D11DeviceContext *Context() const;
    IDXGISwapChain *SwapChain() const;
    ID3D11RenderTargetView *BackBufferRTV() const;
    ID3D11DepthStencilView *DepthStencilDSV() const;

private:
    // Use the repo's preferred COM smart pointer or a local RAII wrapper.
    // Own device, context, swap chain, back buffer, RTV, depth texture, DSV.
};

class CDx11StateCache
{
public:
    void Clear();
    void SetViewport( const D3D11_VIEWPORT &viewport );
    void SetScissorRect( const D3D11_RECT &rect, bool enabled );
    void SetRasterizerState( const Dx11RasterStateKey_t &key );
    void SetDepthStencilState( const Dx11DepthStencilKey_t &key, UINT stencilRef );
    void SetBlendState( const Dx11BlendStateKey_t &key, const float blendFactor[4], UINT sampleMask );
    void SetSamplerState( ShaderStage_t stage, UINT slot, const Dx11SamplerKey_t &key );
};

class CDx11DynamicBufferAllocator
{
public:
    bool Init( ID3D11Device *device, UINT bindFlags, size_t byteSize, const char *debugName );
    void BeginFrame();
    bool Allocate( size_t byteCount, size_t alignment, Dx11BufferAllocation_t &out );
    void EndFrame();
};

class CDx11ConstantBufferAllocator
{
public:
    bool Init( ID3D11Device *device, ID3D11DeviceContext *context );
    void BeginFrame();
    bool UploadDedicated( const void *data, size_t bytes, ID3D11Buffer **outBuffer );
    bool UploadRingView( const void *data, size_t bytes, Dx11ConstantBufferView_t &outView );
    void BindVS( UINT slot, const Dx11ConstantBufferView_t &view );
    void BindPS( UINT slot, const Dx11ConstantBufferView_t &view );
};

class CShaderDeviceMgrDx11 : public CShaderDeviceMgrBase
{
public:
    bool Connect( CreateInterfaceFn factory ) override;
    void Disconnect() override;
    InitReturnVal_t Init() override;
    void Shutdown() override;
    int GetAdapterCount() const override;
    void GetAdapterInfo( int adapter, MaterialAdapterInfo_t &info ) const override;
    int GetModeCount( int adapter ) const override;
    void GetModeInfo( ShaderDisplayMode_t *pInfo, int adapter, int mode ) const override;
    bool SetAdapter( int adapter, int flags ) override;
    CreateInterfaceFn SetMode( void *hWnd, int adapter, const ShaderDeviceInfo_t &mode ) override;
};

class CShaderDeviceDx11 : public CShaderDeviceBase
{
public:
    bool InitDevice( void *hWnd, int adapter, const ShaderDeviceInfo_t &info ) override;
    void ShutdownDevice() override;
    bool IsUsingGraphics() const override;
    void Present() override;
    void GetBackBufferDimensions( int &width, int &height ) const override;

    IShaderBuffer *CompileShader( const char *program, size_t len, const char *shaderVersion ) override;
    VertexShaderHandle_t CreateVertexShader( IShaderBuffer *buffer ) override;
    PixelShaderHandle_t CreatePixelShader( IShaderBuffer *buffer ) override;

    IMesh *CreateStaticMesh( VertexFormat_t fmt, const char *budgetGroup, IMaterial *material ) override;
    IVertexBuffer *CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int vertexCount, const char *budgetGroup ) override;
    IIndexBuffer *CreateIndexBuffer( ShaderBufferType_t type, MaterialIndexFormat_t fmt, int indexCount, const char *budgetGroup ) override;
    IVertexBuffer *GetDynamicVertexBuffer( int streamId, VertexFormat_t fmt, bool buffered ) override;
    IIndexBuffer *GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool buffered ) override;
};
```

Skeleton code may compile with unsupported placeholders, but placeholders must be explicit, logged, and tracked in `Plan.md`.

## Review checklist before finishing any DX11 task

- The code compiles for the intended Windows configuration.
- No new D3D9 dependency is introduced into the DX11 implementation.
- Every COM object is released or held by RAII.
- All new state objects are cached.
- New shader constants are 16-byte aligned and documented.
- Dynamic buffer writes do not overwrite in-flight draw data.
- Unsupported calls log once and return safe placeholder values.
- `Plan.md` is updated with current status and next step.
- The change is small enough to review as one phase.
