#pragma once
#include "prefab.h"
#include "shader.h"
#include "sphericalharmonics.h"


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
        UNCHARTED2,
        LUMA_BASED_REINHARD
    };

    enum eShowOption {
        GBUFFERS,
        SSAO,
        SCENE,
        IRRADIANCE_TEXTURE,
        IRRADIANCE
    };

    //struct to store RenderCalls
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

    //struct to store probes
    struct sProbe
        {
            Vector3 pos;           //where is located
            Vector3 local;         //its ijk pos in the matrix
            int index;             //its index in the linear array
            SphericalHarmonics sh; //coeffs
        };
        
    // This class is in charge of rendering anything in our system.
    // Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:
        std::vector<RenderCall> render_call_vector;
        std::vector<GTR::LightEntity*> lights;
        std::vector<Vector3> rand_points;
        std::vector<sProbe> probes;
        
        Vector3 dim;
        Vector3 start_pos;
        Vector3 end_pos;
        Vector3 delta;
        float irr_normal_distance;
        
        eRenderingMode rendering_mode;
        eRenderingPipeline rendering_pipeline;
        eToneMapper tone_mapper;
        
        bool render_shadowmaps;
        eShowOption show_option;
        
        bool use_ssao;
        bool use_blur_ssao;
        bool use_hdr;
        bool use_dither;
        bool pbr;
        bool show_probes;
        bool show_irradiance;
        bool add_irradiance;
        bool interpolate_irradiance;
        
        FBO* gbuffers_fbo;
        FBO* illumination_fbo;
        FBO* ssao_fbo;
        FBO* blur_ssao_fbo;
        FBO* irr_fbo;
        
        Texture* probes_texture;
        
        sProbe probe;
        Renderer();
                 
		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
        
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);
        
        // set render call vector
        void setRenderCallVector(const Matrix44& model, GTR::Node* node, Camera* camera);
        
        // render forward
        void renderForward(Camera* camera, GTR::Scene* scene, std::vector<RenderCall> render_vector);
        
		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        
        // to render lights -> multipass mode
        void renderLightMultiPass(Mesh* mesh, Shader* shader);
        
        // to render lights -> singlepass mode
        void renderLightSinglePass(Mesh* mesh, GTR::Material* material, Shader* shader);
        
        // render deferred
        void renderDeferred(Camera* camera, GTR::Scene* scene, std::vector<RenderCall> render_vector);
        
        //to render one mesh given its material and transformation matrix
        void renderMeshWithMaterialToGBuffers(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        
        //render SSAO
        void renderSSAO(Camera* camera, GTR::Scene* scene);
        
        // render illumitation for deferred
        void illuminationDeferred(Camera* camera, GTR::Scene* scene);

        // to upload textures to shader
        void uploadTextures(GTR::Material* material, Shader* shader);
        
        // to upload lights to shader
        void uploadLight(LightEntity* light, Shader* shader);
        
        //to render a flat mesh
        void renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        
        // to generate shadow map
        void generateShadowmap(LightEntity* light);
        
        // to show shadowmap
        void showShadowmap(LightEntity* light);
        
        // to render probes
        void renderProbesGrid(float size);
        
        void renderProbe(float size);
        
        // to render probe in all six positions and its compute coefficients
        void captureProbe(sProbe& probe, GTR::Scene* scene);
        
        // to generate and place the probes
        void generateProbesGrid(GTR::Scene* scene);
        
        // to update irradiance texture
        void updateIrradiance(GTR::Scene* scene);
        
        // to upload Probes to the GPU
        void uploadProbes();
        
        // to consider irradiance for the ambient light
        void displayIrradiance(Camera* camera, GTR::Scene* scene);
        
	};
    
	Texture* CubemapFromHDRE(const char* filename);
    std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);
};
