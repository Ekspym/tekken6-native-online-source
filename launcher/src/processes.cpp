// Tekken 6 Native Online launcher - Game processes: ended together with the launcher, leftovers of an earlier launcher.

#include "processes.h"
#include "files.h"

// A game process ends with the launcher, however the launcher ends (crash, Task Manager): it is put into a job object
// that kills its processes when its last handle closes = when the launcher process is gone (2026-10-17: a player's
// launcher went away, its game stayed parked on UDP 7000 reading the same cmd.txt as the next launcher's game - none
// of his matches connected any more until a reboot)
void endWithLauncher(qint64 pid)
{
	static HANDLE job = [] {
		HANDLE j = CreateJobObjectW(nullptr, nullptr);
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
		li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (j && !SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof(li)))
		{
			CloseHandle(j);
			j = nullptr;
		}
		return j;
	}();
	if (!job || pid <= 0)
		return;
	if (HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid)))
	{
		AssignProcessToJobObject(job, h);
		CloseHandle(h);
	}
}

int endLeftoverGames()
{
	const QString exe = QDir::toNativeSeparators(QFileInfo(gameExe()).absoluteFilePath()).toLower();
	int n = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32W e{};
	e.dwSize = sizeof(e);
	for (BOOL ok = snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
	{
		if (_wcsicmp(e.szExeFile, L"tekken6.exe") != 0)
			continue;
		HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, e.th32ProcessID);
		if (!h)
			continue;
		wchar_t path[1024];
		DWORD len = DWORD(std::size(path));
		if (QueryFullProcessImageNameW(h, 0, path, &len) && QString::fromWCharArray(path, int(len)).toLower() == exe && TerminateProcess(h, 1))
			n++;
		CloseHandle(h);
	}
	if (snap != INVALID_HANDLE_VALUE)
		CloseHandle(snap);
	return n;
}
