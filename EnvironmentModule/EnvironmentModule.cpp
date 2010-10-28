/**
 *  For conditions of distribution and use, see copyright notice in license.txt
 *  @file   EnvironmentModule.cpp
 *  @brief  Environment module. Environment module is be responsible of visual environment features like terrain, sky & water.
 */

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "EnvironmentModule.h"
#include "Terrain.h"
#include "Water.h"
#include "Environment.h"
#include "Sky.h"
#include "EnvironmentEditor.h"
#include "PostProcessWidget.h"

#include "EC_WaterPlane.h"
#include "EC_Fog.h"
#include "EC_SkyPlane.h"
#include "EC_SkyBox.h"
#include "EC_SkyDome.h"
#include "EC_EnvironmentLight.h"

#include <EC_OgreEnvironment.h>

#include "Renderer.h"
#include "RealXtend/RexProtocolMsgIDs.h"
#include "OgreTextureResource.h"
#include "SceneManager.h"
#include "NetworkEvents.h"
#include "InputEvents.h"
#include "GenericMessageUtils.h"
#include "ModuleManager.h"
#include "EventManager.h"
#include "RexNetworkUtils.h"
#include "CompositionHandler.h"
#include <EC_Name.h>

#include "UiServiceInterface.h"
#include "UiProxyWidget.h"

#include "TerrainWeightEditor.h"

#include "WorldBuildingServiceInterface.h"

#include "MemoryLeakCheck.h"

namespace Environment
{
    std::string EnvironmentModule::type_name_static_ = "Environment";

    EnvironmentModule::EnvironmentModule() :
        IModule(type_name_static_),
        w_editor_(0),
        waiting_for_regioninfomessage_(false),
        environment_editor_(0),
        postprocess_dialog_(0),
        resource_event_category_(0),
        scene_event_category_(0),
        framework_event_category_(0),
        input_event_category_(0),
        firstTime_(true)
    {
    }

    EnvironmentModule::~EnvironmentModule()
    {
    }

    void EnvironmentModule::Load()
    {
        DECLARE_MODULE_EC(EC_Terrain);
        DECLARE_MODULE_EC(EC_WaterPlane);
        DECLARE_MODULE_EC(EC_Fog);
        DECLARE_MODULE_EC(EC_SkyPlane);
        DECLARE_MODULE_EC(EC_SkyBox);
        DECLARE_MODULE_EC(EC_SkyDome);
        DECLARE_MODULE_EC(EC_EnvironmentLight);
    }

    void EnvironmentModule::Initialize()
    {
    }

    void EnvironmentModule::PostInitialize()
    {
        event_manager_ = framework_->GetEventManager();
        
        // Depends on rexlogic etc. handling messages first to create the scene, so lower priority
        event_manager_->RegisterEventSubscriber(this, 99);

        resource_event_category_ = event_manager_->QueryEventCategory("Resource");
        scene_event_category_ = event_manager_->QueryEventCategory("Scene");
        framework_event_category_ = event_manager_->QueryEventCategory("Framework");
        input_event_category_ = event_manager_->QueryEventCategory("Input");

        OgreRenderer::Renderer *renderer = framework_->GetService<OgreRenderer::Renderer>();
        if (renderer)
        {
            // Initialize post-process dialog.
            postprocess_dialog_ = new PostProcessWidget(renderer->GetCompositionHandler());

            // Add to scene.
            UiServiceInterface *ui = GetFramework()->GetService<UiServiceInterface>();
            if (!ui)
                return;

            ui->AddWidgetToScene(postprocess_dialog_);
            ui->AddWidgetToMenu(postprocess_dialog_, QObject::tr("Post-processing"), QObject::tr("World Tools"),
                "./data/ui/images/menus/edbutton_POSTPR_normal.png");
        }

        environment_editor_ = new EnvironmentEditor(this);
        Foundation::WorldBuildingServicePtr wb_service = GetFramework()->GetService<Foundation::WorldBuildingServiceInterface>(Foundation::Service::ST_WorldBuilding).lock();
        if (wb_service)
        {
            QObject::connect(wb_service.get(), SIGNAL(OverrideServerTime(int)), environment_editor_, SLOT(TimeOfDayOverrideChanged(int)));
            QObject::connect(wb_service.get(), SIGNAL(SetOverrideTime(int)), environment_editor_, SLOT(TimeValueChanged(int)));
        }

        w_editor_ = new TerrainWeightEditor(this);
        w_editor_->Initialize();
        
    }

    void EnvironmentModule::Uninitialize()
    {
        SAFE_DELETE(environment_editor_);
        SAFE_DELETE(postprocess_dialog_);
        SAFE_DELETE(w_editor_);
        terrain_.reset();
        water_.reset();
        environment_.reset();
        sky_.reset();
        event_manager_.reset();
        currentWorldStream_.reset();

        waiting_for_regioninfomessage_ = false;
    }

    void EnvironmentModule::Update(f64 frametime)
    {
        RESETPROFILER;
     
        PROFILE(EnvironmentModule_Update);

        // Idea of next lines:  Because of initialisation chain, enviroment editor stays in wrong state after logout/login-process. 
        // Solution for that problem is that we initialise it again at that moment when user clicks environment editor, 
        // because currently editor is plain QWidget we have not access to show() - slot. So we here poll widget, and when polling tells us that widget is seen, 
        // we will initialise it again. 

        if ( environment_editor_ != 0 && firstTime_ == true )
        {
            if ( environment_editor_->Showed())
            {
                environment_editor_->InitializeTabs();
                firstTime_ = false;
            }
        }

        if ((currentWorldStream_) && currentWorldStream_->IsConnected())
        {
            if (environment_.get() != 0)
                environment_->Update(frametime);
            if (water_.get() !=0 )
                water_->Update();
            //if  (sky_.get() != 0)
            //    sky_->Update();
        }
    }
#ifdef CAELUM
    Caelum::CaelumSystem* EnvironmentModule::GetCaelum()
    {   
         if (environment_.get() != 0)
         {
            EC_OgreEnvironment* ev = environment_->GetEnvironmentComponent();
            if ( ev != 0)
                return ev->GetCaelum();

         }
         
         return 0;
    }
#endif
    
    bool EnvironmentModule::HandleEvent(event_category_id_t category_id, event_id_t event_id, IEventData* data)
    {
        if(category_id == framework_event_category_)
        {
            HandleFrameworkEvent(event_id, data);
        }
        else if(category_id == resource_event_category_)
        {
            HandleResouceEvent(event_id, data);
        }
        else if(category_id == network_in_event_category_)
        {
            HandleNetworkEvent(event_id, data);
        }
        else if (category_id == network_state_event_category_)
        {
            if (event_id == ProtocolUtilities::Events::EVENT_SERVER_CONNECTED)
            {
                if (GetFramework()->GetDefaultWorldScene().get())
                {
                    CreateEnvironment();
                    CreateTerrain();
                    CreateWater();
                    CreateSky();
                }
            }

            if (event_id == ProtocolUtilities::Events::EVENT_SERVER_DISCONNECTED)
            {
                if(postprocess_dialog_)
                    postprocess_dialog_->DisableAllEffects();
                ReleaseTerrain();
                ReleaseWater();
                ReleaseEnvironment();
                ReleaseSky();
                firstTime_ = true;
               
            }
        }
        else if(category_id == input_event_category_)
        {
            HandleInputEvent(event_id, data);
        }
        return false;
    }

    bool EnvironmentModule::HandleResouceEvent(event_id_t event_id, IEventData* data)
    {
        if (event_id == Resource::Events::RESOURCE_READY)
        {
            Resource::Events::ResourceReady *res = dynamic_cast<Resource::Events::ResourceReady*>(data);
            assert(res);
            if (!res)
                return false;

            OgreRenderer::OgreTextureResource *tex = dynamic_cast<OgreRenderer::OgreTextureResource *>(res->resource_.get()); 
            if (tex)
            {
                // Pass the texture asset to the terrain manager - the texture might be in the terrain.
                if (terrain_.get())
                    terrain_->OnTextureReadyEvent(res);

                // Pass the texture asset to the sky manager - the texture might be in the sky.
                if (sky_.get())
                    sky_->OnTextureReadyEvent(res);
            }
            Foundation::TextureInterface *decoded_tex = dynamic_cast<Foundation::TextureInterface *>(res->resource_.get());
            if (decoded_tex)
                // Pass the texture asset to environment editor.
                if (environment_editor_)
                    environment_editor_->HandleResourceReady(res);
        }

        return false;
    }

    bool EnvironmentModule::HandleFrameworkEvent(event_id_t event_id, IEventData* data)
    {
        switch(event_id)
        {
            case Foundation::NETWORKING_REGISTERED:
            {
                // Begin to listen network events.
                network_in_event_category_ = event_manager_->QueryEventCategory("NetworkIn");
                network_state_event_category_ = event_manager_->QueryEventCategory("NetworkState");
                return false;
            }
            case Foundation::WORLD_STREAM_READY:
            {
                ProtocolUtilities::WorldStreamReadyEvent *event_data = dynamic_cast<ProtocolUtilities::WorldStreamReadyEvent *>(data);
                if (event_data)
                    currentWorldStream_ = event_data->WorldStream;

                return false;
            }
        }

        return false;
    }

    bool EnvironmentModule::HandleNetworkEvent(event_id_t event_id, IEventData* data)
    {
        ProtocolUtilities::NetworkEventInboundData *netdata = checked_static_cast<ProtocolUtilities::NetworkEventInboundData *>(data);
        assert(netdata);

        switch(event_id)
        {
        case RexNetMsgLayerData:
        {
            if(terrain_.get())
                return terrain_->HandleOSNE_LayerData(netdata);
        }
        case RexNetMsgGenericMessage:
        {
            ProtocolUtilities::NetInMessage &msg = *netdata->message;
            std::string methodname = ProtocolUtilities::ParseGenericMessageMethod(msg);

            if (methodname == "RexPostP")
            {
                OgreRenderer::Renderer *renderer = framework_->GetService<OgreRenderer::Renderer>();
                if (renderer)
                {
                    StringVector vec = ProtocolUtilities::ParseGenericMessageParameters(msg);
                    //Since postprocessing effect was enabled/disabled elsewhere, we have to notify the dialog about the event.
                    //Also, no need to put effect on from the CompositionHandler since the dialog will notify CompositionHandler when 
                    //button is checked
                    if (postprocess_dialog_)
                    {
                        QString effect_name = renderer->GetCompositionHandler()->MapNumberToEffectName(vec.at(0)).c_str();
                        bool enabled = true;
                        if (vec.at(1) == "False")
                            enabled = false;

                        postprocess_dialog_->EnableEffect(effect_name,enabled);
                    }
                }
            }
            else if(methodname == "RexSky" && sky_.get())
            {
                return GetSkyHandler()->HandleRexGM_RexSky(netdata);
            }
            else if (methodname == "RexWaterHeight")
            {
                msg.ResetReading();
                msg.SkipToFirstVariableByName("Parameter");

                // Variable block begins, should have currently (at least) 1 instances.
                size_t instance_count = msg.ReadCurrentBlockInstanceCount();
                if (instance_count < 1)
                    return false;

                if (water_.get() != 0)
                {
                    std::string message = msg.ReadString();
                    // Convert to float.
                    try
                    {
                        float height = boost::lexical_cast<float>(message);
                        water_->SetWaterHeight(height, AttributeChange::LocalOnly);
                    }
                    catch(boost::bad_lexical_cast&)
                    {
                    }
                }
            }
            else if (methodname == "RexDrawWater")
            {
                msg.ResetReading();
                msg.SkipToFirstVariableByName("Parameter");

                // Variable block begins, should have currently (at least) 1 instances.
                size_t instance_count = msg.ReadCurrentBlockInstanceCount();
                if (instance_count < 1 )
                    return false;

                std::string message = msg.ReadString();
                bool draw = ParseBool(message);
                if (draw)
                    if (water_.get())
                        water_->CreateWaterGeometry();
                    else
                        CreateWater();
                else
                    water_->RemoveWaterGeometry();
            }
            else if (methodname == "RexFog")
            {
                StringVector parameters = ProtocolUtilities::ParseGenericMessageParameters(msg); 
                if ( parameters.size() < 5)
                    return false;

                // may have , instead of . so replace
                ReplaceCharInplace(parameters[0], ',', '.');
                ReplaceCharInplace(parameters[1], ',', '.');
                ReplaceCharInplace(parameters[2], ',', '.');
                ReplaceCharInplace(parameters[3], ',', '.');
                ReplaceCharInplace(parameters[4], ',', '.');
                float fogStart = 0.0, fogEnd = 0.0, fogC_r = 0.0, fogC_g = 0.0, fogC_b = 0.0;

                try
                {
                    fogStart = boost::lexical_cast<float>(parameters[0]);
                    fogEnd = boost::lexical_cast<float>(parameters[1]);
                    fogC_r = boost::lexical_cast<float>(parameters[2]);
                    fogC_g = boost::lexical_cast<float>(parameters[3]);
                    fogC_b = boost::lexical_cast<float>(parameters[4]);
                }
                catch(boost::bad_lexical_cast&)
                {
                    return false;
                }
                if (water_ != 0 )
                {
                    // Adjust fog.
                    QVector<float> color;
                    color<<fogC_r<<fogC_g<<fogC_b;
                    water_->SetWaterFog(fogStart, fogEnd, color); 
                }
            }
            else if (methodname == "RexAmbientL")
            {
                /**
                 * Deals RexAmbientLight message. 
                 **/
                
                StringVector parameters = ProtocolUtilities::ParseGenericMessageParameters(msg); 
                if ( parameters.size() < 3)
                    return false; 

                // may have , instead of . so replace
                ReplaceCharInplace(parameters[0], ',', '.');
                ReplaceCharInplace(parameters[1], ',', '.');
                ReplaceCharInplace(parameters[2], ',', '.');

                const QChar empty(' ');
                StringVector sun_light_direction = SplitString(parameters[0].c_str(), empty.toAscii() );
                StringVector sun_light_color = SplitString(parameters[1].c_str(), empty.toAscii());
                StringVector ambient_light_color = SplitString(parameters[2].c_str(), empty.toAscii());

                if ( environment_ != 0 )
                {
                      EC_EnvironmentLight* light = environment_->GetEnvironmentLight();
                      if ( light != 0)
                      {
                          // Because of caelum defines environment light values normally we need to set it off. 
                          light->useCaelumAttr.Set(false, AttributeChange::LocalOnly);
                      } 
                      else
                      {
                        // Create EC_EnvironmentLight 
                        QString name = "LightEnvironment";
                        CreateEnvironmentEntity(name, EC_EnvironmentLight::TypeNameStatic()); 
                        light = environment_->GetEnvironmentLight();
                        light->useCaelumAttr.Set(false, AttributeChange::LocalOnly);
                      
                      }
                      
                      environment_->SetSunDirection(environment_->ConvertToQVector<float>(sun_light_direction));
                      environment_->SetSunColor(environment_->ConvertToQVector<float>(sun_light_color));
                      environment_->SetAmbientLight(environment_->ConvertToQVector<float>(ambient_light_color));
                 }
            }
        }
        case RexNetMsgSimulatorViewerTimeMessage:
        {
            if (environment_!= 0)
                return environment_->HandleSimulatorViewerTimeMessage(netdata);
            break;
        }
        case RexNetMsgRegionHandshake:
        {
            bool kill_event = HandleOSNE_RegionHandshake(netdata);
            if (environment_editor_)
                environment_editor_->UpdateTerrainTextureRanges();
            return kill_event;
        }
        case RexNetMsgRegionInfo:
        {
            if (waiting_for_regioninfomessage_)
            {
                currentWorldStream_->SendTextureCommitMessage();
                waiting_for_regioninfomessage_ = false;
            }
        }
        }

        return false;
    }

    Scene::EntityPtr EnvironmentModule::CreateEnvironmentEntity(const QString& entity_name, const QString& component_name) 
    {
        
        Scene::ScenePtr active_scene = framework_->GetDefaultWorldScene();
        // Search first that does there exist environment entity
        Scene::EntityPtr entity = active_scene->GetEntityByName(entity_name);
        if (entity != 0)
        {
            // Does it have component? If not create. 
            if ( !entity->HasComponent(component_name) )
                entity->AddComponent(framework_->GetComponentManager()->CreateComponent(component_name), AttributeChange::Replicate);
        
        
            return entity;
        }
       

        entity = active_scene->GetEntityByName("LocalEnvironment");

        if (entity != 0)
        {
             // Does it have component? If not create. 
            if ( !entity->HasComponent(component_name) )
                entity->AddComponent(framework_->GetComponentManager()->CreateComponent(component_name), AttributeChange::LocalOnly);

        }
        else
        {
            int id = active_scene->GetNextFreeId();
            entity = active_scene->CreateEntity(id);
            entity->AddComponent(framework_->GetComponentManager()->CreateComponent(EC_Name::TypeNameStatic()));
            EC_Name* nameComp = entity->GetComponent<EC_Name >().get();
            nameComp->name.Set("LocalEnvironment", AttributeChange::LocalOnly);
            
            // Create param component.
            entity->AddComponent(framework_->GetComponentManager()->CreateComponent(component_name), AttributeChange::LocalOnly);
        }
        
        return entity;
  
    }

    void EnvironmentModule::RemoveLocalEnvironment()
    {
        Scene::ScenePtr active_scene = framework_->GetDefaultWorldScene();
        Scene::Entity* entity = active_scene->GetEntityByName("LocalEnvironment").get();
    
        if ( entity == 0)
            return;
        else
        {   
            if ( entity->HasComponent(EC_WaterPlane::TypeNameStatic()) && active_scene->GetEntityByName("WaterEnvironment").get() != 0 )
                entity->RemoveComponent(entity->GetComponent(EC_WaterPlane::TypeNameStatic()));  
            if  ( entity->HasComponent(EC_Fog::TypeNameStatic()) && active_scene->GetEntityByName("FogEnvironment").get() != 0)
                 entity->RemoveComponent(entity->GetComponent(EC_Fog::TypeNameStatic()));
            if ( entity->HasComponent(EC_SkyPlane::TypeNameStatic()) && active_scene->GetEntityByName("SkyEnvironment").get() != 0)
                entity->RemoveComponent(entity->GetComponent(EC_SkyPlane::TypeNameStatic()));
            if ( entity->HasComponent(EC_SkyBox::TypeNameStatic()) && active_scene->GetEntityByName("SkyEnvironment").get() != 0)
                entity->RemoveComponent(entity->GetComponent(EC_SkyBox::TypeNameStatic()));
             if ( entity->HasComponent(EC_SkyDome::TypeNameStatic()) && active_scene->GetEntityByName("SkyEnvironment").get() != 0)
                entity->RemoveComponent(entity->GetComponent(EC_SkyDome::TypeNameStatic()));
            if ( entity->HasComponent(EC_EnvironmentLight::TypeNameStatic()) && active_scene->GetEntityByName("LightEnvironment").get() != 0)
                entity->RemoveComponent(entity->GetComponent(EC_EnvironmentLight::TypeNameStatic()));
            
        
        }

        if (!entity->HasComponent(EC_WaterPlane::TypeNameStatic()) &&
            !entity->HasComponent(EC_Fog::TypeNameStatic())  && 
            !entity->HasComponent(EC_SkyPlane::TypeNameStatic()) && 
            !entity->HasComponent(EC_SkyBox::TypeNameStatic()) && 
            !entity->HasComponent(EC_EnvironmentLight::TypeNameStatic()) &&
            !entity->HasComponent(EC_SkyDome::TypeNameStatic())) 
                active_scene->RemoveEntity(entity->GetId());
        

    }

    bool EnvironmentModule::HandleInputEvent(event_id_t event_id, IEventData* data)
    {
        return false;
    }

    bool EnvironmentModule::HandleOSNE_RegionHandshake(ProtocolUtilities::NetworkEventInboundData* data)
    {
        ProtocolUtilities::NetInMessage &msg = *data->message;
        msg.ResetReading();

        msg.SkipToNextVariable(); // RegionFlags U32
        msg.SkipToNextVariable(); // SimAccess U8
        msg.SkipToNextVariable(); // SimName
        msg.SkipToNextVariable(); // SimOwner
        msg.SkipToNextVariable(); // IsEstateManager

        // Water height.
        float water_height = msg.ReadF32();
        if(water_.get())
            water_->SetWaterHeight(water_height, AttributeChange::LocalOnly);

        msg.SkipToNextVariable(); // BillableFactor
        msg.SkipToNextVariable(); // CacheID
        for(int i = 0; i < 4; ++i)
            msg.SkipToNextVariable(); // TerrainBase0..3

        // Terrain texture id
        RexAssetID terrain[4];
        terrain[0] = msg.ReadUUID().ToString();
        terrain[1] = msg.ReadUUID().ToString();
        terrain[2] = msg.ReadUUID().ToString();
        terrain[3] = msg.ReadUUID().ToString();

        float TerrainStartHeights[4];
        TerrainStartHeights[0] = msg.ReadF32();
        TerrainStartHeights[1] = msg.ReadF32();
        TerrainStartHeights[2] = msg.ReadF32();
        TerrainStartHeights[3] = msg.ReadF32();

        float TerrainStartRanges[4];
        TerrainStartRanges[0] = msg.ReadF32();
        TerrainStartRanges[1] = msg.ReadF32();
        TerrainStartRanges[2] = msg.ReadF32();
        TerrainStartRanges[3] = msg.ReadF32();

        if(terrain_.get())
        {
            terrain_->SetTerrainTextures(terrain);
            terrain_->SetTerrainHeightValues(TerrainStartHeights, TerrainStartRanges);
        }

        return false;
    }

    TerrainPtr EnvironmentModule::GetTerrainHandler() const
    {
        return terrain_;
    }

    EnvironmentPtr EnvironmentModule::GetEnvironmentHandler() const
    {
        return environment_;
    }

    SkyPtr EnvironmentModule::GetSkyHandler() const
    {
        return sky_;
    }

    WaterPtr EnvironmentModule::GetWaterHandler() const
    {
        return water_;
    }

    void EnvironmentModule::SendModifyLandMessage(f32 x, f32 y, u8 brush, u8 action, float seconds, float height)
    {
        if (currentWorldStream_.get())
            currentWorldStream_->SendModifyLandPacket(x, y, brush, action, seconds, height);
    }

    void EnvironmentModule::SendTextureHeightMessage(float start_height, float height_range, uint corner)
    {
        if (currentWorldStream_.get())
        {
            currentWorldStream_->SendTextureHeightsMessage(start_height, height_range, corner);
            waiting_for_regioninfomessage_ = true;
        }
    }

    void EnvironmentModule::SendTextureDetailMessage(const RexTypes::RexAssetID &new_texture_id, uint texture_index)
    {
        if (currentWorldStream_.get())
        {
            currentWorldStream_->SendTextureDetail(new_texture_id, texture_index);
            waiting_for_regioninfomessage_ = true;
        }
    }

    void EnvironmentModule::CreateTerrain()
    {
        terrain_ = TerrainPtr(new Terrain(this));

        Scene::ScenePtr scene = GetFramework()->GetDefaultWorldScene();
        Scene::EntityPtr entity = scene->CreateEntity(GetFramework()->GetDefaultWorldScene()->GetNextFreeId());
        entity->AddComponent(GetFramework()->GetComponentManager()->CreateComponent("EC_Terrain"));
        scene->EmitEntityCreated(entity);
        terrain_->FindCurrentlyActiveTerrain();
        
        /*if ( environment_editor_ != 0 )
        {
            environment_editor_->InitTerrainTabWindow();
            environment_editor_->InitTerrainTextureTabWindow();
        }*/
        
    }

    void EnvironmentModule::CreateWater()
    {
        water_ = WaterPtr(new Water(this));
        water_->CreateWaterGeometry();
        /*if ( environment_editor_ != 0 )
             environment_editor_->InitWaterTabWindow();*/
    }

    void EnvironmentModule::CreateEnvironment()
    {
        environment_ = EnvironmentPtr(new Environment(this));
        environment_->CreateEnvironment();
    }

    void EnvironmentModule::CreateSky()
    {
        sky_ = SkyPtr(new Sky(this));

        /*if ( environment_editor_ != 0 )
             environment_editor_->InitSkyTabWindow();
        
        if (!GetEnvironmentHandler()->IsCaelum())
            sky_->CreateDefaultSky(true);*/
 /*       
        Scene::ScenePtr scene = GetFramework()->GetDefaultWorldScene();
        Scene::EntityPtr sky_entity = scene->CreateEntity(GetFramework()->GetDefaultWorldScene()->GetNextFreeId());
        sky_entity->AddComponent(GetFramework()->GetComponentManager()->CreateComponent("EC_OgreSky"));
        scene->EmitEntityCreated(sky_entity);
        sky_->FindCurrentlyActiveSky();

        if (!GetEnvironmentHandler()->IsCaelum())
            sky_->CreateDefaultSky();
*/
       
    }

    void EnvironmentModule::ReleaseTerrain()
    {
        terrain_.reset();
        waiting_for_regioninfomessage_ = false;
    }

    void EnvironmentModule::ReleaseWater()
    {
        water_.reset();
    }

    void EnvironmentModule::ReleaseEnvironment()
    {
        environment_.reset();
    }

    void EnvironmentModule::ReleaseSky()
    {
        sky_.reset();
    }
}

extern "C" void POCO_LIBRARY_API SetProfiler(Foundation::Profiler *profiler);
void SetProfiler(Foundation::Profiler *profiler)
{
    Foundation::ProfilerSection::SetProfiler(profiler);
}

using namespace Environment;

POCO_BEGIN_MANIFEST(IModule)
    POCO_EXPORT_CLASS(EnvironmentModule)
POCO_END_MANIFEST
