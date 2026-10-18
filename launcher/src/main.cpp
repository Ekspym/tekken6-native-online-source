// Tekken 6 Native Online launcher - Crash report and the program entry.

#include "launcher.h"

// A crash of the launcher -> launcher_crash.txt next to it: exception, faulting module + offset and the stack with
// symbols (the PDB next to the exe when present). Players can send it with their report.
static LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep)
{
	wchar_t exe[MAX_PATH]{};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring path(exe);
	path = path.substr(0, path.find_last_of(L'\\') + 1) + L"launcher_crash.txt";
	FILE* f = nullptr;
	if (_wfopen_s(&f, path.c_str(), L"a") != 0 || !f)
		return EXCEPTION_CONTINUE_SEARCH;
	SYSTEMTIME t;
	GetLocalTime(&t);
	fprintf(f, "\n%04d-%02d-%02d %02d:%02d:%02d crash %08lX at %p\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
	        ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
	HANDLE proc = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	SymInitialize(proc, nullptr, TRUE);
	CONTEXT ctx = *ep->ContextRecord;
	STACKFRAME64 sf{};
	sf.AddrPC.Offset = ctx.Rip;
	sf.AddrPC.Mode = AddrModeFlat;
	sf.AddrFrame.Offset = ctx.Rbp;
	sf.AddrFrame.Mode = AddrModeFlat;
	sf.AddrStack.Offset = ctx.Rsp;
	sf.AddrStack.Mode = AddrModeFlat;
	for (int i = 0; i < 40 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf, &ctx, nullptr,
	                                       SymFunctionTableAccess64, SymGetModuleBase64, nullptr); i++)
	{
		const DWORD64 a = sf.AddrPC.Offset;
		char mod[MAX_PATH] = "?";
		if (const DWORD64 base = SymGetModuleBase64(proc, a))
		{
			GetModuleFileNameA(reinterpret_cast<HMODULE>(base), mod, MAX_PATH);
			char buf[sizeof(SYMBOL_INFO) + 256]{};
			auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
			sym->SizeOfStruct = sizeof(SYMBOL_INFO);
			sym->MaxNameLen = 255;
			DWORD64 disp = 0;
			IMAGEHLP_LINE64 line{sizeof(line)};
			DWORD ldisp = 0;
			const char* m = strrchr(mod, '\\') ? strrchr(mod, '\\') + 1 : mod;
			if (SymFromAddr(proc, a, &disp, sym))
			{
				if (SymGetLineFromAddr64(proc, a, &ldisp, &line))
					fprintf(f, "  %s!%s+0x%llX  (%s:%lu)\n", m, sym->Name, disp, line.FileName, line.LineNumber);
				else
					fprintf(f, "  %s!%s+0x%llX\n", m, sym->Name, disp);
			}
			else
				fprintf(f, "  %s+0x%llX\n", m, a - base);
		}
		else
			fprintf(f, "  0x%llX\n", a);
	}
	fclose(f);
	return EXCEPTION_CONTINUE_SEARCH;
}

static int launcherMain(int argc, char** argv);

int main(int argc, char** argv)
{
	try
	{
		return launcherMain(argc, argv);
	}
	catch (const std::exception& e)
	{
		MessageBoxA(nullptr, e.what(), "Tekken 6 Online - unexpected error", MB_ICONERROR);
	}
	catch (...)
	{
		MessageBoxA(nullptr, "unknown exception", "Tekken 6 Online - unexpected error", MB_ICONERROR);
	}
	return 1;
}

static int launcherMain(int argc, char** argv)
{
	SetUnhandledExceptionFilter(crashFilter);
	QApplication app(argc, argv);
	app.setWindowIcon(QIcon(":/app.ico"));
	const QStringList args = app.arguments();
	if (const int i = args.indexOf("--instance"); i >= 0 && i + 1 < args.size())
		g_instance = (std::max)(1, args[i + 1].toInt());
	// one launcher (and so one game) per PC; --instance N is the second launcher of the local tests
	// After an update the old launcher starts the new one and quits: the new one waits up to 10 s for the old one to
	// end before it says "already running" (v0.1.6: the restart after an update stopped at that message).
	SetLastError(0);
	HANDLE mutex = CreateMutexW(nullptr, TRUE, g_instance == 1 ? L"Local\\Tekken6NativeOnlineLauncher" : nullptr);
	bool taken = !(g_instance == 1 && GetLastError() == ERROR_ALREADY_EXISTS);
	for (int i = 0; !taken && i < 40; i++)
	{
		if (mutex)
			CloseHandle(mutex);
		Sleep(250);
		SetLastError(0);
		mutex = CreateMutexW(nullptr, TRUE, L"Local\\Tekken6NativeOnlineLauncher");
		taken = GetLastError() != ERROR_ALREADY_EXISTS;
	}
	if (!taken)
	{
		QMessageBox::information(nullptr, "Tekken 6 Online", "Tekken 6 Online is already running.");
		return 0;
	}
	if (g_instance == 1)
		g_leftoverGames = endLeftoverGames();
	// Bai Jamjuree (SIL OFL, launcher/fonts) for the Eurostile of the Sketch design (2026-10-18; Eurostile itself may not
	// be shipped), every weight it uses: regular 400, medium 500, semibold 600, bold 700
	for (const char* f : {":/fonts/BaiJamjuree-Regular.ttf", ":/fonts/BaiJamjuree-Medium.ttf", ":/fonts/BaiJamjuree-SemiBold.ttf",
	                      ":/fonts/BaiJamjuree-Bold.ttf"})
	{
		const int id = QFontDatabase::addApplicationFont(f);
		const QStringList fam = QFontDatabase::applicationFontFamilies(id);
		if (!fam.isEmpty())
			g_body = g_display = fam.first();
	}
	g_updateTest = args.contains("--update-test");
	app.setFont(QFont(g_body, 10));
	app.setStyleSheet(QString(kStyleTemplate).replace("%BODY%", g_body).replace("%DISPLAY%", g_display) +
	                  (QFile::exists(":/art/ui/field.png") ? kArtStyle : ""));
	Launcher w;
	BOOL dark = TRUE;
	DwmSetWindowAttribute(reinterpret_cast<HWND>(w.winId()), 20, &dark, sizeof(dark));
	if (g_updateTest)
	{
		w.setAttribute(Qt::WA_DontShowOnScreen);
		QTimer::singleShot(60000, &app, &QCoreApplication::quit);
	}
	w.show();
	return app.exec();
}
