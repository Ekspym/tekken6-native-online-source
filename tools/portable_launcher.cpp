// Portable Windows test launcher. The actual ReXGlue host lives in bin/.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

static int Fail(const std::wstring& message, DWORD error = GetLastError()) {
  const auto text = message + L"\n\nWindows error: " + std::to_wstring(error);
  MessageBoxW(nullptr, text.c_str(), L"Tekken 6 test", MB_OK | MB_ICONERROR);
  return 1;
}

static bool Directory(const std::wstring& path) {
  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::string Utf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), size, nullptr,
                      nullptr);
  return result;
}

static bool AppendText(const std::wstring& path, const std::string& text, bool create_new = false) {
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                            create_new ? CREATE_NEW : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr) &&
                  written == text.size();
  CloseHandle(file);
  return ok;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  std::vector<wchar_t> module(32768);
  const DWORD length = GetModuleFileNameW(nullptr, module.data(), DWORD(module.size()));
  if (!length || length >= module.size()) return Fail(L"Cannot locate the test folder.");
  const std::wstring executable(module.data(), length);
  const auto root = executable.substr(0, executable.find_last_of(L"\\/"));
  const auto host = root + L"\\bin\\tekken6.exe";
  const auto game = root + L"\\game";
  if (GetFileAttributesW(host.c_str()) == INVALID_FILE_ATTRIBUTES ||
      GetFileAttributesW((game + L"\\default.xex").c_str()) == INVALID_FILE_ATTRIBUTES) {
    return Fail(L"Game or runtime files are missing. Copy the entire Tekken6-Windows-Test folder.",
                ERROR_FILE_NOT_FOUND);
  }

  SYSTEMTIME now;
  GetLocalTime(&now);
  wchar_t run_name[96];
  swprintf_s(run_name, L"%04u-%02u-%02u_%02u-%02u-%02u_%lu", now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
  const auto logs = root + L"\\logs";
  const auto run = logs + L"\\" + run_name;
  if (!Directory(logs) || !Directory(run) || !Directory(root + L"\\user")) {
    return Fail(L"Cannot write diagnostics. Put the test folder somewhere writable.");
  }

  // Child processes inherit these through the Windows environment, including
  // the independently linked runtime DLL's C runtime getenv implementation.
  const struct { const wchar_t* name; const wchar_t* value; } settings[] = {
      {L"TEKKEN6_YIELD_STARTUP_SPIN", L"1"},
      {L"TEKKEN6_FRAME_PROBE_ATTEMPTS", L"0"},
#if !defined(TEKKEN6_LOW_OVERHEAD)
      {L"TEKKEN6_TRACE_OUTPUT", L"1"},
      {L"TEKKEN6_TRACE_RENDERING", L"1"},
      {L"TEKKEN6_TRACE_RENDERING_DETAIL", L"1"},
      {L"TEKKEN6_TRACE_RENDERING_DETAIL_INTERVAL", L"60"},
#endif
      {L"REX_GPU_VSYNC", L"0"},
      {L"REX_FULLSCREEN", L"0"},
      {L"REX_LOG_LEVEL", L"info"},
  };
  for (const auto& setting : settings) {
    if (!SetEnvironmentVariableW(setting.name, setting.value)) {
      return Fail(L"Cannot configure the game process.");
    }
  }
  const wchar_t* disabled_settings[] = {
      L"TEKKEN6_CAPTURE_ALL", L"TEKKEN6_CAPTURE_CONTINUOUS", L"TEKKEN6_TRACE_IO",
      L"TEKKEN6_TRACE_QUEUE", L"TEKKEN6_TRACE_MOVIE", L"TEKKEN6_TRACE_TITLE_STATE",
      L"TEKKEN6_TRACE_STARTUP_GATE",
#if defined(TEKKEN6_LOW_OVERHEAD)
      L"TEKKEN6_TRACE_OUTPUT", L"TEKKEN6_TRACE_RENDERING",
      L"TEKKEN6_TRACE_RENDERING_DETAIL", L"TEKKEN6_TRACE_RENDERING_DETAIL_INTERVAL",
#endif
  };
  for (const auto* setting : disabled_settings) {
    if (!SetEnvironmentVariableW(setting, nullptr)) {
      return Fail(L"Cannot disable unneeded diagnostics.");
    }
  }

  auto command = L"\"" + host + L"\" --game_data_root=\"" + game +
      L"\" --user_data_root=\"" + root + L"\\user\" --cache_root=\"" + run +
      L"\\cache\" --log_file=\"" + run + L"\\tekken6.log\" --fullscreen=false";
#if defined(TEKKEN6_LOW_OVERHEAD)
  command += L" --async_shader_compilation=true --render_target_path_d3d12=rov"
             L" --anisotropic_override=0 --native_2x_msaa=false";
  const wchar_t* variant = L"ROV, anisotropic filtering off, native 2x MSAA off; tracing disabled";
  const wchar_t* launch_title = L"Tekken 6 low overhead launch";
#elif defined(TEKKEN6_ASYNC_SHADER_COMPILATION_OFF)
  command += L" --async_shader_compilation=false";
  const wchar_t* variant = L"async_shader_compilation=false";
  const wchar_t* launch_title = L"Tekken 6 diagnostic launch";
#elif defined(TEKKEN6_ALLOW_INVALID_FETCH_CONSTANTS)
  command += L" --async_shader_compilation=true --gpu_allow_invalid_fetch_constants=true";
  const wchar_t* variant = L"gpu_allow_invalid_fetch_constants=true (async compilation on)";
  const wchar_t* launch_title = L"Tekken 6 diagnostic launch";
#elif defined(TEKKEN6_D3D12_ROV)
  command += L" --async_shader_compilation=true --render_target_path_d3d12=rov";
  const wchar_t* variant = L"render_target_path_d3d12=rov (async compilation on)";
  const wchar_t* launch_title = L"Tekken 6 diagnostic launch";
#elif defined(TEKKEN6_READBACK_RESOLVE_FULL)
  command += L" --async_shader_compilation=true --readback_resolve=full";
  const wchar_t* variant = L"readback_resolve=full (async compilation on)";
  const wchar_t* launch_title = L"Tekken 6 diagnostic launch";
#else
  command += L" --async_shader_compilation=true";
  const wchar_t* variant = L"async_shader_compilation=true (default behavior)";
  const wchar_t* launch_title = L"Tekken 6 diagnostic launch";
#endif
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(0);

  std::wstring launcher_name = executable.substr(executable.find_last_of(L"\\/") + 1);
#if defined(TEKKEN6_LOW_OVERHEAD)
  std::wstring header = std::wstring(launch_title) + L"\r\nlauncher: " + launcher_name +
      L"\r\nsettings: " + variant + L"\r\narguments: " + command +
      L"\r\nset environment:\r\n"
      L"  TEKKEN6_YIELD_STARTUP_SPIN=1\r\n"
      L"  TEKKEN6_FRAME_PROBE_ATTEMPTS=0\r\n"
      L"  REX_GPU_VSYNC=0\r\n  REX_FULLSCREEN=0\r\n  REX_LOG_LEVEL=info\r\n"
      L"cleared environment:\r\n"
      L"  TEKKEN6_TRACE_OUTPUT, TEKKEN6_TRACE_RENDERING,\r\n"
      L"  TEKKEN6_TRACE_RENDERING_DETAIL, TEKKEN6_TRACE_RENDERING_DETAIL_INTERVAL,\r\n"
      L"  TEKKEN6_CAPTURE_ALL, TEKKEN6_CAPTURE_CONTINUOUS, TEKKEN6_TRACE_IO,\r\n"
      L"  TEKKEN6_TRACE_QUEUE, TEKKEN6_TRACE_MOVIE, TEKKEN6_TRACE_TITLE_STATE,\r\n"
      L"  TEKKEN6_TRACE_STARTUP_GATE\r\n\r\n";
#else
  std::wstring header = std::wstring(launch_title) + L"\r\nlauncher: " + launcher_name +
      L"\r\nshader compilation: " + variant + L"\r\narguments: " + command +
      L"\r\nset environment:\r\n"
      L"  TEKKEN6_YIELD_STARTUP_SPIN=1\r\n"
      L"  TEKKEN6_FRAME_PROBE_ATTEMPTS=0\r\n"
      L"  TEKKEN6_TRACE_OUTPUT=1\r\n"
      L"  TEKKEN6_TRACE_RENDERING=1\r\n"
      L"  TEKKEN6_TRACE_RENDERING_DETAIL=1\r\n"
      L"  TEKKEN6_TRACE_RENDERING_DETAIL_INTERVAL=60\r\n"
      L"  REX_GPU_VSYNC=0\r\n  REX_FULLSCREEN=0\r\n  REX_LOG_LEVEL=info\r\n"
      L"cleared environment:\r\n"
      L"  TEKKEN6_CAPTURE_ALL, TEKKEN6_CAPTURE_CONTINUOUS, TEKKEN6_TRACE_IO,\r\n"
      L"  TEKKEN6_TRACE_QUEUE, TEKKEN6_TRACE_MOVIE, TEKKEN6_TRACE_TITLE_STATE,\r\n"
      L"  TEKKEN6_TRACE_STARTUP_GATE\r\n\r\n";
#endif
  const std::string header_utf8 = Utf8(header);
  if (!AppendText(run + L"\\run-info.txt", header_utf8, true) ||
      !AppendText(run + L"\\tekken6.log", header_utf8, true)) {
    return Fail(L"Cannot write diagnostic launch metadata.");
  }

  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE output = CreateFileW((run + L"\\console.log").c_str(), GENERIC_WRITE,
      FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) return Fail(L"Cannot create the console log.");
  DWORD header_written = 0;
  if (!WriteFile(output, header_utf8.data(), DWORD(header_utf8.size()), &header_written, nullptr) ||
      header_written != header_utf8.size()) {
    CloseHandle(output);
    return Fail(L"Cannot write diagnostic launch metadata to the console log.");
  }
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (input == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    CloseHandle(output);
    return Fail(L"Cannot initialize the game process.", error);
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input;
  startup.hStdOutput = startup.hStdError = output;
  PROCESS_INFORMATION process{};
  const BOOL launched = CreateProcessW(host.c_str(), command_buffer.data(), nullptr, nullptr,
      TRUE, 0, nullptr, root.c_str(), &startup, &process);
  const DWORD launch_error = GetLastError();
  CloseHandle(input);
  CloseHandle(output);
  if (!launched) return Fail(L"Cannot start Tekken 6. Diagnostics: " + run, launch_error);
  CloseHandle(process.hThread);
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD result = 0;
  GetExitCodeProcess(process.hProcess, &result);
  CloseHandle(process.hProcess);

  HANDLE status = CreateFileW((run + L"\\exit-code.txt").c_str(), GENERIC_WRITE,
      FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (status != INVALID_HANDLE_VALUE) {
    const auto text = std::to_string(result) + "\r\n";
    DWORD written;
    WriteFile(status, text.data(), DWORD(text.size()), &written, nullptr);
    CloseHandle(status);
  }
  if (result) return Fail(L"Tekken 6 exited with an error. Diagnostics: " + run, result);
  return 0;
}
