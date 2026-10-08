#include "agent_bridge.h"
#include "agent_cli.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace synthcad;
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Write(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
    if (!output) throw std::runtime_error("fixture write failed");
}
struct TempDirectory {
    fs::path path = fs::temp_directory_path() / ("synthcad-bridge-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    TempDirectory() { fs::create_directories(path); }
    ~TempDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};
json Request(const char* command, json args = json::object(), int timeout = 1000) {
    return {{"command", command}, {"arguments", args}, {"timeoutMs", timeout}};
}
json Snapshot(const char* status, const FileSnapshot& attempted,
              const std::string& displayed, const char* view = "scene") {
    return {{"status", status}, {"attemptedRevision", Revision(attempted)},
            {"displayedRevision", displayed}, {"view", view},
            {"exportValid", std::string(status) == "ready"},
            {"selection", nullptr}, {"diagnostic", "broken import"}};
}
void ErrorCode(const json& response, const char* code) {
    Check(!response.at("ok").get<bool>(), "expected error response");
    Check(response.at("error").at("code") == code, "unexpected error code");
}
std::vector<std::shared_ptr<AgentAction>> AwaitActions(AgentBridge& bridge) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        auto actions = bridge.Drain();
        if (!actions.empty()) return actions;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("queued action did not arrive");
}
}
int main() {
    try {
        TempDirectory temp;
        const auto entry = temp.path / "scene.js", imported = temp.path / "new.js";
        Write(entry, "first scene");
        auto first = CaptureFiles({entry});
        const auto firstRevision = Revision(first);
        AgentBridge bridge("test-session");
        ErrorCode(bridge.Handle(Request("revision")), "busy");
        bridge.Publish(Snapshot("ready", first, firstRevision), first);
        Check(bridge.Handle(Request("state"))["data"]["displayedRevision"] == firstRevision, "state reports displayed revision");
        Check(bridge.Handle(Request("selection"))["data"]["selection"].is_null(), "selection null passes through");
        auto token = bridge.Handle(Request("revision"))["data"]["revision"].get<std::string>();
        auto ready = bridge.Handle(Request("wait", {{"revision", token}}));
        Check(ready["ok"] == true && ready["data"]["requestedRevision"] == token
              && ready["data"]["displayedRevision"] == firstRevision, "opaque captured token acknowledges displayed source revision");
        ErrorCode(bridge.Handle(Request("wait", {{"revision", "unknown-token"}})), "stale_revision");

        auto mutation = std::async(std::launch::async, [&] { return bridge.Handle(Request("highlight", {{"ids", {"a"}}})); });
        auto actions = AwaitActions(bridge);
        Check(actions.size() == 1 && actions[0]->request["command"] == "highlight", "mutation queued for owning thread");
        Check(mutation.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout, "mutation awaits owner acknowledgement");
        actions[0]->result.set_value(Success("highlight", {{"applied", true}}, "test-session", firstRevision));
        Check(mutation.get()["data"]["applied"] == true && bridge.Drain().empty(), "queued action completes exactly once");

        Write(entry, "import './new.js'");
        Write(imported, "new dependency");
        auto guarded = Request("frame");
        guarded["expectRevision"] = firstRevision;
        ErrorCode(bridge.Handle(guarded), "stale_revision");
        Check(bridge.Drain().empty(), "stale action never queued after disk edit");
        token = bridge.Handle(Request("revision"))["data"]["revision"].get<std::string>();
        // The displayed model is still the old valid scene; a newly captured
        // disk token must not acknowledge it while reload has not completed.
        ErrorCode(bridge.Handle(Request("wait", {{"revision", token}}, 20)), "timeout");
        auto known = CaptureFiles({entry});
        bridge.Publish(Snapshot("loading", known, firstRevision), known);
        ErrorCode(bridge.Handle(Request("wait", {{"revision", token}}, 20)), "timeout");
        auto pending = std::async(std::launch::async, [&] { return bridge.Handle(Request("wait", {{"revision", token}})); });
        auto expanded = CaptureFiles({entry, imported});
        const auto expandedRevision = Revision(expanded);
        bridge.Publish(Snapshot("ready", expanded, expandedRevision), expanded);
        auto expandedResponse = pending.get();
        Check(expandedResponse["ok"] == true
              && expandedResponse["data"]["requestedRevision"] == token
              && expandedResponse["data"]["displayedRevision"] == expandedRevision
              && token != expandedRevision, "new dependency graph reports distinct requested/displayed revisions");

        Write(imported, "broken dependency");
        auto failed = CaptureFiles({entry, imported});
        token = bridge.Handle(Request("revision"))["data"]["revision"].get<std::string>();
        bridge.Publish(Snapshot("failed", failed, expandedRevision), failed);
        auto failure = bridge.Handle(Request("wait", {{"revision", token}}));
        ErrorCode(failure, "load_failed");
        Check(failure["error"]["details"]["displayedRevision"] == expandedRevision
              && failure["error"]["details"]["attemptedRevision"] == Revision(failed), "failure retains valid displayed revision separately");
        auto invalidGuard = Request("snapshot"); invalidGuard["expectRevision"] = expandedRevision;
        ErrorCode(bridge.Handle(invalidGuard), "stale_revision");
        Write(imported, "newer saved dependency");
        ErrorCode(bridge.Handle(Request("wait", {{"revision", token}})), "superseded");
        token = bridge.Handle(Request("revision"))["data"]["revision"].get<std::string>();
        ErrorCode(bridge.Handle(Request("wait", {{"revision", token}}, 20)), "timeout");
        auto recovered = CaptureFiles({entry, imported});
        bridge.Publish(Snapshot("ready", recovered, Revision(recovered)), recovered);
        Check(bridge.Handle(Request("wait", {{"revision", token}}))["ok"] == true, "recovery acknowledges actual attempted bytes");
        fs::remove(imported);
        auto missingAttempt = CaptureFiles({entry, imported});
        token = bridge.Handle(Request("revision"))["data"]["revision"].get<std::string>();
        bridge.Publish(Snapshot("failed", missingAttempt, Revision(recovered)), missingAttempt);
        ErrorCode(bridge.Handle(Request("wait", {{"revision", token}})), "load_failed");
        Write(imported, "restored missing dependency");
        ErrorCode(bridge.Handle(Request("wait", {{"revision", token}})), "superseded");
        recovered = CaptureFiles({entry, imported});
        bridge.Publish(Snapshot("ready", recovered, Revision(recovered)), recovered);
        token = bridge.Handle(Request("revision"))["data"]["revision"].get<std::string>();
        bridge.Publish(Snapshot("ready", recovered, Revision(recovered), "plate"), recovered);
        ErrorCode(bridge.Handle(Request("wait", {{"revision", token}})), "superseded");
        auto plateToken = bridge.Handle(Request("revision"))["data"]["revision"].get<std::string>();
        Check(plateToken != token, "identical source graph has distinct active-view tokens");
        ErrorCode(bridge.Handle(Request("wait", {{"revision", token}})), "superseded");
        Check(bridge.Handle(Request("wait", {{"revision", plateToken}}))["ok"] == true, "new view capture acknowledges identical source graph");

        AgentBridge expiring("deadline-test");
        auto expired = expiring.Handle(Request("frame", {}, 10));
        ErrorCode(expired, "timeout");
        auto expiredActions = expiring.Drain();
        Check(expiredActions.size() == 1 && expiredActions[0]->deadline <= std::chrono::steady_clock::now(), "expired action retains deadline for dispatcher rejection");
        // A dispatcher MUST check deadline before applying side effects. The
        // bridge cannot undo an action that its owning thread already drained.
        expiredActions[0]->result.set_value(Error("frame", "timeout", "Expired before dispatch"));
        expiring.Close();

        AgentBridge closing("close-test");
        auto cancelledMutation = std::async(std::launch::async, [&] { return closing.Handle(Request("frame")); });
        // Give the handler a chance to queue without draining the action;
        // Close owns cancellation of actions that remain in its queue.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        closing.Close();
        ErrorCode(cancelledMutation.get(), "cancelled");
        Check(closing.Drain().empty(), "close clears pending queue");
        ErrorCode(closing.Handle(Request("state")), "cancelled");
        closing.Close(); // Idempotent close must not fulfill promises twice.

        AgentBridge waitingClose("wait-close-test");
        waitingClose.Publish(Snapshot("loading", recovered, ""), recovered);
        auto closeToken = waitingClose.Handle(Request("revision"))["data"]["revision"].get<std::string>();
        auto cancelledWait = std::async(std::launch::async, [&] { return waitingClose.Handle(Request("wait", {{"revision", closeToken}})); });
        waitingClose.Close();
        ErrorCode(cancelledWait.get(), "cancelled");
        std::cout << "Agent bridge tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Agent bridge test failed: " << e.what() << '\n';
        return 1;
    }
}
