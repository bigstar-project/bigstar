#pragma once

#include <string>

namespace NsmbMvlNetplay::TraceOutput {

// Runtime traces must never wait for redirected stdout.  The launcher keeps
// stdout on disk so an occasional slow filesystem write would otherwise stop
// emulation and, through frame-lead throttling, the peer as well.
void Write(std::string line);
void Printf(const char *format, ...);

// Only use from teardown paths, after frame processing has stopped.
void Flush();

} // namespace NsmbMvlNetplay::TraceOutput
