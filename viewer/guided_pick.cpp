#include "guided_pick.h"
#include <charconv>

namespace synthcad {
using json = nlohmann::json;
namespace {
void ValidateText(const std::string& value, size_t limit, bool nonempty, const char* field) {
  if ((nonempty && value.empty()) || value.size() > limit || value.find('\0') != std::string::npos)
    throw GuidedPickError("invalid_argument", std::string("Invalid ") + field);
  try { (void)json(value).dump(); }
  catch (const json::exception&) { throw GuidedPickError("invalid_argument", std::string("Invalid UTF-8 in ") + field); }
}
std::string RequiredString(const json& args, const char* field) {
  if (!args.is_object() || !args.contains(field) || !args[field].is_string())
    throw GuidedPickError("invalid_argument", std::string("Expected string ") + field);
  return args[field].get<std::string>();
}
bool NonemptyString(const json& value, const char* field) {
  return value.contains(field) && value[field].is_string() && !value[field].get_ref<const std::string&>().empty();
}
}
GuidedPickState::GuidedPickState(std::string epoch) : epoch_(std::move(epoch)) {
  if (epoch_.empty() || epoch_.find(':') != std::string::npos) throw std::invalid_argument("Invalid event epoch");
}
std::string GuidedPickState::Cursor() const { return epoch_ + ":" + std::to_string(sequence_); }
json GuidedPickState::Active() const { return active_.empty() ? json(nullptr) : requests_.at(active_); }
json GuidedPickState::Status(const std::string& id) const {
  ValidateText(id, 128, true, "id");
  const auto it = requests_.find(id);
  if (it == requests_.end()) throw GuidedPickError("not_found", "Unknown pick request ID");
  return it->second;
}
std::optional<json> GuidedPickState::Replay(const json& args) const {
  const auto id = RequiredString(args, "id"), kind = RequiredString(args, "kind"), question = RequiredString(args, "question");
  ValidateText(id, 128, true, "id"); ValidateText(question, 4096, true, "question");
  if (kind != "part" && kind != "surface" && kind != "edge" && kind != "vertex")
    throw GuidedPickError("invalid_argument", "Unknown pick kind");
  const auto it = requests_.find(id);
  if (it != requests_.end()) {
    const auto& old = it->second;
    if (old["kind"] != kind || old["question"] != question)
      throw GuidedPickError("invalid_argument", "Pick request ID is already bound to a different payload");
    return json{{"created", false}, {"request", old}, {"eventCursor", Cursor()}};
  }
  return {};
}
json GuidedPickState::Begin(const json& args, const std::string& revision) {
  if (auto existing=Replay(args)) return *existing;
  const auto id=args["id"].get<std::string>(),kind=args["kind"].get<std::string>(),question=args["question"].get<std::string>();
  if (closed_) throw GuidedPickError("cancelled", "Viewer closed");
  if (!active_.empty() || requests_.size() >= 1024) throw GuidedPickError("busy", "Pick request capacity is occupied");
  if (revision.empty()) throw GuidedPickError("busy", "No displayed revision is available");
  requests_[id] = {{"id", id}, {"kind", kind}, {"question", question}, {"revision", revision}, {"status", "pending"}};
  active_ = id;
  Emit("pick-started", requests_.at(id));
  return {{"created", true}, {"request", requests_.at(id)}, {"eventCursor", Cursor()}};
}
void GuidedPickState::LoadEvent(const std::string& type,const json& context){
  ++sequence_;auto event=context;event["cursor"]=Cursor();event["type"]=type;
  event["requestId"]=nullptr;event["revision"]=context.value("displayedRevision",std::string{});
  events_.push_back(std::move(event));if(events_.size()>256)events_.pop_front();
}
void GuidedPickState::Emit(const std::string& type, const json& request) {
  ++sequence_;
  json event = {{"cursor", Cursor()}, {"type", type}, {"requestId", request.is_null() ? json(nullptr) : request.at("id")},
                {"revision", request.is_null() ? json("") : request.at("revision")}};
  if (request.is_object()) for (const auto* field : {"selection", "reason"}) if (request.contains(field)) event[field] = request[field];
  events_.push_back(std::move(event));
  if (events_.size() > 256) events_.pop_front();
}
void GuidedPickState::Finish(const std::string& status, const std::string& type, const std::string& reason) {
  if (active_.empty()) return;
  auto& request = requests_.at(active_);
  request["status"] = status;
  if (!reason.empty()) request["reason"] = reason;
  Emit(type, request); active_.clear();
}
json GuidedPickState::Cancel(const std::string& id, const std::string& reason) {
  (void)Status(id);
  if (id == active_) Finish("cancelled", "pick-cancelled", reason);
  return Status(id);
}
void GuidedPickState::Invalidate(const std::string& reason) { Finish("invalidated", "pick-invalidated", reason); }
bool GuidedPickState::Confirm(const std::string& id, const json& geometry, const std::string& revision) {
  if (id != active_ || active_.empty() || !geometry.is_object()) return false;
  auto& request = requests_.at(active_);
  if (revision != request["revision"] || !NonemptyString(geometry,"revision") || geometry["revision"] != revision ||
      !NonemptyString(geometry,"kind") || !NonemptyString(geometry,"reference") || !NonemptyString(geometry,"partId")) return false;
  const auto kind = geometry["kind"].get<std::string>();
  const auto wanted = request["kind"].get<std::string>();
  if (wanted == "surface" ? (kind != "planar-face" && kind != "curved-patch") : kind != wanted) return false;
  if (!geometry.contains("sourcePartId") || !geometry.contains("instanceId")) return false;
  if (geometry["sourcePartId"].is_null()) { if (!geometry["instanceId"].is_null()) return false; }
  else if (!NonemptyString(geometry,"sourcePartId") || !NonemptyString(geometry,"instanceId") || geometry["instanceId"] != geometry["partId"]) return false;
  request["selection"] = geometry;
  Finish("confirmed", "pick-confirmed", ""); return true;
}
void GuidedPickState::Close() {
  if (closed_) return;
  closed_ = true; Finish("cancelled", "pick-cancelled", "viewer_closed"); Emit("viewer-closed", nullptr);
}
json GuidedPickState::Events(const std::string& after) const {
  uint64_t sequence = 0;
  if (after != "0") {
    const auto prefix = epoch_ + ":";
    if (after.compare(0, prefix.size(), prefix) != 0) throw GuidedPickError("stale_cursor", "Event cursor belongs to another viewer session");
    const auto text = after.substr(prefix.size());
    const auto parsed = std::from_chars(text.data(), text.data()+text.size(), sequence);
    if (text.empty() || parsed.ec != std::errc() || parsed.ptr != text.data()+text.size() || text != std::to_string(sequence))
      throw GuidedPickError("stale_cursor", "Invalid event cursor");
  }
  if (sequence > sequence_ || (after != "0" && sequence < sequence_ - events_.size())) throw GuidedPickError("stale_cursor", "Event cursor is expired or in the future");
  json result = json::array();
  const auto first = sequence_ - events_.size() + 1;
  for (size_t i = 0; i < events_.size(); ++i) if (first+i > sequence) result.push_back(events_[i]);
  json data={{"events", result}, {"cursor", Cursor()}};
  if(after=="0"&&sequence_>events_.size())data["truncated"]=true;
  return data;
}
}
