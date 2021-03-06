#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"
#include <algorithm>
#include "fbo.h"
#include "application.h"
using namespace GTR;


GTR::Renderer::Renderer(){
    rendering_mode = eRenderingMode::SINGLEPASS;
    rendering_pipeline = DEFERRED;
    
    
    // show options
    render_shadowmaps = true;
    show_option = SCENE;

    // usage bools
    use_ssao = false;
    use_blur_ssao = false;
    use_hdr = false;
    use_dither = false;
    pbr = false;
    show_probes = true;
    add_irradiance = true;
    interpolate_irradiance = true;
    tone_mapper = LUMA_BASED_REINHARD;
    
    float w = Application::instance->window_width;
    float h = Application::instance->window_height;
    
    // Init FBOs
    gbuffers_fbo = new FBO();
    gbuffers_fbo->create(w, h, 3, GL_RGBA, GL_UNSIGNED_BYTE, true);
    
    illumination_fbo = new FBO();
    illumination_fbo->create(w, h, 1, GL_RGB, GL_FLOAT, true);
    
    ssao_fbo = new FBO();
    ssao_fbo->create(w, h, 1, GL_RGB, GL_UNSIGNED_BYTE, false);
    rand_points = generateSpherePoints(64, 1.0, true);
    
    blur_ssao_fbo = new FBO();
    blur_ssao_fbo->create(w, h, 1, GL_RGB, GL_UNSIGNED_BYTE, false);
    
    irr_fbo = new FBO();
    irr_fbo->create(64, 64, 1, GL_RGB, GL_FLOAT);
    
    // probes variables
    probes_texture = NULL;
    
    dim = Vector3(10, 4, 10);
    start_pos = Vector3(-300, 5, -400);
    end_pos = Vector3(300, 250, 400);
    
    delta = (end_pos - start_pos);
    delta.x /= (dim.x - 1);
    delta.y /= (dim.y - 1);
    delta.z /= (dim.z - 1);
    
    irr_normal_distance = 0.1;
    probe.pos.set(90,250,-380);
}

// function to sort the render calls of our scene
struct sortRCVector{
    inline bool operator() (const RenderCall& a, const RenderCall& b){
        if((a.material->alpha_mode == b.material->alpha_mode)&&(a.material->alpha_mode==BLEND)){
            // sort the elements by its distance to the camera -> sort at the end the closest ones
            return a.camera_distance > b.camera_distance;
        }
        //sort by alpha mode. If alpha mode = BLEND, it will be sorted at the end
        return a.material->alpha_mode < b.material->alpha_mode;
    }
};

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
    //reset vectors
    this->render_call_vector.resize(0);
    this->lights.resize(0);
    
	//render entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if(pent->prefab)
				renderPrefab(ent->model, pent->prefab, camera);
		}
        
        //is a light!
        if (ent->entity_type == LIGHT)
        {
            LightEntity* light = (GTR::LightEntity*)ent;
            this->lights.push_back(light);
        }
	}
    
    // sort render_call_vector before rendering
    std::sort(std::begin(this->render_call_vector), std::end(this->render_call_vector), sortRCVector());
    
    
    // generate shadowmaps
    for(int i=0; i < this->lights.size(); i++){
        LightEntity* light = lights[i];
        // if this light casts any shadow, generate shadow map
        if(light->cast_shadows)
            generateShadowmap(light);
    }
    
    if(rendering_pipeline == FORWARD)
        renderForward(camera, scene, this->render_call_vector);
    else if(rendering_pipeline == DEFERRED)
        renderDeferred(camera, scene, this->render_call_vector);
    if(show_probes == true){renderProbesGrid(5.0);}
    /*
    else if(rendering_pipeline == FORWARD_DEFERRED){
        // separete nodes between the once that have alpha and the ones that don't
        std::vector<RenderCall> render_call_alpha;
        std::vector<RenderCall> render_call_no_aplha;
        
        for(int i=0; i < this->render_call_vector.size(); ++i){
            RenderCall& rc = this->render_call_vector[i];
            if(rc.material->alpha_mode == BLEND){ render_call_alpha.push_back(rc);}
            else{render_call_no_aplha.push_back(rc);}
        }
    }*/
        
        
    //glViewport(Application::instance->window_width-256, 0, 256, 256);
    //showShadowmap(lights[3]);
    //glViewport(0, 0, Application::instance->window_width, Application::instance->window_height);
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
    assert(prefab && "PREFAB IS NULL");
    //assign the model to the root node
    //renderNode(model, &prefab->root, camera);
    setRenderCallVector(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
    if (!node->visible)
        return;

    //compute global matrix
    Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

    //does this node have a mesh? then we must render it
    if (node->mesh && node->material)
    {
        //compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
        BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
        
        //if bounding box is inside the camera frustum then the object is probably visible
        if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
        {
            //render node mesh
            renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
            //node->mesh->renderBounding(node_model, true);
        }
    }

    //iterate recursively with children
    for (int i = 0; i < node->children.size(); ++i)
        renderNode(prefab_model, node->children[i], camera);
}

// set render call vector
void Renderer::setRenderCallVector(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
    if (!node->visible)
        return;

    //compute global matrix
    Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

    //does this node have a mesh? then we must render it
    if (node->mesh && node->material)
    {
        //compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
        BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
        
        // set RenderCall object for each node and store it in the render_call_vector
        RenderCall rc;
        rc.material = node->material;
        rc.mesh = node->mesh;
        rc.node_model = node_model;
        rc.world_bounding = world_bounding;
        
        // let's compute the distance to the camera
        Vector3 center_node = world_bounding.center;
        rc.camera_distance = camera->eye.distance(center_node);
        
        rc.camera = camera;
        
        // store node information
        this->render_call_vector.push_back(rc);

    }

    //iterate recursively with children
    for (int i = 0; i < node->children.size(); ++i)
    setRenderCallVector(prefab_model, node->children[i], camera);
}

// forward
void Renderer::renderForward(Camera* camera, GTR::Scene* scene, std::vector<RenderCall> render_vector){
    //set the clear color (the background color)
    glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

    // Clear the color and the depth buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    checkGLErrors();
 
    for(int i=0; i < render_vector.size(); ++i){
        RenderCall& rc = render_vector[i];
        //if bounding box is inside the camera frustum then the object is probably visible
        if (camera->testBoxInFrustum(rc.world_bounding.center, rc.world_bounding.halfsize) )
        {
            renderMeshWithMaterial( rc.node_model, rc.mesh, rc.material, camera);
        }
    }
    
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
    //in case there is nothing to do
    if (!mesh || !mesh->getNumVertices() || !material )
        return;
    assert(glGetError() == GL_NO_ERROR);

    //define locals to simplify coding
    Shader* shader = NULL;
    GTR::Scene* scene = GTR::Scene::instance;
    
    
    //select the blending
    if (material->alpha_mode == GTR::eAlphaMode::BLEND)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
        glDisable(GL_BLEND);
    
    //select if render both sides of the triangles
    if(material->two_sided)
        glDisable(GL_CULL_FACE);
    else
        glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

    //chose a shader
    if(this->rendering_mode == eRenderingMode::TEXTURE)
        shader = Shader::Get("texture");
    else if(this->rendering_mode == eRenderingMode::MULTIPASS)
        shader = Shader::Get("light");
    else if(this->rendering_mode == eRenderingMode::SINGLEPASS)
        shader = Shader::Get("single_light");
    

    assert(glGetError() == GL_NO_ERROR);

    //no shader? then nothing to render
    if (!shader)
        return;
    shader->enable();

    //upload uniforms
    shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader->setUniform("u_camera_position", camera->eye);
    shader->setUniform("u_model", model);
    float t = getTime();
    shader->setUniform("u_time", t );

    shader->setUniform("u_color", material->color);
    shader->setUniform("u_emissive_factor", material->emissive_factor);
    
    //upload textures
    uploadTextures(material, shader);
    

    //this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
    shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
    
    //do the draw call that renders the mesh into the screen
    if(this->rendering_mode ==  eRenderingMode::TEXTURE) mesh->render(GL_TRIANGLES);
    
    
    //render lights
    if(this->rendering_mode == eRenderingMode::MULTIPASS || this->rendering_mode == eRenderingMode::SINGLEPASS){
        shader->setUniform("u_ambient_light", scene->ambient_light);  //ambient light
        shader->setUniform("u_pbr", pbr);
        
        // show scene elements even if there's no light
        if(this->lights.size() == 0) {
            shader->setUniform("u_light_color", Vector3(0,0,0));
            mesh->render(GL_TRIANGLES);
        }
        // if there are more lights
        else{
            if(this->rendering_mode == MULTIPASS){
                renderLightMultiPass(mesh, shader);}
            else{
                renderLightSinglePass(mesh, material, shader);}
        }
    }

    //disable shader
    shader->disable();

    //set the render state as it was before to avoid problems with future renders
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::renderLightMultiPass(Mesh* mesh, Shader* shader){
    
    glDepthFunc(GL_LEQUAL);   //para que el z-buffer deje pasar todas las luces
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); //sumar el color ya pintado con la luz que le llegue
    
    //iterate all lights
    for(int i = 0; i < this->lights.size(); ++i){
        LightEntity* light = lights[i];
        uploadLight(light, shader);
        
        //do the draw call that renders the mesh into the screen
        mesh->render(GL_TRIANGLES);
        
        // enable blending
        glEnable(GL_BLEND);
    
        // to consider ambient light one time
        shader->setUniform("u_ambient_light", Vector3(0,0,0));
    }
    
    //set the render state as it was before to avoid problems with future renders
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glFrontFace(GL_CCW);
}

void Renderer::renderLightSinglePass(Mesh* mesh, GTR::Material* material, Shader* shader){
    const int max_lights = 8;
    Vector3 light_position[max_lights];
    Vector3 light_color[max_lights];
    float light_max_dist[max_lights];
    eLightType light_type[max_lights];
    Vector3 light_vector[max_lights];
    Vector3 light_cone[max_lights];
    
    for(int i = 0; i < this->lights.size(); ++i){
        light_position[i] = lights[i]->model * Vector3();
        light_color[i] = lights[i]->color * lights[i]->intensity;
        light_max_dist[i] = lights[i]->max_dist;
        light_type[i] = lights[i]->light_type;
        light_vector[i] = lights[i]->model.rotateVector(Vector3(0,0,-1));
        light_cone[i] = Vector3(lights[i]->cone_angle, lights[i]->cone_exp, cos(lights[i]->cone_angle*DEG2RAD));
    }
    
    //upload uniforms to shader
    shader->setUniform1("u_num_lights", (int)this->lights.size());
    shader->setUniform3Array("u_light_pos",(float*)light_position, max_lights);
    shader->setUniform3Array("u_light_color",(float*)&light_color, max_lights);
    shader->setUniform1Array("u_light_max_dist",(float*)&light_max_dist, max_lights);
    shader->setUniform1Array("u_light_type", (int*)&light_type, max_lights);
    shader->setUniform3Array("u_light_vec",(float*)&light_vector, max_lights);
    shader->setUniform3Array("u_light_cone",(float*)&light_cone, max_lights);
    
    //do the draw call that renders the mesh into the screen
    mesh->render(GL_TRIANGLES);
}

// deferred
void Renderer::renderDeferred(Camera* camera, GTR::Scene* scene, std::vector<RenderCall> render_vector){
    // Render GBuffers -> propiedades de cada objeto las guardamos en distintas texturas
    float w = Application::instance->window_width;
    float h = Application::instance->window_height;
    
    // Render gBuffers
    gbuffers_fbo->bind();
    
    //set the clear color (the background color)
    glClearColor(0.0, 0.0, 0.0, 1.0);
    // Clear the color and the depth buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    checkGLErrors();
    
    for(int i=0; i < render_vector.size(); ++i){
        RenderCall& rc = render_vector[i];
        //if bounding box is inside the camera frustum then the object is probably visible
        if (camera->testBoxInFrustum(rc.world_bounding.center, rc.world_bounding.halfsize) )
        {
            renderMeshWithMaterialToGBuffers(rc.node_model, rc.mesh, rc.material, rc.camera);
        }
    }
    gbuffers_fbo->unbind();
    
    // show gbuffers
    if(show_option==GBUFFERS){
        glDisable(GL_BLEND);
        
        // show color texture (alpha component contains rougthness)
        glViewport(0, h*0.5, w*0.5, h*0.5);
        gbuffers_fbo->color_textures[0]->toViewport();
        glEnable(GL_DEPTH_TEST);
        
        // show normal texture (alpha component contains metalness)
        glViewport(w*0.5, h*0.5, w*0.5, h*0.5);
        gbuffers_fbo->color_textures[1]->toViewport();
        glEnable(GL_DEPTH_TEST);
        
        // show extra texture with emissive light and occlusion factor
        glViewport(0, 0, w*0.5, h*0.5);
        gbuffers_fbo->color_textures[2]->toViewport();
        glEnable(GL_DEPTH_TEST);
        
        // show depth texture
        // compute depth texture
        Shader* shader = Shader::getDefaultShader("depth");
        shader->enable();
        shader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));
        
        glViewport(w*0.5, 0, w*0.5, h*0.5);
        gbuffers_fbo->depth_texture->toViewport(shader);
        glEnable(GL_DEPTH_TEST);
        shader->disable();
        
        // reset
        glViewport(0, 0, w, h);
        glEnable(GL_DEPTH_TEST);
    }
    
    // Compute SSAO
    renderSSAO(camera, scene);
    
    if(show_option==SSAO){
        glDisable(GL_BLEND);
        ssao_fbo->color_textures[0]->toViewport();
        glEnable(GL_DEPTH_TEST);
    }
    
    // Show irradiance texture
    if(show_option==IRRADIANCE_TEXTURE and probes_texture!=NULL){
       probes_texture->toViewport();
    }
    
    // Show Irradiance
    if(show_option==IRRADIANCE and probes_texture!=NULL){displayIrradiance(camera, scene);}
    
    // render scene
    if(show_option==SCENE){
        illumination_fbo->bind();
        illuminationDeferred(camera, scene);
        illumination_fbo->unbind();
        
        // show in screen
        glDisable(GL_BLEND);
        
        if(use_hdr){
        Shader* shader_hdr = Shader::getDefaultShader("HDR_tonemapping");
        shader_hdr->enable();
        shader_hdr->setUniform("u_tonemapper", tone_mapper);
        
        illumination_fbo->color_textures[0]->toViewport(shader_hdr);
        glEnable(GL_DEPTH_TEST);
        shader_hdr->disable();
        }
        else
        {
            illumination_fbo->color_textures[0]->toViewport();
            glEnable(GL_DEPTH_TEST);
        }
        
    }
    
    //set the render state as it was before to avoid problems with future renders
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);
    
}

// render to gbuffers
void Renderer::renderMeshWithMaterialToGBuffers(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
    //in case the material has transparencies
    if(!use_dither)
        if (material->alpha_mode == eAlphaMode::BLEND)
            return;
    
    //in case there is nothing to do
    if (!mesh || !mesh->getNumVertices() || !material )
        return;
    assert(glGetError() == GL_NO_ERROR);
    
    //define locals to simplify coding
    Shader* shader = NULL;
    
    // no blending
    glDisable(GL_BLEND);
    
    //select if render both sides of the triangles
    if(material->two_sided)
        glDisable(GL_CULL_FACE);
    else
        glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

    //chose shader
    shader = Shader::Get("gbuffers");
    
    assert(glGetError() == GL_NO_ERROR);

    //no shader? then nothing to render
    if (!shader)
        return;
    shader->enable();

    //upload uniforms
    shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader->setUniform("u_camera_position", camera->eye);
    shader->setUniform("u_model", model);
    float t = getTime();
    shader->setUniform("u_time", t );

    shader->setUniform("u_color", material->color);
    shader->setUniform("u_emissive_factor", material->emissive_factor);
    shader->setUniform("u_use_dither", use_dither);
    
    //upload textures
    uploadTextures(material, shader);

    //this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
    shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
    
    //do the draw call that renders the mesh into the screen
    mesh->render(GL_TRIANGLES);
    
    //disable shader
    shader->disable();

    //set the render state as it was before to avoid problems with future renders
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);
}

// compute ssao and ssao+
void Renderer::renderSSAO(Camera* camera, GTR::Scene* scene){
    ssao_fbo->bind();
    
    int w = Application::instance->window_width;
    int h = Application::instance->window_height;
    
    // Compute Inverse View Projection
    Matrix44 inv_vp = camera->viewprojection_matrix;
    inv_vp.inverse();
    
    Mesh* quad = Mesh::getQuad();
    Shader* shader_ssao = Shader::Get("ssao");
    shader_ssao->enable();
    
    // Clear the color and the depth buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    checkGLErrors();
    
    shader_ssao->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 9);
    shader_ssao->setUniform("u_normal_texture", gbuffers_fbo->color_textures[1], 7);
    shader_ssao->setUniform("u_inverse_viewprojection", inv_vp);
    shader_ssao->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));
    shader_ssao->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader_ssao->setUniform3Array("u_points", (float*)&rand_points[0], rand_points.size());
    
    
    quad->render(GL_TRIANGLES);
    
    shader_ssao->disable();
    ssao_fbo->unbind();
    
    // SSAO+
    if(use_blur_ssao){
        blur_ssao_fbo->bind();

        // Clear the color and the depth buffer
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        checkGLErrors();

        Shader* shader_blur_ssao = Shader::Get("blur_ssao");
        shader_blur_ssao->enable();
        
        shader_blur_ssao->setUniform("u_ssao_fbo", ssao_fbo->color_textures[0], 11);
        shader_blur_ssao->setUniform("u_texture_size", Vector2(w,h));
        
        quad->render(GL_TRIANGLES);
        
        shader_blur_ssao->disable();
        blur_ssao_fbo->unbind();
        
        blur_ssao_fbo->color_textures[0]->copyTo(ssao_fbo->color_textures[0]);
    }
    
    
    //set the render state as it was before to avoid problems with future renders
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);
}

// render illumination deferred
void Renderer::illuminationDeferred(Camera* camera, GTR::Scene* scene){
    
    int w = Application::instance->window_width;
    int h = Application::instance->window_height;
    
    // Compute Inverse View Projection
    Matrix44 inv_vp = camera->viewprojection_matrix;
    inv_vp.inverse();
    
    // Clear Sceen
    // Render to screen -> multipass leyendo GBuffers
    glClearColor(0.0, 0.0, 0.0, 1.0);
    
    // Clear the color and the depth buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    checkGLErrors();
    
    // Block writing to depth texture
    //gbuffers_fbo->depth_texture->copyTo(NULL);
    glDepthMask(false);
    
    
    // Spehere mesh for non directional lights
    Mesh* sphere = Mesh::Get("data/meshes/sphere.obj", false, false);
    Shader* shader = Shader::Get("deferred_ws");
    shader->enable();
    
    // pass the gbuffers to the shader
    shader->setUniform("u_color_texture", gbuffers_fbo->color_textures[0], 6);
    shader->setUniform("u_normal_texture", gbuffers_fbo->color_textures[1], 7);
    shader->setUniform("u_extra_texture", gbuffers_fbo->color_textures[2], 8);
    shader->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 9);
    shader->setUniform("u_ssao_texture", ssao_fbo->color_textures[0], 10);
    
    // upload variables to the shader
    shader->setUniform("u_camera_pos", camera->eye);
    shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader->setUniform("u_inverse_viewprojection", inv_vp);
    shader->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));
    shader->setUniform("u_ambient_light", Vector3(0,0,0));  // consider ambient light once
    shader->setUniform("u_use_ssao", use_ssao);
    shader->setUniform("u_use_ssao_blur", use_blur_ssao);
    shader->setUniform("u_use_hdr", use_hdr);
    shader->setUniform("u_pbr", pbr);
    
    // irradiance
    shader->setUniform("u_probes_texture", probes_texture, 12);
    shader->setUniform("u_irr_start", start_pos);
    shader->setUniform("u_irr_end", end_pos);
    shader->setUniform("u_irr_dims", dim);
    shader->setUniform("u_irr_normal_distance",irr_normal_distance);
    shader->setUniform("u_irr_delta", delta);
    shader->setUniform("u_num_probes", (float)probes_texture->height);
    shader->setUniform("u_add_irradiance", add_irradiance);
    shader->setUniform("u_interpolate_irradiance", interpolate_irradiance);
    
    // Render point and spot lights
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glDepthFunc(GL_LEQUAL);   //para que el z-buffer deje pasar todas las luces
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); //sumar el color ya pintado con la luz que le llegue
    
    // initialize a vector to store directional lights
    std::vector<LightEntity*> directional_lights;
    for (int i = 0; i < this->lights.size(); ++i) {
            LightEntity* light = this->lights[i];
            uploadLight(light, shader);

            if (light->light_type != DIRECTIONAL)
            {
                Matrix44 m;
                Vector3 lightPos = light->model.getTranslation();
                float max_dist = light->max_dist;
                m.setTranslation(lightPos.x, lightPos.y, lightPos.z);
                m.scale(max_dist, max_dist, max_dist); //and scale it according to the max_distance of the light
                // upload model
                shader->setUniform("u_model", m); //pass the model to the shader to render the sphere

                sphere->render(GL_TRIANGLES);
                glEnable(GL_BLEND);
                
                shader->setUniform("u_ambient_light", Vector3(0,0,0)); // consider ambient light once
                shader->setUniform("u_add_irradiance", false);
            }
            else{ directional_lights.push_back(light);}
        }
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glDisable(GL_DEPTH_TEST);
    
    
    // Quad mesh for directional lights
    //we need a fullscreen quad
    Mesh* quad = Mesh::getQuad();
    Shader* shader_quad = Shader::Get("deferred");
    shader_quad->enable();

    // pass the gbuffers to the shader
    shader_quad->setUniform("u_color_texture", gbuffers_fbo->color_textures[0], 6);
    shader_quad->setUniform("u_normal_texture", gbuffers_fbo->color_textures[1], 7);
    shader_quad->setUniform("u_extra_texture", gbuffers_fbo->color_textures[2], 8);
    shader_quad->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 9);
    shader_quad->setUniform("u_ssao_texture", ssao_fbo->color_textures[0], 10);
    
    // upload variables to the shader
    shader_quad->setUniform("u_camera_pos", camera->eye);
    shader_quad->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader_quad->setUniform("u_inverse_viewprojection", inv_vp);
    shader_quad->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));
    shader_quad->setUniform("u_ambient_light", scene->ambient_light);  //ambient light
    shader_quad->setUniform("u_use_ssao", use_ssao);
    shader_quad->setUniform("u_use_ssao_blur", use_blur_ssao);
    shader_quad->setUniform("u_use_hdr", use_hdr);
    shader_quad->setUniform("u_pbr", pbr);
    
    // irradiance
    shader_quad->setUniform("u_probes_texture", probes_texture, 12);
    shader_quad->setUniform("u_irr_start", start_pos);
    shader_quad->setUniform("u_irr_end", end_pos);
    shader_quad->setUniform("u_irr_dims", dim);
    shader_quad->setUniform("u_irr_normal_distance",irr_normal_distance);
    shader_quad->setUniform("u_irr_delta", delta);
    shader_quad->setUniform("u_num_probes", (float)probes_texture->height);
    shader_quad->setUniform("u_add_irradiance", add_irradiance);
    shader_quad->setUniform("u_interpolate_irradiance", interpolate_irradiance);
    
    // render directional lights
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); //sumar el color ya pintado con la luz que le llegue
    for(int i = 0; i < directional_lights.size(); ++i)
    {
        LightEntity* light = directional_lights[i];
        uploadLight(light, shader_quad);
        
        quad->render(GL_TRIANGLES);
        shader_quad->setUniform("u_ambient_light", Vector3(0,0,0)); // consider ambient light once
        shader_quad->setUniform("u_add_irradiance", false);
    }
    
    // in case there's no light
    if(this->lights.size()==0){
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        
        shader->setUniform("u_light_color", Vector3(0,0,0));
        sphere->render(GL_TRIANGLES);
        
        shader_quad->setUniform("u_light_color", Vector3(0,0,0));
        quad->render(GL_TRIANGLES);
    }
    
    // disable shaders and clear
    shader->disable();
    //shader_quad->disable();
    directional_lights.resize(0);
    
    //set the render state as it was before to avoid problems with future renders
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);
}


// upload textures to shader
void Renderer::uploadTextures(GTR::Material* material, Shader* shader)
{
    //define locals to simplify coding
    Texture* texture = NULL;
    Texture* emissive_texture = NULL;
    Texture* metallic_roughness_texture = NULL;
    Texture* normal_texture = NULL;
    Texture* occlusion_texture = NULL;
    
    // define textures
    texture = material->color_texture.texture;
    emissive_texture = material->emissive_texture.texture;
    metallic_roughness_texture = material->metallic_roughness_texture.texture;
    normal_texture = material->normal_texture.texture;
    occlusion_texture = material->occlusion_texture.texture;
    
    // if there's no texture...
    if (texture == NULL) texture = Texture::getWhiteTexture(); //a 1x1 white texture
    if (emissive_texture == NULL) emissive_texture = Texture::getWhiteTexture();
    if (normal_texture == NULL) normal_texture = Texture::getBlackTexture();
    if (occlusion_texture == NULL) occlusion_texture = Texture::getWhiteTexture();
    if (metallic_roughness_texture == NULL) metallic_roughness_texture = Texture::getWhiteTexture();
    
    //upload textures
    if(texture)
        shader->setUniform("u_texture", texture, 0);
    if(normal_texture)
        shader->setUniform("u_normal_texture", normal_texture, 1);
    if(emissive_texture)
        shader->setUniform("u_emissive_texture", emissive_texture, 2);
    if(occlusion_texture)
        shader->setUniform("u_occlusion_texture", occlusion_texture, 3);
    if(metallic_roughness_texture)
        shader->setUniform("u_metallic_roughness_texture", metallic_roughness_texture, 4);
    
}

// upload lights to shader
void Renderer::uploadLight(LightEntity* light, Shader* shader)
{
    shader->setUniform("u_light_color", light->color * light->intensity);
    shader->setUniform("u_light_position", light->model * Vector3());
    shader->setUniform("u_light_max_dist", light->max_dist);
    shader->setUniform("u_light_type", light->light_type);
    shader->setUniform("u_cone_angle", light->cone_angle);
    shader->setUniform("u_cone_exp", light->cone_exp);
    shader->setUniform("u_area_size", light->area_size);
    shader->setUniform("u_shadow_bias", light->shadow_bias);
    shader->setUniform("u_cast_shadow", light->cast_shadows);
    shader->setUniform("u_light_vec", light->model.rotateVector(Vector3(0,0,-1)));
    shader->setUniform("u_light_cone", Vector3(light->cone_angle, light->cone_exp, cos(light->cone_angle*DEG2RAD)));
    
    if(light->shadowmap && render_shadowmaps)
    {
        shader->setUniform("u_light_cast_shadows", 1);
        shader->setUniform("u_light_shadowmap", light->shadowmap, 5);
        shader->setUniform("u_shadow_viewproj", light->light_camera->viewprojection_matrix);
        shader->setUniform("u_shadow_bias", light->shadow_bias);
    }
    else
        shader->setUniform("u_light_cast_shadows", 0);
}


void GTR::Renderer::renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
    //in case there is nothing to do
    if (!mesh || !mesh->getNumVertices() || !material )
        return;
    assert(glGetError() == GL_NO_ERROR);

    //define locals to simplify coding
    Shader* shader = NULL;

    //chose a shader
    shader = Shader::Get("flat");

    assert(glGetError() == GL_NO_ERROR);

    //no shader? then nothing to render
    if (!shader)
        return;
    shader->enable();

    //upload uniforms
    shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader->setUniform("u_model", model);


    //this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
    shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
    
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    mesh->render(GL_TRIANGLES);
    //disable shader
    shader->disable();
}


// generate shadowmap
void GTR::Renderer::generateShadowmap(LightEntity* light)
{
    if(light->light_type == eLightType::POINT)
        return;
    
    if(!light->cast_shadows)
    {
        // if light doesn't cast shadows but has fbo --> detele fbo
        if (light->fbo)
        {
            delete light->fbo;
            light->fbo = NULL;
            light->shadowmap = NULL;
        }
        return;
    }
    if(!light->fbo)
    {
        light->fbo = new FBO();
        light->fbo->setDepthOnly(1024, 1024);
        light->shadowmap = light->fbo->depth_texture; //get depth texture just created
    }
    if(!light->light_camera)
        light->light_camera = new Camera();
    
    light->fbo->bind();                     // activate fbo
    glColorMask(false,false,false,false);   //disable writing to the color buffer to speed up the rendering
    glClear(GL_DEPTH_BUFFER_BIT);           // Clear the depth buffer
    
    Camera* view_camera = Camera::current;       // store current camera
    Camera* light_camera = light->light_camera;  // light camera
    
    // set light camera in light position
    float aspect = light->shadowmap->width/(float)light->shadowmap->height;
    light_camera->lookAt(light->model.getTranslation(), light->model*Vector3(0,0,1), light->model.rotateVector(Vector3(0,1,0)));
    
    // SPOT LIGHT->PERSPECTIVE CAMERA
    if(light->light_type == eLightType::SPOT)
    {
        light_camera->setPerspective(light->cone_angle*2, aspect, 0.1, light->max_dist);
    }
    // DIRECTIONAL LIGHT->ORTHOGORAPHIC CAMERA
    else if (light->light_type == eLightType::DIRECTIONAL)
    {
        //use light area to define how big the frustum is
        float halfarea = light->area_size / 2;
        light_camera->setOrthographic( -halfarea, halfarea, halfarea*aspect, -halfarea*aspect, 0.1, light->max_dist);
    }
    
    light_camera->enable();  // enable new camera
    
    for(int i = 0; i<this->render_call_vector.size(); i++)
    {
        RenderCall& rc = render_call_vector[i];
        if(rc.material->alpha_mode == eAlphaMode::BLEND)
            continue; //asume that transparent elements don't generate shadows
        
        //if bounding box is inside the camera frustum then the object is probably visible
        if (light_camera->testBoxInFrustum(rc.world_bounding.center, rc.world_bounding.halfsize) )
        {
            renderFlatMesh(rc.node_model, rc.mesh, rc.material, light_camera);
        }
    }
    
    light->fbo->unbind();             // deactivate fbo
    glColorMask(true,true,true,true); //allow to render back to the color buffer
    view_camera->enable();            // enable previous current camera
}

//to show shadowmap
void GTR::Renderer::showShadowmap(LightEntity* light)
{
    if(!light->shadowmap)
        return;
    Shader* shader = Shader::getDefaultShader("depth");
    shader->enable();
    shader->setUniform("u_camera_nearfar", Vector2(light->light_camera->near_plane, light->light_camera->far_plane));
    
    light->shadowmap->toViewport(shader);
    glEnable(GL_DEPTH_TEST);
}

// to show probes
void GTR::Renderer::renderProbesGrid(float size)
{
    for (int iP = 0; iP < this->probes.size(); ++iP)
    {
        Vector3 pos = this->probes[iP].pos;
        float* coeffs = this->probes[iP].sh.coeffs[0].v;
        
        Camera* camera = Camera::current;
        Shader* shader = Shader::Get("probe");
        Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        Matrix44 model;
        model.setTranslation(pos.x, pos.y, pos.z);
        model.scale(size, size, size);

        shader->enable();
        shader->setUniform("u_viewprojection",camera->viewprojection_matrix);
        shader->setUniform("u_camera_position", camera->eye);
        shader->setUniform("u_model", model);
        shader->setUniform3Array("u_coeffs", coeffs, 9);

        mesh->render(GL_TRIANGLES);
    }
}

void GTR::Renderer::renderProbe(float size)
{
        
        Vector3 pos = this->probe.pos;
        float* coeffs = this->probe.sh.coeffs[0].v;
        
        Camera* camera = Camera::current;
        Shader* shader = Shader::Get("probe");
        Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        Matrix44 model;
        model.setTranslation(pos.x, pos.y, pos.z);
        model.scale(size, size, size);

        shader->enable();
        shader->setUniform("u_viewprojection",camera->viewprojection_matrix);
        shader->setUniform("u_camera_position", camera->eye);
        shader->setUniform("u_model", model);
        shader->setUniform3Array("u_coeffs", coeffs, 9);

        mesh->render(GL_TRIANGLES);
    
}

// to render probe in all six positions and its compute coefficients
void GTR::Renderer::captureProbe(sProbe& probe, GTR::Scene* scene)
{
    FloatImage images[6]; //here we will store the six views
    Camera cam;
    
    //set the fov to 90 and the aspect to 1
    cam.setPerspective(90, 1, 0.1, 1000);
    
    //use singlepass rendering mode
    eRenderingMode current = rendering_mode;
    rendering_mode = eRenderingMode::SINGLEPASS;
    for (int i = 0; i < 6; ++i) //for every cubemap face
    {
    //compute camera orientation using defined vectors
        Vector3 eye = probe.pos;
        Vector3 front = cubemapFaceNormals[i][2];
        Vector3 center = probe.pos + front;
        Vector3 up = cubemapFaceNormals[i][1];
        cam.lookAt(eye, center, up);
        cam.enable();

        //render the scene from this point of view
        irr_fbo->bind();
        renderForward(&cam, scene, this->render_call_vector);
        irr_fbo->unbind();

        //read the pixels back and store in a FloatImage
        images[i].fromTexture(irr_fbo->color_textures[0]);
    }
    //reset rendering mode
    rendering_mode = current;
    
    //compute the coefficients given the six images
    probe.sh = computeSH(images);
}

// to generate and place the probes
void GTR::Renderer::generateProbesGrid(GTR::Scene* scene)
{

    this->probes.clear(); // reset vector
    
    //now delta give us the distance between probes in every axis
    for (int z = 0; z < dim.z; ++z)
        for (int y = 0; y < dim.y; ++y)
            for (int x = 0; x < dim.x; ++x)
                {
                    sProbe p;
                    p.local.set(x, y, z);

                    //index in the linear array
                    p.index = x + y * dim.x + z * dim.x * dim.y;

                    //and its position
                    p.pos = start_pos + delta * Vector3(x,y,z);
                    this->probes.push_back(p);
                }
    //now compute the coeffs for every probe
    for (int iP = 0; iP < this->probes.size(); ++iP)
    {
        captureProbe(this->probes[iP], scene);
    }
    // generate irradiance texture
    uploadProbes();
}

// to update irradiance texture
void GTR::Renderer::updateIrradiance(GTR::Scene* scene)
{
    // compute the coeffs for every probe
    for (int iP = 0; iP < this->probes.size(); ++iP)
    {
        captureProbe(this->probes[iP], scene);
    }
    // generate irradiance texture
    uploadProbes();
}

// to upload Probes to the GPU
void GTR::Renderer::uploadProbes()
{
    //create the texture to store the probes (do this ONCE!!!)
    if(probes_texture != NULL){delete probes_texture;}
    
    probes_texture = new Texture(9,                   //9 coefficients per probe
                                 this->probes.size(), //as many rows as probes
                                 GL_RGB,              //3 channels per coefficient
                                 GL_FLOAT );          //they require a high range
        
    
    //we must create the color information for the texture. because every SH are 27 floats in the RGB,RGB,... order, we can create an array of SphericalHarmonics and use it as pixels of the texture
    SphericalHarmonics* sh_data = NULL;
    sh_data = new SphericalHarmonics[dim.x * dim.y * dim.z];

    //here we fill the data of the array with our probes in x,y,z order
    for (int i = 0; i < probes.size(); ++i)
        sh_data[i] = probes[i].sh;

    //now upload the data to the GPU as a texture
    probes_texture->upload( GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

    //disable any texture filtering when reading
    probes_texture->bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    //always free memory after allocating it!!!
    delete[] sh_data;
}

// to consider irradiance for the ambient light
void GTR::Renderer::displayIrradiance(Camera* camera, GTR::Scene* scene)
{
    int w = Application::instance->window_width;
    int h = Application::instance->window_height;
    
    // Compute Inverse View Projection
    Matrix44 inv_vp = camera->viewprojection_matrix;
    inv_vp.inverse();
    
    //we need a fullscreen quad
    Mesh* quad = Mesh::getQuad();
    Shader* shader = Shader::Get("irradiance");
    shader->enable();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    
    // pass the gbuffers to the shader and irradiance texture
    shader->setUniform("u_color_texture", gbuffers_fbo->color_textures[0], 6);
    shader->setUniform("u_normal_texture", gbuffers_fbo->color_textures[1], 7);
    shader->setUniform("u_extra_texture", gbuffers_fbo->color_textures[2], 8);
    shader->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 9);
    shader->setUniform("u_probes_texture", probes_texture, 12);

    // upload variables to the shader
    shader->setUniform("u_inverse_viewprojection", inv_vp);
    shader->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));

    shader->setUniform("u_irr_start", start_pos);
    shader->setUniform("u_irr_end", end_pos);
    shader->setUniform("u_irr_dims", dim);
    shader->setUniform("u_irr_normal_distance",irr_normal_distance);
    shader->setUniform("u_irr_delta", delta);
    shader->setUniform("u_num_probes", (float)probes_texture->height);

    
    quad->render(GL_TRIANGLES);
    shader->disable();
    
    //set the render state as it was before to avoid problems with future renders
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LESS);
}

Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = HDRE::Get(filename);
	if (!hdre)
		return NULL;

	Texture* texture = new Texture();
	if (hdre->getFacesf(0))
	{
		texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesf(0),
			hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
		for (int i = 1; i < hdre->levels; ++i)
			texture->uploadCubemap(texture->format, texture->type, false,
				(Uint8**)hdre->getFacesf(i), GL_RGBA32F, i);
	}
	else
		if (hdre->getFacesh(0))
		{
			texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesh(0),
				hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_HALF_FLOAT);
			for (int i = 1; i < hdre->levels; ++i)
				texture->uploadCubemap(texture->format, texture->type, false,
					(Uint8**)hdre->getFacesh(i), GL_RGBA16F, i);
		}
	return texture;
}


// generate sphere points
std::vector<Vector3> GTR::generateSpherePoints(int num, float radius, bool hemi)
{
    std::vector<Vector3> points;
    points.resize(num);
    for (int i = 0; i < num; i += 1)
    {
        Vector3& p = points[i];
        float u = random(1.0);
        float v = random(1.0);
        float theta = u * 2.0 * PI;
        float phi = acos(2.0 * v - 1.0);
        float r = cbrt( random(1.0) * 0.9 + 0.1 ) * radius;
        float sinTheta = sin(theta);
        float cosTheta = cos(theta);
        float sinPhi = sin(phi);
        float cosPhi = cos(phi);
        p.x = r * sinPhi * cosTheta;
        p.y = r * sinPhi * sinTheta;
        p.z = r * cosPhi;
        if (hemi && p.z < 0)
            p.z *= -1.0;
    }
    return points;
}
