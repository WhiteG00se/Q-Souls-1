#define WIN32_LEAN_AND_MEAN
#include "TitleOverlay.h"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace {
template<class T> struct Com {
    T *p = nullptr;
    ~Com() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T *operator->() const { return p; }
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
};

char label[64];
BOOL (*isVisible)();
void (*writeLog)(const char *);
decltype(&D3DCompile) compileShader;
SRWLOCK renderLock = SRWLOCK_INIT, hookLock = SRWLOCK_INIT;
struct Lock {
    SRWLOCK *lock;
    explicit Lock(SRWLOCK *value) : lock(value) { AcquireSRWLockExclusive(lock); }
    ~Lock() { ReleaseSRWLockExclusive(lock); }
};

DWORD readVersion(const wchar_t *root)
{
    wchar_t path[MAX_PATH];
    if (swprintf(path, MAX_PATH, L"%ls\\VERSION.txt", root) < 0) return ERROR_BUFFER_OVERFLOW;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    char data[64];
    DWORD count = 0, error = ERROR_SUCCESS;
    if (!ReadFile(file, data, sizeof(data), &count, nullptr)) error = GetLastError();
    CloseHandle(file);
    if (error) return error;
    if (!count || count == sizeof(data)) return ERROR_INVALID_DATA;
    size_t start = 0, end = count;
    const char *space = " \t\r\n";
    while (start < end && data[start] && strchr(space, data[start])) ++start;
    while (end > start && data[end - 1] && strchr(space, data[end - 1])) --end;

    if (start == end || end - start > 31) return ERROR_INVALID_DATA;
    bool digit = false;
    for (size_t i = start; i < end; ++i) {
        if (data[i] >= '0' && data[i] <= '9') digit = true;
        else if (data[i] == '.' && digit) digit = false;
        else return ERROR_INVALID_DATA;
    }
    if (!digit) return ERROR_INVALID_DATA;
    data[end] = 0;
    snprintf(label, sizeof(label), "Q Souls 1 v %s", data + start);
    return ERROR_SUCCESS;
}

struct Layout { UINT font; float right, top; };
Layout layout(UINT width, UINT height)
{
    float scale = (float)height / 1080.0f;
    if ((float)width / 1920.0f < scale) scale = (float)width / 1920.0f;
    UINT font = (UINT)(26.0f * scale + 0.5f);
    // Anchor to a centered 16:9 area, including on ultrawide screens.
    float right = ((float)width + 1920.0f * scale) * 0.5f - 24.0f * scale;
    float top = ((float)height - 1080.0f * scale) * 0.5f + 84.0f * scale;
    return {font < 8 ? 8 : font, right, top};
}

struct Renderer {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext1> context;
    Com<ID3DDeviceContextState> state;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11ShaderResourceView> text;
    Com<ID3D11BlendState> blend;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11RasterizerState> raster;
    UINT fontHeight = 0, textWidth = 0, textHeight = 0;

    void reset()
    {
        text.reset(); vs.reset(); ps.reset(); blend.reset(); sampler.reset(); raster.reset();
        state.reset(); context.reset(); device.reset(); fontHeight = 0;
    }

    HRESULT initialize(ID3D11Device *current)
    {
        reset();
        device.p = current; current->AddRef();
        Com<ID3D11Device1> newer;
        HRESULT hr = current->QueryInterface(__uuidof(ID3D11Device1), (void **)&newer.p);
        if (FAILED(hr)) return hr;
        newer->GetImmediateContext1(&context.p);
        D3D_FEATURE_LEVEL feature = current->GetFeatureLevel();
        UINT flags = (current->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ?
            D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        hr = newer->CreateDeviceContextState(flags, &feature, 1, D3D11_SDK_VERSION,
                                             __uuidof(ID3D11Device), nullptr, &state.p);
        if (FAILED(hr)) return hr;
        const char *shader =
            "struct V{float4 p:SV_Position;float2 uv:TEXCOORD;};"
            "V vs(uint i:SV_VertexID){float2 q[6]={float2(0,0),float2(1,0),float2(0,1),"
            "float2(0,1),float2(1,0),float2(1,1)};V v;v.uv=q[i];"
            "v.p=float4(q[i].x*2-1,1-q[i].y*2,0,1);return v;}"
            "Texture2D t:register(t0);SamplerState s:register(s0);"
            "float4 ps(V v):SV_Target{return float4(0.8,0.8,0.8,t.Sample(s,v.uv).a);}";
        Com<ID3DBlob> code;
        hr = compileShader(shader, strlen(shader), nullptr, nullptr, nullptr, "vs", "vs_4_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code.p, nullptr);
        if (FAILED(hr)) return hr;
        hr = current->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs.p);
        if (FAILED(hr)) return hr;
        code.reset();
        hr = compileShader(shader, strlen(shader), nullptr, nullptr, nullptr, "ps", "ps_4_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code.p, nullptr);
        if (FAILED(hr)) return hr;
        hr = current->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps.p);
        if (FAILED(hr)) return hr;
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0].BlendEnable = TRUE;
        b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = current->CreateBlendState(&b, &blend.p);
        if (FAILED(hr)) return hr;
        D3D11_SAMPLER_DESC s = {};
        s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        hr = current->CreateSamplerState(&s, &sampler.p);
        if (FAILED(hr)) return hr;
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_NONE;
        r.DepthClipEnable = TRUE;
        return current->CreateRasterizerState(&r, &raster.p);
    }

    HRESULT makeText(UINT height)
    {
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) return E_FAIL;
        HFONT font = CreateFontW(-(int)height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_ROMAN, L"Times New Roman");
        if (!font) { DeleteDC(dc); return E_FAIL; }
        HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE size = {};
        BOOL measured = GetTextExtentPoint32A(dc, label, (int)strlen(label), &size);
        UINT width = measured ? (UINT)size.cx + 4 : 0;
        UINT rows = measured ? (UINT)size.cy + 2 : 0;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = (LONG)width; info.bmiHeader.biHeight = -(LONG)rows;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void *pixels = nullptr;
        HBITMAP bitmap = width && rows ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
        HRESULT hr = E_FAIL;
        if (bitmap) {
            HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
            memset(pixels, 0, (size_t)width * rows * 4);
            SetTextColor(dc, RGB(255,255,255)); SetBkMode(dc, TRANSPARENT);
            BOOL drawn = TextOutA(dc, 2, 0, label, (int)strlen(label));
            GdiFlush();
            BYTE *bytes = (BYTE *)pixels;
            for (size_t i = 0; i < (size_t)width * rows; ++i) bytes[i*4+3] = bytes[i*4];
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width; desc.Height = rows; desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data = {pixels, width * 4, 0};
            Com<ID3D11Texture2D> texture;
            if (drawn) hr = device->CreateTexture2D(&desc, &data, &texture.p);
            if (SUCCEEDED(hr)) {
                text.reset();
                hr = device->CreateShaderResourceView(texture.p, nullptr, &text.p);
            }
            SelectObject(dc, oldBitmap); DeleteObject(bitmap);
        }
        SelectObject(dc, oldFont); DeleteObject(font); DeleteDC(dc);
        if (SUCCEEDED(hr)) { fontHeight = height; textWidth = width; textHeight = rows; }
        return hr;
    }

    HRESULT draw(IDXGISwapChain *chain)
    {
        Com<ID3D11Device> current;
        HRESULT hr = chain->GetDevice(__uuidof(ID3D11Device), (void **)&current.p);
        if (FAILED(hr)) return hr;
        if (current.p != device.p || !raster.p) {
            hr = initialize(current.p);
            if (FAILED(hr)) return hr;
        }
        // No retained backbuffer references: ResizeBuffers remains entirely the game's job.
        Com<ID3D11Texture2D> buffer;
        hr = chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer.p);
        if (FAILED(hr)) return hr;
        D3D11_TEXTURE2D_DESC desc;
        buffer->GetDesc(&desc);
        if (!desc.Width || !desc.Height) return S_FALSE;
        Layout place = layout(desc.Width, desc.Height);
        if (place.font != fontHeight || !text.p) {
            hr = makeText(place.font);
            if (FAILED(hr)) return hr;
        }
        Com<ID3D11RenderTargetView> target;
        hr = current->CreateRenderTargetView(buffer.p, nullptr, &target.p);
        if (FAILED(hr)) return hr;
        Com<ID3DDeviceContextState> previous;
        context->SwapDeviceContextState(state.p, &previous.p);
        context->OMSetRenderTargets(1, &target.p, nullptr);
        context->OMSetBlendState(blend.p, nullptr, ~0u);
        context->RSSetState(raster.p);
        D3D11_VIEWPORT viewport = {place.right - (float)textWidth, place.top,
                                  (float)textWidth, (float)textHeight, 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.p, nullptr, 0);
        context->PSSetShader(ps.p, nullptr, 0);
        context->PSSetShaderResources(0, 1, &text.p);
        context->PSSetSamplers(0, 1, &sampler.p);
        context->Draw(6, 0);
        context->ClearState(); // Also removes the overlay state's reference to the backbuffer.
        context->SwapDeviceContextState(previous.p, nullptr);
        return S_OK;
    }
} renderer;

using CreateFactory = HRESULT (WINAPI *)(REFIID, void **);
using CreateChain = HRESULT (STDMETHODCALLTYPE *)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
using Present = HRESULT (STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
CreateFactory realFactory;
struct Hook { void **slot; void *original; };
Hook hooks[32];
size_t hookCount;

void report(HRESULT hr)
{
    static HRESULT last = S_OK;
    if (FAILED(hr) && hr != last) {
        char message[128];
        snprintf(message, sizeof(message), "Title overlay error: 0x%08lx", (unsigned long)hr);
        if (writeLog) writeLog(message);
    }
    last = hr;
}

// Patch only the COM dispatch slot, chaining any previously installed overlay.
HRESULT hook(void **slot, void *replacement)
{
    Lock guard(&hookLock);
    for (size_t i = 0; i < hookCount; ++i) if (hooks[i].slot == slot) return S_OK;
    if (hookCount == sizeof(hooks) / sizeof(*hooks)) return E_OUTOFMEMORY;
    DWORD old, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return HRESULT_FROM_WIN32(GetLastError());
    hooks[hookCount++] = {slot, *slot};
    InterlockedExchangePointer(slot, replacement);
    if (!VirtualProtect(slot, sizeof(*slot), old, &ignored)) return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

void *original(void *object, size_t index)
{
    void **slot = *(void ***)object + index;
    Lock guard(&hookLock);
    for (size_t i = 0; i < hookCount; ++i) if (hooks[i].slot == slot) return hooks[i].original;
    return nullptr;
}

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain *chain, UINT interval, UINT flags)
{
    auto next = (Present)original(chain, 8);
    if (!next) return E_UNEXPECTED;
    if (!(flags & DXGI_PRESENT_TEST) && isVisible && isVisible()) {
        Lock guard(&renderLock);
        report(renderer.draw(chain));
    }
    HRESULT hr = next(chain, interval, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Lock guard(&renderLock);
        renderer.reset();
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE createChain(IDXGIFactory *factory, IUnknown *device,
                                      DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **chain)
{
    auto next = (CreateChain)original(factory, 10);
    if (!next) return E_UNEXPECTED;
    HRESULT hr = next(factory, device, desc, chain);
    if (SUCCEEDED(hr) && chain && *chain) {
        HRESULT hooked = hook(*(void ***)*chain + 8, (void *)present);
        if (FAILED(hooked) && writeLog) writeLog("Could not hook title overlay presentation.");
    }
    return hr;
}

HRESULT WINAPI createFactory(REFIID iid, void **factory)
{
    HRESULT hr = realFactory(iid, factory);
    if (SUCCEEDED(hr) && factory && *factory) {
        HRESULT hooked = hook(*(void ***)*factory + 10, (void *)createChain);
        if (FAILED(hooked) && writeLog) writeLog("Could not hook title overlay swap-chain creation.");
    }
    return hr;
}
} // namespace

extern "C" DWORD initializeTitleOverlay(const wchar_t *root, void **factorySlot,
                                        BOOL (*visible)(void), void (*log)(const char *))
{
    DWORD error = readVersion(root);
    if (error) { log("Cannot read VERSION.txt: expected a numeric version such as 0.3."); return error; }
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!compiler) return GetLastError();
    compileShader = (decltype(compileShader))(void *)GetProcAddress(compiler, "D3DCompile");
    if (!compileShader || !factorySlot) return ERROR_PROC_NOT_FOUND;
    isVisible = visible; writeLog = log;
    realFactory = (CreateFactory)*factorySlot;
    DWORD old, ignored;
    if (!VirtualProtect(factorySlot, sizeof(*factorySlot), PAGE_READWRITE, &old)) return GetLastError();
    InterlockedExchangePointer(factorySlot, (void *)createFactory);
    if (!VirtualProtect(factorySlot, sizeof(*factorySlot), old, &ignored)) return GetLastError();
    log(label);
    return ERROR_SUCCESS;
}
