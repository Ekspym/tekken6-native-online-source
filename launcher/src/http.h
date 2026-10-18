#pragma once
// Tekken 6 Native Online launcher - HTTP requests (WinHTTP).

#include "common.h"

int httpGet(const wchar_t* url, QByteArray& out, const std::function<void(qint64, qint64)>& progress = {});
int httpLocal(const QString& url, const wchar_t* verb, const QString& headers, const QByteArray& body, QByteArray& out);
