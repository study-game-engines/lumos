#include "Editor.h"
#include "SceneViewPanel.h"
#include "GameViewPanel.h"
#include "ConsolePanel.h"
#include "HierarchyPanel.h"
#include "InspectorPanel.h"
#include "ApplicationInfoPanel.h"
#include "GraphicsInfoPanel.h"
#include "TextEditPanel.h"
#include "ResourcePanel.h"
#include "ImGUIConsoleSink.h"
#include "SceneSettingsPanel.h"
#include "EditorSettingsPanel.h"
#include "ProjectSettingsPanel.h"

#include <Lumos/Graphics/Camera/EditorCamera.h>
#include <Lumos/Utilities/Timer.h>
#include <Lumos/Core/Application.h>
#include <Lumos/Core/OS/Input.h>
#include <Lumos/Core/OS/FileSystem.h>
#include <Lumos/Core/OS/OS.h>
#include <Lumos/Core/Version.h>
#include <Lumos/Core/Engine.h>
#include <Lumos/Audio/AudioManager.h>
#include <Lumos/Scene/Scene.h>
#include <Lumos/Scene/SceneManager.h>
#include <Lumos/Scene/Entity.h>
#include <Lumos/Scene/EntityManager.h>
#include <Lumos/Events/ApplicationEvent.h>
#include <Lumos/Scene/Component/Components.h>
#include <Lumos/Scene/Component/ModelComponent.h>
#include <Lumos/Scripting/Lua/LuaScriptComponent.h>
#include <Lumos/Physics/LumosPhysicsEngine/LumosPhysicsEngine.h>
#include <Lumos/Physics/B2PhysicsEngine/B2PhysicsEngine.h>
#include <Lumos/Graphics/MeshFactory.h>
#include <Lumos/Graphics/Sprite.h>
#include <Lumos/Graphics/AnimatedSprite.h>
#include <Lumos/Graphics/Light.h>
#include <Lumos/Graphics/RHI/Texture.h>
#include <Lumos/Graphics/Camera/Camera.h>
#include <Lumos/Graphics/RHI/GraphicsContext.h>
#include <Lumos/Graphics/Renderers/GridRenderer.h>
#include <Lumos/Graphics/Renderers/DebugRenderer.h>
#include <Lumos/Graphics/Model.h>
#include <Lumos/Graphics/Environment.h>
#include <Lumos/Scene/EntityFactory.h>
#include <Lumos/ImGui/IconsMaterialDesignIcons.h>
#include <Lumos/Embedded/EmbedAsset.h>
#include <Lumos/Scene/Component/ModelComponent.h>
#include <imgui/Plugins/imcmd_command_palette.h>
#include <Lumos/Maths/BoundingBox.h>
#include <Lumos/Maths/BoundingSphere.h>
#include <Lumos/Maths/Frustum.h>
#include <Lumos/Maths/Plane.h>
#include <Lumos/Maths/MathsUtilities.h>
#include <Lumos/Core/LMLog.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include <imgui/imgui_internal.h>
#include <imgui/Plugins/ImGuizmo.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <cereal/version.hpp>

namespace Lumos
{
    Editor* Editor::s_Editor = nullptr;

    Editor::Editor()
        : Application()
        , m_IniFile("")
    {
        spdlog::sink_ptr sink = std::make_shared<ImGuiConsoleSink_mt>();

        Lumos::Debug::Log::AddSink(sink);

        // Remove?
        s_Editor = this;
    }

    Editor::~Editor()
    {
    }

    void Editor::OnQuit()
    {
        SaveEditorSettings();

        for(auto panel : m_Panels)
            panel->DestroyGraphicsResources();

        m_GridRenderer.reset();
        m_PreviewTexture.reset();
        m_PreviewSphere.reset();
        m_Panels.clear();

        Application::OnQuit();
    }

    bool show_demo_window     = true;
    bool show_command_palette = false;
    ImVec4 clear_color        = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    ImCmd::Command toggle_demo_cmd;

    void Editor::Init()
    {
        LUMOS_PROFILE_FUNCTION();
        ImCmd::CreateContext();
        toggle_demo_cmd.Name            = "Toggle ImGui demo window";
        toggle_demo_cmd.InitialCallback = [&]()
        {
            show_demo_window = !show_demo_window;
        };

        ImCmd::Command select_theme_cmd;
        select_theme_cmd.Name            = "Select theme";
        select_theme_cmd.InitialCallback = [&]()
        {
            ImCmd::Prompt(std::vector<std::string> {
                "Black",
                "Dark",
                "Dracula",
                "Grey",
                "Light",
                "Blue",
                "ClassicLight",
                "ClassicDark",
                "Classic",
                "Cherry",
                "Cinder",
                "Cosy" });
        };
        select_theme_cmd.SubsequentCallback = [&](int selected_option)
        {
            ImGuiUtilities::SetTheme(ImGuiUtilities::Theme(selected_option));
        };
        ImCmd::AddCommand(std::move(select_theme_cmd));

        ImCmd::Command example_cmd;
        example_cmd.Name            = "Open Project";
        example_cmd.InitialCallback = [&]()
        {
            ImGui::OpenPopup("Open Project");
        };

        ImCmd::Command add_example_cmd_cmd;
        add_example_cmd_cmd.Name            = "Add 'Example command'";
        add_example_cmd_cmd.InitialCallback = [&]()
        {
            ImCmd::AddCommand(example_cmd);
        };

        ImCmd::Command remove_example_cmd_cmd;
        remove_example_cmd_cmd.Name            = "Remove 'Example command'";
        remove_example_cmd_cmd.InitialCallback = [&]()
        {
            ImCmd::RemoveCommand(example_cmd.Name.c_str());
        },

        ImCmd::AddCommand(example_cmd); // Copy intentionally
        ImCmd::AddCommand(std::move(add_example_cmd_cmd));
        ImCmd::AddCommand(std::move(remove_example_cmd_cmd));

        auto& guizmoStyle                    = ImGuizmo::GetStyle();
        guizmoStyle.HatchedAxisLineThickness = -1.0f;

#ifdef LUMOS_PLATFORM_IOS
        m_TempSceneSaveFilePath = OS::Instance()->GetAssetPath();
#else
#ifdef LUMOS_PLATFORM_LINUX
        m_TempSceneSaveFilePath = std::filesystem::current_path().string();
#else
        m_TempSceneSaveFilePath = std::filesystem::temp_directory_path().string();
#endif

        m_TempSceneSaveFilePath += "Lumos/";
        if(!FileSystem::FolderExists(m_TempSceneSaveFilePath))
            std::filesystem::create_directory(m_TempSceneSaveFilePath);

        std::vector<std::string> iniLocation = {
            StringUtilities::GetFileLocation(OS::Instance()->GetExecutablePath()) + "Editor.ini",
            StringUtilities::GetFileLocation(OS::Instance()->GetExecutablePath()) + "../../../Editor.ini"
        };
        bool fileFound = false;
        std::string filePath;
        for(auto& path : iniLocation)
        {
            if(FileSystem::FileExists(path))
            {
                LUMOS_LOG_INFO("Loaded Editor Ini file {0}", path);
                filePath  = path;
                m_IniFile = IniFile(filePath);
                // ImGui::GetIO().IniFilename = ini[i];
                fileFound = true;
                LoadEditorSettings();
                break;
            }
        }

        if(!fileFound)
        {
            LUMOS_LOG_INFO("Editor Ini not found");
#ifdef LUMOS_PLATFORM_MACOS
            filePath = StringUtilities::GetFileLocation(OS::Instance()->GetExecutablePath()) + "../../../Editor.ini";
#else
            filePath = StringUtilities::GetFileLocation(OS::Instance()->GetExecutablePath()) + "Editor.ini";
#endif
            LUMOS_LOG_INFO("Creating Editor Ini {0}", filePath);

            //  FileSystem::WriteTextFile(filePath, "");
            m_IniFile = IniFile(filePath);
            AddDefaultEditorSettings();
            // ImGui::GetIO().IniFilename = "editor.ini";
        }
#endif

        Application::Init();
        Application::SetEditorState(EditorState::Preview);
        Application::Get().GetWindow()->SetEventCallback(BIND_EVENT_FN(Editor::OnEvent));

        m_EditorCamera  = CreateSharedPtr<Camera>(-20.0f,
                                                 -40.0f,
                                                 glm::vec3(-31.0f, 12.0f, 51.0f),
                                                 60.0f,
                                                 0.01f,
                                                 m_Settings.m_CameraFar,
                                                 (float)Application::Get().GetWindowSize().x / (float)Application::Get().GetWindowSize().y);
        m_CurrentCamera = m_EditorCamera.get();

        glm::mat4 viewMat = glm::inverse(glm::lookAt(glm::vec3(-31.0f, 12.0f, 51.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        m_EditorCameraTransform.SetLocalTransform(viewMat);
        m_EditorCameraTransform.SetWorldMatrix(glm::mat4(1.0f));

        m_ComponentIconMap[typeid(Graphics::Light).hash_code()]          = ICON_MDI_LIGHTBULB;
        m_ComponentIconMap[typeid(Camera).hash_code()]                   = ICON_MDI_CAMERA;
        m_ComponentIconMap[typeid(SoundComponent).hash_code()]           = ICON_MDI_VOLUME_HIGH;
        m_ComponentIconMap[typeid(Graphics::Sprite).hash_code()]         = ICON_MDI_IMAGE;
        m_ComponentIconMap[typeid(Maths::Transform).hash_code()]         = ICON_MDI_VECTOR_LINE;
        m_ComponentIconMap[typeid(RigidBody2DComponent).hash_code()]     = ICON_MDI_SQUARE_OUTLINE;
        m_ComponentIconMap[typeid(RigidBody3DComponent).hash_code()]     = ICON_MDI_CUBE_OUTLINE;
        m_ComponentIconMap[typeid(Graphics::ModelComponent).hash_code()] = ICON_MDI_VECTOR_POLYGON;
        m_ComponentIconMap[typeid(Graphics::Model).hash_code()]          = ICON_MDI_VECTOR_POLYGON;
        m_ComponentIconMap[typeid(LuaScriptComponent).hash_code()]       = ICON_MDI_LANGUAGE_LUA;
        m_ComponentIconMap[typeid(Graphics::Environment).hash_code()]    = ICON_MDI_EARTH;
        m_ComponentIconMap[typeid(Editor).hash_code()]                   = ICON_MDI_SQUARE;
        m_ComponentIconMap[typeid(TextComponent).hash_code()]            = ICON_MDI_TEXT;

        m_Panels.emplace_back(CreateSharedPtr<ConsolePanel>());
        m_Panels.emplace_back(CreateSharedPtr<SceneViewPanel>());
        m_Panels.emplace_back(CreateSharedPtr<GameViewPanel>());
        m_Panels.emplace_back(CreateSharedPtr<InspectorPanel>());
        m_Panels.emplace_back(CreateSharedPtr<ApplicationInfoPanel>());
        m_Panels.emplace_back(CreateSharedPtr<SceneSettingsPanel>());
        m_Panels.emplace_back(CreateSharedPtr<HierarchyPanel>());
        m_Panels.emplace_back(CreateSharedPtr<EditorSettingsPanel>());
        m_Panels.back()->SetActive(false);
        m_Panels.emplace_back(CreateSharedPtr<ProjectSettingsPanel>());
        m_Panels.back()->SetActive(false);
        m_Panels.emplace_back(CreateSharedPtr<GraphicsInfoPanel>());
        m_Panels.back()->SetActive(false);
#ifndef LUMOS_PLATFORM_IOS
        m_Panels.emplace_back(CreateSharedPtr<ResourcePanel>());
#endif

        for(auto& panel : m_Panels)
            panel->SetEditor(this);

        CreateGridRenderer();

        m_Settings.m_ShowImGuiDemo = false;

        m_SelectedEntities.clear();
        // m_SelectedEntity = entt::null;
        m_PreviewTexture = nullptr;

        Application::Get().GetSystem<LumosPhysicsEngine>()->SetDebugDrawFlags(m_Settings.m_Physics3DDebugFlags);
        Application::Get().GetSystem<B2PhysicsEngine>()->SetDebugDrawFlags(m_Settings.m_Physics2DDebugFlags);

        ImGuiUtilities::SetTheme(m_Settings.m_Theme);
        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
        Application::Get().GetWindow()->SetWindowTitle("Lumos Editor");

        ImGuizmo::SetGizmoSizeClipSpace(m_Settings.m_ImGuizmoScale);
        // ImGuizmo::SetGizmoSizeScale(Application::Get().GetWindowDPI());
    }

    bool Editor::IsTextFile(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string extension = StringUtilities::GetFilePathExtension(filePath);

        if(extension == "txt" || extension == "glsl" || extension == "shader" || extension == "vert"
           || extension == "frag" || extension == "lua" || extension == "Lua")
            return true;

        return false;
    }

    bool Editor::IsFontFile(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string extension = StringUtilities::GetFilePathExtension(filePath);

        if(extension == "ttf")
            return true;

        return false;
    }

    bool Editor::IsAudioFile(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string extension = StringUtilities::GetFilePathExtension(filePath);

        if(extension == "ogg" || extension == "wav")
            return true;

        return false;
    }

    bool Editor::IsShaderFile(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string extension = StringUtilities::GetFilePathExtension(filePath);

        if(extension == "vert" || extension == "frag" || extension == "comp")
            return true;

        return false;
    }

    bool Editor::IsSceneFile(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string extension = StringUtilities::GetFilePathExtension(filePath);

        if(extension == "lsn")
            return true;

        return false;
    }

    bool Editor::IsModelFile(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string extension = StringUtilities::GetFilePathExtension(filePath);

        if(extension == "obj" || extension == "gltf" || extension == "glb" || extension == "fbx" || extension == "FBX")
            return true;

        return false;
    }

    bool Editor::IsTextureFile(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string extension = StringUtilities::GetFilePathExtension(filePath);
        extension             = StringUtilities::ToLower(extension);
        if(extension == "png" || extension == "tga" || extension == "jpg")
            return true;

        return false;
    }

    bool IsPrefab(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string extension = StringUtilities::GetFilePathExtension(filePath);
        extension             = StringUtilities::ToLower(extension);
        if(extension == "lprefab")
            return true;

        return false;
    }

    void Editor::OnImGui()
    {
        LUMOS_PROFILE_FUNCTION();
        DrawMenuBar();

        BeginDockSpace(m_Settings.m_FullScreenOnPlay && Application::Get().GetEditorState() == EditorState::Play);

        for(auto& panel : m_Panels)
        {
            if(panel->Active())
                panel->OnImGui();
        }

        if(m_Settings.m_ShowImGuiDemo)
            ImGui::ShowDemoWindow(&m_Settings.m_ShowImGuiDemo);

        m_Settings.m_View2D = m_CurrentCamera->IsOrthographic();

        m_FileBrowserPanel.OnImGui();
        auto& io  = ImGui::GetIO();
        auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
        if(ctrl && Input::Get().GetKeyPressed(Lumos::InputCode::Key::P))
        {
            show_command_palette = !show_command_palette;
        }
        if(show_command_palette)
        {
            ImCmd::CommandPaletteWindow("CommandPalette", &show_command_palette);
        }

        if(Application::Get().GetEditorState() == EditorState::Preview)
            Application::Get().GetSceneManager()->GetCurrentScene()->UpdateSceneGraph();

        EndDockSpace();

        ImGuiDockContext* dc = &ImGui::GetCurrentContext()->DockContext;
        for(int n = 0; n < dc->Nodes.Data.Size; n++)
            if(ImGuiDockNode* node = (ImGuiDockNode*)dc->Nodes.Data[n].val_p)
            {
                if(node->TabBar)
                    for(int n = 0; n < node->TabBar->Tabs.Size; n++)
                    {
                        const bool tab_visible     = node->TabBar->VisibleTabId == node->TabBar->Tabs[n].ID;
                        const bool tab_bar_focused = (node->TabBar->Flags & ImGuiTabBarFlags_IsFocused) != 0;
                        if(tab_bar_focused || tab_visible)
                        {
                            auto tab = node->TabBar->Tabs[n];

                            ImVec2 pos = tab.Window->Pos;
                            pos.x      = pos.x + tab.Offset + ImGui::GetStyle().FramePadding.x;
                            pos.y      = pos.y + ImGui::GetStyle().ItemSpacing.y;
                            ImRect bb(pos, { pos.x + tab.ContentWidth, pos.y });

                            tab.Window->DrawList->AddLine(bb.Min, bb.Max, (!tab_bar_focused) ? ImGui::GetColorU32(ImGuiCol_SliderGrabActive) : ImGui::GetColorU32(ImGuiCol_Text), 2.0f);
                        }
                    }
            }

        Application::OnImGui();
    }

    Graphics::RenderAPI StringToRenderAPI(const std::string& name)
    {
#ifdef LUMOS_RENDER_API_VULKAN
        if(name == "Vulkan")
            return Graphics::RenderAPI::VULKAN;
#endif
#ifdef LUMOS_RENDER_API_OPENGL
        if(name == "OpenGL")
            return Graphics::RenderAPI::OPENGL;
#endif
#ifdef LUMOS_RENDER_API_DIRECT3D
        if(name == "Direct3D11")
            return Graphics::RenderAPI::DIRECT3D;
#endif

        LUMOS_LOG_ERROR("Unsupported Graphics API");

        return Graphics::RenderAPI::OPENGL;
    }

    void Editor::OpenFile()
    {
        LUMOS_PROFILE_FUNCTION();

        // Set filePath to working directory
        auto path = OS::Instance()->GetExecutablePath();
        std::filesystem::current_path(path);
        m_FileBrowserPanel.SetCallback(BIND_FILEBROWSER_FN(Editor::FileOpenCallback));
        m_FileBrowserPanel.Open();
    }

    void Editor::EmbedFile()
    {
        m_FileBrowserPanel.SetCallback(BIND_FILEBROWSER_FN(Editor::FileEmbedCallback));
        m_FileBrowserPanel.Open();
    }

    static std::string projectLocation = "../";
    static bool reopenNewProjectPopup  = false;
    static bool locationPopupOpened    = false;

    void Editor::NewProjectLocationCallback(const std::string& path)
    {
        projectLocation       = path;
        m_NewProjectPopupOpen = false;
        reopenNewProjectPopup = true;
        locationPopupOpened   = false;
    }

    void Editor::DrawMenuBar()
    {
        LUMOS_PROFILE_FUNCTION();

        bool openSaveScenePopup   = false;
        bool openNewScenePopup    = false;
        bool openReloadScenePopup = false;
        bool openProjectLoadPopup = !m_ProjectLoaded;

        if(ImGui::BeginMainMenuBar())
        {
            if(ImGui::BeginMenu("File"))
            {
                if(ImGui::MenuItem("Open Project"))
                {
                    reopenNewProjectPopup = false;
                    openProjectLoadPopup  = true;
                }

                if(ImGui::BeginMenu("Open Recent Project", !m_Settings.m_RecentProjects.empty()))
                {
                    for(auto& recentProject : m_Settings.m_RecentProjects)
                    {
                        if(ImGui::MenuItem(recentProject.c_str()))
                        {
                            Application::Get().OpenProject(recentProject);

                            for(int i = 0; i < int(m_Panels.size()); i++)
                            {
                                m_Panels[i]->OnNewProject();
                            }
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                if(ImGui::MenuItem("Open File"))
                {
                    m_FileBrowserPanel.SetCurrentPath(m_ProjectSettings.m_ProjectRoot);
                    m_FileBrowserPanel.SetCallback(BIND_FILEBROWSER_FN(Editor::FileOpenCallback));
                    m_FileBrowserPanel.Open();
                }

                ImGui::Separator();

                if(ImGui::MenuItem("New Scene", "CTRL+N"))
                {
                    openNewScenePopup = true;
                }

                if(ImGui::MenuItem("Save Scene", "CTRL+S"))
                {
                    openSaveScenePopup = true;
                }

                if(ImGui::MenuItem("Reload Scene", "CTRL+R"))
                {
                    openReloadScenePopup = true;
                }

                ImGui::Separator();

                if(ImGui::BeginMenu("Style"))
                {
                    if(ImGui::MenuItem("Dark", "", m_Settings.m_Theme == ImGuiUtilities::Dark))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Dark;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Dark);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Dracula", "", m_Settings.m_Theme == ImGuiUtilities::Dracula))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Dracula;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Dracula);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Black", "", m_Settings.m_Theme == ImGuiUtilities::Black))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Black;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Black);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Grey", "", m_Settings.m_Theme == ImGuiUtilities::Grey))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Grey;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Grey);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Light", "", m_Settings.m_Theme == ImGuiUtilities::Light))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Light;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Light);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Cherry", "", m_Settings.m_Theme == ImGuiUtilities::Cherry))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Cherry;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Cherry);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Blue", "", m_Settings.m_Theme == ImGuiUtilities::Blue))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Blue;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Blue);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Cinder", "", m_Settings.m_Theme == ImGuiUtilities::Cinder))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Cinder;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Cinder);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Classic", "", m_Settings.m_Theme == ImGuiUtilities::Classic))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Classic;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Classic);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("ClassicDark", "", m_Settings.m_Theme == ImGuiUtilities::ClassicDark))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::ClassicDark;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::ClassicDark);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("ClassicLight", "", m_Settings.m_Theme == ImGuiUtilities::ClassicLight))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::ClassicLight;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::ClassicLight);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    if(ImGui::MenuItem("Cosy", "", m_Settings.m_Theme == ImGuiUtilities::Cosy))
                    {
                        m_Settings.m_Theme = ImGuiUtilities::Cosy;
                        ImGuiUtilities::SetTheme(ImGuiUtilities::Cosy);
                        OS::Instance()->SetTitleBarColour(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                if(ImGui::MenuItem("Exit"))
                {
                    Application::Get().SetAppState(AppState::Closing);
                }

                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Edit"))
            {
                // TODO
                //  if(ImGui::MenuItem("Undo", "CTRL+Z"))
                //  {
                //  }
                //  if(ImGui::MenuItem("Redo", "CTRL+Y", false, false))
                //  {
                //  } // Disabled item
                //  ImGui::Separator();

                bool enabled = !m_SelectedEntities.empty();

                if(ImGui::MenuItem("Cut", "CTRL+X", false, enabled))
                {
                    for(auto entity : m_SelectedEntities)
                        SetCopiedEntity(entity, true);
                }

                if(ImGui::MenuItem("Copy", "CTRL+C", false, enabled))
                {
                    for(auto entity : m_SelectedEntities)
                        SetCopiedEntity(entity, false);
                }

                enabled = !m_CopiedEntities.empty();

                if(ImGui::MenuItem("Paste", "CTRL+V", false, enabled))
                {
                    for(auto entity : m_CopiedEntities)
                    {
                        Application::Get().GetCurrentScene()->DuplicateEntity({ entity, Application::Get().GetCurrentScene() });
                        if(entity != entt::null)
                        {
                            /// if(entity == m_SelectedEntity)
                            ///  m_SelectedEntity = entt::null;
                            Entity(entity, Application::Get().GetCurrentScene()).Destroy();
                        }
                    }
                }

                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Panels"))
            {
                for(auto& panel : m_Panels)
                {
                    if(ImGui::MenuItem(panel->GetName().c_str(), "", &panel->Active(), true))
                    {
                        panel->SetActive(true);
                    }
                }

                if(ImGui::MenuItem("ImGui Demo", "", &m_Settings.m_ShowImGuiDemo, true))
                {
                    m_Settings.m_ShowImGuiDemo = true;
                }

                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Scenes"))
            {
                auto scenes = Application::Get().GetSceneManager()->GetSceneNames();

                for(size_t i = 0; i < scenes.size(); i++)
                {
                    auto name = scenes[i];
                    if(ImGui::MenuItem(name.c_str()))
                    {
                        Application::Get().GetSceneManager()->SwitchScene(name);
                    }
                }

                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Entity"))
            {
                auto scene = Application::Get().GetSceneManager()->GetCurrentScene();

                if(ImGui::MenuItem("CreateEmpty"))
                {
                    scene->CreateEntity();
                }

                if(ImGui::MenuItem("Cube"))
                {
                    auto entity = scene->CreateEntity("Cube");
                    entity.AddComponent<Graphics::ModelComponent>(Graphics::PrimitiveType::Cube);
                }

                if(ImGui::MenuItem("Sphere"))
                {
                    auto entity = scene->CreateEntity("Sphere");
                    entity.AddComponent<Graphics::ModelComponent>(Graphics::PrimitiveType::Sphere);
                }

                if(ImGui::MenuItem("Pyramid"))
                {
                    auto entity = scene->CreateEntity("Pyramid");
                    entity.AddComponent<Graphics::ModelComponent>(Graphics::PrimitiveType::Pyramid);
                }

                if(ImGui::MenuItem("Plane"))
                {
                    auto entity = scene->CreateEntity("Plane");
                    entity.AddComponent<Graphics::ModelComponent>(Graphics::PrimitiveType::Plane);
                }

                if(ImGui::MenuItem("Cylinder"))
                {
                    auto entity = scene->CreateEntity("Cylinder");
                    entity.AddComponent<Graphics::ModelComponent>(Graphics::PrimitiveType::Cylinder);
                }

                if(ImGui::MenuItem("Capsule"))
                {
                    auto entity = scene->CreateEntity("Capsule");
                    entity.AddComponent<Graphics::ModelComponent>(Graphics::PrimitiveType::Capsule);
                }

                if(ImGui::MenuItem("Terrain"))
                {
                    auto entity = scene->CreateEntity("Terrain");
                    entity.AddComponent<Graphics::ModelComponent>(Graphics::PrimitiveType::Terrain);
                }

                if(ImGui::MenuItem("Light Cube"))
                {
                    EntityFactory::AddLightCube(Application::Get().GetSceneManager()->GetCurrentScene(), glm::vec3(0.0f), glm::vec3(0.0f));
                }

                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Graphics"))
            {
                if(ImGui::MenuItem("Compile Shaders"))
                {
                    RecompileShaders();
                }
                if(ImGui::MenuItem("Embed Shaders"))
                {
                    std::string coreDataPath;
                    VFS::Get().ResolvePhysicalPath("//CoreShaders", coreDataPath, true);
                    auto shaderPath = std::filesystem::path(coreDataPath + "/CompiledSPV/");
                    int shaderCount = 0;
                    if(std::filesystem::is_directory(shaderPath))
                    {
                        for(auto entry : std::filesystem::directory_iterator(shaderPath))
                        {
                            auto extension = StringUtilities::GetFilePathExtension(entry.path().string());
                            if(extension == "spv")
                            {
                                EmbedShader(entry.path().string());
                                shaderCount++;
                            }
                        }
                    }
                    LUMOS_LOG_INFO("Embedded {0} shaders. Recompile to use", shaderCount);
                }
                if(ImGui::MenuItem("Embed File"))
                {
                    EmbedFile();
                }

                if(ImGui::BeginMenu("GPU Index"))
                {
                    uint32_t gpuCount = Graphics::Renderer::GetRenderer()->GetGPUCount();

                    if(gpuCount == 1)
                    {
                        ImGui::TextUnformatted("Default");
                        ImGuiUtilities::Tooltip("Only default GPU selectable");
                    }
                    else
                    {
                        int8_t currentDesiredIndex = Application::Get().GetProjectSettings().DesiredGPUIndex;
                        int8_t newIndex            = currentDesiredIndex;

                        if(ImGui::Selectable("Default", currentDesiredIndex == -1))
                        {
                            newIndex = -1;
                        }

                        for(uint32_t index = 0; index < gpuCount; index++)
                        {
                            if(ImGui::Selectable(std::to_string(index).c_str(), index == uint32_t(currentDesiredIndex)))
                            {
                                newIndex = index;
                            }
                        }

                        Application::Get().GetProjectSettings().DesiredGPUIndex = newIndex;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("About"))
            {
                auto& version = Lumos::LumosVersion;
                ImGui::Text("Version : %d.%d.%d", version.major, version.minor, version.patch);
                ImGui::Separator();

                std::string githubMenuText = ICON_MDI_GITHUB_BOX " Github";
                if(ImGui::MenuItem(githubMenuText.c_str()))
                {
                    Lumos::OS::Instance()->OpenURL("https://www.github.com/jmorton06/Lumos");
                }
                ImGui::Separator();

                ImGui::TextUnformatted("Third-Party");
                if(ImGui::MenuItem(fmt::format("ImGui - Version : {0}, Revision - {1}", IMGUI_VERSION, IMGUI_VERSION_NUM).c_str()))
                    Lumos::OS::Instance()->OpenURL("https://github.com/ocornut/imgui");
                if(ImGui::MenuItem(fmt::format("Entt - Version  : {0}", ENTT_VERSION).c_str()))
                    Lumos::OS::Instance()->OpenURL("https://github.com/skypjack/entt");
                if(ImGui::MenuItem(fmt::format("Cereal - Version : {0}.{1}.{2}", CEREAL_VERSION_MAJOR, CEREAL_VERSION_MINOR, CEREAL_VERSION_PATCH).c_str()))
                    Lumos::OS::Instance()->OpenURL("https://github.com/USCiLab/cereal");

                ImGui::EndMenu();
            }

            if(m_ProjectLoaded)
            {
                {
                    ImGuiUtilities::ScopedFont boldFont(ImGui::GetIO().Fonts->Fonts[1]);
                    ImGuiUtilities::ScopedColour border(ImGuiCol_Border, IM_COL32(40, 40, 40, 255));

                    ImGui::SameLine(ImGui::GetCursorPosX() + 40.0f);
                    ImGui::Separator();
                    ImGui::SameLine();
                    ImGui::TextUnformatted(m_ProjectSettings.m_ProjectName.c_str());

                    ImGuiUtilities::Tooltip(("Current project (" + m_ProjectSettings.m_ProjectName + ".lmproj)").c_str());
                    ImGuiUtilities::DrawBorder(ImGuiUtilities::RectExpanded(ImGuiUtilities::GetItemRect(), 24.0f, 68.0f), 1.0f, 3.0f, 0.0f, -60.0f);

                    ImGui::SameLine();
                    ImGui::Separator();
                    ImGui::SameLine(ImGui::GetCursorPosX() + 32.0f);
                    ImGui::TextUnformatted(GetCurrentScene()->GetSceneName().c_str());
                    ImGuiUtilities::Tooltip(("Current Scene (" + GetCurrentScene()->GetSceneName() + ".lsn)").c_str());
                    ImGuiUtilities::DrawBorder(ImGuiUtilities::RectExpanded(ImGuiUtilities::GetItemRect(), 24.0f, 68.0f), 1.0f, 3.0f, 0.0f, -60.0f);
                }

#ifdef LUMOS_DEBUG
                {
                    ImGuiUtilities::ScopedFont boldFont(ImGui::GetIO().Fonts->Fonts[1]);
                    ImGuiUtilities::ScopedColour border(ImGuiCol_Text, IM_COL32(200, 40, 40, 255));
                    ImGui::SameLine(ImGui::GetCursorPosX() + 40.0f);
                    ImGui::TextUnformatted("DEBUG");
                }
#endif
            }

            ImGui::SameLine((ImGui::GetWindowContentRegionMax().x * 0.5f) - (1.5f * (ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.x)));

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.2f, 0.7f, 0.0f));

            if(Application::Get().GetEditorState() == EditorState::Next)
                Application::Get().SetEditorState(EditorState::Paused);

            bool selected;
            {
                selected = Application::Get().GetEditorState() == EditorState::Play;
                if(selected)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiUtilities::GetSelectedColour());

                if(ImGui::Button(ICON_MDI_PLAY))
                {
                    Application::Get().GetSystem<LumosPhysicsEngine>()->SetPaused(selected);
                    Application::Get().GetSystem<B2PhysicsEngine>()->SetPaused(selected);

                    Application::Get().GetSystem<AudioManager>()->UpdateListener(Application::Get().GetCurrentScene());
                    Application::Get().GetSystem<AudioManager>()->SetPaused(selected);
                    Application::Get().SetEditorState(selected ? EditorState::Preview : EditorState::Play);

                    m_SelectedEntities.clear();
                    // m_SelectedEntity = entt::null;
                    if(selected)
                    {
                        ImGui::SetWindowFocus("###scene");
                        LoadCachedScene();
                    }
                    else
                    {
                        ImGui::SetWindowFocus("###game");
                        CacheScene();
                        Application::Get().GetCurrentScene()->OnInit();
                    }
                }
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("Play");

                if(selected)
                    ImGui::PopStyleColor();
            }

            ImGui::SameLine();

            {
                selected = Application::Get().GetEditorState() == EditorState::Paused;
                if(selected)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiUtilities::GetSelectedColour());

                if(ImGui::Button(ICON_MDI_PAUSE))
                    Application::Get().SetEditorState(selected ? EditorState::Play : EditorState::Paused);

                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("Pause");

                if(selected)
                    ImGui::PopStyleColor();
            }

            ImGui::SameLine();

            {
                selected = Application::Get().GetEditorState() == EditorState::Next;
                if(selected)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGuiUtilities::GetSelectedColour());

                if(ImGui::Button(ICON_MDI_STEP_FORWARD))
                    Application::Get().SetEditorState(EditorState::Next);

                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("Next");

                if(selected)
                    ImGui::PopStyleColor();
            }

            static Engine::Stats stats = {};
            static double timer        = 1.1;
            timer += Engine::GetTimeStep().GetSeconds();

            if(timer > 1.0)
            {
                timer = 0.0;
                stats = Engine::Get().Statistics();
            }

            auto size                  = ImGui::CalcTextSize("%.2f ms (%.i FPS)");
            float sizeOfGfxAPIDropDown = ImGui::GetFontSize() * 8;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - size.x - ImGui::GetStyle().ItemSpacing.x * 2.0f - sizeOfGfxAPIDropDown);

            int fps = int(Maths::Round(1000.0 / stats.FrameTime));
            ImGui::Text("%.2f ms (%.i FPS)", stats.FrameTime, fps);

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetColorU32(ImGuiCol_TitleBg));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 2));

            bool setNewValue             = false;
            static std::string RenderAPI = "";

            auto renderAPI = (Graphics::RenderAPI)m_ProjectSettings.RenderAPI;

            bool needsRestart = false;
            if(renderAPI != Graphics::GraphicsContext::GetRenderAPI())
            {
                needsRestart = true;
            }

            switch(renderAPI)
            {
#ifdef LUMOS_RENDER_API_OPENGL
            case Graphics::RenderAPI::OPENGL:
                RenderAPI = "OpenGL";
                break;
#endif

#ifdef LUMOS_RENDER_API_VULKAN
            case Graphics::RenderAPI::VULKAN:
                RenderAPI = "Vulkan";
                break;
#endif

#ifdef LUMOS_RENDER_API_DIRECT3D
            case DIRECT3D:
                RenderAPI = "Direct3D";
                break;
#endif
            default:
                break;
            }

            int numSupported = 0;
#ifdef LUMOS_RENDER_API_OPENGL
            numSupported++;
#endif
#ifdef LUMOS_RENDER_API_VULKAN
            numSupported++;
#endif
#ifdef LUMOS_RENDER_API_DIRECT3D11
            numSupported++;
#endif
            const char* api[]       = { "OpenGL", "Vulkan", "Direct3D11" };
            const char* current_api = RenderAPI.c_str();
            if(needsRestart)
                RenderAPI = "*" + RenderAPI;

            ImGui::PushItemWidth(-1.0f);
            if(ImGui::BeginCombo(
                   "", current_api, 0)) // The second parameter is the label previewed before opening the combo.
            {
                for(int n = 0; n < numSupported; n++)
                {
                    bool is_selected = (current_api == api[n]);
                    if(ImGui::Selectable(api[n], current_api))
                    {
                        setNewValue = true;
                        current_api = api[n];
                    }
                    if(is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if(needsRestart)
                ImGuiUtilities::Tooltip("Restart needed to switch Render API");

            if(setNewValue)
            {
                m_ProjectSettings.RenderAPI = int(StringToRenderAPI(current_api));
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();

            ImGui::EndMainMenuBar();
        }

        if(openSaveScenePopup)
            ImGui::OpenPopup("Save Scene");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if(ImGui::BeginPopupModal("Save Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Save Current Scene Changes?\n\n");
            ImGui::Separator();

            if(ImGui::Button("OK", ImVec2(120, 0)))
            {
                Application::Get().GetSceneManager()->GetCurrentScene()->Serialise(m_ProjectSettings.m_ProjectRoot + "Assets/Scenes/", false);
                Graphics::Renderer::GetRenderer()->SaveScreenshot(m_ProjectSettings.m_ProjectRoot + "Assets/Scenes/Cache/" + Application::Get().GetSceneManager()->GetCurrentScene()->GetSceneName() + ".png", m_RenderPasses->GetForwardData().m_RenderTexture);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if(locationPopupOpened)
        {
            // Cancel clicked on project location popups
            if(!m_FileBrowserPanel.IsOpen())
            {
                m_NewProjectPopupOpen = false;
                locationPopupOpened   = false;
                reopenNewProjectPopup = true;
            }
        }
        if(openNewScenePopup)
            ImGui::OpenPopup("New Scene");

        if((reopenNewProjectPopup || openProjectLoadPopup) && !m_NewProjectPopupOpen)
        {
            ImGui::OpenPopup("Open Project");
            reopenNewProjectPopup = false;
        }

        if(ImGui::BeginPopupModal("New Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if(ImGui::Button("Save Current Scene Changes"))
            {
                Application::Get().GetSceneManager()->GetCurrentScene()->Serialise(m_ProjectSettings.m_ProjectRoot + "Assets/Scenes/", false);
            }

            ImGui::Text("Create New Scene?\n\n");
            ImGui::Separator();

            static bool defaultSetup = false;

            static std::string newSceneName = "NewScene";
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Name : ");
            ImGui::SameLine();
            ImGuiUtilities::InputText(newSceneName);

            ImGui::Checkbox("Default Setup", &defaultSetup);

            ImGui::Separator();

            if(ImGui::Button("OK", ImVec2(120, 0)))
            {
                std::string sceneName = newSceneName;
                int sameNameCount     = 0;
                auto sceneNames       = m_SceneManager->GetSceneNames();

                while(FileSystem::FileExists("//Scenes/" + sceneName + ".lsn") || std::find(sceneNames.begin(), sceneNames.end(), sceneName) != sceneNames.end())
                {
                    sameNameCount++;
                    sceneName = fmt::format(newSceneName + "{0}", sameNameCount);
                }
                auto scene = new Scene(sceneName);

                if(defaultSetup)
                {
                    auto light          = scene->GetEntityManager()->Create("Light");
                    auto lightComp      = light.AddComponent<Graphics::Light>();
                    glm::mat4 lightView = glm::inverse(glm::lookAt(glm::vec3(30.0f, 9.0f, 50.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
                    light.GetTransform().SetLocalTransform(lightView);

                    auto camera = scene->GetEntityManager()->Create("Camera");
                    camera.AddComponent<Camera>();
                    glm::mat4 viewMat = glm::inverse(glm::lookAt(glm::vec3(-2.3f, 1.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
                    camera.GetTransform().SetLocalTransform(viewMat);
                    camera.AddComponent<DefaultCameraController>(DefaultCameraController::ControllerType::EditorCamera);

                    auto cube = scene->GetEntityManager()->Create("Cube");
                    cube.AddComponent<Graphics::ModelComponent>(Graphics::PrimitiveType::Cube);

                    auto environment = scene->GetEntityManager()->Create("Environment");
                    environment.AddComponent<Graphics::Environment>();
                    environment.GetComponent<Graphics::Environment>().Load();

                    scene->Serialise(m_ProjectSettings.m_ProjectRoot + "Assets/Scenes/");
                }
                Application::Get().GetSceneManager()->EnqueueScene(scene);
                Application::Get().GetSceneManager()->SwitchScene((int)(Application::Get().GetSceneManager()->GetScenes().size()) - 1);

                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if(ImGui::BeginPopupModal("Open Project", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if(ImGui::Button("Load Project"))
            {
                ImGui::CloseCurrentPopup();

                m_NewProjectPopupOpen = true;
                locationPopupOpened   = true;

                // Set filePath to working directory
                const auto& path  = OS::Instance()->GetExecutablePath();
                auto& browserPath = m_FileBrowserPanel.GetPath();
                browserPath       = std::filesystem::path(path);
                m_FileBrowserPanel.SetFileTypeFilters({ ".lmproj" });
                m_FileBrowserPanel.SetOpenDirectory(false);
                m_FileBrowserPanel.SetCallback(BIND_FILEBROWSER_FN(ProjectOpenCallback));
                m_FileBrowserPanel.Open();
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Create New Project?\n");

            static std::string newProjectName = "New Project";
            ImGuiUtilities::InputText(newProjectName);

            if(ImGui::Button(ICON_MDI_FOLDER))
            {
                ImGui::CloseCurrentPopup();

                m_NewProjectPopupOpen = true;
                locationPopupOpened   = true;

                // Set filePath to working directory
                const auto& path  = OS::Instance()->GetExecutablePath();
                auto& browserPath = m_FileBrowserPanel.GetPath();
                browserPath       = std::filesystem::path(path);
                m_FileBrowserPanel.ClearFileTypeFilters();
                m_FileBrowserPanel.SetOpenDirectory(true);
                m_FileBrowserPanel.SetCallback(BIND_FILEBROWSER_FN(NewProjectLocationCallback));
                m_FileBrowserPanel.Open();
            }

            ImGui::SameLine();

            ImGui::TextUnformatted(projectLocation.c_str());

            ImGui::Separator();

            if(ImGui::Button("Create", ImVec2(120, 0)))
            {
                Application::Get().OpenNewProject(projectLocation, newProjectName);
                m_FileBrowserPanel.SetOpenDirectory(false);

                for(int i = 0; i < int(m_Panels.size()); i++)
                {
                    m_Panels[i]->OnNewProject();
                }

                ImGui::CloseCurrentPopup();
            }

            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Exit", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                SetAppState(AppState::Closing);
            }

            if(Application::Get().m_ProjectLoaded)
            {
                ImGui::SameLine();

                if(ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        if(openReloadScenePopup)
            ImGui::OpenPopup("Reload Scene");

        if(ImGui::BeginPopupModal("Reload Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Reload Scene?\n\n");
            ImGui::Separator();

            if(ImGui::Button("OK", ImVec2(120, 0)))
            {
                auto scene = new Scene("New Scene");
                Application::Get().GetSceneManager()->SwitchScene(Application::Get().GetSceneManager()->GetCurrentSceneIndex());

                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    static const float identityMatrix[16] = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };

    void Editor::OnImGuizmo()
    {
        LUMOS_PROFILE_FUNCTION();
        glm::mat4 view = glm::inverse(m_EditorCameraTransform.GetWorldMatrix());
        glm::mat4 proj = m_CurrentCamera->GetProjectionMatrix();

#ifdef USE_IMGUIZMO_GRID
        if(m_Settings.m_ShowGrid && !m_CurrentCamera->IsOrthographic())
            ImGuizmo::DrawGrid(glm::value_ptr(view),
                               glm::value_ptr(proj), identityMatrix, 120.f);
#endif

        if(!m_Settings.m_ShowGizmos || m_SelectedEntities.empty() || m_ImGuizmoOperation == 4)
            return;

        auto& registry = Application::Get().GetSceneManager()->GetCurrentScene()->GetRegistry();

        if(m_SelectedEntities.size() == 1)
        {
            entt::entity m_SelectedEntity = entt::null;

            m_SelectedEntity = m_SelectedEntities.front();
            if(registry.valid(m_SelectedEntity))
            {
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetOrthographic(m_CurrentCamera->IsOrthographic());

                auto transform = registry.try_get<Maths::Transform>(m_SelectedEntity);
                if(transform != nullptr)
                {
                    glm::mat4 model = transform->GetWorldMatrix();

                    float snapAmount[3] = { m_Settings.m_SnapAmount, m_Settings.m_SnapAmount, m_Settings.m_SnapAmount };
                    float delta[16];

                    ImGuizmo::Manipulate(glm::value_ptr(view),
                                         glm::value_ptr(proj),
                                         static_cast<ImGuizmo::OPERATION>(m_ImGuizmoOperation),
                                         ImGuizmo::LOCAL,
                                         glm::value_ptr(model),
                                         delta,
                                         m_Settings.m_SnapQuizmo ? snapAmount : nullptr);

                    if(ImGuizmo::IsUsing())
                    {
                        Entity parent = Entity(m_SelectedEntity, m_SceneManager->GetCurrentScene()).GetParent(); // m_CurrentScene->TryGetEntityWithUUID(entity.GetParentUUID());
                        if(parent && parent.HasComponent<Maths::Transform>())
                        {
                            glm::mat4 parentTransform = parent.GetTransform().GetWorldMatrix();
                            model                     = glm::inverse(parentTransform) * model;
                        }

                        if(ImGuizmo::IsScaleType()) // static_cast<ImGuizmo::OPERATION>(m_ImGuizmoOperation) & ImGuizmo::OPERATION::SCALE)
                        {
                            transform->SetLocalScale(Lumos::Maths::GetScale(model));
                        }
                        else
                        {
                            transform->SetLocalTransform(model);

                            RigidBody2DComponent* rigidBody2DComponent = registry.try_get<Lumos::RigidBody2DComponent>(m_SelectedEntity);

                            if(rigidBody2DComponent)
                            {
                                rigidBody2DComponent->GetRigidBody()->SetPosition(
                                    { model[3].x, model[3].y });
                            }
                            else
                            {
                                Lumos::RigidBody3DComponent* rigidBody3DComponent = registry.try_get<Lumos::RigidBody3DComponent>(m_SelectedEntity);
                                if(rigidBody3DComponent)
                                {
                                    rigidBody3DComponent->GetRigidBody()->SetPosition(model[3]);
                                    rigidBody3DComponent->GetRigidBody()->SetOrientation(Maths::GetRotation(model));
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            glm::vec3 medianPointLocation = glm::vec3(0.0f);
            glm::vec3 medianPointScale    = glm::vec3(0.0f);
            int validcount                = 0;
            for(auto entityID : m_SelectedEntities)
            {
                if(!registry.valid(entityID))
                    continue;

                Entity entity = { entityID, m_SceneManager->GetCurrentScene() };

                if(!entity.HasComponent<Maths::Transform>())
                    continue;

                medianPointLocation += entity.GetTransform().GetWorldPosition();
                medianPointScale += entity.GetTransform().GetLocalScale();
                validcount++;
            }
            medianPointLocation /= validcount; // m_SelectedEntities.size();
            medianPointScale /= validcount;    // m_SelectedEntities.size();

            glm::mat4 medianPointMatrix = glm::translate(glm::mat4(1.0f), medianPointLocation) * glm::scale(glm::mat4(1.0f), medianPointScale);

            ImGuizmo::SetDrawlist();
            ImGuizmo::SetOrthographic(m_CurrentCamera->IsOrthographic());

            float snapAmount[3]   = { m_Settings.m_SnapAmount, m_Settings.m_SnapAmount, m_Settings.m_SnapAmount };
            glm::mat4 deltaMatrix = glm::mat4(1.0f);

            ImGuizmo::Manipulate(glm::value_ptr(view),
                                 glm::value_ptr(proj),
                                 static_cast<ImGuizmo::OPERATION>(m_ImGuizmoOperation),
                                 ImGuizmo::LOCAL,
                                 glm::value_ptr(medianPointMatrix),
                                 glm::value_ptr(deltaMatrix),
                                 m_Settings.m_SnapQuizmo ? snapAmount : nullptr);

            if(ImGuizmo::IsUsing())
            {
                glm::vec3 deltaTranslation, deltaScale;
                glm::quat deltaRotation;
                glm::vec3 skew;
                glm::vec4 perspective;
                glm::decompose(deltaMatrix, deltaScale, deltaRotation, deltaTranslation, skew, perspective);

                //                    if (parent && parent.HasComponent<Maths::Transform>())
                //                    {
                //                        glm::mat4 parentTransform = parent.GetTransform().GetWorldMatrix();
                //                        model = glm::inverse(parentTransform) * model;
                //                    }
                //

                static const bool MedianPointOrigin = false;

                if(MedianPointOrigin)
                {
                    for(auto entityID : m_SelectedEntities)
                    {
                        if(!registry.valid(entityID))
                            continue;
                        auto transform = registry.try_get<Maths::Transform>(entityID);

                        if(!transform)
                            continue;
                        if(ImGuizmo::IsScaleType()) // static_cast<ImGuizmo::OPERATION>(m_ImGuizmoOperation) == ImGuizmo::OPERATION::SCALE)
                        {
                            transform->SetLocalScale(transform->GetLocalScale() * deltaScale);
                            // transform->SetLocalTransform(deltaMatrix * transform->GetLocalMatrix());
                        }
                        else
                        {
                            transform->SetLocalTransform(deltaMatrix * transform->GetLocalMatrix());

                            // World matrix wont have updated yet so need to multiply by delta
                            // TODO: refactor
                            auto worldMatrix = deltaMatrix * transform->GetWorldMatrix();

                            RigidBody2DComponent* rigidBody2DComponent = registry.try_get<Lumos::RigidBody2DComponent>(entityID);

                            if(rigidBody2DComponent)
                            {
                                rigidBody2DComponent->GetRigidBody()->SetPosition({ worldMatrix[3].x, worldMatrix[3].y });
                            }
                            else
                            {
                                Lumos::RigidBody3DComponent* rigidBody3DComponent = registry.try_get<Lumos::RigidBody3DComponent>(entityID);
                                if(rigidBody3DComponent)
                                {
                                    rigidBody3DComponent->GetRigidBody()->SetPosition(worldMatrix[3]);
                                    rigidBody3DComponent->GetRigidBody()->SetOrientation(Maths::GetRotation(worldMatrix));
                                }
                            }
                        }
                    }
                }
                else
                {
                    for(auto entityID : m_SelectedEntities)
                    {
                        if(!registry.valid(entityID))
                            continue;
                        auto transform = registry.try_get<Maths::Transform>(entityID);

                        if(!transform)
                            continue;
                        if(ImGuizmo::IsScaleType()) // static_cast<ImGuizmo::OPERATION>(m_ImGuizmoOperation) & ImGuizmo::OPERATION::SCALE)
                        {
                            transform->SetLocalScale(transform->GetLocalScale() * deltaScale);
                            // transform->SetLocalTransform(deltaMatrix * transform->GetLocalMatrix());
                        }
                        else if(ImGuizmo::IsRotateType()) // static_cast<ImGuizmo::OPERATION>(m_ImGuizmoOperation) & ImGuizmo::OPERATION::ROTATE)
                        {
                            transform->SetLocalOrientation(glm::quat(glm::eulerAngles(transform->GetLocalOrientation()) + glm::eulerAngles(deltaRotation)));
                        }
                        else
                        {
                            transform->SetLocalTransform(deltaMatrix * transform->GetLocalMatrix());

                            // World matrix wont have updated yet so need to multiply by delta
                            // TODO: refactor
                            auto worldMatrix = deltaMatrix * transform->GetWorldMatrix();

                            RigidBody2DComponent* rigidBody2DComponent = registry.try_get<Lumos::RigidBody2DComponent>(entityID);

                            if(rigidBody2DComponent)
                            {
                                rigidBody2DComponent->GetRigidBody()->SetPosition({ worldMatrix[3].x, worldMatrix[3].y });
                            }
                            else
                            {
                                Lumos::RigidBody3DComponent* rigidBody3DComponent = registry.try_get<Lumos::RigidBody3DComponent>(entityID);
                                if(rigidBody3DComponent)
                                {
                                    rigidBody3DComponent->GetRigidBody()->SetPosition(worldMatrix[3]);
                                    rigidBody3DComponent->GetRigidBody()->SetOrientation(Maths::GetRotation(worldMatrix));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    void Editor::BeginDockSpace(bool gameFullScreen)
    {
        LUMOS_PROFILE_FUNCTION();
        static bool p_open                    = true;
        static bool opt_fullscreen_persistant = true;
        static ImGuiDockNodeFlags opt_flags   = ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;
        bool opt_fullscreen                   = opt_fullscreen_persistant;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
        if(opt_fullscreen)
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();

            auto pos     = viewport->Pos;
            auto size    = viewport->Size;
            bool menuBar = true;
            if(menuBar)
            {
                const float infoBarSize = ImGui::GetFrameHeight();
                pos.y += infoBarSize;
                size.y -= infoBarSize;
            }

            ImGui::SetNextWindowPos(pos);
            ImGui::SetNextWindowSize(size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        // When using ImGuiDockNodeFlags_PassthruDockspace, DockSpace() will render our background and handle the
        // pass-thru hole, so we ask Begin() to not render a background.
        if(opt_flags & ImGuiDockNodeFlags_DockSpace)
            window_flags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("MyDockspace", &p_open, window_flags);
        ImGui::PopStyleVar();

        if(opt_fullscreen)
            ImGui::PopStyleVar(2);

        ImGuiID DockspaceID = ImGui::GetID("MyDockspace");

        static std::vector<SharedPtr<EditorPanel>> hiddenPanels;
        if(m_Settings.m_FullScreenSceneView != gameFullScreen)
        {
            m_Settings.m_FullScreenSceneView = gameFullScreen;

            if(m_Settings.m_FullScreenSceneView)
            {
                for(auto panel : m_Panels)
                {
                    if(panel->GetSimpleName() != "Game" && panel->Active())
                    {
                        panel->SetActive(false);
                        hiddenPanels.push_back(panel);
                    }
                }
            }
            else
            {
                for(auto panel : hiddenPanels)
                {
                    panel->SetActive(true);
                }

                hiddenPanels.clear();
            }
        }

        if(!ImGui::DockBuilderGetNode(DockspaceID))
        {
            ImGui::DockBuilderRemoveNode(DockspaceID); // Clear out existing layout
            ImGui::DockBuilderAddNode(DockspaceID);    // Add empty node
            ImGui::DockBuilderSetNodeSize(DockspaceID, ImGui::GetIO().DisplaySize * ImGui::GetIO().DisplayFramebufferScale);

            ImGuiID dock_main_id = DockspaceID;
            ImGuiID DockBottom   = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.3f, nullptr, &dock_main_id);
            ImGuiID DockLeft     = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
            ImGuiID DockRight    = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);

            ImGuiID DockLeftChild         = ImGui::DockBuilderSplitNode(DockLeft, ImGuiDir_Down, 0.875f, nullptr, &DockLeft);
            ImGuiID DockRightChild        = ImGui::DockBuilderSplitNode(DockRight, ImGuiDir_Down, 0.875f, nullptr, &DockRight);
            ImGuiID DockingLeftDownChild  = ImGui::DockBuilderSplitNode(DockLeftChild, ImGuiDir_Down, 0.06f, nullptr, &DockLeftChild);
            ImGuiID DockingRightDownChild = ImGui::DockBuilderSplitNode(DockRightChild, ImGuiDir_Down, 0.06f, nullptr, &DockRightChild);

            ImGuiID DockBottomChild         = ImGui::DockBuilderSplitNode(DockBottom, ImGuiDir_Down, 0.2f, nullptr, &DockBottom);
            ImGuiID DockingBottomLeftChild  = ImGui::DockBuilderSplitNode(DockLeft, ImGuiDir_Down, 0.4f, nullptr, &DockLeft);
            ImGuiID DockingBottomRightChild = ImGui::DockBuilderSplitNode(DockRight, ImGuiDir_Down, 0.4f, nullptr, &DockRight);

            ImGuiID DockMiddle       = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.8f, nullptr, &dock_main_id);
            ImGuiID DockBottomMiddle = ImGui::DockBuilderSplitNode(DockMiddle, ImGuiDir_Down, 0.3f, nullptr, &DockMiddle);
            ImGuiID DockMiddleLeft   = ImGui::DockBuilderSplitNode(DockMiddle, ImGuiDir_Left, 0.5f, nullptr, &DockMiddle);
            ImGuiID DockMiddleRight  = ImGui::DockBuilderSplitNode(DockMiddle, ImGuiDir_Right, 0.5f, nullptr, &DockMiddle);

            ImGui::DockBuilderDockWindow("###game", DockMiddleRight);
            ImGui::DockBuilderDockWindow("###scene", DockMiddleLeft);
            ImGui::DockBuilderDockWindow("###inspector", DockRight);
            ImGui::DockBuilderDockWindow("###console", DockBottomMiddle);
            ImGui::DockBuilderDockWindow("###profiler", DockingBottomLeftChild);
            ImGui::DockBuilderDockWindow("###resources", DockingBottomLeftChild);
            ImGui::DockBuilderDockWindow("Dear ImGui Demo", DockLeft);
            ImGui::DockBuilderDockWindow("GraphicsInfo", DockLeft);
            ImGui::DockBuilderDockWindow("ApplicationInfo", DockLeft);
            ImGui::DockBuilderDockWindow("###hierarchy", DockLeft);
            ImGui::DockBuilderDockWindow("###textEdit", DockMiddleLeft);
            ImGui::DockBuilderDockWindow("###scenesettings", DockLeft);
            ImGui::DockBuilderDockWindow("###editorsettings", DockLeft);
            ImGui::DockBuilderDockWindow("###projectsettings", DockLeft);

            ImGui::DockBuilderFinish(DockspaceID);
        }

        // Dockspace
        ImGuiIO& io = ImGui::GetIO();
        if(io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGui::DockSpace(DockspaceID, ImVec2(0.0f, 0.0f), opt_flags);
        }
    }

    void Editor::EndDockSpace()
    {
        ImGui::End();
    }

    void Editor::OnNewScene(Scene* scene)
    {
        LUMOS_PROFILE_FUNCTION();
        Application::OnNewScene(scene);
        // m_SelectedEntity = entt::null;
        m_SelectedEntities.clear();
        glm::mat4 viewMat = glm::inverse(glm::lookAt(glm::vec3(-31.0f, 12.0f, 51.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        m_EditorCameraTransform.SetLocalTransform(viewMat);

        for(auto panel : m_Panels)
        {
            panel->OnNewScene(scene);
        }

        std::string Configuration;
        std::string Platform;
        std::string RenderAPI;
        std::string dash = " - ";

#ifdef LUMOS_DEBUG
        Configuration = "Debug";
#elif LUMOS_RELEASE
        Configuration = "Release";
#elif LUMOS_PRODUCTION
        Configuration = "Production";
#endif

#ifdef LUMOS_PLATFORM_WINDOWS
        Platform = "Windows";
#elif LUMOS_PLATFORM_LINUX
        Platform      = "Linux";
#elif LUMOS_PLATFORM_MACOS
        Platform      = "MacOS";
#elif LUMOS_PLATFORM_IOS
        Platform = "iOS";
#endif

        switch(Graphics::GraphicsContext::GetRenderAPI())
        {
#ifdef LUMOS_RENDER_API_OPENGL
        case Graphics::RenderAPI::OPENGL:
            RenderAPI = "OpenGL";
            break;
#endif

#ifdef LUMOS_RENDER_API_VULKAN
#if defined(LUMOS_PLATFORM_MACOS) || defined(LUMOS_PLATFORM_IOS)
        case Graphics::RenderAPI::VULKAN:
            RenderAPI = "Vulkan ( MoltenVK )";
            break;
#else
        case Graphics::RenderAPI::VULKAN:
            RenderAPI = "Vulkan";
            break;
#endif
#endif

#ifdef LUMOS_RENDER_API_DIRECT3D
        case DIRECT3D:
            RenderAPI = "Direct3D";
            break;
#endif
        default:
            break;
        }

        std::stringstream Title;
        Title << Platform << dash << RenderAPI << dash << Configuration << dash << scene->GetSceneName() << dash << Application::Get().GetWindow()->GetTitle();

        Application::Get().GetWindow()->SetWindowTitle(Title.str());
    }

    void Editor::Draw3DGrid()
    {
        LUMOS_PROFILE_FUNCTION();
#if 1
        if(!m_GridRenderer || !Application::Get().GetSceneManager()->GetCurrentScene())
        {
            return;
        }

        DebugRenderer::DrawHairLine(glm::vec3(-5000.0f, 0.0f, 0.0f), glm::vec3(5000.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        DebugRenderer::DrawHairLine(glm::vec3(0.0f, -5000.0f, 0.0f), glm::vec3(0.0f, 5000.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
        DebugRenderer::DrawHairLine(glm::vec3(0.0f, 0.0f, -5000.0f), glm::vec3(0.0f, 0.0f, 5000.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

        m_GridRenderer->OnImGui();

        m_GridRenderer->BeginScene(Application::Get().GetSceneManager()->GetCurrentScene(), m_EditorCamera.get(), &m_EditorCameraTransform);
        m_GridRenderer->RenderScene();
#endif
    }

    void Editor::Draw2DGrid(ImDrawList* drawList,
                            const ImVec2& cameraPos,
                            const ImVec2& windowPos,
                            const ImVec2& canvasSize,
                            const float factor,
                            const float thickness)
    {
        LUMOS_PROFILE_FUNCTION();
        static const auto graduation = 10;
        float GRID_SZ                = canvasSize.y * 0.5f / factor;
        const ImVec2& offset         = {
            canvasSize.x * 0.5f - cameraPos.x * GRID_SZ, canvasSize.y * 0.5f + cameraPos.y * GRID_SZ
        };

        ImU32 GRID_COLOR    = IM_COL32(200, 200, 200, 40);
        float gridThickness = 1.0f;

        const auto& gridColor      = GRID_COLOR;
        auto smallGraduation       = GRID_SZ / graduation;
        const auto& smallGridColor = IM_COL32(100, 100, 100, smallGraduation);

        for(float x = -GRID_SZ; x < canvasSize.x + GRID_SZ; x += GRID_SZ)
        {
            auto localX = floorf(x + fmodf(offset.x, GRID_SZ));
            drawList->AddLine(
                ImVec2 { localX, 0.0f } + windowPos, ImVec2 { localX, canvasSize.y } + windowPos, gridColor, gridThickness);

            if(smallGraduation > 5.0f)
            {
                for(int i = 1; i < graduation; ++i)
                {
                    const auto graduation = floorf(localX + smallGraduation * i);
                    drawList->AddLine(ImVec2 { graduation, 0.0f } + windowPos,
                                      ImVec2 { graduation, canvasSize.y } + windowPos,
                                      smallGridColor,
                                      1.0f);
                }
            }
        }

        for(float y = -GRID_SZ; y < canvasSize.y + GRID_SZ; y += GRID_SZ)
        {
            auto localY = floorf(y + fmodf(offset.y, GRID_SZ));
            drawList->AddLine(
                ImVec2 { 0.0f, localY } + windowPos, ImVec2 { canvasSize.x, localY } + windowPos, gridColor, gridThickness);

            if(smallGraduation > 5.0f)
            {
                for(int i = 1; i < graduation; ++i)
                {
                    const auto graduation = floorf(localY + smallGraduation * i);
                    drawList->AddLine(ImVec2 { 0.0f, graduation } + windowPos,
                                      ImVec2 { canvasSize.x, graduation } + windowPos,
                                      smallGridColor,
                                      1.0f);
                }
            }
        }
    }

    bool Editor::OnFileDrop(WindowFileEvent& e)
    {
        FileOpenCallback(e.GetFilePath());
        return true;
    }

    void Editor::OnEvent(Event& e)
    {
        LUMOS_PROFILE_FUNCTION();
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowFileEvent>(BIND_EVENT_FN(Editor::OnFileDrop));
        // Block events here

        Application::OnEvent(e);
    }

    Maths::Ray Editor::GetScreenRay(int x, int y, Camera* camera, int width, int height)
    {
        LUMOS_PROFILE_FUNCTION();
        if(!camera)
            return Maths::Ray();

        float screenX = (float)x / (float)width;
        float screenY = (float)y / (float)height;

        bool flipY = true;

#ifdef LUMOS_RENDER_API_OPENGL
        if(Graphics::GraphicsContext::GetRenderAPI() == Graphics::RenderAPI::OPENGL)
            flipY = true;
#endif
        return camera->GetScreenRay(screenX, screenY, glm::inverse(m_EditorCameraTransform.GetWorldMatrix()), flipY);
    }

    void Editor::OnUpdate(const TimeStep& ts)
    {
        LUMOS_PROFILE_FUNCTION();

        static float autoSaveTimer = 0.0f;
        if(m_AutoSaveSettingsTime > 0)
        {
            if(autoSaveTimer > m_AutoSaveSettingsTime)
            {
                SaveEditorSettings();
                autoSaveTimer = 0;
            }

            autoSaveTimer += (float)ts.GetMillis();
        }

        if(m_EditorState == EditorState::Play)
            autoSaveTimer = 0.0f;

        if(Input::Get().GetKeyPressed(Lumos::InputCode::Key::Escape))
        {
            if(GetEditorState() == EditorState::Preview)
            {
                m_CurrentState = AppState::Closing;
                return;
            }
            else
            {
                Application::Get().GetSystem<LumosPhysicsEngine>()->SetPaused(true);
                Application::Get().GetSystem<B2PhysicsEngine>()->SetPaused(true);

                Application::Get().GetSystem<AudioManager>()->UpdateListener(Application::Get().GetCurrentScene());
                Application::Get().GetSystem<AudioManager>()->SetPaused(true);
                Application::Get().SetEditorState(EditorState::Preview);

                // m_SelectedEntity = entt::null;
                m_SelectedEntities.clear();
                ImGui::SetWindowFocus("###scene");
                LoadCachedScene();
                SetEditorState(EditorState::Preview);
            }
        }

        if(m_SceneViewActive)
        {
            auto& registry = Application::Get().GetSceneManager()->GetCurrentScene()->GetRegistry();

            // if(Application::Get().GetSceneActive())
            {
                const glm::vec2 mousePos = Input::Get().GetMousePosition();
                m_EditorCameraController.SetCamera(m_EditorCamera);
                m_EditorCameraController.HandleMouse(m_EditorCameraTransform, (float)ts.GetSeconds(), mousePos.x, mousePos.y);
                m_EditorCameraController.HandleKeyboard(m_EditorCameraTransform, (float)ts.GetSeconds());
                m_EditorCameraTransform.SetWorldMatrix(glm::mat4(1.0f));

                if(!m_SelectedEntities.empty() && Input::Get().GetKeyPressed(InputCode::Key::F))
                {
                    if(registry.valid(m_SelectedEntities.front()))
                    {
                        auto transform = registry.try_get<Maths::Transform>(m_SelectedEntities.front());
                        if(transform)
                            FocusCamera(transform->GetWorldPosition(), 2.0f, 2.0f);
                    }
                }
            }

            if(Input::Get().GetKeyHeld(InputCode::Key::O))
            {
                FocusCamera(glm::vec3(0.0f, 0.0f, 0.0f), 2.0f, 2.0f);
            }

            if(m_TransitioningCamera)
            {
                if(m_CameraTransitionStartTime < 0.0f)
                    m_CameraTransitionStartTime = (float)ts.GetElapsedSeconds();

                float focusProgress         = Maths::Min(((float)ts.GetElapsedSeconds() - m_CameraTransitionStartTime) / m_CameraTransitionSpeed, 1.f);
                glm::vec3 newCameraPosition = glm::mix(m_CameraStartPosition, m_CameraDestination, focusProgress);
                m_EditorCameraTransform.SetLocalPosition(newCameraPosition);

                if(m_EditorCameraTransform.GetLocalPosition() == m_CameraDestination)
                    m_TransitioningCamera = false;
            }

            if(!Input::Get().GetMouseHeld(InputCode::MouseKey::ButtonRight) && !ImGuizmo::IsUsing())
            {
                if(Input::Get().GetKeyPressed(InputCode::Key::Q))
                {
                    SetImGuizmoOperation(ImGuizmo::OPERATION::BOUNDS);
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::W))
                {
                    SetImGuizmoOperation(ImGuizmo::OPERATION::TRANSLATE);
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::E))
                {
                    SetImGuizmoOperation(ImGuizmo::OPERATION::ROTATE);
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::R))
                {
                    SetImGuizmoOperation(ImGuizmo::OPERATION::SCALE);
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::T))
                {
                    SetImGuizmoOperation(ImGuizmo::OPERATION::UNIVERSAL);
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::Y))
                {
                    ToggleSnap();
                }
            }

            if((Input::Get().GetKeyHeld(InputCode::Key::LeftSuper) || (Input::Get().GetKeyHeld(InputCode::Key::LeftControl))))
            {
                if(Input::Get().GetKeyPressed(InputCode::Key::S) && Application::Get().GetSceneActive())
                {
                    Application::Get().GetSceneManager()->GetCurrentScene()->Serialise(m_ProjectSettings.m_ProjectRoot + "Assets/scenes/", false);
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::O))
                    Application::Get().GetSceneManager()->GetCurrentScene()->Deserialise(m_ProjectSettings.m_ProjectRoot + "Assets/scenes/", false);

                if(Input::Get().GetKeyPressed(InputCode::Key::X))
                {
                    for(auto entity : m_SelectedEntities)
                        SetCopiedEntity(entity, true);
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::C))
                {
                    for(auto entity : m_SelectedEntities)
                        SetCopiedEntity(entity, false);
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::V) && !m_CopiedEntities.empty())
                {
                    for(auto entity : m_CopiedEntities)
                    {
                        Application::Get().GetCurrentScene()->DuplicateEntity({ entity, Application::Get().GetCurrentScene() });
                        if(entity != entt::null)
                        {
                            // if(m_CopiedEntity == m_SelectedEntity)
                            //   m_SelectedEntity = entt::null;
                            Entity(entity, Application::Get().GetCurrentScene()).Destroy();
                        }
                    }
                }

                if(Input::Get().GetKeyPressed(InputCode::Key::D) && !m_SelectedEntities.empty())
                {
                    for(auto entity : m_CopiedEntities)
                        Application::Get().GetCurrentScene()->DuplicateEntity({ entity, Application::Get().GetCurrentScene() });
                }
            }
        }
        else
            m_EditorCameraController.StopMovement();

        Application::OnUpdate(ts);
    }

    void Editor::SetSelected(entt::entity entity)
    {
        auto& registry = Application::Get().GetSceneManager()->GetCurrentScene()->GetRegistry();

        if(!registry.valid(entity))
            return;
        if(std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end())
            return;

        m_SelectedEntities.push_back(entity);
    }

    void Editor::UnSelect(entt::entity entity)
    {
        auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);

        if(it != m_SelectedEntities.end())
        {
            m_SelectedEntities.erase(it);
        }
    }

    void Editor::FocusCamera(const glm::vec3& point, float distance, float speed)
    {
        LUMOS_PROFILE_FUNCTION();
        if(m_CurrentCamera->IsOrthographic())
        {
            m_EditorCameraTransform.SetLocalPosition(point);
            // m_CurrentCamera->SetScale(distance * 0.5f);
        }
        else
        {
            m_TransitioningCamera = true;

            m_CameraDestination         = point + m_EditorCameraTransform.GetForwardDirection() * distance;
            m_CameraTransitionStartTime = -1.0f;
            m_CameraTransitionSpeed     = 1.0f / speed;
            m_CameraStartPosition       = m_EditorCameraTransform.GetLocalPosition();
        }
    }

    bool Editor::OnWindowResize(WindowResizeEvent& e)
    {
        return false;
    }

    void Editor::RecompileShaders()
    {
        LUMOS_PROFILE_FUNCTION();
        LUMOS_LOG_INFO("Recompiling shaders Disabled");

#ifdef LUMOS_RENDER_API_VULKAN
#ifdef LUMOS_PLATFORM_WINDOWS
        // std::string filePath = ROOT_DIR"/Lumos/Assets/EngineShaders/CompileShadersWindows.bat";
        // system(filePath.c_str());
#elif LUMOS_PLATFORM_MACOS
        // std::string filePath = ROOT_DIR "/Lumos/Assets/EngineShaders/CompileShadersMac.sh";
        // system(filePath.c_str());
#endif
#endif
    }

    void Editor::DebugDraw()
    {
        LUMOS_PROFILE_FUNCTION();
        auto& registry           = Application::Get().GetSceneManager()->GetCurrentScene()->GetRegistry();
        glm::vec4 selectedColour = glm::vec4(0.9f);
        if(m_Settings.m_DebugDrawFlags & EditorDebugFlags::MeshBoundingBoxes)
        {
            auto group = registry.group<Graphics::ModelComponent>(entt::get<Maths::Transform>);

            for(auto entity : group)
            {
                const auto& [model, trans] = group.get<Graphics::ModelComponent, Maths::Transform>(entity);
                auto& meshes               = model.ModelRef->GetMeshes();
                for(auto mesh : meshes)
                {
                    if(mesh->GetActive())
                    {
                        auto& worldTransform = trans.GetWorldMatrix();
                        auto bbCopy          = mesh->GetBoundingBox()->Transformed(worldTransform);
                        DebugRenderer::DebugDraw(bbCopy, selectedColour, true);
                    }
                }
            }
        }

        if(m_Settings.m_DebugDrawFlags & EditorDebugFlags::SpriteBoxes)
        {
            auto group = registry.group<Graphics::Sprite>(entt::get<Maths::Transform>);

            for(auto entity : group)
            {
                const auto& [sprite, trans] = group.get<Graphics::Sprite, Maths::Transform>(entity);

                {
                    auto& worldTransform = trans.GetWorldMatrix();

                    auto bb = Maths::BoundingBox(Maths::Rect(sprite.GetPosition(), sprite.GetScale()));
                    bb.Transform(trans.GetWorldMatrix());
                    DebugRenderer::DebugDraw(bb, selectedColour, true);
                }
            }

            auto animGroup = registry.group<Graphics::AnimatedSprite>(entt::get<Maths::Transform>);

            for(auto entity : animGroup)
            {
                const auto& [sprite, trans] = animGroup.get<Graphics::AnimatedSprite, Maths::Transform>(entity);

                {
                    auto& worldTransform = trans.GetWorldMatrix();

                    auto bb = Maths::BoundingBox(Maths::Rect(sprite.GetPosition(), sprite.GetScale()));
                    bb.Transform(trans.GetWorldMatrix());
                    DebugRenderer::DebugDraw(bb, selectedColour, true);
                }
            }
        }

        if(m_Settings.m_DebugDrawFlags & EditorDebugFlags::CameraFrustum)
        {
            auto cameraGroup = registry.group<Camera>(entt::get<Maths::Transform>);

            for(auto entity : cameraGroup)
            {
                const auto& [camera, trans] = cameraGroup.get<Camera, Maths::Transform>(entity);

                {
                    DebugRenderer::DebugDraw(camera.GetFrustum(glm::inverse(trans.GetWorldMatrix())), glm::vec4(0.9f));
                }
            }
        }

        if(m_Settings.m_DebugDrawFlags & EditorDebugFlags::EntityNames)
        {
            auto transform = registry.view<Maths::Transform>();

            for(auto entity : transform)
            {
                Entity e = { entity, GetCurrentScene() };
                {
                    DebugRenderer::DrawTextWsNDT(e.GetTransform().GetWorldPosition(), 20.0f, glm::vec4(1.0f), e.GetName());
                }
            }
        }

        for(auto m_SelectedEntity : m_SelectedEntities)
            if(registry.valid(m_SelectedEntity)) // && Application::Get().GetEditorState() == EditorState::Preview)
            {
                auto transform = registry.try_get<Maths::Transform>(m_SelectedEntity);

                auto model = registry.try_get<Graphics::ModelComponent>(m_SelectedEntity);
                if(transform && model && model->ModelRef)
                {
                    auto& meshes = model->ModelRef->GetMeshes();
                    for(auto mesh : meshes)
                    {
                        if(mesh->GetActive())
                        {
                            auto& worldTransform = transform->GetWorldMatrix();
                            auto bbCopy          = mesh->GetBoundingBox()->Transformed(worldTransform);
                            DebugRenderer::DebugDraw(bbCopy, selectedColour, true);
                        }
                    }
                    if(model->ModelRef->GetSkeleton())
                    {
                        auto& skeleton       = *model->ModelRef->GetSkeleton().get();
                        const int num_joints = skeleton.num_joints();

                        // Iterate through each joint in the skeleton
                        for(int i = 0; i < num_joints; ++i)
                        {
                            // Get the current joint's world space transform

                            const ozz::math::Transform joint_transform; //= skeleton.joint_rest_poses()[i];

                            // Convert ozz::math::Transform to glm::mat4
                            glm::mat4 joint_world_transform = glm::translate(glm::mat4(1.0f), glm::vec3(joint_transform.translation.x, joint_transform.translation.y, joint_transform.translation.z));
                            joint_world_transform *= glm::mat4_cast(glm::quat(joint_transform.rotation.x, joint_transform.rotation.y, joint_transform.rotation.z, joint_transform.rotation.w));
                            joint_world_transform = glm::scale(joint_world_transform, glm::vec3(joint_transform.scale.x, joint_transform.scale.y, joint_transform.scale.z));

                            // Multiply the joint's world transform by the entity transform
                            glm::mat4 final_transform = transform->GetWorldMatrix() * joint_world_transform;

                            // Get the parent joint's world space transform
                            const int parent_index = skeleton.joint_parents()[i];
                            glm::mat4 parent_world_transform(1.0f);
                            if(parent_index != ozz::animation::Skeleton::Constants::kNoParent)
                            {
                                const ozz::math::Transform parent_transform; // = skeleton.joint_rest_poses()[parent_index];
                                parent_world_transform = glm::translate(glm::mat4(1.0f), glm::vec3(parent_transform.translation.x, parent_transform.translation.y, parent_transform.translation.z));
                                parent_world_transform *= glm::mat4_cast(glm::quat(parent_transform.rotation.x, parent_transform.rotation.y, parent_transform.rotation.z, parent_transform.rotation.w));
                                parent_world_transform = glm::scale(parent_world_transform, glm::vec3(parent_transform.scale.x, parent_transform.scale.y, parent_transform.scale.z));
                                parent_world_transform = transform->GetWorldMatrix() * parent_world_transform;
                            }

                            // Draw a line between the current joint and its parent joint
                            // (assuming you have a function to draw a line between two points)
                            DebugRenderer::DrawHairLine(glm::vec3(final_transform[3]), glm::vec3(parent_world_transform[3]), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)); // Example color: red
                        }
                    }
                }

                auto sprite = registry.try_get<Graphics::Sprite>(m_SelectedEntity);
                if(transform && sprite)
                {
                    {
                        auto& worldTransform = transform->GetWorldMatrix();

                        auto bb = Maths::BoundingBox(Maths::Rect(sprite->GetPosition(), sprite->GetPosition() + sprite->GetScale()));
                        bb.Transform(worldTransform);
                        DebugRenderer::DebugDraw(bb, selectedColour, true);
                    }
                }

                auto animSprite = registry.try_get<Graphics::AnimatedSprite>(m_SelectedEntity);
                if(transform && animSprite)
                {
                    {
                        auto& worldTransform = transform->GetWorldMatrix();

                        auto bb = Maths::BoundingBox(Maths::Rect(animSprite->GetPosition(), animSprite->GetPosition() + animSprite->GetScale()));
                        bb.Transform(worldTransform);
                        DebugRenderer::DebugDraw(bb, selectedColour, true);
                    }
                }

                auto camera = registry.try_get<Camera>(m_SelectedEntity);
                if(camera && transform)
                {
                    DebugRenderer::DebugDraw(camera->GetFrustum(glm::inverse(transform->GetWorldMatrix())), glm::vec4(0.9f));
                }

                auto light = registry.try_get<Graphics::Light>(m_SelectedEntity);
                if(light && transform)
                {
                    DebugRenderer::DebugDraw(light, transform->GetWorldOrientation(), glm::vec4(glm::vec3(light->Colour), 0.2f));
                }

                auto sound = registry.try_get<SoundComponent>(m_SelectedEntity);
                if(sound)
                {
                    DebugRenderer::DebugDraw(sound->GetSoundNode(), glm::vec4(0.8f, 0.8f, 0.8f, 0.2f));
                }

                auto phys3D = registry.try_get<RigidBody3DComponent>(m_SelectedEntity);
                if(phys3D)
                {
                    auto cs = phys3D->GetRigidBody()->GetCollisionShape();
                    if(cs)
                        cs->DebugDraw(phys3D->GetRigidBody());
                }
            }
    }

    void Editor::SelectObject(const Maths::Ray& ray)
    {
        LUMOS_PROFILE_FUNCTION();
        auto& registry                    = Application::Get().GetSceneManager()->GetCurrentScene()->GetRegistry();
        float closestEntityDist           = Maths::M_INFINITY;
        entt::entity currentClosestEntity = entt::null;

        auto group = registry.group<Graphics::ModelComponent>(entt::get<Maths::Transform>);

        static Timer timer;
        static float timeSinceLastSelect = 0.0f;

        for(auto entity : group)
        {
            const auto& [model, trans] = group.get<Graphics::ModelComponent, Maths::Transform>(entity);

            auto& meshes = model.ModelRef->GetMeshes();

            for(auto mesh : meshes)
            {
                if(mesh->GetActive())
                {
                    auto& worldTransform = trans.GetWorldMatrix();

                    auto bbCopy = mesh->GetBoundingBox()->Transformed(worldTransform);
                    float distance;
                    ray.Intersects(bbCopy, distance);

                    if(distance < Maths::M_INFINITY)
                    {
                        if(distance < closestEntityDist)
                        {
                            closestEntityDist    = distance;
                            currentClosestEntity = entity;
                        }
                    }
                }
            }
        }

        if(!m_SelectedEntities.empty())
        {
            if(registry.valid(currentClosestEntity) && IsSelected(currentClosestEntity))
            {
                if(timer.GetElapsedS() - timeSinceLastSelect < 1.0f)
                {
                    auto& trans = registry.get<Maths::Transform>(currentClosestEntity);
                    auto& model = registry.get<Graphics::ModelComponent>(currentClosestEntity);
                    auto bb     = model.ModelRef->GetMeshes().front()->GetBoundingBox()->Transformed(trans.GetWorldMatrix());

                    FocusCamera(trans.GetWorldPosition(), glm::distance(bb.Max(), bb.Min()));
                }
                else
                {
                    UnSelect(currentClosestEntity);
                }
            }

            timeSinceLastSelect = timer.GetElapsedS();

            auto& io  = ImGui::GetIO();
            auto ctrl = Input::Get().GetKeyHeld(InputCode::Key::LeftSuper) || (Input::Get().GetKeyHeld(InputCode::Key::LeftControl));

            if(!ctrl)
                m_SelectedEntities.clear();

            SetSelected(currentClosestEntity);
            return;
        }

        auto spriteGroup = registry.group<Graphics::Sprite>(entt::get<Maths::Transform>);

        for(auto entity : spriteGroup)
        {
            const auto& [sprite, trans] = spriteGroup.get<Graphics::Sprite, Maths::Transform>(entity);

            auto& worldTransform = trans.GetWorldMatrix();
            auto bb              = Maths::BoundingBox(Maths::Rect(sprite.GetPosition(), sprite.GetPosition() + sprite.GetScale()));
            bb.Transform(trans.GetWorldMatrix());

            float distance;
            ray.Intersects(bb, distance);
            if(distance < Maths::M_INFINITY)
            {
                if(distance < closestEntityDist)
                {
                    closestEntityDist    = distance;
                    currentClosestEntity = entity;
                }
            }
        }

        auto animSpriteGroup = registry.group<Graphics::AnimatedSprite>(entt::get<Maths::Transform>);

        for(auto entity : animSpriteGroup)
        {
            const auto& [sprite, trans] = animSpriteGroup.get<Graphics::AnimatedSprite, Maths::Transform>(entity);

            auto& worldTransform = trans.GetWorldMatrix();
            auto bb              = Maths::BoundingBox(Maths::Rect(sprite.GetPosition(), sprite.GetPosition() + sprite.GetScale()));
            bb.Transform(trans.GetWorldMatrix());
            float distance;
            ray.Intersects(bb, distance);
            if(distance < Maths::M_INFINITY)
            {
                if(distance < closestEntityDist)
                {
                    closestEntityDist    = distance;
                    currentClosestEntity = entity;
                }
            }
        }

        {
            if(IsSelected(currentClosestEntity))
            {
                auto& trans  = registry.get<Maths::Transform>(currentClosestEntity);
                auto& sprite = registry.get<Graphics::Sprite>(currentClosestEntity);
                auto bb      = Maths::BoundingBox(Maths::Rect(sprite.GetPosition(), sprite.GetPosition() + sprite.GetScale()));

                FocusCamera(trans.GetWorldPosition(), glm::distance(bb.Max(), bb.Min()));
            }
        }

        SetSelected(currentClosestEntity);
    }

    void Editor::OpenTextFile(const std::string& filePath, const std::function<void()>& callback)
    {
        LUMOS_PROFILE_FUNCTION();
        std::string physicalPath;
        if(!VFS::Get().ResolvePhysicalPath(filePath, physicalPath))
        {
            LUMOS_LOG_ERROR("Failed to Load Lua script {0}", filePath);
            return;
        }

        for(int i = 0; i < int(m_Panels.size()); i++)
        {
            EditorPanel* w = m_Panels[i].get();
            if(w->GetSimpleName() == "TextEdit")
            {
                m_Panels.erase(m_Panels.begin() + i);
                break;
            }
        }

        m_Panels.emplace_back(CreateSharedPtr<TextEditPanel>(physicalPath));
        m_Panels.back().As<TextEditPanel>()->SetOnSaveCallback(callback);
        m_Panels.back()->SetEditor(this);
    }

    EditorPanel* Editor::GetTextEditPanel()
    {
        for(int i = 0; i < int(m_Panels.size()); i++)
        {
            EditorPanel* w = m_Panels[i].get();
            if(w->GetSimpleName() == "TextEdit")
            {
                return w;
            }
        }

        return nullptr;
    }

    void Editor::RemovePanel(EditorPanel* panel)
    {
        LUMOS_PROFILE_FUNCTION();
        for(int i = 0; i < int(m_Panels.size()); i++)
        {
            EditorPanel* w = m_Panels[i].get();
            if(w == panel)
            {
                m_Panels.erase(m_Panels.begin() + i);
                return;
            }
        }
    }

    void Editor::ShowPreview()
    {
        LUMOS_PROFILE_FUNCTION();
        ImGui::Begin("Preview");
        if(m_PreviewTexture)
            ImGuiUtilities::Image(m_PreviewTexture.get(), { 200, 200 });
        ImGui::End();
    }

    void Editor::OnDebugDraw()
    {
        Application::OnDebugDraw();
        DebugDraw();

        // Application::Get().GetEditorState() == EditorState::Preview &&
    }

    void Editor::OnRender()
    {
        LUMOS_PROFILE_FUNCTION();
        // DrawPreview();

        bool isProfiling       = false;
        static bool firstFrame = true;
#if LUMOS_PROFILE
        isProfiling = tracy::GetProfiler().IsConnected();
#endif
        if(!isProfiling && m_Settings.m_SleepOutofFocus && !Application::Get().GetWindow()->GetWindowFocus() && m_EditorState != EditorState::Play && !firstFrame)
            OS::Instance()->Delay(1000000);

        Application::OnRender();

        for(int i = 0; i < int(m_Panels.size()); i++)
        {
            m_Panels[i]->OnRender();
        }

        if(m_Settings.m_ShowGrid && !m_EditorCamera->IsOrthographic())
            Draw3DGrid();

        firstFrame = false;
    }

    void Editor::DrawPreview()
    {
        LUMOS_PROFILE_FUNCTION();
        if(!m_PreviewTexture)
        {
            Graphics::TextureDesc desc;
            desc.format = Graphics::RHIFormat::R8G8B8A8_Unorm;
            desc.flags  = Graphics::TextureFlags::Texture_RenderTarget;

            m_PreviewTexture = SharedPtr<Graphics::Texture2D>(Graphics::Texture2D::Create(desc, 200, 200));

            // m_PreviewRenderer = CreateSharedPtr<Graphics::ForwardRenderer>(200, 200, false);
            m_PreviewSphere = SharedPtr<Graphics::Mesh>(Graphics::CreateSphere());

            // m_PreviewRenderer->SetRenderTarget(m_PreviewTexture.get(), true);
        }

        glm::mat4 proj = glm::perspective(0.1f, 10.0f, 200.0f / 200.0f, 60.0f);
        glm::mat4 view = glm::inverse(Maths::Mat4FromTRS(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                                                         glm::vec3(1.0f)));

        //        m_PreviewRenderer->Begin();
        //        //m_PreviewRenderer->BeginScene(proj, view);
        //        m_PreviewRenderer->SubmitMesh(m_PreviewSphere.get(), nullptr, glm::mat4(1.0f), glm::mat4(1.0f));
        //        m_PreviewRenderer->Present();
        //        m_PreviewRenderer->End();
    }

    void Editor::FileOpenCallback(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();

        auto path = filePath;
        path      = StringUtilities::BackSlashesToSlashes(path);
        if(IsTextFile(path))
            OpenTextFile(path, NULL);
        else if(IsModelFile(path))
        {
            Entity modelEntity = Application::Get().GetSceneManager()->GetCurrentScene()->GetEntityManager()->Create(StringUtilities::GetFileName(path));
            modelEntity.AddComponent<Graphics::ModelComponent>(path);

            SetSelected(modelEntity.GetHandle());
            // m_SelectedEntity = modelEntity.GetHandle();
        }
        else if(IsAudioFile(path))
        {
            std::string physicalPath;
            Lumos::VFS::Get().ResolvePhysicalPath(path, physicalPath);
            auto sound = Sound::Create(physicalPath, StringUtilities::GetFilePathExtension(path));

            auto soundNode = SharedPtr<SoundNode>(SoundNode::Create());
            soundNode->SetSound(sound);
            soundNode->SetVolume(1.0f);
            soundNode->SetPosition(glm::vec3(0.1f, 10.0f, 10.0f));
            soundNode->SetLooping(true);
            soundNode->SetIsGlobal(false);
            soundNode->SetPaused(false);
            soundNode->SetReferenceDistance(1.0f);
            soundNode->SetRadius(30.0f);

            Entity entity = Application::Get().GetSceneManager()->GetCurrentScene()->GetEntityManager()->Create(StringUtilities::GetFileName(path));
            entity.AddComponent<SoundComponent>(soundNode);
            entity.GetOrAddComponent<Maths::Transform>();
            SetSelected(entity.GetHandle());
        }
        else if(IsSceneFile(path))
        {
            int index = Application::Get().GetSceneManager()->EnqueueSceneFromFile(path);
            Application::Get().GetSceneManager()->SwitchScene(index);
        }
        else if(IsTextureFile(path))
        {
            auto entity  = Application::Get().GetSceneManager()->GetCurrentScene()->CreateEntity(StringUtilities::GetFileName(path));
            auto& sprite = entity.AddComponent<Graphics::Sprite>();
            entity.GetOrAddComponent<Maths::Transform>();

            SharedPtr<Graphics::Texture2D> texture = SharedPtr<Graphics::Texture2D>(Graphics::Texture2D::CreateFromFile(path, path));
            sprite.SetTexture(texture);
        }
        else if(IsPrefab(path))
        {
            m_SceneManager->GetCurrentScene()->InstantiatePrefab(path);
        }
    }

    void Editor::FileEmbedCallback(const std::string& filePath)
    {
        if(IsTextureFile(filePath))
        {
            std::string fileName = StringUtilities::RemoveFilePathExtension(StringUtilities::GetFileName(filePath));
            std::string outPath  = StringUtilities::GetFileLocation(filePath) + fileName + ".inl";

            LUMOS_LOG_INFO("Embed texture from {0} to {1}", filePath, outPath);
            EmbedTexture(filePath, outPath, fileName);
        }
        else if(IsShaderFile(filePath))
        {
            EmbedShader(filePath);
        }
    }

    void Editor::ProjectOpenCallback(const std::string& filePath)
    {
        m_NewProjectPopupOpen = false;
        reopenNewProjectPopup = false;
        locationPopupOpened   = false;
        m_FileBrowserPanel.ClearFileTypeFilters();

        if(FileSystem::FileExists(filePath))
        {
            auto it = std::find(m_Settings.m_RecentProjects.begin(), m_Settings.m_RecentProjects.end(), filePath);
            if(it == m_Settings.m_RecentProjects.end())
                m_Settings.m_RecentProjects.push_back(filePath);
        }

        Application::Get().OpenProject(filePath);

        for(int i = 0; i < int(m_Panels.size()); i++)
        {
            m_Panels[i]->OnNewProject();
        }
    }

    void Editor::NewProjectOpenCallback(const std::string& filePath)
    {
        Application::Get().OpenNewProject(filePath);
        m_FileBrowserPanel.SetOpenDirectory(false);

        for(int i = 0; i < int(m_Panels.size()); i++)
        {
            m_Panels[i]->OnNewProject();
        }
    }

    void Editor::SaveEditorSettings()
    {
        LUMOS_PROFILE_FUNCTION();
        m_IniFile.RemoveAll();
        m_IniFile.SetOrAdd("ShowGrid", m_Settings.m_ShowGrid);
        m_IniFile.SetOrAdd("ShowGizmos", m_Settings.m_ShowGizmos);
        m_IniFile.SetOrAdd("ShowViewSelected", m_Settings.m_ShowViewSelected);
        m_IniFile.SetOrAdd("ShowImGuiDemo", m_Settings.m_ShowImGuiDemo);
        m_IniFile.SetOrAdd("SnapAmount", m_Settings.m_SnapAmount);
        m_IniFile.SetOrAdd("SnapQuizmo", m_Settings.m_SnapQuizmo);
        m_IniFile.SetOrAdd("DebugDrawFlags", m_Settings.m_DebugDrawFlags);
        m_IniFile.SetOrAdd("PhysicsDebugDrawFlags", Application::Get().GetSystem<LumosPhysicsEngine>()->GetDebugDrawFlags());
        m_IniFile.SetOrAdd("PhysicsDebugDrawFlags2D", Application::Get().GetSystem<B2PhysicsEngine>()->GetDebugDrawFlags());
        m_IniFile.SetOrAdd("Theme", (int)m_Settings.m_Theme);
        m_IniFile.SetOrAdd("ProjectRoot", m_ProjectSettings.m_ProjectRoot);
        m_IniFile.SetOrAdd("ProjectName", m_ProjectSettings.m_ProjectName);
        m_IniFile.SetOrAdd("SleepOutofFocus", m_Settings.m_SleepOutofFocus);
        m_IniFile.SetOrAdd("CameraSpeed", m_Settings.m_CameraSpeed);
        m_IniFile.SetOrAdd("CameraNear", m_Settings.m_CameraNear);
        m_IniFile.SetOrAdd("CameraFar", m_Settings.m_CameraFar);

        std::sort(m_Settings.m_RecentProjects.begin(), m_Settings.m_RecentProjects.end());
        m_Settings.m_RecentProjects.erase(std::unique(m_Settings.m_RecentProjects.begin(), m_Settings.m_RecentProjects.end()), m_Settings.m_RecentProjects.end());
        m_IniFile.SetOrAdd("RecentProjectCount", int(m_Settings.m_RecentProjects.size()));

        for(int i = 0; i < int(m_Settings.m_RecentProjects.size()); i++)
        {
            m_IniFile.SetOrAdd("RecentProject" + std::to_string(i), m_Settings.m_RecentProjects[i]);
        }

        m_IniFile.Rewrite();
    }

    void Editor::AddDefaultEditorSettings()
    {
        LUMOS_PROFILE_FUNCTION();
        m_ProjectSettings.m_ProjectRoot = "../../ExampleProject/";
        m_ProjectSettings.m_ProjectName = "Example";

        m_IniFile.Add("ShowGrid", m_Settings.m_ShowGrid);
        m_IniFile.Add("ShowGizmos", m_Settings.m_ShowGizmos);
        m_IniFile.Add("ShowViewSelected", m_Settings.m_ShowViewSelected);
        m_IniFile.Add("TransitioningCamera", m_TransitioningCamera);
        m_IniFile.Add("ShowImGuiDemo", m_Settings.m_ShowImGuiDemo);
        m_IniFile.Add("SnapAmount", m_Settings.m_SnapAmount);
        m_IniFile.Add("SnapQuizmo", m_Settings.m_SnapQuizmo);
        m_IniFile.Add("DebugDrawFlags", m_Settings.m_DebugDrawFlags);
        m_IniFile.Add("PhysicsDebugDrawFlags", 0);
        m_IniFile.Add("PhysicsDebugDrawFlags2D", 0);
        m_IniFile.Add("Theme", (int)m_Settings.m_Theme);
        m_IniFile.Add("ProjectRoot", m_ProjectSettings.m_ProjectRoot);
        m_IniFile.Add("ProjectName", m_ProjectSettings.m_ProjectName);
        m_IniFile.Add("SleepOutofFocus", m_Settings.m_SleepOutofFocus);
        m_IniFile.Add("RecentProjectCount", 0);
        m_IniFile.Add("CameraSpeed", m_Settings.m_CameraSpeed);
        m_IniFile.Add("CameraNear", m_Settings.m_CameraNear);
        m_IniFile.Add("CameraFar", m_Settings.m_CameraFar);
        m_IniFile.Rewrite();
    }

    void Editor::LoadEditorSettings()
    {
        LUMOS_PROFILE_FUNCTION();
        m_Settings.m_ShowGrid         = m_IniFile.GetOrDefault("ShowGrid", m_Settings.m_ShowGrid);
        m_Settings.m_ShowGizmos       = m_IniFile.GetOrDefault("ShowGizmos", m_Settings.m_ShowGizmos);
        m_Settings.m_ShowViewSelected = m_IniFile.GetOrDefault("ShowViewSelected", m_Settings.m_ShowViewSelected);
        m_TransitioningCamera         = m_IniFile.GetOrDefault("TransitioningCamera", m_TransitioningCamera);
        m_Settings.m_ShowImGuiDemo    = m_IniFile.GetOrDefault("ShowImGuiDemo", m_Settings.m_ShowImGuiDemo);
        m_Settings.m_SnapAmount       = m_IniFile.GetOrDefault("SnapAmount", m_Settings.m_SnapAmount);
        m_Settings.m_SnapQuizmo       = m_IniFile.GetOrDefault("SnapQuizmo", m_Settings.m_SnapQuizmo);
        m_Settings.m_DebugDrawFlags   = m_IniFile.GetOrDefault("DebugDrawFlags", m_Settings.m_DebugDrawFlags);
        m_Settings.m_Theme            = ImGuiUtilities::Theme(m_IniFile.GetOrDefault("Theme", (int)m_Settings.m_Theme));

        m_ProjectSettings.m_ProjectRoot  = m_IniFile.GetOrDefault("ProjectRoot", std::string("../../ExampleProject/"));
        m_ProjectSettings.m_ProjectName  = m_IniFile.GetOrDefault("ProjectName", std::string("Example"));
        m_Settings.m_Physics2DDebugFlags = m_IniFile.GetOrDefault("PhysicsDebugDrawFlags2D", 0);
        m_Settings.m_Physics3DDebugFlags = m_IniFile.GetOrDefault("PhysicsDebugDrawFlags", 0);
        m_Settings.m_SleepOutofFocus     = m_IniFile.GetOrDefault("SleepOutofFocus", true);
        m_Settings.m_CameraSpeed         = m_IniFile.GetOrDefault("CameraSpeed", 1000.0f);
        m_Settings.m_CameraNear          = m_IniFile.GetOrDefault("CameraNear", 0.01f);
        m_Settings.m_CameraFar           = m_IniFile.GetOrDefault("CameraFar", 1000.0f);

        m_EditorCameraController.SetSpeed(m_Settings.m_CameraSpeed);

        int recentProjectCount  = 0;
        std::string projectPath = m_ProjectSettings.m_ProjectRoot + m_ProjectSettings.m_ProjectName + std::string(".lmproj");

        if(FileSystem::FileExists(projectPath))
        {
            auto it = std::find(m_Settings.m_RecentProjects.begin(), m_Settings.m_RecentProjects.end(), projectPath);
            if(it == m_Settings.m_RecentProjects.end())
                m_Settings.m_RecentProjects.push_back(projectPath);
        }

        recentProjectCount = m_IniFile.GetOrDefault("RecentProjectCount", 0);
        for(int i = 0; i < recentProjectCount; i++)
        {
            projectPath = m_IniFile.GetOrDefault("RecentProject" + std::to_string(i), std::string());

            if(FileSystem::FileExists(projectPath))
            {
                auto it = std::find(m_Settings.m_RecentProjects.begin(), m_Settings.m_RecentProjects.end(), projectPath);
                if(it == m_Settings.m_RecentProjects.end())
                    m_Settings.m_RecentProjects.push_back(projectPath);
            }
        }

        std::sort(m_Settings.m_RecentProjects.begin(), m_Settings.m_RecentProjects.end());
        m_Settings.m_RecentProjects.erase(std::unique(m_Settings.m_RecentProjects.begin(), m_Settings.m_RecentProjects.end()), m_Settings.m_RecentProjects.end());
    }

    const char* Editor::GetIconFontIcon(const std::string& filePath)
    {
        LUMOS_PROFILE_FUNCTION();
        if(IsTextFile(filePath))
        {
            return ICON_MDI_FILE_XML;
        }
        else if(IsModelFile(filePath))
        {
            return ICON_MDI_SHAPE;
        }
        else if(IsAudioFile(filePath))
        {
            return ICON_MDI_FILE_MUSIC;
        }
        else if(IsTextureFile(filePath))
        {
            return ICON_MDI_FILE_IMAGE;
        }

        return ICON_MDI_FILE;
    }

    void Editor::CreateGridRenderer()
    {
        LUMOS_PROFILE_FUNCTION();
        if(!m_GridRenderer)
            m_GridRenderer = CreateSharedPtr<Graphics::GridRenderer>(uint32_t(Application::Get().m_SceneViewWidth), uint32_t(Application::Get().m_SceneViewHeight));
    }

    const SharedPtr<Graphics::GridRenderer>& Editor::GetGridRenderer()
    {
        LUMOS_PROFILE_FUNCTION();
        if(!m_GridRenderer)
            m_GridRenderer = CreateSharedPtr<Graphics::GridRenderer>(uint32_t(Application::Get().m_SceneViewWidth), uint32_t(Application::Get().m_SceneViewHeight));
        return m_GridRenderer;
    }

    void Editor::CacheScene()
    {
        LUMOS_PROFILE_FUNCTION();
        Application::Get().GetCurrentScene()->Serialise(m_TempSceneSaveFilePath, false);
    }

    void Editor::LoadCachedScene()
    {
        LUMOS_PROFILE_FUNCTION();

        if(FileSystem::FileExists(m_TempSceneSaveFilePath + Application::Get().GetCurrentScene()->GetSceneName() + ".lsn"))
        {
            Application::Get().GetCurrentScene()->Deserialise(m_TempSceneSaveFilePath, false);
        }
        else
        {
            std::string physicalPath;
            if(Lumos::VFS::Get().ResolvePhysicalPath("//Scenes/" + Application::Get().GetCurrentScene()->GetSceneName() + ".lsn", physicalPath))
            {
                auto newPath = StringUtilities::RemoveName(physicalPath);
                Application::Get().GetCurrentScene()->Deserialise(newPath, false);
            }
        }
    }
}
