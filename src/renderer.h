#pragma once
#include "prefab.h"
#include "shader.h"

//forward declarations
class Camera;

namespace GTR {

	class Prefab;
	class Material;
	
    enum eRenderingMode {
        TEXTURE,
        MULTIPASS,
        SINGLEPASS
    };
    
    enum eRenderingPipeline{
        FORWARD,
        DEFERRED,
        FORWARD_DEFERRED
    };
	
    enum GBuffersOption{
        COLOR,
        NORMALMAP,
        DEPTH
    };

    enum eToneMapper{
        UNCHARTED2 = 0,
        LUMA_BASED_REINHARD = 1
    };

    struct RenderCall
        {
            Material* material;
            float camera_distance;
            Matrix44 node_model;
            Mesh* mesh;
            Camera* camera;
            BoundingBox world_bounding;
            
            RenderCall() {
                material = NULL;
                camera_distance = 0.0;
                node_model.setIdentity();
                mesh = NULL;
                camera = NULL;
            }
            
        };
    
    // This class is in charge of rendering anything in our system.
    // Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:
        std::vector<RenderCall> render_call_vector;
        std::vector<GTR::LightEntity*> lights;
        eRenderingMode rendering_mode;
        eRenderingPipeline rendering_pipeline;
        eToneMapper tone_mapper;
        
        bool render_shadowmaps;
        bool show_gbuffers;
        bool show_ssao;
        bool show_scene;
        
        bool use_ssao;
        bool use_blur_ssao;
        bool use_hdr;
        bool use_dither;
        bool pbr;
        bool add_lights;
        int num_lights;
        
        FBO* gbuffers_fbo;
        FBO* illumination_fbo;
        FBO* ssao_fbo;
        FBO* blur_ssao_fbo;
        
        std::vector<Vector3> rand_points;
        std::vector<GTR::LightEntity*> rand_lights;
        
        Renderer();
                 
		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
        
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);
        
        // set render call vector
        void setRenderCallVector(const Matrix44& model, GTR::Node* node, Camera* camera);
        
		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        
        //to render one mesh given its material and transformation matrix
        void renderMeshWithMaterialToGBuffers(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        
        // to upload textures to shader
        void uploadTextures(GTR::Material* material, Shader* shader);
        
        // to upload lights to shader
        void uploadLight(LightEntity* light, Shader* shader);
        
        // to render lights -> multipass mode
        void renderLightMultiPass(Mesh* mesh, Shader* shader);
        
        // to render lights -> singlepass mode
        void renderLightSinglePass(Mesh* mesh, GTR::Material* material, Shader* shader);
        
        // to generate shadow map
        void generateShadowmap(LightEntity* light);
        
        //to render shadowmap texture
        void renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        
        // to show shadowmap
        void showShadowmap(LightEntity* light);
        
        // render forward
        void renderForward(Camera* camera, GTR::Scene* scene, std::vector<RenderCall> render_vector);
        
        // render deferred
        void renderDeferred(Camera* camera, GTR::Scene* scene, std::vector<RenderCall> render_vector);
        
        // render illumitation for deferred
        void illuminationDeferred(Camera* camera, GTR::Scene* scene);
        
        //render SSAO
        void renderSSAO(Camera* camera, GTR::Scene* scene);

	};
    
	Texture* CubemapFromHDRE(const char* filename);
    std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);
};
