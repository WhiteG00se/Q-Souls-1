// Allowlisted read-only asset overrides, outside the game's junction-backed tree.
static wchar_t respecOverrideRoot[MAX_PATH];
static wchar_t respecOverrideNames[64][128];
static size_t respecOverrideCount;

static BOOL respecAssetName(const wchar_t *name)
{
    size_t n = wcslen(name);
    if (!n || n >= 128 || wcsstr(name, L"..") || wcschr(name, L':') || wcschr(name, L'/')) return FALSE;
    // Exact arena assets only; never expose an unrestricted map/event override.
    static const wchar_t *hellAssets[] = {
        L"map\\MapStudio\\m10_02_00_00.msb", L"map\\MapStudio\\m14_01_00_00.msb",
        L"map\\m14_01_00_00\\m9900B1A14.flver.dcx",
        L"map\\m14_01_00_00\\m9901B1A14.flver.dcx", L"map\\m14\\m14_9999.tpf.dcx",
        L"map\\m14\\GI_EnvM_m14.tpfbhd", L"map\\m14\\GI_EnvM_m14.tpfbdt",
        L"param\\DrawParam\\a14_DrawParam.parambnd.dcx",
        L"map\\m14_01_00_00\\m14_01_00_00.nvmbnd.dcx",
        L"map\\m14_01_00_00\\m14_01_00_00.mcp", L"map\\m14_01_00_00\\m14_01_00_00.mcg",
        L"map\\m14_01_00_00\\h14_01_00_00.hkxbhd", L"map\\m14_01_00_00\\h14_01_00_00.hkxbdt",
        L"map\\m14_01_00_00\\l14_01_00_00.hkxbhd", L"map\\m14_01_00_00\\l14_01_00_00.hkxbdt",
        L"event\\m10_02_00_00.emevd.dcx", L"event\\m14_01_00_00.emevd.dcx"
    };
    for (size_t i = 0; i < sizeof(hellAssets) / sizeof(*hellAssets); ++i)
        if (!_wcsicmp(name, hellAssets[i])) return TRUE;
    if (!_wcsnicmp(name, L"script\\talk\\", 12)) {
        const wchar_t *file = name + 12;
        return !wcschr(file, L'\\') && n > 27 && !_wcsicmp(name + n - 15, L".talkesdbnd.dcx");
    }
    if (!_wcsicmp(name, L"menu\\menu.drb") || !_wcsicmp(name, L"menu\\menu.drb.dcx")) return TRUE;
    if (!_wcsnicmp(name, L"msg\\", 4)) {
        const wchar_t *slash = wcschr(name + 4, L'\\');
        return slash && slash > name + 4 && (!_wcsicmp(slash, L"\\menu.msgbnd.dcx") || !_wcsicmp(slash, L"\\item.msgbnd.dcx"));
    }
    return FALSE;
}
static inline DWORD loadRespecOverrides(const wchar_t *root)
{
    wchar_t manifest[MAX_PATH];
    if (swprintf(respecOverrideRoot, MAX_PATH, L"%ls\\.local\\respec", root) < 0 ||
        swprintf(manifest, MAX_PATH, L"%ls\\overrides.txt", respecOverrideRoot) < 0) return ERROR_BUFFER_OVERFLOW;
    FILE *file = _wfopen(manifest, L"rb");
    if (!file) return ERROR_FILE_NOT_FOUND;
    respecOverrideCount = 0;
    char line[256]; DWORD error = ERROR_SUCCESS;
    while (fgets(line, sizeof(line), file)) {
        size_t n = strcspn(line, "\r\n");
        if (!n || n >= 128 || respecOverrideCount == 64) { error = ERROR_INVALID_DATA; break; }
        line[n] = 0;
        wchar_t *name = respecOverrideNames[respecOverrideCount];
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line, -1, name, 128) || !respecAssetName(name)) { error = ERROR_INVALID_DATA; break; }
        wchar_t path[MAX_PATH];
        if (swprintf(path, MAX_PATH, L"%ls\\%ls", respecOverrideRoot, name) < 0) { error = ERROR_BUFFER_OVERFLOW; break; }
        DWORD attrs = GetFileAttributesW(path);
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) { error = ERROR_FILE_NOT_FOUND; break; }
        ++respecOverrideCount;
    }
    if (ferror(file)) error = ERROR_READ_FAULT;
    fclose(file);
    return error ? error : respecOverrideCount ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
static BOOL mapRespecAssetW(const wchar_t *path, wchar_t output[MAX_PATH])
{
    if (!path) return FALSE;
    size_t length = wcslen(path);
    for (size_t i = 0; i < respecOverrideCount; ++i) {
        const wchar_t *name = respecOverrideNames[i]; size_t n = wcslen(name);
        if (length < n || (length > n && path[length - n - 1] != L'\\' && path[length - n - 1] != L'/')) continue;
        size_t j;
        for (j = 0; j < n; ++j) {
            wchar_t ch = path[length - n + j]; if (ch == L'/') ch = L'\\';
            if (towlower(ch) != towlower(name[j])) break;
        }
        if (j == n) return swprintf(output, MAX_PATH, L"%ls\\%ls", respecOverrideRoot, name) >= 0;
    }
    return FALSE;
}
static BOOL mapRespecAssetA(const char *path, wchar_t output[MAX_PATH])
{
    wchar_t wide[1024];
    return path && MultiByteToWideChar(CP_ACP, 0, path, -1, wide, 1024) && mapRespecAssetW(wide, output);
}
