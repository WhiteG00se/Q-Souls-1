// Optional integration check against running RTSS. Shows a small test window;
// never loads the game, sends input, or writes RTSS settings/shared memory.
#include "../runtime/TitleOverlay.cpp"

static int rtssD3D11Frames()
{
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"RTSSSharedMemoryV2");
    if (!mapping) return -1;
    BYTE *data = (BYTE *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    int frames = -1;
    if (data) {
        MEMORY_BASIC_INFORMATION memory = {};
        VirtualQuery(data, &memory, sizeof(memory));
        auto header = (const DWORD *)data;
        if (memory.RegionSize >= 32 && header[0] == 0x52545353 && (header[1] >> 16) == 2 &&
            header[2] >= 284 && header[3] <= memory.RegionSize &&
            header[4] <= (memory.RegionSize - header[3]) / header[2]) {
            for (DWORD i = 0; i < header[4]; ++i) {
                const BYTE *app = data + header[3] + (size_t)i * header[2];
                if (*(const DWORD *)app != GetCurrentProcessId()) continue;
                DWORD flags = *(const DWORD *)(app + 264);
                DWORD count = *(const DWORD *)(app + 276);
                printf("RTSS API=%lu (D3D11=7), measured frames=%lu\n",
                       (unsigned long)(flags & 0xffff), (unsigned long)count);
                if ((flags & 0xffff) == 7 && count) frames = (int)count;
                break;
            }
        }
        UnmapViewOfFile(data);
    }
    CloseHandle(mapping);
    return frames;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2 || readVersion(argv[1])) {
        fputs("Usage: Test-RTSS.exe <repository>\n", stderr); return 1;
    }
    HANDLE server = OpenFileMappingW(FILE_MAP_READ, FALSE, L"RTSSSharedMemoryV2");
    if (!server) { fputs("Start RTSS before running this optional check.\n", stderr); return 1; }
    CloseHandle(server);
    HMODULE compiler = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!compiler) return 1;
    compileShader = (decltype(compileShader))(void *)GetProcAddress(compiler, "D3DCompile");
    if (!compileShader) return 1;
    HWND window = CreateWindowExW(0, L"STATIC", L"Q Souls isolated RTSS compatibility check",
        WS_OVERLAPPEDWINDOW, 30, 30, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 1;
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferDesc.Width = 640; desc.BufferDesc.Height = 480;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1; desc.OutputWindow = window; desc.Windowed = TRUE;
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<IDXGISwapChain> chain;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain.p, &device.p, nullptr, &context.p);
    if (FAILED(hr)) { DestroyWindow(window); return 1; }
    ShowWindow(window, SW_SHOWNOACTIVATE);
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < 24000) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (!IsWindow(window)) break;
        {
            Com<ID3D11Texture2D> buffer;
            Com<ID3D11RenderTargetView> target;
            hr = chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer.p);
            if (FAILED(hr)) break;
            hr = device->CreateRenderTargetView(buffer.p, nullptr, &target.p);
            if (FAILED(hr)) break;
            const FLOAT black[4] = {0, 0, 0, 1};
            context->ClearRenderTargetView(target.p, black);
        }
        hr = renderer.draw(chain.p);
        if (FAILED(hr)) break;
        hr = chain->Present(0, 0);
        if (FAILED(hr)) break;
        Sleep(16);
    }
    bool passed = SUCCEEDED(hr) && rtssD3D11Frames() > 0;
    renderer.reset(); chain.reset(); DestroyWindow(window);
    puts(passed ? "PASS: real RTSS measures D3D11 frames with the mod renderer active. In-game F12 still requires user testing."
                : "FAIL: RTSS did not measure D3D11 frames. Keep the test window unobscured; allow its injection delay to expire.");
    return passed ? 0 : 1;
}
