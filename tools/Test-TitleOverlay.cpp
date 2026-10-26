// Isolated software rendering in a hidden test window; never loads the game.
#include "../runtime/TitleOverlay.cpp"
#include <math.h>
#define CHECK(ok, message) do { if (!(ok)) { fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } } while (0)

static BOOL qualityVisible = FALSE;
static BOOL testQuality(char *text, size_t capacity) {
    snprintf(text, capacity, "Q Souls\n\n  FPS < 60 (default) >\n> FOV (Y axis) < 48\xB0 (default) >\n  Camera Distance < 100%% (default) >");
    return qualityVisible;
}
static BOOL visible = TRUE;
static BOOL testVisible() { return visible; }
static void testLog(const char *message) { fprintf(stderr, "%s\n", message); }
static unsigned presents;
static UINT lastInterval, lastFlags;
static HRESULT STDMETHODCALLTYPE testPresent(IDXGISwapChain *, UINT interval, UINT flags)
{
    ++presents; lastInterval = interval; lastFlags = flags; return S_OK;
}
static HRESULT testFrame(IDXGISwapChain *chain, UINT interval, UINT flags)
{
    if (!(flags & DXGI_PRESENT_TEST)) drawFrame(chain);
    return testPresent(chain, interval, flags);
}

static bool fixture(const wchar_t *root, const char *data, DWORD length)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\VERSION.txt", root);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    BOOL ok = WriteFile(file, data, length, &written, nullptr);
    CloseHandle(file);
    return ok && written == length;
}

static HRESULT clear(IDXGISwapChain *chain, ID3D11Device *device, ID3D11DeviceContext *context)
{
    Com<ID3D11Texture2D> buffer;
    HRESULT hr = chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer.p);
    if (FAILED(hr)) return hr;
    Com<ID3D11RenderTargetView> target;
    hr = device->CreateRenderTargetView(buffer.p, nullptr, &target.p);
    if (SUCCEEDED(hr)) {
        const FLOAT color[] = {0,0,0,1};
        context->ClearRenderTargetView(target.p, color);
    }
    return hr;
}

struct Pixels { UINT count, minX, maxX, minY, maxY, goldCount, goldMinX, goldMaxX; };
static HRESULT pixels(IDXGISwapChain *chain, ID3D11Device *device,
                       ID3D11DeviceContext *context, Pixels *result, const wchar_t *exportPath = nullptr)
{
    Com<ID3D11Texture2D> buffer, staging;
    HRESULT hr = chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer.p);
    if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC desc;
    buffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    hr = device->CreateTexture2D(&desc, nullptr, &staging.p);
    if (FAILED(hr)) return hr;
    context->CopyResource(staging.p, buffer.p);
    D3D11_MAPPED_SUBRESOURCE map;
    hr = context->Map(staging.p, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) return hr;
    if (exportPath) {
        FILE *file = _wfopen(exportPath, L"wb");
        if (file) {
            BITMAPFILEHEADER header = {}; BITMAPINFOHEADER info = {};
            header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(info); header.bfSize = header.bfOffBits + desc.Width * desc.Height * 4;
            info.biSize = sizeof(info); info.biWidth = (LONG)desc.Width; info.biHeight = -(LONG)desc.Height; info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
            fwrite(&header, sizeof(header), 1, file); fwrite(&info, sizeof(info), 1, file);
            for (UINT row=0;row<desc.Height;++row) for(UINT x=0;x<desc.Width;++x) {
                BYTE *pixel=(BYTE *)map.pData+row*map.RowPitch+x*4;
                BYTE bgra[]={pixel[2],pixel[1],pixel[0],pixel[3]}; fwrite(bgra,4,1,file);
            }
            fclose(file);
        }
    }
    *result = {0, desc.Width, 0, desc.Height, 0, 0, desc.Width, 0};
    UINT font=layout(desc.Width,desc.Height).font;
    UINT headingBottom=(desc.Height-font*22)/2+font*3;
    for (UINT y = 0; y < desc.Height; ++y) {
        BYTE *row = (BYTE *)map.pData + y * map.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) if (row[x*4] || row[x*4+1] || row[x*4+2]) {
            if(y<headingBottom && row[x*4]>row[x*4+1]+10 && row[x*4+1]>row[x*4+2]+10) {
                ++result->goldCount;
                if(x<result->goldMinX) result->goldMinX=x;
                if(x>result->goldMaxX) result->goldMaxX=x;
            }
            ++result->count;
            if (x < result->minX) result->minX = x;
            if (x > result->maxX) result->maxX = x;
            if (y < result->minY) result->minY = y;
            if (y > result->maxY) result->maxY = y;
        }
    }
    context->Unmap(staging.p, 0);
    return S_OK;
}

int wmain(int argc, wchar_t **argv)
{
    CHECK(argc == 3, "arguments: repository, temporary fixture directory");

    CHECK(readVersion(argv[1]) == ERROR_SUCCESS && !strncmp(label, "Q Souls 1 v ", 12), "repo VERSION.txt label");
    CreateDirectoryW(argv[2], nullptr);
    CHECK(readVersion(argv[2]) == ERROR_FILE_NOT_FOUND, "missing version rejected");
    const char *invalid[] = {"", " ", ".1", "1.", "1..2", "v0.1", "1\n2", "1/2", "1.2-beta"};
    for (const char *value : invalid) {
        CHECK(fixture(argv[2], value, (DWORD)strlen(value)), "write invalid fixture");
        CHECK(readVersion(argv[2]) == ERROR_INVALID_DATA, "malformed version rejected");
    }
    const char embedded[] = {'0', '.', '1', 0, '2'};
    CHECK(fixture(argv[2], embedded, sizeof(embedded)) && readVersion(argv[2]) == ERROR_INVALID_DATA, "embedded NUL rejected");
    char longVersion[64]; memset(longVersion, '1', sizeof(longVersion));
    CHECK(fixture(argv[2], longVersion, sizeof(longVersion)) && readVersion(argv[2]) == ERROR_INVALID_DATA, "oversize version rejected");
    CHECK(fixture(argv[2], " \t12.34.5\r\n", 11) && readVersion(argv[2]) == ERROR_SUCCESS &&
          !strcmp(label, "Q Souls 1 v 12.34.5"), "trim whitespace and load changed version");

    // The production initializer only patches the installed game's validated
    // call site. Initialize graphics dependencies here without touching any API.
    compileShader = (decltype(compileShader))(void *)GetProcAddress(LoadLibraryW(L"d3dcompiler_47.dll"), "D3DCompile");
    CHECK(compileShader && !readVersion(argv[1]), "isolated renderer dependencies");
    isVisible = testVisible; writeLog = testLog;
    Com<IDXGIFactory> factory;
    CHECK(SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&factory.p)), "native factory creation");
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                     D3D11_SDK_VERSION, &device.p, nullptr, &context.p)), "software rendering device");
    HWND window = CreateWindowExW(0, L"STATIC", L"Q Souls isolated overlay test", WS_OVERLAPPEDWINDOW,
                                  0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(window, "hidden test window");
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferDesc.Width = 1920; desc.BufferDesc.Height = 1080;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1; desc.OutputWindow = window; desc.Windowed = TRUE;
    Com<IDXGISwapChain> chain;
    CHECK(SUCCEEDED(factory->CreateSwapChain(device.p, &desc, &chain.p)), "hooked swap-chain creation");
    void **nativeTable = *(void ***)chain.p;
    void *nativePresent = nativeTable[8];
    CHECK(SUCCEEDED(clear(chain.p, device.p, context.p)), "clear target");
    visible = FALSE;
    CHECK(SUCCEEDED(testFrame(chain.p, 2, 0)) && presents == 1 && lastInterval == 2 && !lastFlags, "forward Present with original arguments");
    Pixels before;
    CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &before)) && !before.count, "hidden outside title");
    visible = TRUE;
    CHECK(SUCCEEDED(testFrame(chain.p, 0, DXGI_PRESENT_TEST)) && presents == 2 && lastFlags == DXGI_PRESENT_TEST, "forward test-only Present");
    CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &before)) && !before.count, "test-only Present does not draw");
    CHECK(SUCCEEDED(testFrame(chain.p, 0, 0)), "title Present");
    Pixels full;
    CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &full)) && full.count > 100, "independent title label has visible pixels");
    CHECK(full.minX > 1600 && full.maxX < 1900 && full.maxX > 1880 && full.minY >= 84 && full.minY < 105 && full.maxY < 125, "top-right full-HD placement");

    // Sentinel state: the overlay must restore even bindings it never uses.
    D3D11_TEXTURE2D_DESC sentinelDesc = {};
    sentinelDesc.Width = sentinelDesc.Height = 32;
    sentinelDesc.MipLevels = sentinelDesc.ArraySize = sentinelDesc.SampleDesc.Count = 1;
    sentinelDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sentinelDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Com<ID3D11Texture2D> sentinelTexture;
    Com<ID3D11RenderTargetView> sentinelTarget;
    CHECK(SUCCEEDED(device->CreateTexture2D(&sentinelDesc, nullptr, &sentinelTexture.p)) &&
          SUCCEEDED(device->CreateRenderTargetView(sentinelTexture.p, nullptr, &sentinelTarget.p)), "sentinel target");
    D3D11_VIEWPORT viewport = {3, 4, 20, 21, 0.2f, 0.8f};
    D3D11_RECT scissor = {1,2,17,18};
    const FLOAT factors[] = {0.1f,0.2f,0.3f,0.4f};
    context->OMSetRenderTargets(1, &sentinelTarget.p, nullptr);
    context->OMSetBlendState(renderer.blend.p, factors, 0x12345678);
    context->RSSetViewports(1, &viewport); context->RSSetScissorRects(1, &scissor);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    context->VSSetShader(renderer.vs.p, nullptr, 0);
    context->PSSetShaderResources(3, 1, &renderer.text.p);
    context->VSSetSamplers(4, 1, &renderer.sampler.p);
    // A game geometry shader that emits no vertices must not hide our UI.
    const char *geometry = "struct V{float4 p:SV_Position;float2 uv:TEXCOORD;};"
        "[maxvertexcount(3)]void main(triangle V v[3],inout TriangleStream<V> stream){}";
    Com<ID3DBlob> geometryCode;
    Com<ID3D11GeometryShader> gameGeometry;
    CHECK(SUCCEEDED(compileShader(geometry, strlen(geometry), nullptr, nullptr, nullptr,
        "main", "gs_4_0", 0, 0, &geometryCode.p, nullptr)) &&
        SUCCEEDED(device->CreateGeometryShader(geometryCode->GetBufferPointer(),
        geometryCode->GetBufferSize(), nullptr, &gameGeometry.p)), "game geometry-shader fixture");
    context->GSSetShader(gameGeometry.p, nullptr, 0);
    Com<ID3D11Predicate> gamePredicate;
    D3D11_QUERY_DESC predicateDesc = {D3D11_QUERY_OCCLUSION_PREDICATE, 0};
    CHECK(SUCCEEDED(device->CreatePredicate(&predicateDesc, &gamePredicate.p)), "game predication fixture");
    context->SetPredication(gamePredicate.p, FALSE);
    CHECK(SUCCEEDED(clear(chain.p, device.p, context.p)), "clear before pipeline restoration check");
    CHECK(SUCCEEDED(renderer.draw(chain.p)), "draw with existing game state");
    setQualityOverlay(testQuality); qualityVisible = TRUE; visible = FALSE;
    CHECK(SUCCEEDED(drawFrame(chain.p)), "child draws independently of title visibility");
    Com<ID3D11GeometryShader> restoredGeometry;
    context->GSGetShader(&restoredGeometry.p, nullptr, nullptr);
    CHECK(restoredGeometry.p == gameGeometry.p, "game geometry shader restored");
    Com<ID3D11Predicate> restoredPredicate; BOOL predicateValue = TRUE;
    context->GetPredication(&restoredPredicate.p, &predicateValue);
    CHECK(restoredPredicate.p == gamePredicate.p && !predicateValue, "game predicate and comparison restored");
    context->SetPredication(nullptr, FALSE);
    wchar_t preview[MAX_PATH]; swprintf(preview,MAX_PATH,L"%ls\\quality-panel.bmp",argv[2]);
    Pixels pipelinePixels;
    CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &pipelinePixels, preview)) && pipelinePixels.count > 100,
          "label draws despite game geometry shader and predication");
    Com<ID3D11RenderTargetView> gotTarget;
    context->OMGetRenderTargets(1, &gotTarget.p, nullptr);
    CHECK(gotTarget.p == sentinelTarget.p, "render target restored");
    Com<ID3D11BlendState> gotBlend;
    FLOAT gotFactors[4]; UINT mask;
    context->OMGetBlendState(&gotBlend.p, gotFactors, &mask);
    CHECK(gotBlend.p == renderer.blend.p && !memcmp(gotFactors, factors, sizeof(factors)) && mask == 0x12345678, "blend state restored");
    D3D11_VIEWPORT gotViewport; UINT count = 1;
    context->RSGetViewports(&count, &gotViewport);
    CHECK(count == 1 && !memcmp(&gotViewport, &viewport, sizeof(viewport)), "viewport restored");
    D3D11_RECT gotScissor; count = 1;
    context->RSGetScissorRects(&count, &gotScissor);
    CHECK(count == 1 && !memcmp(&gotScissor, &scissor, sizeof(scissor)), "scissor restored");
    D3D11_PRIMITIVE_TOPOLOGY topology;
    context->IAGetPrimitiveTopology(&topology);
    CHECK(topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, "topology restored");
    Com<ID3D11VertexShader> gotVS;
    context->VSGetShader(&gotVS.p, nullptr, nullptr);
    CHECK(gotVS.p == renderer.vs.p, "shader restored");
    Com<ID3D11ShaderResourceView> gotResource;
    context->PSGetShaderResources(3, 1, &gotResource.p);
    CHECK(gotResource.p == renderer.text.p, "unrelated shader resource restored");
    Com<ID3D11SamplerState> gotSampler;
    context->VSGetSamplers(4, 1, &gotSampler.p);
    CHECK(gotSampler.p == renderer.sampler.p, "unrelated sampler restored");
    qualityVisible = FALSE;
    CHECK(SUCCEEDED(clear(chain.p, device.p, context.p)) && SUCCEEDED(drawFrame(chain.p)), "child closed");
    Pixels hiddenQuality;
    CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &hiddenQuality)) && !hiddenQuality.count, "child hides without a menu");
    visible = TRUE;
    context->ClearState();
    CHECK(SUCCEEDED(chain->ResizeBuffers(1, 1280, 720, DXGI_FORMAT_UNKNOWN, 0)), "resize after overlay without retained backbuffer");
    CHECK(SUCCEEDED(clear(chain.p, device.p, context.p)) && SUCCEEDED(renderer.draw(chain.p)), "draw after resize");
    Pixels small;
    CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &small)) && small.count > 50, "resized label visible");
    CHECK(small.minX > 1050 && small.maxX > 1250 && small.maxX < 1275 && small.minY >= 56 && small.maxY < 85, "top-right 720p placement");
    CHECK(full.maxY - full.minY > small.maxY - small.minY, "font scales with resolution");
    CHECK(SUCCEEDED(chain->ResizeBuffers(1, 2560, 1080, DXGI_FORMAT_UNKNOWN, 0)), "ultrawide resize");
    CHECK(SUCCEEDED(clear(chain.p, device.p, context.p)) && SUCCEEDED(renderer.draw(chain.p)), "ultrawide draw");
    Pixels wide;
    CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &wide)) && wide.minX == full.minX + 320 && wide.maxX == full.maxX + 320 && wide.minY == full.minY && wide.maxY == full.maxY, "ultrawide centered 16:9 alignment");
    CHECK(wide.maxY - wide.minY == full.maxY - full.minY, "ultrawide preserves font size");
    struct Screen { UINT width, height; float scale, left, top; };
    const Screen screens[] = {
        {2560, 1440, 4.0f / 3.0f, 0, 0},
        {3440, 1440, 4.0f / 3.0f, 440, 0},
        {3840, 2160, 2, 0, 0},
        {5120, 1440, 4.0f / 3.0f, 1280, 0},
        {1920, 1440, 1, 0, 180}
    };
    for (const Screen &screen : screens) {
        CHECK(SUCCEEDED(chain->ResizeBuffers(1, screen.width, screen.height, DXGI_FORMAT_UNKNOWN, 0)), "resolution resize");
        qualityVisible = TRUE;
        CHECK(SUCCEEDED(clear(chain.p, device.p, context.p)) && SUCCEEDED(drawFrame(chain.p)), "quality panel resolution draw");
        Pixels panel;
        CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &panel)) && panel.count > 1000 &&
            panel.minX > 0 && panel.maxX < screen.width && panel.minY > 0 && panel.maxY < screen.height, "quality panel stays inside screen");
        qualityVisible = FALSE;
        CHECK(SUCCEEDED(clear(chain.p, device.p, context.p)) && SUCCEEDED(renderer.draw(chain.p)), "resolution draw");
        Pixels scaled;
        CHECK(SUCCEEDED(pixels(chain.p, device.p, context.p, &scaled)) && scaled.count > 100, "scaled label visible");
        CHECK(fabsf(((float)scaled.maxX - screen.left) / screen.scale - (float)full.maxX) <= 2 &&
              fabsf(((float)scaled.minY - screen.top) / screen.scale - (float)full.minY) <= 2,
              "same relative position within centered 16:9 area across resolutions");
    }
    renderer.reset();
    CHECK(SUCCEEDED(renderer.draw(chain.p)), "recreate renderer after device reset");
    CHECK(*(void ***)chain.p == nativeTable && nativeTable[8] == nativePresent, "all native graphics dispatch remains unchanged");
    renderer.reset();
    context->ClearState();
    CHECK(SUCCEEDED(chain->ResizeBuffers(1,1920,1080,DXGI_FORMAT_UNKNOWN,0)),"preview resolution");
    {
        Com<ID3D11Texture2D> backbuffer; Com<ID3D11RenderTargetView> background;
        CHECK(SUCCEEDED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void **)&backbuffer.p)) &&
            SUCCEEDED(device->CreateRenderTargetView(backbuffer.p,nullptr,&background.p)),"preview background");
        const FLOAT color[]={0.18f,0.20f,0.22f,1}; context->ClearRenderTargetView(background.p,color);
    }
    qualityVisible=TRUE; visible=FALSE;
    CHECK(SUCCEEDED(drawFrame(chain.p)),"reference panel preview");
    CHECK(qualityRenderer.textWidth==27*26 && qualityRenderer.textHeight==22*26,"exact reference panel dimensions at 1080p");
    Pixels previewPixels;
    CHECK(SUCCEEDED(pixels(chain.p,device.p,context.p,&previewPixels,preview)),"export isolated reference-style preview");
    CHECK(previewPixels.goldCount>100 && abs((int)(previewPixels.goldMinX+previewPixels.goldMaxX)-1920)<=4,"gold headline is centered horizontally");
    qualityRenderer.reset(); setQualityOverlay(nullptr);
    chain.reset(); DestroyWindow(window);
    wchar_t fixturePath[MAX_PATH];
    swprintf(fixturePath, MAX_PATH, L"%ls\\VERSION.txt", argv[2]);
    DeleteFileW(fixturePath);
    puts("PASS: independent title rendering, original pixel placement, unchanged native dispatch, state restoration, resizing and recreation.");
    return 0;
}
