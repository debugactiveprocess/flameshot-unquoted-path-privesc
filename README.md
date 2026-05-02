# Flameshot Unquoted Path Privilege Escalation

**Severity:** HIGH (CVSS ~7.8)  
**Affected:** Flameshot ≤12.1.0 on Windows  
**Technique:** Unquoted Registry Path (T1574.009) → FodHelper UAC Bypass (T1548.002)

## Summary

Flameshot writes its executable path to the Windows `Run` registry key without quotes:

HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run Flameshot = C:\Program Files\Flameshot\bin\flameshot.exe


When the default installation path contains spaces, Windows tries space-delimited prefixes at logon:
1. `C:\Program.exe`
2. `C:\Program Files\Flameshot.exe`
3. `C:\Program Files\Flameshot\bin\flameshot.exe`

A low-privilege attacker can drop `C:\Program.exe` and hijack logon execution. This PoC then silently elevates to Administrator via the `fodhelper.exe` UAC bypass.

## Proof of Concept

### Deploy Payload
```cmd
cl.exe silent_elevate.c shell32.lib advapi32.lib /link /subsystem:windows
copy silent_elevate.exe "C:\Program.exe"

