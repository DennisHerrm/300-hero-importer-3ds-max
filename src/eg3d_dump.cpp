#include "eg3d_dump.h"

#include <ostream>
#include <string>
#include <vector>

namespace eg3d {

namespace {

std::string I(long long value) { return std::to_string(value); }

std::string IntList(const std::vector<int>& values) {
    if (values.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ",";
        out += std::to_string(values[i]);
    }
    return out;
}

std::string Vec4Text(const Vec4& v) {
    return FormatFloat(v.x) + " " + FormatFloat(v.y) + " " +
           FormatFloat(v.z) + " " + FormatFloat(v.w);
}

const char* AttrLabel(int id) {
    switch (id) {
    case kAttrPosition: return "POSITION";
    case kAttrNormal:   return "NORMAL";
    case kAttrTangent:  return "TANGENT";
    case kAttrColor:    return "COLOR";
    case kAttrBones:    return "BONE_INDICES";
    case kAttrWeights:  return "BONE_WEIGHTS";
    case kAttrUV0:      return "UV0";
    case kAttrUV1:      return "UV1";
    default:            return "ATTR";
    }
}

std::string ChannelKindText(const Channel& channel) {
    switch (channel.kind) {
    case kChannelLocation: return "location";
    case kChannelRotation: return "rotation";
    case kChannelScale:    return "scale";
    default: return channel.kindName.empty() ? std::string("?") : channel.kindName;
    }
}

} // namespace

DumpSections DumpSections::All() {
    DumpSections s;
    s.vertices = s.triangles = s.bindpose = s.animation = s.layout = true;
    return s;
}

DumpSections DumpSections::Summary() {
    DumpSections s;
    s.vertices = s.triangles = s.bindpose = s.animation = s.layout = false;
    return s;
}

// ============================================================
//  .model
// ============================================================

void DumpModel(const ModelFile& file, const ReadResult& result,
               const DumpSections& sections, std::ostream& out) {
    out << "FILE " << file.name << " format=model\n";

    if (sections.container) {
        out << "container size=" << I(static_cast<long long>(file.fileSize))
            << " version=" << I(file.version)
            << " mainSize=" << I(file.mainSize)
            << " jsonStart=" << I(static_cast<long long>(file.jsonStart))
            << " jsonChars=" << I(static_cast<long long>(file.jsonChars))
            << " trailing=" << I(static_cast<long long>(file.jsonTrailing)) << "\n";
        out << "header generator=" << (file.generator.empty() ? "-" : file.generator)
            << " version=" << (file.generatorVersion.empty() ? "-" : file.generatorVersion)
            << " leftHand=" << (file.leftHandCoord ? 1 : 0) << "\n";
        out << "counts materials=" << I(static_cast<long long>(file.materials.size()))
            << " textures=" << I(static_cast<long long>(file.textures.size()))
            << " groups=" << I(static_cast<long long>(file.groups.size()))
            << " submeshes=" << I(file.SubmeshCount())
            << " palettes=" << I(static_cast<long long>(file.palettes.size()))
            << " nodes=" << I(static_cast<long long>(file.nodes.size()))
            << " instances=" << I(static_cast<long long>(file.instances.size()))
            << " clips=" << I(static_cast<long long>(file.clips.size()))
            << " verts=" << I(file.VertexCount())
            << " tris=" << I(file.TriangleCount()) << "\n";
    }

    if (sections.meta) {
        for (size_t i = 0; i < file.materials.size(); ++i) {
            const Material& m = file.materials[i];
            out << "material " << I(static_cast<long long>(i))
                << " name=" << (m.name.empty() ? "-" : m.name)
                << " type=" << (m.type.empty() ? "-" : m.type)
                << " blend=" << I(m.blendMode)
                << " double=" << (m.doubleSide ? 1 : 0)
                << " depthWrite=" << (m.depthWrite ? 1 : 0)
                << " depthTest=" << (m.depthTest ? 1 : 0) << "\n";
            // std::map ist nach Schluessel sortiert - die Reihenfolge
            // ist damit unabhaengig von der Reihenfolge im JSON.
            for (std::map<std::string, int>::const_iterator it = m.textures.begin();
                 it != m.textures.end(); ++it)
                out << "mattex " << I(static_cast<long long>(i)) << " " << it->first
                    << " " << I(it->second) << "\n";
            for (std::map<std::string, Vec4>::const_iterator it = m.vectors.begin();
                 it != m.vectors.end(); ++it)
                out << "matvec " << I(static_cast<long long>(i)) << " " << it->first
                    << " " << Vec4Text(it->second) << "\n";
        }
        for (size_t i = 0; i < file.textures.size(); ++i)
            out << "texture " << I(static_cast<long long>(i)) << " "
                << (file.textures[i].uri.empty() ? "-" : file.textures[i].uri) << "\n";
    }

    for (size_t g = 0; g < file.groups.size(); ++g) {
        for (size_t s = 0; s < file.groups[g].size(); ++s) {
            const Submesh& sub = file.groups[g][s];
            if (sections.geometry) {
                out << "submesh " << sub.Name()
                    << " material=" << I(sub.material)
                    << " verts=" << I(sub.vertexCount)
                    << " idxOffset=" << I(sub.idxOffset)
                    << " idxCount=" << I(sub.idxCount)
                    << " attrs=" << I(static_cast<long long>(sub.attrs.size()))
                    << " badIdx=" << I(sub.outOfRangeIndices) << "\n";
                for (size_t a = 0; a < sub.attrs.size(); ++a) {
                    const AttrDesc& d = sub.attrs[a];
                    out << "attr " << sub.Name() << " " << AttrLabel(d.id)
                        << " id=" << I(d.id)
                        << " type=" << I(d.typeCode)
                        << " qual=" << I(d.qualifier)
                        << " comps=" << I(d.components)
                        << " norm=" << I(d.normalizeFlag)
                        << " stride=" << I(static_cast<long long>(d.stride))
                        << " start=" << I(static_cast<long long>(d.absStart))
                        << " end=" << I(static_cast<long long>(d.absEnd));
                    if (d.hasQuant) {
                        out << " scale=";
                        for (int c = 0; c < d.components; ++c)
                            out << (c ? "," : "") << FormatFloat(d.scale[c], 9);
                        out << " bias=";
                        for (int c = 0; c < d.components; ++c)
                            out << (c ? "," : "") << FormatFloat(d.bias[c], 9);
                    }
                    out << "\n";
                }
            }

            if (sections.vertices) {
                const bool hasNormal = !sub.normals.empty();
                const bool hasTangent = !sub.tangents.empty();
                const bool hasColor = !sub.colors.empty();
                const bool hasSkin = !sub.boneIndices.empty();
                const bool hasWeight = !sub.weights.empty();
                const bool hasUv0 = !sub.uv0.empty();
                const bool hasUv1 = !sub.uv1.empty();
                for (int v = 0; v < sub.vertexCount; ++v) {
                    const size_t vi = static_cast<size_t>(v);
                    out << "v " << sub.Name() << " " << I(v);
                    if (!sub.positions.empty()) out << " P " << Vec3Text(sub.positions[vi]);
                    if (hasNormal)  out << " N " << Vec3Text(sub.normals[vi]);
                    if (hasTangent) out << " T " << Vec4Text(sub.tangents[vi]);
                    if (hasColor)   out << " C " << Vec4Text(sub.colors[vi]);
                    if (hasSkin) {
                        out << " B";
                        for (int c = 0; c < 4; ++c)
                            out << " " << I(sub.boneIndices[vi * 4 + static_cast<size_t>(c)]);
                    }
                    if (hasWeight) {
                        out << " W";
                        for (int c = 0; c < 4; ++c)
                            out << " " << FormatFloat(sub.weights[vi * 4 + static_cast<size_t>(c)]);
                    }
                    if (hasUv0) out << " UV0 " << Vec2Text(sub.uv0[vi]);
                    if (hasUv1) out << " UV1 " << Vec2Text(sub.uv1[vi]);
                    out << "\n";
                }
            }

            if (sections.triangles) {
                for (size_t t = 0; t + 2 < sub.indices.size(); t += 3)
                    out << "t " << sub.Name() << " " << I(static_cast<long long>(t / 3))
                        << " " << I(sub.indices[t])
                        << " " << I(sub.indices[t + 1])
                        << " " << I(sub.indices[t + 2]) << "\n";
            }
        }
    }

    if (sections.skeleton) {
        for (size_t p = 0; p < file.palettes.size(); ++p) {
            const Palette& palette = file.palettes[p];
            out << "palette " << I(static_cast<long long>(p))
                << " bones=" << I(static_cast<long long>(palette.bones.size()))
                << " start=" << I(static_cast<long long>(palette.absStart))
                << " end=" << I(static_cast<long long>(palette.absEnd))
                << " singular=" << I(palette.singular)
                << " nodes=" << IntList(palette.bones) << "\n";
            if (sections.bindpose) {
                for (size_t b = 0; b < palette.bones.size(); ++b) {
                    out << "ibm " << I(static_cast<long long>(p)) << " "
                        << I(static_cast<long long>(b))
                        << " node=" << I(palette.bones[b]);
                    for (int r = 0; r < 4; ++r)
                        for (int c = 0; c < 4; ++c)
                            out << " " << FormatFloat(palette.invBind[b].m[r][c]);
                    out << "\n";
                }
            }
        }

        for (size_t i = 0; i < file.nodes.size(); ++i) {
            const ModelNode& node = file.nodes[i];
            out << "node " << I(static_cast<long long>(i))
                << " name=" << (node.name.empty() ? "-" : node.name)
                << " parent=" << I(node.parent)
                << " mesh=" << I(node.meshGroup)
                << " pal=" << I(node.palette)
                << " hasT=" << (node.hasTranslation ? 1 : 0)
                << " hasR=" << (node.hasRotation ? 1 : 0)
                << " hasS=" << (node.hasScale ? 1 : 0)
                << " T " << Vec3Text(node.translation)
                << " R " << QuatText(node.rotation)
                << " S " << Vec3Text(node.scale)
                << " qlen=" << FormatFloat(node.quatLength)
                << " children=" << IntList(node.children) << "\n";
        }
        for (size_t i = 0; i < file.instances.size(); ++i)
            out << "inst " << I(static_cast<long long>(i))
                << " group=" << I(file.instances[i].group)
                << " node=" << I(file.instances[i].node)
                << " pal=" << I(file.instances[i].palette) << "\n";
        if (!file.unplacedGroups.empty())
            out << "unplaced " << IntList(file.unplacedGroups) << "\n";
    }

    if (sections.clips) {
        for (size_t c = 0; c < file.clips.size(); ++c) {
            const Clip& clip = file.clips[c];
            out << "clip " << I(static_cast<long long>(c))
                << " name=" << (clip.name.empty() ? "-" : clip.name)
                << " flag=" << (clip.flag ? 1 : 0)
                << " channels=" << I(static_cast<long long>(clip.channels.size()))
                << " durationMs=" << FormatFloat(clip.durationMs) << "\n";
            if (!sections.animation) continue;
            for (size_t h = 0; h < clip.channels.size(); ++h) {
                const Channel& channel = clip.channels[h];
                out << "chan " << I(static_cast<long long>(c)) << " "
                    << I(static_cast<long long>(h))
                    << " node=" << I(channel.node)
                    << " kind=" << ChannelKindText(channel)
                    << " f2=" << I(channel.field2)
                    << " keys=" << I(channel.keyCount)
                    << " comps=" << I(channel.components)
                    << " tOff=" << I(channel.timeOffset)
                    << " tType=" << I(channel.timeType)
                    << " vOff=" << I(channel.valueOffset)
                    << " vType=" << I(channel.valueType)
                    << " decoded=" << (channel.decoded ? 1 : 0)
                    << " bool=" << (channel.isBool ? 1 : 0) << "\n";
                for (int k = 0; k < channel.keyCount; ++k) {
                    out << "key " << I(static_cast<long long>(c)) << " "
                        << I(static_cast<long long>(h)) << " " << I(k)
                        << " " << FormatFloat(channel.timesMs[static_cast<size_t>(k)], 3);
                    if (channel.decoded) {
                        for (int comp = 0; comp < channel.components; ++comp) {
                            const size_t at = static_cast<size_t>(k) *
                                              static_cast<size_t>(channel.components) +
                                              static_cast<size_t>(comp);
                            if (channel.isBool)
                                out << " " << I(static_cast<long long>(
                                                    channel.boolValues[at]));
                            else
                                out << " " << FormatFloat(channel.values[at]);
                        }
                    }
                    out << "\n";
                }
            }
        }
    }

    if (sections.layout) {
        long long previousEnd = 0;
        for (size_t i = 0; i < file.layout.size(); ++i) {
            const LayoutBlock& block = file.layout[i];
            const long long gap = (i == 0) ? 0
                : (static_cast<long long>(block.start) - previousEnd);
            out << "layout " << I(static_cast<long long>(i)) << " " << block.owner
                << " " << I(static_cast<long long>(block.start))
                << " " << I(static_cast<long long>(block.end))
                << " " << (i == 0 ? "-" : I(gap)) << "\n";
            previousEnd = static_cast<long long>(block.end);
        }
        const LayoutReport report = VerifyLayout(file);
        out << "layoutsum blocks=" << I(static_cast<long long>(report.blocks))
            << " gaps=" << I(static_cast<long long>(report.gaps))
            << " nonzero=" << I(static_cast<long long>(report.nonZeroGaps))
            << " maxGap=" << I(report.maxGap)
            << " allSame=" << (report.allNonZeroSame ? 1 : 0)
            << " commonGap=" << I(report.commonGap) << "\n";
    }

    if (sections.warnings) {
        for (size_t i = 0; i < result.warnings.size(); ++i)
            out << "warn " << result.warnings[i] << "\n";
    }
    out << "END " << file.name << "\n";
}

// ============================================================
//  .x
// ============================================================

void DumpX(const XFile& file, const ReadResult& result,
           const DumpSections& sections, std::ostream& out) {
    out << "FILE " << file.name << " format=x\n";

    if (sections.container) {
        out << "xcontainer size=" << I(static_cast<long long>(file.fileSize))
            << " sig=" << (file.signature.empty() ? "-" : file.signature)
            << " block1=" << I(static_cast<long long>(file.block1Size))
            << " block2=" << I(static_cast<long long>(file.block2Size))
            << " frames=" << I(file.frames)
            << " dev=" << FormatFloat(file.frameSolveDeviation, 9)
            << " unit=" << FormatFloat(file.frameSolveUnitShare, 6)
            << " rejected=" << IntList(file.rejectedFrameCounts) << "\n";
        out << "xcounts textures=" << I(static_cast<long long>(file.textures.size()))
            << " materials=" << I(static_cast<long long>(file.materials.size()))
            << " meshes=" << I(static_cast<long long>(file.meshes.size()))
            << " bones=" << I(static_cast<long long>(file.bones.size()))
            << " clips=" << I(static_cast<long long>(file.clips.size()))
            << " verts=" << I(file.VertexCount())
            << " tris=" << I(file.TriangleCount())
            << " links=" << I(file.hierarchyLinks)
            << " nanBones=" << I(file.nanBones)
            << " nanFrames=" << I(file.nanFrames)
            << " denormBones=" << I(file.denormBones)
            << " denormKeys=" << I(file.denormKeys) << "\n";
    }

    if (sections.meta) {
        // std::map, also nach Tagnamen sortiert.
        for (std::map<std::string, uint32_t>::const_iterator it = file.tags.begin();
             it != file.tags.end(); ++it)
            out << "tag " << it->first << " " << I(it->second) << "\n";
        for (size_t i = 0; i < file.textures.size(); ++i)
            out << "xtexture " << I(static_cast<long long>(i)) << " "
                << file.textures[i] << "\n";
        for (size_t i = 0; i < file.materials.size(); ++i)
            out << "xmaterial " << I(static_cast<long long>(i))
                << " texture=" << I(file.materials[i].texture) << "\n";
    }

    for (size_t m = 0; m < file.meshes.size(); ++m) {
        const XMesh& mesh = file.meshes[m];
        if (sections.geometry)
            out << "xmesh " << I(static_cast<long long>(m))
                << " name=" << (mesh.name.empty() ? "-" : mesh.name)
                << " material=" << I(mesh.material)
                << " verts=" << I(mesh.vertexCount)
                << " tris=" << I(static_cast<long long>(mesh.indices.size() / 3))
                << " declaredTris=" << I(mesh.triangleCount)
                << " dropped=" << I(mesh.droppedTriangles)
                << " cleared=" << I(mesh.clearedBoneSlots) << "\n";

        if (sections.vertices) {
            for (int v = 0; v < mesh.vertexCount; ++v) {
                const size_t vi = static_cast<size_t>(v);
                out << "xv " << I(static_cast<long long>(m)) << " " << I(v)
                    << " P " << Vec3Text(mesh.positions[vi])
                    << " N " << Vec3Text(mesh.normals[vi])
                    << " UV " << Vec2Text(mesh.uvs[vi])
                    << " PAL " << I(mesh.paletteIndex[vi])
                    << " B";
                for (int c = 0; c < 4; ++c)
                    out << " " << I(mesh.boneIndices[vi * 4 + static_cast<size_t>(c)]);
                out << " W";
                for (int c = 0; c < 4; ++c)
                    out << " " << FormatFloat(mesh.weights[vi * 4 + static_cast<size_t>(c)]);
                out << "\n";
            }
        }
        if (sections.triangles) {
            for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
                out << "xt " << I(static_cast<long long>(m)) << " "
                    << I(static_cast<long long>(t / 3))
                    << " " << I(mesh.indices[t])
                    << " " << I(mesh.indices[t + 1])
                    << " " << I(mesh.indices[t + 2]) << "\n";
        }
    }

    if (sections.skeleton) {
        for (size_t b = 0; b < file.bones.size(); ++b) {
            const XBone& bone = file.bones[b];
            out << "xbone " << I(static_cast<long long>(b))
                << " name=" << (bone.name.empty() ? "-" : bone.name)
                << " parent=" << I(bone.parent)
                << " bpf=" << I(static_cast<long long>(bone.bytesPerFrame))
                << " scale=" << (bone.HasScale() ? 1 : 0)
                << " vis=" << (bone.HasVisible() ? 1 : 0)
                << " children=" << IntList(bone.children) << "\n";
        }
    }

    if (sections.animation) {
        for (size_t b = 0; b < file.bones.size(); ++b) {
            const XBone& bone = file.bones[b];
            for (size_t f = 0; f < bone.pos.size(); ++f) {
                out << "xkey " << I(static_cast<long long>(b)) << " "
                    << I(static_cast<long long>(f))
                    << " P " << Vec3Text(bone.pos[f])
                    << " Q " << QuatText(bone.quat[f]);
                if (bone.HasScale())   out << " S " << Vec3Text(bone.scale[f]);
                if (bone.HasVisible()) out << " V " << I(bone.visible[f] ? 1 : 0);
                out << "\n";
            }
        }
    }

    if (sections.clips) {
        for (size_t c = 0; c < file.clips.size(); ++c)
            out << "xclip " << I(static_cast<long long>(c))
                << " name=" << (file.clips[c].name.empty() ? "-" : file.clips[c].name)
                << " start=" << I(file.clips[c].start)
                << " end=" << I(file.clips[c].end)
                << " synthetic=" << (file.clips[c].synthetic ? 1 : 0) << "\n";
    }

    if (sections.warnings) {
        for (size_t i = 0; i < result.warnings.size(); ++i)
            out << "warn " << result.warnings[i] << "\n";
    }
    out << "END " << file.name << "\n";
}

} // namespace eg3d
