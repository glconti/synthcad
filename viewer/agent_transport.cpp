#include "agent_transport.h"
#include "agent_cli.h"
#include "project_contract.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace synthcad {
namespace {
using nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr size_t kMaxMessage = 16 * 1024 * 1024;
void Pause() { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
Clock::time_point Deadline(int timeout) {
  return Clock::now() + std::chrono::milliseconds(std::max(0, timeout));
}
std::string Hash(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : value) { hash ^= byte; hash *= 1099511628211ull; }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}
std::string Nonce() {
  std::random_device random;
  return Hash(std::to_string(random()) + std::to_string(random()) +
              std::to_string(Clock::now().time_since_epoch().count()));
}
std::string Canonical(const std::string& path) {
  return CanonicalPath(fs::u8path(path));
}
#ifdef _WIN32
std::wstring Wide(const std::string& value) {
  if (value.empty()) return {};
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
  if (!size) throw std::runtime_error("Invalid UTF-8 path or session name");
  std::wstring result(size, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), size);
  return result;
}
struct UserSecurity {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  SECURITY_ATTRIBUTES attributes{};
  std::string identity;
  UserSecurity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
      throw std::runtime_error("Cannot read current user identity");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    bool ok = GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
      throw std::runtime_error("Cannot read current user SID");
    std::wstring sidString(sid);
    LocalFree(sid);
    for (wchar_t character : sidString) identity.push_back(static_cast<char>(character));
    std::wstring sddl = L"D:P(A;OICI;GA;;;" + sidString + L")";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
      throw std::runtime_error("Cannot restrict session access to current user");
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
  }
  ~UserSecurity() { if (descriptor) LocalFree(descriptor); }
};
unsigned long ProcessId() { return GetCurrentProcessId(); }
std::string ProcessIdentity(unsigned long pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  FILETIME created{}, exit{}, kernel{}, user{};
  bool ok = GetProcessTimes(process, &created, &exit, &kernel, &user) != 0;
  DWORD status = 0;
  ok = ok && GetExitCodeProcess(process, &status) && status == STILL_ACTIVE;
  CloseHandle(process);
  if (!ok) return {};
  return std::to_string((uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime);
}
#else
unsigned long ProcessId() { return static_cast<unsigned long>(getpid()); }
std::string ProcessIdentity(unsigned long pid) {
  if (kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) return {};
  std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
  std::string line;
  std::getline(stat, line);
  auto close = line.rfind(')');
  if (close == std::string::npos) return {};
  std::istringstream fields(line.substr(close + 2));
  std::string value;
  // The first field following comm is field 3, the start time is field 22.
  for (int field = 3; field <= 22; ++field) if (!(fields >> value)) return {};
  return value;
}
#endif

fs::path Root() {
  const char* overridePath = std::getenv("SYNTHCAD_SESSION_DIR");
  fs::path root;
  if (overridePath && *overridePath) root = fs::absolute(fs::u8path(overridePath));
  else {
#ifdef _WIN32
    UserSecurity security;
    root = fs::temp_directory_path() / ("synthcad-" + Hash(security.identity));
#else
    root = fs::temp_directory_path() / ("synthcad-" + std::to_string(getuid()));
#endif
  }
#ifdef _WIN32
  UserSecurity security;
  if (!CreateDirectoryW(root.c_str(), &security.attributes) && GetLastError() != ERROR_ALREADY_EXISTS)
    throw std::runtime_error("Cannot create private session directory: " + root.u8string());
  // Apply a protected owner-only DACL even when the directory already exists.
  if (!SetFileSecurityW(root.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                        security.descriptor))
    throw std::runtime_error("Cannot secure session directory: " + root.u8string());
#else
  if (mkdir(root.c_str(), 0700) != 0 && errno != EEXIST)
    throw std::runtime_error("Cannot create private session directory: " + root.u8string());
  struct stat info{};
  if (lstat(root.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != getuid())
    throw std::runtime_error("Session directory is not owned by the current user");
  if (chmod(root.c_str(), 0700)) throw std::runtime_error("Cannot secure session directory");
#endif
  return root;
}
fs::path RecordPath(const fs::path& root, const std::string& name) {
  return root / (Hash(name) + ".json");
}

class OpenLock {
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int handle_ = -1;
#endif
 public:
  OpenLock(const fs::path& root, Clock::time_point deadline) {
    fs::path path = root / "open.lock";
    do {
#ifdef _WIN32
      handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle_ != INVALID_HANDLE_VALUE) return;
#else
      if (handle_ < 0) handle_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
      if (handle_ < 0) throw std::runtime_error("Cannot open session launch lock");
      if (!flock(handle_, LOCK_EX | LOCK_NB)) return;
#endif
      Pause();
    } while (Clock::now() < deadline);
#ifndef _WIN32
    if (handle_ >= 0) { close(handle_); handle_ = -1; }
#endif
    throw std::runtime_error("Timed out acquiring session launch lock");
  }
  ~OpenLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (handle_ >= 0) { flock(handle_, LOCK_UN); close(handle_); }
#endif
  }
};

#ifdef _WIN32
using Connection = HANDLE;
const Connection kInvalid = INVALID_HANDLE_VALUE;
void Close(Connection connection) { CloseHandle(connection); }
#else
using Connection = int;
const Connection kInvalid = -1;
void Close(Connection connection) { close(connection); }
#endif
bool WriteMessage(Connection connection, const std::string& message, Clock::time_point deadline) {
  size_t offset = 0;
  do {
#ifdef _WIN32
    DWORD sent = 0;
    if (!WriteFile(connection, message.data() + offset, DWORD(message.size() - offset), &sent, nullptr)) {
      DWORD error = GetLastError();
      if (error != ERROR_NO_DATA && error != ERROR_PIPE_BUSY) return false;
    }
#else
    ssize_t sent = send(connection, message.data() + offset, message.size() - offset, MSG_NOSIGNAL);
    if (sent < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
      sent = 0;
    }
#endif
    offset += static_cast<size_t>(sent);
    if (offset == message.size()) return true;
    Pause();
  } while (Clock::now() < deadline);
  return false;
}
bool ReadMessage(Connection connection, std::string& message, Clock::time_point deadline,
                 const std::atomic<bool>* stop = nullptr) {
  char buffer[8192];
  do {
    if (stop && stop->load()) return false;
#ifdef _WIN32
    DWORD read = 0;
    if (!ReadFile(connection, buffer, sizeof(buffer), &read, nullptr)) {
      DWORD error = GetLastError();
      if (error != ERROR_NO_DATA && error != ERROR_PIPE_LISTENING) return false;
    }
#else
    ssize_t read = recv(connection, buffer, sizeof(buffer), 0);
    if (read == 0) return false;
    if (read < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
      read = 0;
    }
#endif
    message.append(buffer, static_cast<size_t>(read));
    if (message.size() > kMaxMessage) return false;
    auto newline = message.find('\n');
    if (newline != std::string::npos) { message.resize(newline); return true; }
    Pause();
  } while (Clock::now() < deadline);
  return false;
}

Connection Connect(const std::string& endpoint, Clock::time_point deadline) {
  do {
#ifdef _WIN32
    Connection result = CreateFileW(Wide(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE,
                                     0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (result != kInvalid) {
      DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
      if (SetNamedPipeHandleState(result, &mode, nullptr, nullptr)) return result;
      Close(result);
      return kInvalid;
    }
    if (GetLastError() != ERROR_PIPE_BUSY && GetLastError() != ERROR_FILE_NOT_FOUND) return kInvalid;
#else
    Connection result = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (result < 0) return kInvalid;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (endpoint.size() >= sizeof(address.sun_path)) { Close(result); return kInvalid; }
    std::copy(endpoint.begin(), endpoint.end(), address.sun_path);
    if (!connect(result, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return result;
    Close(result);
    if (errno != ECONNREFUSED && errno != ENOENT && errno != EAGAIN) return kInvalid;
#endif
    Pause();
  } while (Clock::now() < deadline);
  return kInvalid;
}
json Exchange(const json& record, const json& request, Clock::time_point deadline) {
  const std::string command = request.value("command", "request");
  const std::string name = record.value("session", "");
  Connection connection = Connect(record.at("endpoint").get<std::string>(), deadline);
  if (connection == kInvalid) {
    if (Clock::now() >= deadline)
      return Error(command, "timeout", "Session connection did not become available within the timeout", {}, name);
    return Error(command, "no_session", "Session is no longer reachable; open the project again", {}, name);
  }
  json authenticated = request;
  authenticated["sessionNonce"] = record.at("nonce");
  std::string text;
  bool ok = WriteMessage(connection, authenticated.dump() + "\n", deadline) &&
            ReadMessage(connection, text, deadline);
  if (ok) WriteMessage(connection, "\n", deadline);
  Close(connection);
  if (!ok) return Error(command, "timeout", "Session did not respond within the timeout", {}, name);
  try {
    json response = json::parse(text);
    if (!response.is_object() || response.value("protocolVersion", 0) != 1 || !response.contains("ok"))
      throw std::runtime_error("Malformed response envelope");
    return response;
  } catch (const std::exception& error) {
    return Error(command, "io_error", std::string("Invalid session response: ") + error.what(), {}, name);
  }
}

std::vector<json> Records(const fs::path& root, bool includeUnreachable = false) {
  std::vector<json> records;
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.path().extension() != ".json" || !entry.is_regular_file()) continue;
    try {
      std::ifstream input(entry.path());
      json record;
      input >> record;
      input.close();
      if (record.at("protocolVersion") != 1 || !record.at("session").is_string() ||
          !record.at("projectPath").is_string() || !record.at("endpoint").is_string() ||
          !record.at("nonce").is_string()) continue;
      auto identity = ProcessIdentity(record.at("pid").get<unsigned long>());
      if (identity.empty() || identity != record.at("processIdentity").get<std::string>()) {
        std::error_code ignored;
        fs::remove(entry.path(), ignored);
#ifndef _WIN32
        // Only unlink socket entries within our private registry directory.
        const fs::path endpoint = fs::u8path(record.at("endpoint").get<std::string>());
        if (endpoint.parent_path() == root && endpoint.extension() == ".sock") fs::remove(endpoint, ignored);
#endif
        continue;
      }
      auto ping = Exchange(record, {{"command", "__ping"}}, Deadline(100));
      const bool reachable = ping.value("ok", false) && ping.value("session", "") == record.at("session");
      if (reachable || includeUnreachable) {
        record["reachable"] = reachable;
        records.push_back(record);
      }
    } catch (const std::exception&) { /* Ignore incomplete/unrelated registry entries. */ }
  }
  return records;
}
json PublicRecord(json record) {
  record.erase("nonce");
  record.erase("processIdentity");
  record.erase("protocolVersion");
  return record;
}

#ifdef _WIN32
std::wstring Quote(const std::wstring& argument) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t c : argument) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'\"') result.append(slashes * 2 + 1, L'\\');
    else result.append(slashes, L'\\');
    slashes = 0;
    result.push_back(c);
  }
  result.append(slashes * 2, L'\\');
  result.push_back(L'\"');
  return result;
}
#endif
bool Spawn(const std::string& viewer, const std::string& session,
           const std::string& project, bool hidden, unsigned long& pid, std::string& error) {
#ifdef _WIN32
  std::wstring exe = Wide(viewer);
  std::wstring command = Quote(exe) + L" --agent-session " + Quote(Wide(session)) +
      L" --agent-project " + Quote(Wide(project));
  if (hidden) command += L" --agent-hidden";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = hidden ? SW_HIDE : SW_SHOWNORMAL;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                       CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr, &startup, &process)) {
    error = "Cannot launch viewer (Windows error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(process.hThread);
  pid = process.dwProcessId;
  CloseHandle(process.hProcess);
  return true;
#else
  // Double fork detaches the persistent viewer from the short-lived CLI.
  int identityPipe[2];
  if (pipe2(identityPipe, O_CLOEXEC)) { error = "Cannot create launch identity pipe"; return false; }
  pid_t child = fork();
  if (child < 0) { close(identityPipe[0]); close(identityPipe[1]); error = "Cannot launch viewer"; return false; }
  if (child == 0) {
    close(identityPipe[0]);
    if (setsid() < 0) _exit(127);
    pid_t grandchild = fork();
    if (grandchild < 0) _exit(127);
    if (grandchild > 0) _exit(0);
    unsigned long identity = ProcessId();
    if (write(identityPipe[1], &identity, sizeof(identity)) != sizeof(identity)) _exit(127);
    close(identityPipe[1]);
    int null = open("/dev/null", O_RDWR);
    if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); if (null > 2) close(null); }
    std::vector<std::string> args = {viewer, "--agent-session", session, "--agent-project", project};
    if (hidden) args.push_back("--agent-hidden");
    std::vector<char*> pointers;
    for (auto& arg : args) pointers.push_back(arg.data());
    pointers.push_back(nullptr);
    execv(viewer.c_str(), pointers.data());
    _exit(127);
  }
  close(identityPipe[1]);
  ssize_t received = read(identityPipe[0], &pid, sizeof(pid));
  close(identityPipe[0]);
  int status = 0;
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
  if (!WIFEXITED(status) || WEXITSTATUS(status) || received != sizeof(pid)) { error = "Cannot detach viewer process"; return false; }
  return true;
#endif
}
}  // namespace

struct SessionServer::Impl {
  std::atomic<bool> stop{false};
  std::thread worker;
  json record;
  fs::path recordPath;
  Connection listener = kInvalid;
  std::function<json(const json&)> handler;

  void Handle(Connection connection) {
    std::string text;
    if (ReadMessage(connection, text, Deadline(1000), &stop)) {
      json response;
      int timeout = 10000;
      try {
        json request = json::parse(text);
        timeout = std::clamp(request.value("timeoutMs", 10000), 0, request.value("command","")=="events"?301000:300000);
        request["timeoutMs"] = timeout;
        std::string command = request.value("command", "request");
        if (request.value("sessionNonce", "") != record.at("nonce"))
          response = Error(command, "no_session", "Session identity does not match", {}, record.at("session"));
        else if (command == "__ping") response = Success(command, {}, record.at("session"));
        else response = handler(request);
      } catch (const std::exception& error) {
        response = Error("request", "io_error", error.what(), {}, record.at("session"));
      }
      if (WriteMessage(connection, response.dump() + "\n", Deadline(std::min(timeout + 100, 5000)))) {
        // DisconnectNamedPipe discards unread bytes. A bounded acknowledgement
        // ensures delivery without a blocking FlushFileBuffers on an untrusted client.
        std::string acknowledgement;
        // A stopping server must still drain a response it already wrote.
        // On Windows DisconnectNamedPipe discards unread bytes, so cancelling
        // this bounded acknowledgement wait can lose the final close events.
        ReadMessage(connection, acknowledgement, Deadline(1000));
      }
    }
#ifdef _WIN32
    DisconnectNamedPipe(connection);
#endif
    Close(connection);
  }

  void Serve() {
    std::vector<std::future<void>> clients;
    while (!stop.load()) {
      clients.erase(std::remove_if(clients.begin(), clients.end(), [](std::future<void>& client) {
        return client.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
      }), clients.end());
      if (clients.size() >= 16) { Pause(); continue; }
#ifdef _WIN32
      bool connected = ConnectNamedPipe(listener, nullptr) != 0;
      DWORD code = GetLastError();
      // In PIPE_NOWAIT mode a successful call makes a disconnected instance
      // available; ERROR_PIPE_CONNECTED acknowledges the actual client.
      if (connected || code != ERROR_PIPE_CONNECTED) { Pause(); continue; }
      Connection connection = listener;
      UserSecurity security;
      listener = CreateNamedPipeW(Wide(record.at("endpoint").get<std::string>()).c_str(), PIPE_ACCESS_DUPLEX,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
          17, 65536, 65536, 0, &security.attributes);
      if (listener == kInvalid) {
        DisconnectNamedPipe(connection);
        listener = connection;
        Pause();
        continue;
      }
#else
      Connection connection = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (connection == kInvalid) { Pause(); continue; }
#endif
      clients.push_back(std::async(std::launch::async, [this, connection] { Handle(connection); }));
    }
  }
};

SessionServer::SessionServer() : impl_(std::make_unique<Impl>()) {}
SessionServer::~SessionServer() { Stop(); }

bool SessionServer::Start(const std::string& session, const std::string& projectPath,
                          std::function<json(const json&)> handler, std::string& error) {
  if (impl_->worker.joinable()) { error = "Session server is already running"; return false; }
  if (session.empty()) { error = "Session name must not be empty"; return false; }
  try {
    fs::path root = Root();
    fs::path recordPath = RecordPath(root, session);
    if (fs::exists(recordPath)) {
      std::ifstream input(recordPath);
      json previous;
      input >> previous;
      std::string identity = ProcessIdentity(previous.value("pid", 0ul));
      if (!identity.empty() && identity == previous.value("processIdentity", ""))
        throw std::runtime_error("Session name is already in use: " + session);
    }
    std::string nonce = Nonce();
    std::string endpoint;
#ifdef _WIN32
    UserSecurity security;
    endpoint = "\\\\.\\pipe\\synthcad-" + Hash(security.identity) + "-" + nonce;
    impl_->listener = CreateNamedPipeW(Wide(endpoint).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
        17, 65536, 65536, 0, &security.attributes);
    if (impl_->listener == kInvalid) throw std::runtime_error("Cannot create private session named pipe");
#else
    endpoint = (root / (nonce + ".sock")).u8string();
    sockaddr_un address{};
    if (endpoint.size() >= sizeof(address.sun_path)) throw std::runtime_error("Session socket path is too long");
    address.sun_family = AF_UNIX;
    std::copy(endpoint.begin(), endpoint.end(), address.sun_path);
    impl_->listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (impl_->listener < 0 || bind(impl_->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
        chmod(endpoint.c_str(), 0600) || listen(impl_->listener, 16))
      throw std::runtime_error("Cannot create private session socket");
#endif
    impl_->record = {{"protocolVersion", 1}, {"session", session}, {"projectPath", Canonical(projectPath)},
        {"pid", ProcessId()}, {"processIdentity", ProcessIdentity(ProcessId())}, {"endpoint", endpoint}, {"nonce", nonce}};
    impl_->recordPath = recordPath;
    impl_->handler = std::move(handler);
    impl_->stop.store(false);
    impl_->worker = std::thread([this] { impl_->Serve(); });
    const fs::path temporary = root / (nonce + ".tmp");
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      output << impl_->record.dump();
      output.close();
      if (!output) throw std::runtime_error("Cannot write session registry");
    }
    std::error_code ignored;
    fs::remove(recordPath, ignored);
    fs::rename(temporary, recordPath);
    return true;
  } catch (const std::exception& failure) {
    error = failure.what();
    Stop();
    return false;
  }
}

void SessionServer::Stop() {
  impl_->stop.store(true);
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->listener != kInvalid) { Close(impl_->listener); impl_->listener = kInvalid; }
  std::error_code ignored;
  if (!impl_->recordPath.empty()) {
    // Do not remove a newer process's record if a session name has been reused.
    try {
      std::ifstream input(impl_->recordPath);
      json record;
      input >> record;
      input.close();
      if (record.value("nonce", "") == impl_->record.value("nonce", "")) fs::remove(impl_->recordPath, ignored);
    } catch (const std::exception&) {}
  }
#ifndef _WIN32
  if (impl_->record.contains("endpoint")) fs::remove(fs::u8path(impl_->record.at("endpoint").get<std::string>()), ignored);
#endif
  impl_->record = json::object();
  impl_->recordPath.clear();
}

json ListSessions() {
  try {
    json sessions = json::array();
    for (const auto& record : Records(Root())) sessions.push_back(PublicRecord(record));
    return Success("sessions", {{"sessions", sessions}});
  } catch (const std::exception& error) { return Error("sessions", "io_error", error.what()); }
}

json Request(const std::string& session, const json& request, int timeoutMs) {
  std::string command = request.value("command", "request");
  try {
    // The CLI reserves up to 500 ms for response delivery after the viewer's
    // own deadline. Preserve that distinction rather than extending actions.
    const int maxRequestTimeout=command=="events"?301000:300000;
    if (timeoutMs < 0 || timeoutMs > maxRequestTimeout+500)
      return Error(command, "invalid_argument", "Transport timeout exceeds the command limit", {}, session);
    json timedRequest = request;
    if (!timedRequest.contains("timeoutMs")) timedRequest["timeoutMs"] = std::min(timeoutMs, maxRequestTimeout);
    if (!timedRequest["timeoutMs"].is_number_integer() || timedRequest["timeoutMs"].get<int64_t>() < 0 ||
        timedRequest["timeoutMs"].get<int64_t>() > maxRequestTimeout)
      return Error(command, "invalid_argument", "Request timeout exceeds the command limit", {}, session);
    const auto deadline = Deadline(timeoutMs);
    auto records = Records(Root(), true);
    if (records.empty()) return Error(command, "no_session", "No live session; run synthcad open PATH first");
    json selected;
    if (session.empty()) {
      if (records.size() != 1) {
        json names = json::array();
        for (const auto& record : records) names.push_back(record.at("session"));
        return Error(command, "ambiguous_session", "Several sessions are running; use --session NAME", {{"sessions", names}});
      }
      selected = records.front();
    } else {
      for (const auto& record : records) if (record.at("session") == session) { selected = record; break; }
      if (selected.is_null()) return Error(command, "no_session", "No live session named " + session, {}, session);
    }
    return Exchange(selected, timedRequest, deadline);
  } catch (const std::exception& error) { return Error(command, "io_error", error.what(), {}, session); }
}

json OpenSession(const std::string& projectPath, const std::string& requestedName,
                 bool hidden, const std::string& viewerExe, int timeoutMs) {
  try {
    if (timeoutMs < 0 || timeoutMs > 300000)
      return Error("open", "invalid_argument", "Open timeout must be from 0 to 300000 milliseconds");
    if (!fs::exists(fs::u8path(projectPath))) return Error("open", "not_found", "Project path does not exist: " + projectPath);
    const std::string project = Canonical(projectPath);
    const fs::path root = Root();
    const auto deadline = Deadline(timeoutMs);
    OpenLock lock(root, deadline);
    for (const auto& record : Records(root, true)) {
      if (record.at("projectPath") == project) {
        if (!record.value("reachable", false))
          return Error("open", "busy", "An existing project process is not responding; retry after pending calls finish",
                       {{"pid", record.at("pid")}}, record.at("session"));
        if (!requestedName.empty() && record.at("session") != requestedName)
          return Error("open", "invalid_argument", "Project is already open as session " + record.at("session").get<std::string>(),
                       {{"existingSession", record.at("session")}});
        json data = PublicRecord(record);
        data["reused"] = true;
        return Success("open", data, record.at("session"));
      }
      if (!requestedName.empty() && record.at("session") == requestedName)
        return Error("open", "invalid_argument", "Session name belongs to another project: " + requestedName);
    }
    const std::string name = requestedName.empty() ? "project-" + Hash(project) : requestedName;
    const fs::path pendingPath = root / (Hash(project) + ".launch");
    bool alreadyLaunching = false;
    unsigned long launchedPid = 0;
    std::string launchedIdentity;
    if (fs::exists(pendingPath)) {
      try {
        std::ifstream input(pendingPath);
        json pending;
        input >> pending;
        launchedPid = pending.at("pid").get<unsigned long>();
        launchedIdentity = pending.at("processIdentity").get<std::string>();
        alreadyLaunching = !launchedIdentity.empty() && ProcessIdentity(launchedPid) == launchedIdentity;
        if (alreadyLaunching && pending.at("session") != name)
          return Error("open", "busy", "Project is already opening as session " + pending.at("session").get<std::string>());
      } catch (const std::exception&) {}
    }
    std::string error;
    if (!alreadyLaunching) {
      if (!Spawn(viewerExe, name, project, hidden, launchedPid, error)) return Error("open", "io_error", error);
      launchedIdentity = ProcessIdentity(launchedPid);
      std::ofstream output(pendingPath, std::ios::binary | std::ios::trunc);
      output << json({{"pid", launchedPid}, {"processIdentity", launchedIdentity}, {"session", name}}).dump();
      output.close();
      if (!output) return Error("open", "io_error", "Cannot record pending viewer launch");
    }
    do {
      for (const auto& record : Records(root)) {
        if (record.at("projectPath") == project && record.at("session") == name) {
          std::error_code ignored;
          fs::remove(pendingPath, ignored);
          json data = PublicRecord(record);
          data["reused"] = alreadyLaunching;
          return Success("open", data, name);
        }
      }
      if (ProcessIdentity(launchedPid).empty()) {
        std::error_code ignored;
        fs::remove(pendingPath, ignored);
        return Error("open", "io_error", "Viewer exited before publishing its session", {}, name);
      }
      Pause();
    } while (Clock::now() < deadline);
    return Error("open", "timeout", "Viewer did not publish its session before the timeout; check sessions before opening again", {}, name);
  } catch (const std::exception& error) {
    const bool timeout = std::string(error.what()).find("Timed out") != std::string::npos;
    return Error("open", timeout ? "timeout" : "io_error", error.what());
  }
}

}  // namespace synthcad
