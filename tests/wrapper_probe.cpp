// A Qt-free fixture for testing the self-extracting launcher without touching user data.
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    wchar_t path[32768];
    if (!GetModuleFileNameW(nullptr, path, 32768)) return 1;
    wchar_t *filename = path;
    for (wchar_t *p = path; *p; ++p)
        if (*p == L'\\') filename = p + 1;
    lstrcpyW(filename, L"launches.txt");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 2;
    DWORD written = 0;
    const bool ok = WriteFile(file, "launch\r\n", 8, &written, nullptr) && written == 8;
    CloseHandle(file);
    return ok ? 0 : 3;
}
