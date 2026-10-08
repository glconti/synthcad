#include "agent_scene.h"

#include <iostream>
#include <stdexcept>

using namespace synthcad;
using namespace dingcad;
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
DisplayPart Part(std::string id, std::string name, std::vector<std::string> group,
                 bool exportable, double x) {
    return {std::make_shared<manifold::Manifold>(
        manifold::Manifold::Cube({2, 3, 4}).Translate({x, 10, 20})),
        Color{0x12, 0xab, 0xef, 255}, id, name, group, exportable};
}
}
int main() {
    try {
        PartTree tree;
        tree.Reload({Part("a", u8"Flangia più larga", {u8"Assieme città", "Interno"}, true, 100),
                     Part("b", u8"Parete è", {u8"Assieme città", "Interno"}, false, -5),
                     Part("reference", "Riferimento", {}, false, 0)});
        size_t inner = 0;
        for (size_t i = 0; i < tree.nodes.size(); ++i) if (tree.nodes[i].name == "Interno") inner = i;
        tree.state.flags.at("a").visible = false;
        tree.state.flags.at("b").exportable = true;
        tree.state.flags.at("b").exportOverride = true;
        tree.state.collapsed.insert(tree.nodes[inner].key);
        tree.Select(inner);
        const auto selectedBefore = tree.state.selected;
        const auto flagsBefore = tree.state.flags;
        const auto collapsedBefore = tree.state.collapsed;
        Camera3D camera{{10, 11, 12}, {1, 2, 3}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
        Dimension dimension{DimensionType::Diameter, u8"Diametro più stretto", 12.5,
                            {1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
        auto snapshot = ReviewSnapshot(tree, {dimension}, camera, {"b"});
        Check(snapshot["parts"].size() == 3 && snapshot["groups"].size() == 2, "semantic parts/groups count");
        Check(snapshot["parts"][0]["name"] == u8"Flangia più larga", "authored Italian Unicode preserved");
        Check(snapshot.dump(-1, ' ', false).find(u8"Flangia più larga") != std::string::npos, "UTF-8 readable JSON");
        Check(snapshot["parts"][0]["color"] == "#12abef", "RGB color hex");
        Check(snapshot["parts"][0]["visible"] == false && snapshot["parts"][0]["exportable"] == true, "hidden remains exportable");
        Check(snapshot["parts"][1]["visible"] == true && snapshot["parts"][1]["exportable"] == true, "export override exposed");
        Check(snapshot["parts"][2]["exportable"] == false, "reference export default exposed");
        for (const auto& part : snapshot["parts"])
            Check(!part.contains("sourcePartId") && !part.contains("instanceId") && !part.contains("transform"),
                  "legacy snapshots retain their original part schema");
        for (const auto& group : snapshot["groups"])
            Check(!group.contains("sourceId"), "legacy snapshots retain their original group schema");
        Check(snapshot["parts"][0]["bounds"]["min"] == nlohmann::json::array({100, 10, 20})
              && snapshot["parts"][0]["bounds"]["max"] == nlohmann::json::array({102, 13, 24}), "bounds retain CAD millimetres and Z up");
        Check(snapshot["groups"][0]["parent"].is_null()
              && snapshot["groups"][1]["parent"] == tree.nodes[0].key, "nested group parents exposed");
        Check(snapshot["selection"]["group"] == true
              && snapshot["selection"]["partIds"] == nlohmann::json::array({"a", "b"}), "group selection semantic IDs");
        Check(snapshot["camera"]["position"] == nlohmann::json::array({10, 11, 12})
              && snapshot["camera"]["fovy"] == 45, "camera remains world coordinates");
        Check(snapshot["annotations"][0]["type"] == "diameter"
              && snapshot["annotations"][0]["label"] == dimension.label
              && snapshot["annotations"][0]["marker"] == nlohmann::json::array({7, 8, 9})
              && snapshot["annotations"][0]["value"] == 12.5, "annotations preserve authored CAD coordinates");
        Check(snapshot["highlights"] == nlohmann::json::array({"b"}), "agent highlights exposed separately");
        Check(tree.state.selected == selectedBefore && tree.state.collapsed == collapsedBefore
              && tree.state.flags.size() == flagsBefore.size(), "serializer preserves session state");
        for (const auto& item : flagsBefore) {
            const auto& now = tree.state.flags.at(item.first);
            Check(now.visible == item.second.visible && now.exportable == item.second.exportable
                  && now.exportOverride == item.second.exportOverride, "serializer preserves every part flag");
        }
        Check(ResolveReviewParts(tree, {}) == std::vector<size_t>({1, 2}), "default frame resolves visible parts");
        Check(ResolveReviewParts(tree, {}, true) == std::vector<size_t>({0, 1}), "selection includes hidden descendants");
        Check(ResolveReviewParts(tree, {"a"}) == std::vector<size_t>({0}), "authored hidden ID resolves");
        Check(ResolveReviewParts(tree, {"b", tree.nodes[inner].key, "a", "b"}) == std::vector<size_t>({0, 1}), "references union dedupes in tree order");
        for (const auto& invalid : {std::string("unknown"), std::string("p:a"), std::string("Interno")}) {
            bool failed = false;
            try { ResolveReviewParts(tree, {"a", invalid}); } catch (const std::runtime_error&) { failed = true; }
            Check(failed, "unknown references reject entire request");
        }
        tree.state.selected.clear();
        Check(ResolveReviewParts(tree, {}, true).empty(), "absent selection resolves empty");
        Check(ReviewSnapshot(tree, {}, camera, {})["selection"].is_null(), "absent selection serializes null");
        auto instance = Part("instance-1", "Instance", {}, true, 4);
        instance.sourcePartId = "source";
        instance.rotation = {0, 0, 90};
        instance.translation = {4, 10, 20};
        instance.memberships = {{{"left", "Left"}, {"shared", "Shared"}},
                                {{"right", "Right"}, {"shared", "Shared"}},
                                {{"left", "Left"}, {"shared", "Shared"}}};
        auto otherInstance = instance;
        otherInstance.id = "instance-2";
        otherInstance.memberships = {{{"left", "Left"}, {"shared", "Shared"}}};
        tree.Reload({instance, otherInstance});
        auto graphSnapshot = ReviewSnapshot(tree, {}, camera, {});
        Check(graphSnapshot["parts"].size() == 2 && graphSnapshot["groups"].size() == 4,
              "snapshot emits unique instances and unfolded group paths");
        Check(graphSnapshot["parts"][0]["id"] == "instance-1"
              && graphSnapshot["parts"][0]["instanceId"] == "instance-1"
              && graphSnapshot["parts"][0]["sourcePartId"] == "source"
              && graphSnapshot["parts"][1]["sourcePartId"] == "source", "instance and source identities exposed separately");
        Check(graphSnapshot["parts"][0]["transform"]["rotate"] == nlohmann::json::array({0, 0, 90})
              && graphSnapshot["parts"][0]["transform"]["translate"] == nlohmann::json::array({4, 10, 20}),
              "snapshot preserves authored instance transform");
        Check(graphSnapshot["groups"][0]["sourceId"] == "left"
              && graphSnapshot["groups"][1]["sourceId"] == "shared"
              && graphSnapshot["groups"][3]["sourceId"] == "shared"
              && graphSnapshot["groups"][1]["key"] != graphSnapshot["groups"][3]["key"],
              "group snapshots expose authored IDs and distinct path keys");
        Check(graphSnapshot["groups"][0]["partIds"] == nlohmann::json::array({"instance-1", "instance-2"}),
              "snapshot ancestor membership excludes duplicate aliases");
        Check(ResolveReviewParts(tree, {tree.nodes[0].key, "instance-1"}) == std::vector<size_t>({0, 1}),
              "graph group review resolution dedupes shared physical instances");
        tree.Reload({});
        auto empty = ReviewSnapshot(tree, {}, camera, {});
        Check(empty["parts"].empty() && empty["groups"].empty() && empty["annotations"].empty(), "empty scene snapshot");
        std::cout << "Agent scene tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Agent scene test failed: " << e.what() << '\n';
        return 1;
    }
}
