#define WIN32_LEAN_AND_MEAN
#include "TitleOverlay.h"
#include <d3d11.h>


#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "GamePresentation.h"
#include "QualitySettings.h"

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
BOOL (*qualityText)(char *, size_t);
void (*writeLog)(const char *);


decltype(&D3DCompile) compileShader;
SRWLOCK renderLock = SRWLOCK_INIT;
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

// Save only the pipeline state this renderer changes. Context-state swapping
// exposes a different emulated device to overlay hooks (RTSS detects D3D10).
struct PipelineState {
    ID3D11DeviceContext *context;
    ID3D11RenderTargetView *targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    UINT targetCount = 0;
    ID3D11UnorderedAccessView *uavs[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
    Com<ID3D11DepthStencilView> depth;
    Com<ID3D11BlendState> blend;
    FLOAT factors[4]; UINT mask;
    Com<ID3D11RasterizerState> raster;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    Com<ID3D11Predicate> predicate; BOOL predicateValue;
    Com<ID3D11ShaderResourceView> resource;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11GeometryShader> gs;
    Com<ID3D11HullShader> hs;
    Com<ID3D11DomainShader> ds;
    struct Instances {
        ID3D11ClassInstance *items[256] = {}; UINT count = 256;
        ~Instances() { for (UINT i = 0; i < count; ++i) if (items[i]) items[i]->Release(); }
    } vi, pi, gi, hi, di;
    explicit PipelineState(ID3D11DeviceContext *value) : context(value)
    {
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth.p);
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) if (targets[i]) targetCount = i + 1;
        context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr,
            0, D3D11_PS_CS_UAV_REGISTER_COUNT, uavs);
        context->OMGetBlendState(&blend.p, factors, &mask);
        context->RSGetState(&raster.p);
        context->RSGetViewports(&viewportCount, viewports);
        context->IAGetPrimitiveTopology(&topology);
        context->GetPredication(&predicate.p, &predicateValue);
        context->PSGetShaderResources(0, 1, &resource.p);
        context->PSGetSamplers(0, 1, &sampler.p);
        context->VSGetShader(&vs.p, vi.items, &vi.count);
        context->PSGetShader(&ps.p, pi.items, &pi.count);
        context->GSGetShader(&gs.p, gi.items, &gi.count);
        context->HSGetShader(&hs.p, hi.items, &hi.count);
        context->DSGetShader(&ds.p, di.items, &di.count);
    }
    ~PipelineState()
    {
        UINT counts[D3D11_PS_CS_UAV_REGISTER_COUNT];
        for (auto &count : counts) count = ~0u; // Preserve append/consume counters.
        context->OMSetRenderTargetsAndUnorderedAccessViews(targetCount, targets, depth.p,
            targetCount, D3D11_PS_CS_UAV_REGISTER_COUNT - targetCount, uavs + targetCount, counts);
        context->OMSetBlendState(blend.p, factors, mask);
        context->RSSetState(raster.p);
        context->RSSetViewports(viewportCount, viewports);
        context->IASetPrimitiveTopology(topology);
        context->SetPredication(predicate.p, predicateValue);
        context->VSSetShader(vs.p, vi.items, vi.count);
        context->PSSetShader(ps.p, pi.items, pi.count);
        context->GSSetShader(gs.p, gi.items, gi.count);
        context->HSSetShader(hs.p, hi.items, hi.count);
        context->DSSetShader(ds.p, di.items, di.count);
        context->PSSetShaderResources(0, 1, &resource.p);
        context->PSSetSamplers(0, 1, &sampler.p);
        for (auto target : targets) if (target) target->Release();
        for (auto uav : uavs) if (uav) uav->Release();
    }
};

struct Renderer {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11ShaderResourceView> text;
    Com<ID3D11BlendState> blend;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11RasterizerState> raster;
    UINT fontHeight = 0, textWidth = 0, textHeight = 0;
    char cachedText[512] = {};

    void reset()
    {
        text.reset(); vs.reset(); ps.reset(); blend.reset(); sampler.reset(); raster.reset();
        context.reset(); device.reset(); fontHeight = 0;
    }

    HRESULT initialize(ID3D11Device *current)
    {
        reset();
        device.p = current; current->AddRef();
        current->GetImmediateContext(&context.p);
        HRESULT hr;
        const char *shader =
            "struct V{float4 p:SV_Position;float2 uv:TEXCOORD;};"
            "V vs(uint i:SV_VertexID){float2 q[6]={float2(0,0),float2(1,0),float2(0,1),"
            "float2(0,1),float2(1,0),float2(1,1)};V v;v.uv=q[i];"
            "v.p=float4(q[i].x*2-1,1-q[i].y*2,0,1);return v;}"
            "Texture2D t:register(t0);SamplerState s:register(s0);"
            "float4 ps(V v):SV_Target{return t.Sample(s,v.uv);}";
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

    HRESULT makeText(UINT height, const char *contents = label, bool panel = false)
    {
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) return E_FAIL;
        HFONT font = CreateFontW(-(int)height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_ROMAN, L"Times New Roman");
        if (!font) { DeleteDC(dc); return E_FAIL; }
        HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE size = {};
        BOOL measured = GetTextExtentPoint32A(dc, contents, (int)strlen(contents), &size);
        UINT width = panel ? height * 32 : measured ? (UINT)size.cx + 4 : 0;
        UINT rows = panel ? height * 13 : measured ? (UINT)size.cy + 2 : 0;
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
            RECT rect = {(LONG)height, (LONG)height, (LONG)(width - height), (LONG)(rows - height)};
            BOOL drawn = panel ? DrawTextA(dc, contents, -1, &rect, DT_LEFT | DT_TOP | DT_NOPREFIX) != 0 :
                TextOutA(dc, 2, 0, contents, (int)strlen(contents));
            GdiFlush();
            BYTE *bytes = (BYTE *)pixels;
            for (size_t i = 0; i < (size_t)width * rows; ++i) {
                BYTE shade = bytes[i*4];
                bytes[i*4] = bytes[i*4+1] = bytes[i*4+2] = (BYTE)((panel ? 20 : 0) + shade * 0.8f);
                bytes[i*4+3] = panel ? 255 : shade;
            }
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
        if (SUCCEEDED(hr)) { fontHeight = height; textWidth = width; textHeight = rows; snprintf(cachedText, sizeof(cachedText), "%s", contents); }
        return hr;
    }

    HRESULT draw(IDXGISwapChain *chain, const char *contents = label, bool panel = false)
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
        if (place.font != fontHeight || !text.p || strcmp(contents, cachedText)) {
            hr = makeText(place.font, contents, panel);
            if (FAILED(hr)) return hr;
        }
        Com<ID3D11RenderTargetView> target;
        hr = current->CreateRenderTargetView(buffer.p, nullptr, &target.p);
        if (FAILED(hr)) return hr;
        PipelineState previous(context.p);
        context->SetPredication(nullptr, FALSE);
        context->OMSetRenderTargets(1, &target.p, nullptr);
        context->OMSetBlendState(blend.p, nullptr, ~0u);
        context->RSSetState(raster.p);
        D3D11_VIEWPORT viewport = {panel ? (desc.Width - (float)textWidth) * 0.5f : place.right - (float)textWidth,
                                  panel ? (desc.Height - (float)textHeight) * 0.5f : place.top,
                                  (float)textWidth, (float)textHeight, 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->VSSetShader(vs.p, nullptr, 0);
        context->PSSetShader(ps.p, nullptr, 0);
        context->PSSetShaderResources(0, 1, &text.p);
        context->PSSetSamplers(0, 1, &sampler.p);
        context->Draw(6, 0);
        // PipelineState restores the game bindings and releases our backbuffer target.
        return S_OK;
    }
} renderer, qualityRenderer;

void **presentationSlot;

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

HRESULT drawFrame(IDXGISwapChain *chain)
{
    Lock guard(&renderLock);
    char quality[512];
    bool showQuality = qualityText && qualityText(quality, sizeof(quality));
    HRESULT drawn = showQuality ? qualityRenderer.draw(chain, quality, true) :
        isVisible && isVisible() ? renderer.draw(chain) : S_OK;
    report(drawn);
    if (drawn == DXGI_ERROR_DEVICE_REMOVED || drawn == DXGI_ERROR_DEVICE_RESET) { renderer.reset(); qualityRenderer.reset(); }
    return drawn;
}

DWORD configureTitleOverlay(const wchar_t *root, BYTE *site,
                            BOOL (*visible)(void), void (*log)(const char *))
{
    DWORD error = readVersion(root);
    if (error) { log("Cannot read VERSION.txt: expected a numeric version such as 0.3."); return error; }
    if (!site) return ERROR_REVISION_MISMATCH;
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!compiler) return GetLastError();
    compileShader = (decltype(compileShader))(void *)GetProcAddress(compiler, "D3DCompile");
    if (!compileShader) return ERROR_PROC_NOT_FOUND;
    isVisible = visible; writeLog = log;
    error = patchGamePresentation(site, (void *)drawFrame, &presentationSlot);
    if (error) return error;
    log(label);
    log("Pre-Present drawing connected; original game Present instruction and caller are unchanged. Title label uses the original top-right layout.");
    return ERROR_SUCCESS;
}
} // namespace

extern "C" DWORD initializeTitleOverlay(const wchar_t *root, BOOL (*visible)(void), void (*log)(const char *))
{
    return configureTitleOverlay(root, findGamePresentation((BYTE *)GetModuleHandleW(nullptr)), visible, log);
}

extern "C" void setQualityOverlay(BOOL (*text)(char *, size_t))
{
    Lock guard(&renderLock); qualityText = text;
}
