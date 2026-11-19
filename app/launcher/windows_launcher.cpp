// The Windows package's z3d.exe: starts python\pythonw.exe -I -m z3dmods.packed play from the
// folder it sits in, which opens the title picker, and returns its exit status.
#include <string>

#include <windows.h>

namespace {

// The folder this program is in, with its trailing backslash.
std::wstring ownFolder() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length < path.size()) {
      path.resize(length);
      break;
    }
    path.resize(path.size() * 2);
  }
  return path.substr(0, path.find_last_of(L'\\') + 1);
}

} // namespace

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, PWSTR /*arguments*/, int /*show*/) {
  const std::wstring folder = ownFolder();
  std::wstring command = L"\"" + folder + L"python\\pythonw.exe\" -I -m z3dmods.packed play";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup,
                     &process) == 0) {
    MessageBoxW(nullptr, L"z3d could not start its bundled Python (python\\pythonw.exe).", L"z3d", MB_ICONERROR);
    return 1;
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD status = 1;
  GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<int>(status);
}
