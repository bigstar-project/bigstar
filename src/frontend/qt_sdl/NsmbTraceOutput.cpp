#include "NsmbTraceOutput.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace NsmbMvlNetplay::TraceOutput {
namespace {

constexpr std::size_t kMaxQueuedBytes = 4 * 1024 * 1024;

class Runtime {
public:
  Runtime() : Worker(&Runtime::Run, this) {}

  ~Runtime() {
    {
      std::lock_guard<std::mutex> lock(Mutex);
      Stopping = true;
    }
    Ready.notify_one();
    if (Worker.joinable())
      Worker.join();
  }

  void Write(std::string line) {
    if (line.empty())
      return;

    {
      std::lock_guard<std::mutex> lock(Mutex);
      // Dropping diagnostic text is preferable to ever blocking emulation.
      // Four MiB is several minutes of the current verbose rollback trace.
      if (Stopping || QueuedBytes + line.size() > kMaxQueuedBytes) {
        DroppedLines++;
        return;
      }
      QueuedBytes += line.size();
      Queue.emplace_back(std::move(line));
    }
    Ready.notify_one();
  }

  void Flush() {
    std::unique_lock<std::mutex> lock(Mutex);
    Ready.notify_one();
    Drained.wait(lock, [this] { return Queue.empty() && !Writing; });
    lock.unlock();
    std::fflush(stdout);
  }

private:
  void Run() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    std::deque<std::string> pending;
    auto nextFlush = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    for (;;) {
      std::size_t droppedLines = 0;
      bool stopping = false;
      {
        std::unique_lock<std::mutex> lock(Mutex);
        Ready.wait_until(lock, nextFlush,
                         [this] { return Stopping || !Queue.empty(); });
        if (Queue.empty() && Stopping)
          break;
        if (Queue.empty()) {
          lock.unlock();
          std::fflush(stdout);
          nextFlush =
              std::chrono::steady_clock::now() + std::chrono::seconds(1);
          continue;
        }
        if (!Stopping)
          Ready.wait_for(lock, std::chrono::milliseconds(2),
                         [this] { return Stopping; });
        Queue.swap(pending);
        QueuedBytes = 0;
        droppedLines = std::exchange(DroppedLines, 0);
        Writing = true;
        stopping = Stopping;
      }

      for (const std::string &line : pending)
        std::fwrite(line.data(), 1, line.size(), stdout);
      if (droppedLines != 0)
        std::fprintf(stdout, "NSMB TraceOutput: dropped buffered lines=%zu\n",
                     droppedLines);
      const auto now = std::chrono::steady_clock::now();
      if (stopping || now >= nextFlush) {
        std::fflush(stdout);
        nextFlush = now + std::chrono::seconds(1);
      }
      pending.clear();

      {
        std::lock_guard<std::mutex> lock(Mutex);
        Writing = false;
        if (Queue.empty())
          Drained.notify_all();
      }
    }

    std::fflush(stdout);
    {
      std::lock_guard<std::mutex> lock(Mutex);
      Writing = false;
      Drained.notify_all();
    }
  }

  std::mutex Mutex;
  std::condition_variable Ready;
  std::condition_variable Drained;
  std::deque<std::string> Queue;
  std::size_t QueuedBytes = 0;
  std::size_t DroppedLines = 0;
  bool Writing = false;
  bool Stopping = false;
  std::thread Worker;
};

Runtime &GetRuntime() {
  static Runtime runtime;
  return runtime;
}

bool UseSynchronousOutput() {
  static const bool synchronous =
      std::getenv("MELONDS_NSML_SYNC_TRACE_OUTPUT") != nullptr;
  return synchronous;
}

} // namespace

void Write(std::string line) {
  if (UseSynchronousOutput()) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
    return;
  }
  GetRuntime().Write(std::move(line));
}

void Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  std::array<char, 1024> localBuffer{};
  va_list localArgs;
  va_copy(localArgs, args);
  const int length =
      std::vsnprintf(localBuffer.data(), localBuffer.size(), format, localArgs);
  va_end(localArgs);
  if (length < 0) {
    va_end(args);
    return;
  }
  if (static_cast<std::size_t>(length) < localBuffer.size()) {
    va_end(args);
    Write(std::string(localBuffer.data(), static_cast<std::size_t>(length)));
    return;
  }

  std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
  std::vsnprintf(buffer.data(), buffer.size(), format, args);
  va_end(args);
  Write(std::string(buffer.data(), static_cast<std::size_t>(length)));
}

void Flush() {
  if (UseSynchronousOutput()) {
    std::fflush(stdout);
    return;
  }
  GetRuntime().Flush();
}

} // namespace NsmbMvlNetplay::TraceOutput
