// Redirect only the starting-class archive, including absolute paths built by the game.
// Included after the runtime's import helpers; never changes the installed archive.
static wchar_t parameterPath[MAX_PATH];
static HANDLE (WINAPI *parameterCreateW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE (WINAPI *parameterCreateA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static DWORD (WINAPI *parameterAttributesW)(LPCWSTR);
static volatile LONG parameterOpened;

static BOOL parameterNameW(const wchar_t *path)
{
    const wchar_t suffix[] = L"param\\GameParam\\GameParam.parambnd.dcx";
    if (!path) return FALSE;
    size_t size = wcslen(path), length = sizeof(suffix) / sizeof(*suffix) - 1;
    if (size < length || (size > length && path[size - length - 1] != L'\\' &&
                         path[size - length - 1] != L'/')) return FALSE;
    path += size - length;
    for (size_t i = 0; i < length; ++i) {
        wchar_t value = path[i] == L'/' ? L'\\' : path[i];
        if (towlower(value) != towlower(suffix[i])) return FALSE;
    }
    return TRUE;
}

static BOOL parameterNameA(const char *path)
{
    // The matched suffix is ASCII; no conversion of the installation path is needed.
    if (!path) return FALSE;
    const char suffix[] = "param\\GameParam\\GameParam.parambnd.dcx";
    size_t size = strlen(path), length = sizeof(suffix) - 1;
    if (size < length || (size > length && path[size - length - 1] != '\\' &&
                         path[size - length - 1] != '/')) return FALSE;
    path += size - length;
    for (size_t i = 0; i < length; ++i) {
        char value = path[i] == '/' ? '\\' : path[i];
        if (tolower((unsigned char)value) != tolower((unsigned char)suffix[i])) return FALSE;
    }
    return TRUE;
}

static HANDLE parameterResult(HANDLE result)
{
    DWORD error = GetLastError();
    if (result != INVALID_HANDLE_VALUE && !InterlockedExchange(&parameterOpened, 1))
        logMessage("Starting-class parameter override opened by the game.");
    if (result == INVALID_HANDLE_VALUE)
        logMessage("Starting-class parameter override could not be opened.");
    SetLastError(error);
    return result;
}

static HANDLE WINAPI openParameterW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                    DWORD disposition, DWORD flags, HANDLE templateFile)
{
    if (parameterNameW(path) && disposition == OPEN_EXISTING &&
        !(access & (GENERIC_WRITE | GENERIC_ALL | DELETE | FILE_WRITE_DATA | FILE_APPEND_DATA)))
        return parameterResult(parameterCreateW(parameterPath, access, share, security, disposition, flags, templateFile));
    return parameterCreateW(path, access, share, security, disposition, flags, templateFile);
}

static HANDLE WINAPI openParameterA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                                    DWORD disposition, DWORD flags, HANDLE templateFile)
{
    if (parameterNameA(path) && disposition == OPEN_EXISTING &&
        !(access & (GENERIC_WRITE | GENERIC_ALL | DELETE | FILE_WRITE_DATA | FILE_APPEND_DATA)))
        return parameterResult(parameterCreateW(parameterPath, access, share, security, disposition, flags, templateFile));
    return parameterCreateA(path, access, share, security, disposition, flags, templateFile);
}

static DWORD WINAPI attributesParameterW(LPCWSTR path)
{
    return parameterAttributesW(parameterNameW(path) ? parameterPath : path);
}

static DWORD initializeParamOverride(const wchar_t *root)
{
    if (swprintf(parameterPath, MAX_PATH, L"%ls\\.local\\game\\param\\GameParam\\GameParam.parambnd.dcx", root) < 0)
        return ERROR_BUFFER_OVERFLOW;
    DWORD attributes = GetFileAttributesW(parameterPath);
    if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return ERROR_DIRECTORY;
    void **wide = findImport("KERNEL32.dll", "CreateFileW");
    void **ansi = findImport("KERNEL32.dll", "CreateFileA");
    void **info = findImport("KERNEL32.dll", "GetFileAttributesW");
    if (!wide || !ansi || !info) return ERROR_PROC_NOT_FOUND;
    parameterCreateW = (void *)*wide;
    parameterCreateA = (void *)*ansi;
    parameterAttributesW = (void *)*info;
    if (!replaceImport(wide, (void *)openParameterW) || !replaceImport(ansi, (void *)openParameterA) ||
        !replaceImport(info, (void *)attributesParameterW)) return ERROR_WRITE_FAULT;
    logMessage("Starting-class parameter file redirection ready.");
    return ERROR_SUCCESS;
}
