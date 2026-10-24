// Route native .sl2 and Seamless .co2 save I/O to the launcher selection.
#define SAVE_PATH_CAPACITY 1024
static wchar_t selectedSaveExtension[121], selectedSaveRoot[SAVE_PATH_CAPACITY];
static BOOL seamlessSavePaths;
static HANDLE (WINAPI *saveCreateW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE (WINAPI *saveCreateA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static DWORD (WINAPI *saveAttributesW)(LPCWSTR);
static BOOL (WINAPI *saveDeleteW)(LPCWSTR), (WINAPI *saveDeleteA)(LPCSTR);
static BOOL (WINAPI *saveMoveW)(LPCWSTR, LPCWSTR), (WINAPI *saveMoveA)(LPCSTR, LPCSTR);
static HANDLE (WINAPI *saveFindW)(LPCWSTR, LPWIN32_FIND_DATAW);

// 0 = unrelated, 1 = mapped, -1 = target save path could not be represented safely.
static int mapSaveW(const wchar_t *path, wchar_t output[SAVE_PATH_CAPACITY])
{
    if (!path) return 0;
    const wchar_t *name = path;
    for (const wchar_t *p = path; *p; ++p) if (*p == L'\\' || *p == L'/') name = p + 1;
    const wchar_t *extension = NULL;
    if (!_wcsicmp(name, L"*.sl2") || (seamlessSavePaths && !_wcsicmp(name, L"*.co2"))) extension = name + 1;
    else if (!_wcsnicmp(name, L"DRAKS", 5)) {
        const wchar_t *p = name + 5;
        while ((*p >= L'0' && *p <= L'9') || *p == L'*' || *p == L'?') ++p;
        if (p > name + 5 && (!_wcsnicmp(p, L".sl2", 4) || (seamlessSavePaths && !_wcsnicmp(p, L".co2", 4))) && (!p[4] || p[4] == L'.')) extension = p;
    }
    if (!extension) return 0;
    wchar_t absolute[SAVE_PATH_CAPACITY];
    DWORD length = GetFullPathNameW(path, SAVE_PATH_CAPACITY, absolute, NULL);
    if (!length || length >= SAVE_PATH_CAPACITY) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return -1; }
    for (wchar_t *p = absolute; *p; ++p) if (*p == L'/') *p = L'\\';
    size_t rootLength = wcslen(selectedSaveRoot);
    if (_wcsnicmp(absolute, selectedSaveRoot, rootLength) || absolute[rootLength] != L'\\') return 0;
    // Absolute conversion changes the directory prefix, but never the final filename.
    wchar_t *absoluteName = wcsrchr(absolute, L'\\');
    if (!absoluteName) return 0;
    absoluteName++;
    size_t before = (size_t)(absoluteName - absolute) + (size_t)(extension - name);
    const wchar_t *tail = absolute + before + 4;
    size_t selected = wcslen(selectedSaveExtension), trailing = wcslen(tail);
    if (before + 1 + selected + trailing >= SAVE_PATH_CAPACITY) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return -1; }
    wmemcpy(output, absolute, before);
    output[before] = L'.';
    wmemcpy(output + before + 1, selectedSaveExtension, selected);
    wcscpy(output + before + 1 + selected, tail);
    return 1;
}
static int mapSaveA(const char *path, wchar_t output[SAVE_PATH_CAPACITY])
{
    if (!path) return 0;
    wchar_t wide[SAVE_PATH_CAPACITY];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wide, SAVE_PATH_CAPACITY)) return -1;
    return mapSaveW(wide, output);
}
static HANDLE WINAPI saveOpenW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE templateFile)
{
    wchar_t mapped[SAVE_PATH_CAPACITY]; int result = mapSaveW(path, mapped);
    if (result < 0) return INVALID_HANDLE_VALUE;
    return saveCreateW(result ? mapped : path, access, share, security, creation, flags, templateFile);
}
static HANDLE WINAPI saveOpenA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE templateFile)
{
    wchar_t mapped[SAVE_PATH_CAPACITY]; int result = mapSaveA(path, mapped);
    if (result < 0) return INVALID_HANDLE_VALUE;
    return result ? saveCreateW(mapped, access, share, security, creation, flags, templateFile) : saveCreateA(path, access, share, security, creation, flags, templateFile);
}
static DWORD WINAPI saveInfoW(LPCWSTR path)
{
    wchar_t mapped[SAVE_PATH_CAPACITY]; int result = mapSaveW(path, mapped);
    return result < 0 ? INVALID_FILE_ATTRIBUTES : saveAttributesW(result ? mapped : path);
}
static BOOL WINAPI saveRemoveW(LPCWSTR path)
{
    wchar_t mapped[SAVE_PATH_CAPACITY]; int result = mapSaveW(path, mapped);
    return result < 0 ? FALSE : saveDeleteW(result ? mapped : path);
}
static BOOL WINAPI saveRemoveA(LPCSTR path)
{
    wchar_t mapped[SAVE_PATH_CAPACITY]; int result = mapSaveA(path, mapped);
    return result < 0 ? FALSE : result ? saveDeleteW(mapped) : saveDeleteA(path);
}
static BOOL WINAPI saveRenameW(LPCWSTR from, LPCWSTR to)
{
    wchar_t source[SAVE_PATH_CAPACITY], destination[SAVE_PATH_CAPACITY];
    int a = mapSaveW(from, source), b = mapSaveW(to, destination);
    return a < 0 || b < 0 ? FALSE : saveMoveW(a ? source : from, b ? destination : to);
}
static BOOL WINAPI saveRenameA(LPCSTR from, LPCSTR to)
{
    wchar_t source[SAVE_PATH_CAPACITY], destination[SAVE_PATH_CAPACITY];
    int a = mapSaveA(from, source), b = mapSaveA(to, destination);
    if (a < 0 || b < 0) return FALSE;
    if (!a && !b) return saveMoveA(from, to);
    if (!a && !MultiByteToWideChar(CP_ACP, 0, from, -1, source, SAVE_PATH_CAPACITY)) return FALSE;
    if (!b && !MultiByteToWideChar(CP_ACP, 0, to, -1, destination, SAVE_PATH_CAPACITY)) return FALSE;
    return saveMoveW(source, destination);
}
static HANDLE WINAPI saveSearchW(LPCWSTR path, LPWIN32_FIND_DATAW data)
{
    wchar_t mapped[SAVE_PATH_CAPACITY]; int result = mapSaveW(path, mapped);
    return result < 0 ? INVALID_HANDLE_VALUE : saveFindW(result ? mapped : path, data);
}
static DWORD initializeSaveExtension(void)
{
    DWORD length = GetEnvironmentVariableW(L"QSOULS_SAVE_EXTENSION", selectedSaveExtension, 121);
    if (!length) return ERROR_SUCCESS;
    if (length > 120) return ERROR_INVALID_PARAMETER;
    for (DWORD i = 0; i < length; ++i) {
        wchar_t c = selectedSaveExtension[i];
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))) return ERROR_INVALID_PARAMETER;
    }
    wchar_t seamless[2];
    seamlessSavePaths = GetEnvironmentVariableW(L"QSOULS_SEAMLESS", seamless, 2) == 1 && seamless[0] == L'1';
    if (!seamlessSavePaths && !_wcsicmp(selectedSaveExtension, L"sl2")) return ERROR_SUCCESS;
    wchar_t root[SAVE_PATH_CAPACITY];
    length = GetEnvironmentVariableW(L"QSOULS_SAVE_ROOT", root, SAVE_PATH_CAPACITY);
    if (!length || length >= SAVE_PATH_CAPACITY) return ERROR_INVALID_PARAMETER;
    length = GetFullPathNameW(root, SAVE_PATH_CAPACITY, selectedSaveRoot, NULL);
    if (!length || length >= SAVE_PATH_CAPACITY) return ERROR_FILENAME_EXCED_RANGE;
    for (wchar_t *p = selectedSaveRoot; *p; ++p) if (*p == L'/') *p = L'\\';
    while (length > 3 && selectedSaveRoot[length - 1] == L'\\') selectedSaveRoot[--length] = 0;
    const char *names[] = {"CreateFileW", "CreateFileA", "GetFileAttributesW", "DeleteFileW", "DeleteFileA", "MoveFileW", "MoveFileA", "FindFirstFileW"};
    void *hooks[] = {(void *)saveOpenW, (void *)saveOpenA, (void *)saveInfoW, (void *)saveRemoveW, (void *)saveRemoveA, (void *)saveRenameW, (void *)saveRenameA, (void *)saveSearchW};
    void **slots[8];
    for (size_t i = 0; i < 8; ++i) { slots[i] = findImport("KERNEL32.dll", names[i]); if (!slots[i]) return ERROR_PROC_NOT_FOUND; }
    saveCreateW = (void *)*slots[0]; saveCreateA = (void *)*slots[1]; saveAttributesW = (void *)*slots[2];
    saveDeleteW = (void *)*slots[3]; saveDeleteA = (void *)*slots[4]; saveMoveW = (void *)*slots[5]; saveMoveA = (void *)*slots[6]; saveFindW = (void *)*slots[7];
    for (size_t i = 0; i < 8; ++i) if (!replaceImport(slots[i], hooks[i])) return ERROR_WRITE_FAULT;
    logMessage("Launcher-selected save extension routing is active.");
    return ERROR_SUCCESS;
}
