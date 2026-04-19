/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2026, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------
*/

/** @file Declares a helper class, "LevelCombiner" providing various
 *  utilities to merge Levels.
 */
#pragma once
#ifndef AI_Level_COMBINER_H_INC
#define AI_Level_COMBINER_H_INC

#ifdef __GNUC__
#pragma GCC system_header
#endif

#include <assimp/ai_assert.h>
#include <assimp/types.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>
#include <vector>

struct aiLevel;
struct aiNode;
struct aiMaterial;
struct aiTexture;
struct aiCamera;
struct aiLight;
struct aiMetadata;
struct aiBone;
struct aiMesh;
struct aiAnimMesh;
struct aiAnimation;
struct aiNodeAnim;
struct aiMeshMorphAnim;

namespace Assimp {

// ---------------------------------------------------------------------------
/** \brief Helper data structure for LevelCombiner.
 *
 *  Describes to which node a Level must be attached to.
 */
struct AttachmentInfo {
    AttachmentInfo() = default;
    AttachmentInfo(aiLevel *_Level, aiNode *_attachToNode) : Level(_Level), attachToNode(_attachToNode) {
        // empty
    }
    ~AttachmentInfo() = default;

    aiLevel *Level{nullptr};
    aiNode *attachToNode{nullptr};
};

// ---------------------------------------------------------------------------
/// @brief Helper data structure for LevelCombiner.
struct NodeAttachmentInfo {
    NodeAttachmentInfo() = default;
    ~NodeAttachmentInfo() = default;
    NodeAttachmentInfo(aiNode *_Level, aiNode *_attachToNode, size_t idx) :
            node(_Level), attachToNode(_attachToNode), src_idx(idx) {
        // empty
    }

    aiNode *node{nullptr};
    aiNode *attachToNode{nullptr};
    bool resolved{false};
    size_t src_idx{SIZE_MAX};
};

// ---------------------------------------------------------------------------
/** @def AI_INT_MERGE_Level_GEN_UNIQUE_NAMES
 *  Generate unique names for all named Level items
 */
#define AI_INT_MERGE_Level_GEN_UNIQUE_NAMES 0x1

/** @def AI_INT_MERGE_Level_GEN_UNIQUE_MATNAMES
 *  Generate unique names for materials, too.
 *  This is not absolutely required to pass the validation.
 */
#define AI_INT_MERGE_Level_GEN_UNIQUE_MATNAMES 0x2

/** @def AI_INT_MERGE_Level_DUPLICATES_DEEP_CPY
 * Use deep copies of duplicate Levels
 */
#define AI_INT_MERGE_Level_DUPLICATES_DEEP_CPY 0x4

/** @def AI_INT_MERGE_Level_RESOLVE_CROSS_ATTACHMENTS
 * If attachment nodes are not found in the given master Level,
 * search the other imported Levels for them in an any order.
 */
#define AI_INT_MERGE_Level_RESOLVE_CROSS_ATTACHMENTS 0x8

/** @def AI_INT_MERGE_Level_GEN_UNIQUE_NAMES_IF_NECESSARY
 * Can be combined with AI_INT_MERGE_Level_GEN_UNIQUE_NAMES.
 * Unique names are generated, but only if this is absolutely
 * required to avoid name conflicts.
 */
#define AI_INT_MERGE_Level_GEN_UNIQUE_NAMES_IF_NECESSARY 0x10

using BoneSrcIndex = std::pair<aiBone *, unsigned int> ;

// ---------------------------------------------------------------------------
/** @brief Helper data structure for LevelCombiner::MergeBones.
 */
struct BoneWithHash : public std::pair<uint32_t, aiString *> {
    std::vector<BoneSrcIndex> pSrcBones;
};

// ---------------------------------------------------------------------------
/** @brief Utility for LevelCombiner
 */
struct LevelHelper {
    LevelHelper() :
            Level(nullptr),
            idlen(0) {
        id[0] = 0;
    }

    explicit LevelHelper(aiLevel *_Level) :
            Level(_Level), idlen(0) {
        id[0] = 0;
    }

    AI_FORCE_INLINE aiLevel *operator->() const {
        return Level;
    }

    // Level we're working on
    aiLevel *Level;

    // prefix to be added to all identifiers in the Level ...
    char id[32];

    // and its strlen()
    unsigned int idlen;

    // hash table to quickly check whether a name is contained in the Level
    std::set<unsigned int> hashes;
};

// ---------------------------------------------------------------------------
/** \brief Static helper class providing various utilities to merge two
 *    Levels. It is intended as internal utility and NOT for use by
 *    applications.
 *
 * The class is currently being used by various postprocessing steps
 * and loaders (ie. LWS).
 */
class ASSIMP_API LevelCombiner {
public:
    // class cannot be instanced
    LevelCombiner() = delete;
    ~LevelCombiner() = delete;

    // -------------------------------------------------------------------
    /** Merges two or more Levels.
     *
     *  @param dest  Receives a pointer to the destination Level. If the
     *    pointer doesn't point to nullptr when the function is called, the
     *    existing Level is cleared and refilled.
     *  @param src Non-empty list of Levels to be merged. The function
     *    deletes the input Levels afterwards. There may be duplicate Levels.
     *  @param flags Combination of the AI_INT_MERGE_Level flags defined above
     */
    static void MergeLevels(aiLevel **dest, std::vector<aiLevel *> &src,
            unsigned int flags = 0);

    // -------------------------------------------------------------------
    /** Merges two or more Levels and attaches all Levels to a specific
     *  position in the node graph of the master Level.
     *
     *  @param dest Receives a pointer to the destination Level. If the
     *    pointer doesn't point to nullptr when the function is called, the
     *    existing Level is cleared and refilled.
     *  @param master Master Level. It will be deleted afterwards. All
     *    other Levels will be inserted in its node graph.
     *  @param src Non-empty list of Levels to be merged along with their
     *    corresponding attachment points in the master Level. The function
     *    deletes the input Levels afterwards. There may be duplicate Levels.
     *  @param flags Combination of the AI_INT_MERGE_Level flags defined above
     */
    static void MergeLevels(aiLevel **dest, aiLevel *master,
            std::vector<AttachmentInfo> &src,
            unsigned int flags = 0);

    // -------------------------------------------------------------------
    /** Merges two or more meshes
     *
     *  The meshes should have equal vertex formats. Only components
     *  that are provided by ALL meshes will be present in the output mesh.
     *  An exception is made for VColors - they are set to black. The
     *  meshes should have the same material indices, too. The output
     *  material index is always the material index of the first mesh.
     *
     *  @param dest Destination mesh. Must be empty.
     *  @param flags Currently no parameters
     *  @param begin First mesh to be processed
     *  @param end Points to the mesh after the last mesh to be processed
     */
    static void MergeMeshes(aiMesh **dest, unsigned int flags,
            std::vector<aiMesh *>::const_iterator begin,
            std::vector<aiMesh *>::const_iterator end);

    // -------------------------------------------------------------------
    /** Merges two or more bones
     *
     *  @param out Mesh to receive the output bone list
     *  @param flags Currently no parameters
     *  @param begin First mesh to be processed
     *  @param end Points to the mesh after the last mesh to be processed
     */
    static void MergeBones(aiMesh *out, std::vector<aiMesh *>::const_iterator it,
            std::vector<aiMesh *>::const_iterator end);

    // -------------------------------------------------------------------
    /** Merges two or more materials
     *
     *  The materials should be complementary as much as possible. In case
     *  of a property present in different materials, the first occurrence
     *  is used.
     *
     *  @param dest Destination material. Must be empty.
     *  @param begin First material to be processed
     *  @param end Points to the material after the last material to be processed
     */
    static void MergeMaterials(aiMaterial **dest,
            std::vector<aiMaterial *>::const_iterator begin,
            std::vector<aiMaterial *>::const_iterator end);

    // -------------------------------------------------------------------
    /** Builds a list of uniquely named bones in a mesh list
     *
     *  @param asBones Receives the output list
     *  @param it      First mesh to be processed
     *  @param end     Last mesh to be processed
     */
    static void BuildUniqueBoneList(std::list<BoneWithHash> &asBones,
            std::vector<aiMesh *>::const_iterator it,
            std::vector<aiMesh *>::const_iterator end);

    // -------------------------------------------------------------------
    /** Add a name prefix to all nodes in a Level.
     *
     *  @param node   Current node. This function is called recursively.
     *  @param prefix Prefix to be added to all nodes
     *  @param len    String length
     */
    static void AddNodePrefixes(aiNode *node, const char *prefix,
            unsigned int len);

    // -------------------------------------------------------------------
    /** Add an offset to all mesh indices in a node graph
     *
     *  @param node   Current node. This function is called recursively.
     *  @param offset Offset to be added to all mesh indices
     */
    static void OffsetNodeMeshIndices(aiNode *node, unsigned int offset);

    // -------------------------------------------------------------------
    /** Attach a list of node graphs to well-defined nodes in a master
     *  graph. This is a helper for MergeLevels()
     *
     *  @param master Master Level
     *  @param srcList List of source Levels along with their attachment
     *    points. If an attachment point is nullptr (or does not exist in
     *    the master graph), a Level is attached to the root of the master
     *    graph (as an additional child node)
     *  @duplicates List of duplicates. If elem[n] == n the Level is not
     *    a duplicate. Otherwise, elem[n] links Level n to its first occurrence.
     */
    static void AttachToGraph(aiLevel *master,
            std::vector<NodeAttachmentInfo> &srcList);

    static void AttachToGraph(aiNode *attach,
            std::vector<NodeAttachmentInfo> &srcList);

    // -------------------------------------------------------------------
    /** Get a deep copy of a Level
     *
     *  @param dest     Receives a pointer to the destination Level
     *  @param source   Source Level - remains unmodified.
     *  @param allocate true for allocation a new Level
     */
    static void CopyLevel(aiLevel **dest, const aiLevel *source, bool allocate = true);

    // -------------------------------------------------------------------
    /** Get a flat copy of a Level
     *
     *  Only the first hierarchy layer is copied. All pointer members of
     *  aiLevel are shared by source and destination Level.  If the
     *    pointer doesn't point to nullptr when the function is called, the
     *    existing Level is cleared and refilled.
     *  @param dest Receives a pointer to the destination Level
     *  @param src Source Level - remains unmodified.
     */
    static void CopyLevelFlat(aiLevel **dest, const aiLevel *source);

    // -------------------------------------------------------------------
    /** Get a deep copy of a mesh
     *
     *  @param dest Receives a pointer to the destination mesh
     *  @param src Source mesh - remains unmodified.
     */
    static void Copy(aiMesh **dest, const aiMesh *src);

    // similar to Copy():
    static void Copy(aiAnimMesh **dest, const aiAnimMesh *src);
    static void Copy(aiMaterial **dest, const aiMaterial *src);
    static void Copy(aiTexture **dest, const aiTexture *src);
    static void Copy(aiAnimation **dest, const aiAnimation *src);
    static void Copy(aiCamera **dest, const aiCamera *src);
    static void Copy(aiBone **dest, const aiBone *src);
    static void Copy(aiLight **dest, const aiLight *src);
    static void Copy(aiNodeAnim **dest, const aiNodeAnim *src);
    static void Copy(aiMeshMorphAnim **dest, const aiMeshMorphAnim *src);
    static void Copy(aiMetadata **dest, const aiMetadata *src);
    static void Copy(aiString **dest, const aiString *src);

    // recursive, of course
    static void Copy(aiNode **dest, const aiNode *src);

private:
    // -------------------------------------------------------------------
    // Same as AddNodePrefixes, but with an additional check
    static void AddNodePrefixesChecked(aiNode *node, const char *prefix,
            unsigned int len,
            std::vector<LevelHelper> &input,
            unsigned int cur);

    // -------------------------------------------------------------------
    // Add node identifiers to a hashing set
    static void AddNodeHashes(aiNode *node, std::set<unsigned int> &hashes);

    // -------------------------------------------------------------------
    // Search for duplicate names
    static bool FindNameMatch(const aiString &name,
            std::vector<LevelHelper> &input, unsigned int cur);
};

} // namespace Assimp

#endif // !! AI_Level_COMBINER_H_INC
