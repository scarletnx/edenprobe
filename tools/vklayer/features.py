#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes features.inc, the tables the masquerade layer and its check read, from the Vulkan
# registry (vk.xml): every device extension's name, every feature structure that extends
# VkPhysicalDeviceFeatures2 with its structure type and its members, the core version that
# has it, and the extensions that add it. `make features` runs it again on a newer registry.
#
#   features.py VK_XML OUT
#
# A feature structure is sType, pNext, then VkBool32 members only; a structure that is not
# stops the script, since the layer finds a member by its place after pNext.

import re
import sys
import xml.etree.ElementTree as ET


def vulkan(element):
    return "vulkan" in (element.get("api") or "vulkan").split(",")


def enum_value(enum, number):
    if enum.get("value") is not None:
        return int(enum.get("value"), 0)
    value = 1000000000 + (int(enum.get("extnumber") or number) - 1) * 1000 + int(enum.get("offset"))
    return -value if enum.get("dir") == "-" else value


def main():
    registry = ET.parse(sys.argv[1]).getroot()
    # "#define VK_HEADER_VERSION 357" and "... VK_MAKE_API_VERSION(0, 1, 4, VK_HEADER_VERSION)", Vulkan's
    defines = {t.findtext("name"): "".join(t.itertext()) for t in registry.find("types")
               if t.get("category") == "define" and vulkan(t) and t.findtext("name")}
    major, minor = re.search(r"\(\s*0\s*,\s*(\d+)\s*,\s*(\d+)\s*,", defines["VK_HEADER_VERSION_COMPLETE"]).groups()
    version = "%s.%s.%s" % (major, minor, defines["VK_HEADER_VERSION"].split()[-1])

    # VkStructureType's values: core ones in the enums block, the rest where a version or an
    # extension adds them; an alias resolves to the value it names
    values, aliases = {}, {}
    for enum in registry.find("enums[@name='VkStructureType']"):
        if enum.tag == "enum" and enum.get("value") is not None:
            values[enum.get("name")] = int(enum.get("value"), 0)
        elif enum.tag == "enum" and enum.get("alias"):
            aliases[enum.get("name")] = enum.get("alias")
    blocks = [(f, None) for f in registry.findall("feature")]
    blocks += [(e, e.get("number")) for e in registry.find("extensions")]
    for block, number in blocks:
        for enum in block.iter("enum"):
            if enum.get("extends") != "VkStructureType":
                continue
            if enum.get("alias"):
                aliases[enum.get("name")] = enum.get("alias")
            else:
                values[enum.get("name")] = enum_value(enum, number)
    while True:
        resolved = {k: values[v] for k, v in aliases.items() if v in values and k not in values}
        if not resolved:
            break
        values.update(resolved)

    # the feature structures, canonical names only (an alias names the same structure type)
    structs, alias_of = {}, {}
    for t in registry.find("types"):
        if t.get("category") != "struct":
            continue
        if t.get("alias"):
            alias_of[t.get("name")] = t.get("alias")
            continue
        if "VkPhysicalDeviceFeatures2" not in (t.get("structextends") or "").split(","):
            continue
        members = [m for m in t.findall("member") if vulkan(m)]
        if [m.findtext("name") for m in members[:2]] != ["sType", "pNext"]:
            sys.exit("features.py: %s does not start with sType and pNext" % t.get("name"))
        for m in members[2:]:
            if m.findtext("type") != "VkBool32" or "[" in "".join(m.itertext()):
                sys.exit("features.py: %s.%s is not a VkBool32" % (t.get("name"), m.findtext("name")))
        stype = members[0].get("values")
        structs[t.get("name")] = {"stype": values[stype], "stype_name": stype, "core": 0, "owners": set(),
                                  "members": [m.findtext("name") for m in members[2:]]}
    core10 = registry.find("types/type[@name='VkPhysicalDeviceFeatures']")
    first = {"stype": 0, "stype_name": "VkPhysicalDeviceFeatures", "core": 10, "owners": set(),
             "members": [m.findtext("name") for m in core10.findall("member") if vulkan(m)]}

    def canonical(name):
        while name in alias_of:
            name = alias_of[name]
        return name

    for feature in registry.findall("feature"):
        if not vulkan(feature):
            continue
        major, minor = feature.get("number").split(".")
        for t in feature.iter("type"):
            s = structs.get(canonical(t.get("name")))
            if s is not None and (s["core"] == 0 or s["core"] > int(major) * 10 + int(minor)):
                s["core"] = int(major) * 10 + int(minor)
    extensions = []
    for e in registry.find("extensions"):
        if "vulkan" not in (e.get("supported") or "").split(","):
            continue
        if e.get("type") == "device":
            extensions.append(e.get("name"))
        for require in e.findall("require"):
            if not vulkan(require):
                continue
            for t in require.iter("type"):
                s = structs.get(canonical(t.get("name")))
                if s is not None:
                    s["owners"].add(e.get("name"))

    ordered = [("VkPhysicalDeviceFeatures", first)] + sorted(structs.items(), key=lambda kv: kv[1]["stype"])
    out = open(sys.argv[2], "w", newline="\n")
    out.write("// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors\n")
    out.write("// SPDX-License-Identifier: GPL-3.0-or-later\n")
    out.write("//\n// Made by features.py from the Vulkan registry %s: `make features` makes it again.\n" % version)
    out.write("// Read by masquerade.c and check_masquerade.c through features.h, nothing else.\n\n")
    out.write('#define MASQUERADE_REGISTRY "%s"\n\n' % version)
    out.write("static const char *const MEMBERS[] = {\n")
    starts, at = [], 0
    for name, s in ordered:
        starts.append(at)
        out.write("    // %s\n" % name)
        for m in s["members"]:
            out.write('    "%s",\n' % m)
        at += len(s["members"])
    out.write("};\n\n")
    out.write("// sType (0: VkPhysicalDeviceFeatures, inside VkPhysicalDeviceFeatures2), the core version that\n")
    out.write("// has it as tens (13 is 1.3; 0 when none does), its first member in MEMBERS and their count\n")
    out.write("static const MaskStruct STRUCTS[] = {\n")
    for (name, s), start in zip(ordered, starts):
        out.write("    { %d, %d, %d, %d }, // %s\n" % (s["stype"], s["core"], start, len(s["members"]), name))
    out.write("};\n\n")
    out.write("// each extension that adds a feature structure, with that structure's place in STRUCTS\n")
    out.write("static const MaskOwner OWNERS[] = {\n")
    for i, (name, s) in enumerate(ordered):
        for owner in sorted(s["owners"]):
            out.write('    { "%s", %d },\n' % (owner, i))
    out.write("};\n\n")
    out.write("static const char *const DEVICE_EXTENSIONS[] = {\n")
    for e in sorted(extensions):
        out.write('    "%s",\n' % e)
    out.write("};\n")


main()
