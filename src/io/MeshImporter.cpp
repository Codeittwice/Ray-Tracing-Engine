#include "scrt/io/MeshImporter.hpp"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stdexcept>

namespace scrt::io {

ImportedMesh import_mesh(const std::filesystem::path& path, double scale_to_meters) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
            aiProcess_GenNormals);

    if (!scene || !scene->HasMeshes())
        throw std::runtime_error("MeshImporter: failed to load " + path.string() +
                                 " — " + importer.GetErrorString());

    ImportedMesh result;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        std::uint32_t base = static_cast<std::uint32_t>(result.vertices.size());

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            const auto& p = mesh->mVertices[v];
            result.vertices.push_back(
                math::vec3(p.x, p.y, p.z) * scale_to_meters);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            result.indices.push_back(base + face.mIndices[0]);
            result.indices.push_back(base + face.mIndices[1]);
            result.indices.push_back(base + face.mIndices[2]);
        }
    }

    if (result.vertices.empty())
        throw std::runtime_error("MeshImporter: " + path.string() + " has no geometry");

    return result;
}

} // namespace scrt::io
