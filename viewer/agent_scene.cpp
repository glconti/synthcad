#include "agent_scene.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace synthcad {
namespace {
using json = nlohmann::json;
template<class Vector> json Coordinates(const Vector& point) {
    return json::array({point.x, point.y, point.z});
}
json PartIds(const dingcad::PartTree& tree, const std::vector<size_t>& parts) {
    auto result = json::array();
    for (auto i : parts) result.push_back(tree.parts.at(i).id);
    return result;
}
std::string ColorHex(Color color) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "#";
    for (auto channel : {color.r, color.g, color.b}) {
        result += hex[channel >> 4];
        result += hex[channel & 15];
    }
    return result;
}
const char* AnnotationType(dingcad::DimensionType type) {
    switch (type) {
        case dingcad::DimensionType::Linear: return "linear";
        case dingcad::DimensionType::Diameter: return "diameter";
        case dingcad::DimensionType::Radius: return "radius";
    }
    throw std::runtime_error("Unknown annotation type");
}
}

json ReviewSnapshot(const dingcad::PartTree& tree,
                    const std::vector<dingcad::Dimension>& dimensions,
                    const Camera3D& camera,
                    const std::vector<std::string>& highlights) {
    json result = {{"parts", json::array()}, {"groups", json::array()},
                   {"selection", nullptr}, {"annotations", json::array()},
                   {"highlights", highlights},
                   {"camera", {{"position", Coordinates(camera.position)},
                               {"target", Coordinates(camera.target)},
                               {"up", Coordinates(camera.up)}, {"fovy", camera.fovy}}}};
    for (size_t i = 0; i < tree.parts.size(); ++i) {
        const auto& part = tree.parts[i];
        json bounds = nullptr;
        if (part.solid && !part.solid->IsEmpty()) {
            const auto box = part.solid->BoundingBox();
            if (std::isfinite(box.min.x) && std::isfinite(box.min.y) && std::isfinite(box.min.z)
                    && std::isfinite(box.max.x) && std::isfinite(box.max.y) && std::isfinite(box.max.z)) {
                bounds = {{"min", Coordinates(box.min)}, {"max", Coordinates(box.max)}};
            }
        }
        json item = {{"id", part.id}, {"name", part.name},
            {"group", part.group}, {"color", ColorHex(part.color)},
            {"visible", tree.Visible(i)},
            {"exportable", tree.state.flags.at(part.id).exportable},
            {"bounds", std::move(bounds)}};
        if (!part.sourcePartId.empty()) {
            item["sourcePartId"] = part.sourcePartId;
            item["instanceId"] = part.id;
            item["transform"] = {{"rotate", part.rotation}, {"translate", part.translation}};
        }
        result["parts"].push_back(std::move(item));
    }
    for (const auto& node : tree.nodes) {
        if (!node.group) continue;
        json group = {{"key", node.key}, {"name", node.name},
                      {"parent", nullptr}, {"partIds", PartIds(tree, node.parts)}};
        if (node.parent >= 0) group["parent"] = tree.nodes.at(static_cast<size_t>(node.parent)).key;
        if (!node.sourceId.empty()) group["sourceId"] = node.sourceId;
        result["groups"].push_back(std::move(group));
    }
    if (const auto selection = tree.Selection()) {
        const auto& node = tree.nodes[*selection];
        result["selection"] = {{"key", node.key}, {"name", node.name},
                               {"group", node.group}, {"partIds", PartIds(tree, node.parts)}};
    }
    for (const auto& dimension : dimensions) {
        result["annotations"].push_back({{"type", AnnotationType(dimension.type)},
            {"label", dimension.label}, {"value", dimension.value},
            {"start", Coordinates(dimension.start)}, {"end", Coordinates(dimension.end)},
            {"marker", Coordinates(dimension.marker)}});
    }
    return result;
}

std::vector<size_t> ResolveReviewParts(const dingcad::PartTree& tree,
                                      const std::vector<std::string>& ids,
                                      bool selection) {
    std::vector<bool> included(tree.parts.size(), false);
    if (ids.empty()) {
        if (selection) {
            if (const auto selected = tree.Selection()) {
                for (auto i : tree.nodes[*selected].parts) included.at(i) = true;
            }
        } else {
            for (size_t i = 0; i < tree.parts.size(); ++i) included[i] = tree.Visible(i);
        }
    } else {
        for (const auto& id : ids) {
            const auto part = std::find_if(tree.parts.begin(), tree.parts.end(),
                [&](const auto& candidate) { return candidate.id == id; });
            if (part != tree.parts.end()) {
                included[static_cast<size_t>(part - tree.parts.begin())] = true;
                continue;
            }
            const auto group = std::find_if(tree.nodes.begin(), tree.nodes.end(),
                [&](const auto& candidate) { return candidate.group && candidate.key == id; });
            if (group == tree.nodes.end()) throw std::runtime_error("Unknown part or group reference '" + id + "'");
            for (auto i : group->parts) included.at(i) = true;
        }
    }
    std::vector<size_t> result;
    for (size_t i = 0; i < included.size(); ++i) if (included[i]) result.push_back(i);
    return result;
}
}
