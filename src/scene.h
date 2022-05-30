#ifndef SCENE_H
#define SCENE_H

#include "framework.h"
#include "camera.h"
#include <string>

//forward declaration
class cJSON;
class FBO;
class Texture;

//our namespace
namespace GTR {

	enum eEntityType {
		NONE = 0,
		PREFAB = 1,
		LIGHT = 2,
		CAMERA = 3,
		REFLECTION_PROBE = 4,
		DECALL = 5
	};

    // define an enum for the light type
    enum eLightType{
        POINT = 0,
        DIRECTIONAL = 1,
        SPOT = 2
    };


	class Scene;
	class Prefab;

	//represents one element of the scene (could be lights, prefabs, cameras, etc)
	class BaseEntity
	{
	public:
		Scene* scene;
		std::string name;
		eEntityType entity_type;
		Matrix44 model;
		bool visible;
		BaseEntity() { entity_type = NONE; visible = true; }
		virtual ~BaseEntity() {}
		virtual void renderInMenu();
		virtual void configure(cJSON* json) {}
	};

	//represents one prefab in the scene
	class PrefabEntity : public GTR::BaseEntity
	{
	public:
		std::string filename;
		Prefab* prefab;
		
		PrefabEntity();
		virtual void renderInMenu();
		virtual void configure(cJSON* json);
	};

    
    class LightEntity : public GTR::BaseEntity
    {
    public:
        Vector3 color;
        float intensity;
        float max_dist;
        float angle;
        float cone_angle;
        float cone_exp;
        float area_size;
        float shadow_bias;
        bool cast_shadows;
        
        eLightType light_type;
        
        FBO* fbo;
        Texture* shadowmap;
        Camera* light_camera;
        
        LightEntity();
        virtual void renderInMenu();
        virtual void configure(cJSON* json);
    };

	//contains all entities of the scene
	class Scene
	{
	public:
		static Scene* instance;

		Vector3 background_color;
		Vector3 ambient_light;
		Camera main_camera;
        
		Scene();

		std::string filename;
		std::vector<BaseEntity*> entities;

		void clear();
		void addEntity(BaseEntity* entity);

		bool load(const char* filename);
		BaseEntity* createEntity(std::string type);
	};

};

#endif
