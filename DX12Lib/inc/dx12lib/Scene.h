#pragma once

/*
 *  Copyright(c) 2019 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

/**
 *  @file Scene.h
 *  @date March 21, 2019
 *  @author Jeremiah van Oosten
 *
 *  @brief Scene file for storing scene data.
 */

#include <DirectXCollision.h> // For DirectX::BoundingBox

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>

class aiMaterial;
class aiMesh;
class aiNode;
class aiScene;

namespace dx12lib
{
class CommandList;
class Device;
class SceneNode;
class Mesh;
class Material;
class Visitor;
class AccelerationStructure;
class Texture;

class Scene
{
public:
    Scene( float scale = 1.0f )
    : _sceneScale( scale )
    { }
    ~Scene() = default;

    void BuildBottomLevelAccelerationStructure( dx12lib::Device* pDevice, 
        dx12lib::CommandList* pCommandList, AccelerationStructure* pDes );

    void SetRootNode( std::shared_ptr<SceneNode> node )
    {
        m_RootNode = node;
    }

    std::shared_ptr<SceneNode> GetRootNode() const
    {
        return m_RootNode;
    }

    /* Geometry and material counts */
    size_t GetGeometryCount() const
    {
        return m_Meshes.size();
    }

    size_t GetMaterialCount() const 
    {
        return m_Materials.size();
    }

    /* Texture counts */
    size_t GetDiffuseTextureCount() const 
    {
        return _diffuse.size();
    }
    size_t GetNormalTextureCount() const
    {
        return _normal.size();
    }

    size_t GetSpecularTextureCount() const
    {
        return _specular.size();
    }

    size_t GetMaskTextureCount() const 
    {
        return _opacity.size();
    }

    float GetSceneScale() const {
        return _sceneScale;
    }

    bool HasSkybox() const 
    {
        return skyboxIntensity.get() && skyboxDiffuse.get();
    }

    /**
     * Get the AABB of the scene.
     * This returns the AABB of the root node of the scene.
     */
    DirectX::BoundingBox GetAABB() const;

    /**
     * Accept a visitor.
     * This will first visit the scene, then it will visit the root node of the scene.
     */
    virtual void Accept( Visitor& visitor );

    void SetSkybox( std::shared_ptr<dx12lib::Texture> skyboxIntensity,
        std::shared_ptr<dx12lib::Texture> skyboxDiffuse );

    void MergeScene( std::shared_ptr<Scene> other );

protected:
    friend class CommandList;
    friend class AccelerationBuffer;
    friend class ShaderTableResourceView;

    /**
     * Load a scene from a file on disc.
     */
    bool LoadSceneFromFile( CommandList& commandList, const std::wstring& fileName,
                            const std::function<bool( float )>& loadingProgress );

    /**
     * Load a scene from a string.
     * The scene can be preloaded into a byte array and the
     * scene can be loaded from the loaded byte array.
     *
     * @param scene The byte encoded scene file.
     * @param format The format of the scene file.
     */
    bool LoadSceneFromString( CommandList& commandList, const std::string& sceneStr, const std::string& format );

private:
    void ImportScene( CommandList& commandList, const aiScene& scene, std::filesystem::path parentPath );
    void ImportMaterial( CommandList& commandList, const aiMaterial& material, std::filesystem::path parentPath );
    void ImportMesh( CommandList& commandList, const aiMesh& mesh );
    std::shared_ptr<SceneNode> ImportSceneNode( CommandList& commandList, std::shared_ptr<SceneNode> parent,
                                                const aiNode* aiNode );

    using MaterialMap  = std::map<std::string, std::shared_ptr<Material>>;
    using MaterialList = std::vector<std::shared_ptr<Material>>;
    using MeshList     = std::vector<std::shared_ptr<Mesh>>;

    MaterialMap  m_MaterialMap;
    MaterialList m_Materials;
    MeshList     m_Meshes;

    std::shared_ptr<SceneNode> m_RootNode;

    std::wstring m_SceneFile;

    // NEW
    std::set<dx12lib::Texture*> _diffuse;
    std::set<dx12lib::Texture*> _normal;
    std::set<dx12lib::Texture*> _specular;
    std::set<dx12lib::Texture*> _opacity;

    std::shared_ptr<dx12lib::Texture> skyboxIntensity, skyboxDiffuse;
    
    float _sceneScale = 1.0;

};
}  // namespace dx12lib