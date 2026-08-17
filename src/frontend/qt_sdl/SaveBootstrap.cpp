/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#include "SaveBootstrap.h"

#include <chrono>
#include <mutex>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSaveFile>
#include <QTimer>

#include "SaveManager.h"

namespace SaveBootstrap
{
namespace
{
std::mutex StateLock;
bool Enabled = false;
bool Finished = false;
QString ResultPath;
QString CancelPath;
QString RomHash;
std::chrono::steady_clock::time_point StartedAt;

QString Sha256File(const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QString("cannot open %1: %2").arg(path, file.errorString());
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
    {
        error = QString("cannot hash %1").arg(path);
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

void Finish(bool success, const QString& savePath, melonDS::u32 frame, const QString& error)
{
    std::lock_guard guard(StateLock);
    if (!Enabled || Finished) return;
    Finished = true;

    QString saveHash;
    qint64 saveSize = 0;
    QString finalError = error;
    if (success)
    {
        QString hashError;
        saveHash = Sha256File(savePath, hashError);
        saveSize = QFileInfo(savePath).size();
        if (saveHash.isEmpty())
            finalError = hashError;
    }

    const bool finalSuccess = success && finalError.isEmpty();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - StartedAt).count();
    QJsonObject result{
        {"success", finalSuccess},
        {"rom_sha256", RomHash},
        {"save_path", savePath},
        {"save_size", saveSize},
        {"save_sha256", saveHash},
        {"frame", static_cast<qint64>(frame)},
        {"elapsed_ms", static_cast<qint64>(elapsed)},
        {"error", finalError},
    };

    QSaveFile output(ResultPath);
    if (output.open(QIODevice::WriteOnly))
    {
        output.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
        output.write("\n");
        output.commit();
    }
    QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
}
}

void Initialize(const std::optional<QString>& romPath)
{
    const QString resultPath = qEnvironmentVariable("MELONDS_SAVE_BOOTSTRAP_RESULT");
    if (resultPath.isEmpty()) return;

    {
        std::lock_guard guard(StateLock);
        Enabled = true;
        ResultPath = resultPath;
        CancelPath = qEnvironmentVariable("MELONDS_SAVE_BOOTSTRAP_CANCEL");
        StartedAt = std::chrono::steady_clock::now();
    }

    if (!romPath.has_value())
    {
        QTimer::singleShot(0, [] { Finish(false, {}, 0, "ROM path is missing"); });
        return;
    }

    QString hashError;
    RomHash = Sha256File(*romPath, hashError);
    if (RomHash.isEmpty())
    {
        QTimer::singleShot(0, [hashError] { Finish(false, {}, 0, hashError); });
        return;
    }
    const QString expected = qEnvironmentVariable("MELONDS_SAVE_BOOTSTRAP_ROM_SHA256").toLower();
    if (!expected.isEmpty() && expected != RomHash)
    {
        QTimer::singleShot(0, [] { Finish(false, {}, 0, "ROM hash does not match"); });
        return;
    }

    bool timeoutOk = false;
    int timeoutMs = qEnvironmentVariableIntValue("MELONDS_SAVE_BOOTSTRAP_TIMEOUT_MS", &timeoutOk);
    if (!timeoutOk || timeoutMs < 1'000) timeoutMs = 30'000;
    QTimer::singleShot(timeoutMs, [] { Finish(false, {}, 0, "save bootstrap timed out"); });

    if (!CancelPath.isEmpty())
    {
        auto* timer = new QTimer(qApp);
        timer->setInterval(100);
        QObject::connect(timer, &QTimer::timeout, [timer]
        {
            if (QFileInfo::exists(CancelPath))
            {
                timer->stop();
                Finish(false, {}, 0, "save bootstrap cancelled");
            }
        });
        timer->start();
    }
}

bool IsEnabled()
{
    std::lock_guard guard(StateLock);
    return Enabled;
}

void Observe(SaveManager* save, melonDS::u32 frame)
{
    if (!save || !IsEnabled()) return;
    if (save->FailedFileFlushVersion() != 0)
    {
        Finish(false, QString::fromStdString(save->GetPath()), frame, "save file flush failed");
        return;
    }
    if (save->CompletedFileFlushVersion() != 0)
        Finish(true, QString::fromStdString(save->GetPath()), frame, {});
}
}
