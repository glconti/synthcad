#!/usr/bin/env python3
"""Independently validate core 3MF output and optionally compare a saved archive.

Uses only Python's standard library. Geometry comparison follows component
references, including production-extension paths, rather than resource IDs.
"""
import argparse
from collections import Counter
import hashlib
import io
import json
import math
from pathlib import Path
import posixpath
import sys
import zipfile
import xml.etree.ElementTree as ET

CORE = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
REL = "http://schemas.openxmlformats.org/package/2006/relationships"
TYPES = "http://schemas.openxmlformats.org/package/2006/content-types"
MODEL_TYPE = "application/vnd.ms-package.3dmanufacturing-3dmodel+xml"
MODEL_REL = "http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"
NS = {"m": CORE}
IDENTITY = (1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def transform(node):
    values = tuple(float(v) for v in node.get("transform", " ".join(map(str, IDENTITY))).split())
    require(len(values) == 12 and all(math.isfinite(v) for v in values), "Invalid affine transform")
    determinant = (values[0]*(values[4]*values[8]-values[7]*values[5])
                   - values[3]*(values[1]*values[8]-values[7]*values[2])
                   + values[6]*(values[1]*values[5]-values[4]*values[2]))
    require(abs(determinant) > 1e-15, "Singular affine transform")
    return values


def apply(matrix, point):
    return tuple(sum(matrix[axis+3*column]*point[column] for column in range(3)) + matrix[axis+9] for axis in range(3))


def digest(triangles):
    normalized = sorted(sorted(tuple(round(v, 3) for v in p) for p in triangle) for triangle in triangles)
    return hashlib.sha256(json.dumps(normalized, separators=(",", ":")).encode()).hexdigest()


def inspect(path, strict_metadata=True):
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        require(len(entries) <= 10000, "Archive member budget exceeded")
        require(len({e.filename for e in entries}) == len(entries), "Duplicate archive member names")
        require(sum(e.file_size for e in entries) <= 512*1024*1024, "Archive expanded-size budget exceeded")
        require(all(not e.filename.startswith("/") and ".." not in e.filename.split("/") for e in entries), "Unsafe archive member name")
        require(archive.testzip() is None, "Archive CRC failure")
        types = ET.fromstring(archive.read("[Content_Types].xml"))
        require(types.tag == "{"+TYPES+"}Types", "Invalid OPC content types namespace")
        defaults = {e.get("Extension"): e.get("ContentType") for e in types.findall("{"+TYPES+"}Default")}
        require(defaults.get("rels") == "application/vnd.openxmlformats-package.relationships+xml", "Incorrect OPC relationship content type")
        overrides = {e.get("PartName"): e.get("ContentType") for e in types.findall("{"+TYPES+"}Override")}
        relationships = ET.fromstring(archive.read("_rels/.rels"))
        require(relationships.tag == "{"+REL+"}Relationships", "Invalid OPC relationships namespace")
        roots = [e for e in relationships if e.get("Type") == MODEL_REL]
        require(len(roots) == 1 and roots[0].get("TargetMode", "Internal") == "Internal", "Expected one internal model relationship")
        root_path = roots[0].get("Target", "").lstrip("/")
        require(overrides.get("/"+root_path, defaults.get("model")) == MODEL_TYPE, "Incorrect OPC 3D model content type")
        models = {e.filename: ET.fromstring(archive.read(e)) for e in entries if e.filename.endswith(".model")}
        require(root_path in models, "Model relationship target missing")
        objects, positions, meshes, metadata_issues = {}, {}, {}, []
        for model_path, model in models.items():
            require(model.tag == "{"+CORE+"}model" and model.get("unit", "millimeter") == "millimeter", "Expected core model in millimeters")
            namespaces = dict(value for _, value in ET.iterparse(io.BytesIO(archive.read(model_path)), events=("start-ns",)))
            for entry in model.iter("{"+CORE+"}metadata"):
                name = entry.get("name", "")
                valid_name = bool(name) and (":" not in name or name.split(":", 1)[0] in namespaces)
                if strict_metadata:
                    require(valid_name, "Undeclared metadata QName namespace")
                elif not valid_name:
                    metadata_issues.append({"model": model_path, "name": name, "issue": "undeclared-metadata-QName-namespace"})
            for position, obj in enumerate(model.findall("m:resources/m:object", NS)):
                identifier = obj.get("id", "")
                require(identifier.isdigit() and 0 < int(identifier) < 2147483648, "Invalid resource ID")
                key = (model_path, identifier)
                require(key not in objects, "Duplicate resource ID")
                objects[key], positions[key] = obj, position
                require(obj.find("m:metadata", NS) is None, "Object metadata must use metadatagroup")
                mesh, components = obj.find("m:mesh", NS), obj.find("m:components", NS)
                require((mesh is None) != (components is None), "Object requires exactly one mesh or component list")
                if mesh is None:
                    continue
                vertices = [tuple(float(v.get(axis)) for axis in ("x", "y", "z")) for v in mesh.findall("m:vertices/m:vertex", NS)]
                require(len(vertices) >= 3 and all(math.isfinite(v) for p in vertices for v in p), "Invalid mesh vertices")
                triangles, edges = [], Counter()
                for tri in mesh.findall("m:triangles/m:triangle", NS):
                    indices = tuple(int(tri.get(a)) for a in ("v1", "v2", "v3"))
                    require(len(set(indices)) == 3 and all(0 <= v < len(vertices) for v in indices), "Invalid triangle indices")
                    points = tuple(vertices[v] for v in indices)
                    a, b, c = points
                    u, v = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
                    require(any(u[(i+1)%3]*v[(i+2)%3]-u[(i+2)%3]*v[(i+1)%3] != 0 for i in range(3)), "Degenerate triangle")
                    triangles.append(points)
                    edges.update(zip(indices, indices[1:]+indices[:1]))
                require(triangles and all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()), "Mesh is not closed with consistently oriented indexed edges")
                meshes[key] = triangles

        def target(model_path, node):
            external = next((value for key, value in node.attrib.items() if key.rsplit("}", 1)[-1] == "path"), None)
            child = external.lstrip("/") if external and external.startswith("/") else posixpath.normpath(posixpath.join(posixpath.dirname(model_path), external)) if external else model_path
            key = (child, node.get("objectid"))
            require(key in objects, "Missing component/build object reference")
            return key

        for key, obj in objects.items():
            for component in obj.findall("m:components/m:component", NS):
                child = target(key[0], component)
                require(child[0] != key[0] or positions[child] < positions[key], "Component must reference a preceding object resource")

        def geometry(key, visited=()):
            require(key not in visited and len(visited) < 100, "Cyclic/deep component graph")
            if key in meshes:
                return meshes[key]
            result = []
            for component in objects[key].findall("m:components/m:component", NS):
                matrix = transform(component)
                result.extend(tuple(apply(matrix, p) for p in tri) for tri in geometry(target(key[0], component), (*visited, key)))
                require(len(result) <= 2000000, "Expanded geometry budget exceeded")
            require(result, "Empty component object")
            return result

        builds = []
        for item in models[root_path].findall("m:build/m:item", NS):
            key, matrix = target(root_path, item), transform(item)
            triangles = [tuple(apply(matrix, p) for p in tri) for tri in geometry(key)]
            points = [p for tri in triangles for p in tri]
            builds.append({"name": objects[key].get("name"), "worldGeometryDigest": digest(triangles), "triangles": len(triangles),
                           "bounds": {"min": [min(p[a] for p in points) for a in range(3)], "max": [max(p[a] for p in points) for a in range(3)]}})
        require(builds, "Empty manufacturing build")
        return {"archive": str(Path(path).resolve()), "meshResources": len(meshes), "objectResources": len(objects), "buildInstances": builds,
                "metadataIssues": metadata_issues, "status": "warning" if metadata_issues else "passed"}


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True, help="Actual core 3MF export")
    parser.add_argument("--compare", type=Path, help="Optional real-app saved archive; compare world triangles at 0.001 mm")
    args = parser.parse_args()
    report = inspect(args.archive)
    if args.compare:
        # Some consumers retain a metadata QName but discard its declaration.
        # Keep original validation strict; expose saved-file metadata defects
        # separately while still measuring whether its geometry was preserved.
        saved = inspect(args.compare, strict_metadata=False)
        require(Counter(b["worldGeometryDigest"] for b in report["buildInstances"]) == Counter(b["worldGeometryDigest"] for b in saved["buildInstances"]), "Distinct build-instance world geometry changed at 0.001 mm precision")
        report = {"original": report, "saved": saved, "geometryComparison": "passed", "worldGeometryPrecisionMm": 0.001}
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
