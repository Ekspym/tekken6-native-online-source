// Tekken 6 Native Online launcher - HTTP requests (WinHTTP).

#include "http.h"

// One WinHTTP request (system TLS for https, redirects followed). Returns the HTTP status, 0 when it failed.
// local: the router on this network (no proxy, 2-3 s timeouts, at most 1 MB of answer); else the internet through the
// system proxy. progress(got, total) after every part of the answer (total -1 = no Content-Length).
struct HttpOptions
{
	const wchar_t* verb = L"GET";
	std::wstring headers;
	QByteArray body;
	bool local = false;
	std::function<void(qint64, qint64)> progress;
};

static int httpRequest(const std::wstring& url, const HttpOptions& o, QByteArray& out)
{
	out.clear();
	URL_COMPONENTS u{};
	u.dwStructSize = sizeof(u);
	wchar_t host[256]{}, path[2048]{}, extra[2048]{};
	u.lpszHostName = host;
	u.dwHostNameLength = DWORD(std::size(host));
	u.lpszUrlPath = path;
	u.dwUrlPathLength = DWORD(std::size(path));
	u.lpszExtraInfo = extra;
	u.dwExtraInfoLength = DWORD(std::size(extra));
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u))
		return 0;
	const std::wstring object = std::wstring(path) + extra;
	int status = 0;
	HINTERNET s = WinHttpOpen(L"Tekken6NativeOnlineLauncher", o.local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (s && o.local)
		WinHttpSetTimeouts(s, 2000, 2000, 3000, 3000);
	HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : nullptr;
	HINTERNET r = c ? WinHttpOpenRequest(c, o.verb, object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                     u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
	                : nullptr;
	if (r && WinHttpSendRequest(r, o.headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : o.headers.c_str(), o.headers.empty() ? 0 : DWORD(-1),
	                            o.body.isEmpty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(o.body.constData()), DWORD(o.body.size()),
	                            DWORD(o.body.size()), 0) &&
	    WinHttpReceiveResponse(r, nullptr))
	{
		DWORD code = 0, size = sizeof(code);
		WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
		wchar_t len[32]{};
		DWORD lenSize = sizeof(len);
		const qint64 total = WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &lenSize, WINHTTP_NO_HEADER_INDEX)
		                         ? QString::fromWCharArray(len).toLongLong()
		                         : -1;
		bool ok = true;
		for (DWORD avail = 0; ok && WinHttpQueryDataAvailable(r, &avail) && avail && (!o.local || out.size() < (1 << 20));)
		{
			const qsizetype at = out.size();
			out.resize(at + avail);
			DWORD read = 0;
			ok = WinHttpReadData(r, out.data() + at, avail, &read);
			out.resize(at + read);
			if (o.progress)
				o.progress(out.size(), total);
		}
		status = ok ? int(code) : 0;
	}
	for (HINTERNET h : {r, c, s})
		if (h)
			WinHttpCloseHandle(h);
	return status;
}

// GET over HTTPS (GitHub releases and assets)
int httpGet(const wchar_t* url, QByteArray& out, const std::function<void(qint64, qint64)>& progress)
{
	HttpOptions o;
	o.headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
	o.progress = progress;
	return httpRequest(url, o, out);
}

// Plain HTTP on the local network (the router's UPnP)
int httpLocal(const QString& url, const wchar_t* verb, const QString& headers, const QByteArray& body, QByteArray& out)
{
	HttpOptions o;
	o.verb = verb;
	o.headers = headers.toStdWString();
	o.body = body;
	o.local = true;
	return httpRequest(url.toStdWString(), o, out);
}
