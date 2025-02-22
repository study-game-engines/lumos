#include "HierarchyPanel.h"
#include "Editor.h"
#include "InspectorPanel.h"

#include <Lumos/Core/OS/Input.h>
#include <Lumos/Core/Application.h>
#include <Lumos/Scene/SceneManager.h>
#include <Lumos/ImGui/ImGuiUtilities.h>
#include <Lumos/Scene/SceneGraph.h>
#include <Lumos/Maths/Transform.h>
#include <Lumos/Scene/Entity.h>
#include <Lumos/Graphics/Camera/Camera.h>
#include <Lumos/Graphics/Light.h>
#include <Lumos/Graphics/Environment.h>
#include <Lumos/Graphics/Sprite.h>
#include <Lumos/Scene/Component/Components.h>
#include <Lumos/Scripting/Lua/LuaScriptComponent.h>
#include <Lumos/ImGui/IconsMaterialDesignIcons.h>
#include <Lumos/Core/StringUtilities.h>

#include <typeinfo>
#include <imgui/imgui_internal.h>
#include <sol/sol.hpp>

namespace Lumos
{
    HierarchyPanel::HierarchyPanel()
        : m_HadRecentDroppedEntity(entt::null)
        , m_DoubleClicked(entt::null)
    {
        m_Name       = "Hierarchy###hierarchy";
        m_SimpleName = "Hierarchy";
    }

    void HierarchyPanel::DrawNode(entt::entity node, entt::registry& registry)
    {
        LUMOS_PROFILE_FUNCTION();
        bool show = true;

        if(!registry.valid(node))
            return;

        Entity nodeEntity = { node, Application::Get().GetSceneManager()->GetCurrentScene() };

        static const char* defaultName     = "Entity";
        const NameComponent* nameComponent = registry.try_get<NameComponent>(node);
        const char* name                   = nameComponent ? nameComponent->name.c_str() : defaultName; // StringUtilities::ToString(entt::to_integral(node));

        if(m_HierarchyFilter.IsActive())
        {
            if(!m_HierarchyFilter.PassFilter(name))
            {
                show = false;
            }
        }

        if(show)
        {
            ImGui::PushID((int)node);
            auto hierarchyComponent = registry.try_get<Hierarchy>(node);
            bool noChildren         = true;

            if(hierarchyComponent != nullptr && hierarchyComponent->First() != entt::null)
                noChildren = false;

            ImGuiTreeNodeFlags nodeFlags = ((m_Editor->IsSelected(node)) ? ImGuiTreeNodeFlags_Selected : 0);

            nodeFlags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;

            if(noChildren)
            {
                nodeFlags |= ImGuiTreeNodeFlags_Leaf;
            }

            // auto activeComponent = registry.try_get<ActiveComponent>(node);
            bool active = Entity(node, m_Editor->GetCurrentScene()).Active(); // activeComponent ? activeComponent->active : true;

            if(!active)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            bool doubleClicked = false;
            if(node == m_DoubleClicked)
            {
                doubleClicked = true;
            }

            if(doubleClicked)
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1.0f, 2.0f });

            if(m_HadRecentDroppedEntity == node)
            {
                ImGui::SetNextItemOpen(true);
                m_HadRecentDroppedEntity = entt::null;
            }

            std::string icon = ICON_MDI_CUBE_OUTLINE;
            auto& iconMap    = m_Editor->GetComponentIconMap();

            if(registry.all_of<Camera>(node))
            {
                if(iconMap.find(typeid(Camera).hash_code()) != iconMap.end())
                    icon = iconMap[typeid(Camera).hash_code()];
            }
            if(registry.all_of<LuaScriptComponent>(node))
            {
                if(iconMap.find(typeid(LuaScriptComponent).hash_code()) != iconMap.end())
                    icon = iconMap[typeid(LuaScriptComponent).hash_code()];
            }
            else if(registry.all_of<SoundComponent>(node))
            {
                if(iconMap.find(typeid(SoundComponent).hash_code()) != iconMap.end())
                    icon = iconMap[typeid(SoundComponent).hash_code()];
            }
            else if(registry.all_of<RigidBody2DComponent>(node))
            {
                if(iconMap.find(typeid(RigidBody2DComponent).hash_code()) != iconMap.end())
                    icon = iconMap[typeid(RigidBody2DComponent).hash_code()];
            }
            else if(registry.all_of<Graphics::Light>(node))
            {
                if(iconMap.find(typeid(Graphics::Light).hash_code()) != iconMap.end())
                    icon = iconMap[typeid(Graphics::Light).hash_code()];
            }
            else if(registry.all_of<Graphics::Environment>(node))
            {
                if(iconMap.find(typeid(Graphics::Environment).hash_code()) != iconMap.end())
                    icon = iconMap[typeid(Graphics::Environment).hash_code()];
            }
            else if(registry.all_of<Graphics::Sprite>(node))
            {
                if(iconMap.find(typeid(Graphics::Sprite).hash_code()) != iconMap.end())
                    icon = iconMap[typeid(Graphics::Sprite).hash_code()];
            }
            else if(registry.all_of<TextComponent>(node))
            {
                if(iconMap.find(typeid(TextComponent).hash_code()) != iconMap.end())
                    icon = iconMap[typeid(TextComponent).hash_code()];
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImGuiUtilities::GetIconColour());
            // ImGui::BeginGroup();
            bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)entt::to_integral(node), nodeFlags, "%s", icon.c_str());
            {
                // Allow clicking of icon and text. Need twice as they are separated
                if(ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
                {
                    bool ctrlDown = Input::Get().GetKeyHeld(Lumos::InputCode::Key::LeftControl) || Input::Get().GetKeyHeld(Lumos::InputCode::Key::RightControl) || Input::Get().GetKeyHeld(Lumos::InputCode::Key::LeftSuper);
                    if(!ctrlDown)
                        m_Editor->ClearSelected();

                    if(!m_Editor->IsSelected(node))
                        m_Editor->SetSelected(node);
                    else
                        m_Editor->UnSelect(node);
                }
                else if(m_DoubleClicked == node && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered(ImGuiHoveredFlags_None))
                    m_DoubleClicked = entt::null;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if(!doubleClicked)
            {
                bool isPrefab = false;
                if(registry.any_of<PrefabComponent>(node))
                    isPrefab = true;
                else
                {
                    auto Parent = nodeEntity.GetParent();
                    while(Parent && Parent.Valid())
                    {
                        if(Parent.HasComponent<PrefabComponent>())
                        {
                            isPrefab = true;
                            Parent   = {};
                        }
                        else
                        {
                            Parent = Parent.GetParent();
                        }
                    }
                }

                if(isPrefab)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
                ImGui::TextUnformatted(name);
                if(isPrefab)
                    ImGui::PopStyleColor();
            }
            // ImGui::EndGroup();

            if(doubleClicked)
            {
                static char objName[INPUT_BUF_SIZE];
                strcpy(objName, name);
                ImGui::PushItemWidth(-1);
                if(ImGui::InputText("##Name", objName, IM_ARRAYSIZE(objName), 0))
                    registry.get_or_emplace<NameComponent>(node).name = objName;
                ImGui::PopStyleVar();
            }

            if(!active)
                ImGui::PopStyleColor();

            bool deleteEntity = false;
            if(ImGui::BeginPopupContextItem(name))
            {
                if(ImGui::Selectable("Copy"))
                {
                    if(!m_Editor->IsSelected(node))
                    {
                        m_Editor->SetCopiedEntity(node);
                    }
                    for(auto entity : m_Editor->GetSelected())
                        m_Editor->SetCopiedEntity(entity);
                }

                if(ImGui::Selectable("Cut"))
                {
                    for(auto entity : m_Editor->GetSelected())
                        m_Editor->SetCopiedEntity(node, true);
                }

                if(m_Editor->GetCopiedEntity().size() > 0 && registry.valid(m_Editor->GetCopiedEntity().front()))
                {
                    if(ImGui::Selectable("Paste"))
                    {
                        for(auto entity : m_Editor->GetCopiedEntity())
                        {
                            auto scene          = Application::Get().GetSceneManager()->GetCurrentScene();
                            Entity copiedEntity = { entity, scene };
                            if(!copiedEntity.Valid())
                            {
                                m_Editor->SetCopiedEntity(entt::null);
                            }
                            else
                            {
                                scene->DuplicateEntity(copiedEntity, { node, scene });

                                if(m_Editor->GetCutCopyEntity())
                                    deleteEntity = true;
                            }
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Paste");
                }

                ImGui::Separator();

                if(ImGui::Selectable("Duplicate"))
                {
                    auto scene = Application::Get().GetSceneManager()->GetCurrentScene();
                    scene->DuplicateEntity({ node, scene });
                }
                if(ImGui::Selectable("Delete"))
                    deleteEntity = true;
                // if(m_Editor->IsSelected(node))
                //   m_Editor->UnSelect(node);
                ImGui::Separator();
                if(ImGui::Selectable("Rename"))
                    m_DoubleClicked = node;
                ImGui::Separator();

                if(ImGui::Selectable("Add Child"))
                {
                    auto scene = Application::Get().GetSceneManager()->GetCurrentScene();
                    auto child = scene->CreateEntity();
                    child.SetParent({ node, scene });
                }

                if(ImGui::Selectable("Zoom to"))
                {
                    // if(Application::Get().GetEditorState() == EditorState::Preview)
                    {
                        auto transform = registry.try_get<Maths::Transform>(node);
                        if(transform)
                            m_Editor->FocusCamera(transform->GetWorldPosition(), 2.0f, 2.0f);
                    }
                }
                ImGui::EndPopup();
            }

            if(!doubleClicked && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                auto ptr = node;
                ImGui::SetDragDropPayload("Drag_Entity", &ptr, sizeof(entt::entity*));
                ImGui::Text(ICON_MDI_ARROW_UP);
                ImGui::EndDragDropSource();
            }

            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if(payload != NULL && payload->IsDataType("Drag_Entity"))
            {
                bool acceptable;

                LUMOS_ASSERT(payload->DataSize == sizeof(entt::entity*), "Error ImGUI drag entity");
                auto entity             = *reinterpret_cast<entt::entity*>(payload->Data);
                auto hierarchyComponent = registry.try_get<Hierarchy>(entity);
                if(hierarchyComponent != nullptr)
                {
                    acceptable = entity != node && (!IsParentOfEntity(entity, node, registry)) && (hierarchyComponent->Parent() != node);
                }
                else
                    acceptable = entity != node;

                if(ImGui::BeginDragDropTarget())
                {
                    // Drop directly on to node and append to the end of it's children list.
                    if(ImGui::AcceptDragDropPayload("Drag_Entity"))
                    {
                        if(acceptable)
                        {
                            if(hierarchyComponent)
                                Hierarchy::Reparent(entity, node, registry, *hierarchyComponent);
                            else
                            {
                                registry.emplace<Hierarchy>(entity, node);
                            }
                            m_HadRecentDroppedEntity = node;
                        }
                    }

                    ImGui::EndDragDropTarget();
                }

                if(m_Editor->IsSelected(entity))
                    m_Editor->UnSelect(entity);
            }

            if(ImGui::IsItemClicked() && !deleteEntity)
                m_Editor->SetSelected(node);
            else if(m_DoubleClicked == node && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered(ImGuiHoveredFlags_None))
                m_DoubleClicked = entt::null;

            if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered(ImGuiHoveredFlags_None))
            {
                m_DoubleClicked = node;
                if(Application::Get().GetEditorState() == EditorState::Preview)
                {
                    auto transform = registry.try_get<Maths::Transform>(node);
                    if(transform)
                        m_Editor->FocusCamera(transform->GetWorldPosition(), 2.0f, 2.0f);
                }
            }

            if(deleteEntity)
            {
                DestroyEntity(node, registry);
                if(nodeOpen)
                    ImGui::TreePop();

                ImGui::PopID();
                return;
            }

            if(m_SelectUp)
            {
                if(!m_Editor->GetSelected().empty() && m_Editor->GetSelected().front() == node && registry.valid(m_CurrentPrevious))
                {
                    m_SelectUp = false;
                    m_Editor->SetSelected(m_CurrentPrevious);
                }
            }

            if(m_SelectDown)
            {
                if(!m_Editor->GetSelected().empty() && registry.valid(m_CurrentPrevious) && m_CurrentPrevious == m_Editor->GetSelected().front())
                {
                    m_SelectDown = false;
                    m_Editor->SetSelected(node);
                }
            }

            m_CurrentPrevious = node;

#if 1
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(ICON_MDI_EYE).x * 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.7f, 0.7f, 0.0f));
            if(ImGui::Button(active ? ICON_MDI_EYE : ICON_MDI_EYE_OFF))
            {
                auto& activeComponent = registry.get_or_emplace<ActiveComponent>(node);

                activeComponent.active = !active;
            }
            ImGui::PopStyleColor();
#endif

            if(nodeOpen == false)
            {
                ImGui::PopID();
                return;
            }

            const ImColor TreeLineColor = ImColor(128, 128, 128, 128);
            const float SmallOffsetX    = 6.0f * Application::Get().GetWindowDPI();
            ImDrawList* drawList        = ImGui::GetWindowDrawList();

            ImVec2 verticalLineStart = ImGui::GetCursorScreenPos();
            verticalLineStart.x += SmallOffsetX; // to nicely line up with the arrow symbol
            ImVec2 verticalLineEnd = verticalLineStart;

            if(!noChildren)
            {
                entt::entity child = hierarchyComponent->First();
                while(child != entt::null && registry.valid(child))
                {
                    float HorizontalTreeLineSize = 20.0f * Application::Get().GetWindowDPI(); // chosen arbitrarily
                    auto currentPos              = ImGui::GetCursorScreenPos();
                    ImGui::Indent(10.0f);

                    auto childHerarchyComponent = registry.try_get<Hierarchy>(child);

                    if(childHerarchyComponent)
                    {
                        entt::entity firstChild = childHerarchyComponent->First();
                        if(firstChild != entt::null && registry.valid(firstChild))
                        {
                            HorizontalTreeLineSize *= 0.1f;
                        }
                    }
                    DrawNode(child, registry);
                    ImGui::Unindent(10.0f);

                    const ImRect childRect = ImRect(currentPos, currentPos + ImVec2(0.0f, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y));

                    const float midpoint = (childRect.Min.y + childRect.Max.y) * 0.5f;
                    drawList->AddLine(ImVec2(verticalLineStart.x, midpoint), ImVec2(verticalLineStart.x + HorizontalTreeLineSize, midpoint), TreeLineColor);
                    verticalLineEnd.y = midpoint;

                    if(registry.valid(child))
                    {
                        auto hierarchyComponent = registry.try_get<Hierarchy>(child);
                        child                   = hierarchyComponent ? hierarchyComponent->Next() : entt::null;
                    }
                }
            }

            drawList->AddLine(verticalLineStart, verticalLineEnd, TreeLineColor);

            ImGui::TreePop();
            ImGui::PopID();
        }
    }

    void HierarchyPanel::DestroyEntity(entt::entity entity, entt::registry& registry)
    {
        LUMOS_PROFILE_FUNCTION();
        auto hierarchyComponent = registry.try_get<Hierarchy>(entity);
        if(hierarchyComponent)
        {
            entt::entity child = hierarchyComponent->First();
            while(child != entt::null)
            {
                auto hierarchyComponent = registry.try_get<Hierarchy>(child);
                auto next               = hierarchyComponent ? hierarchyComponent->Next() : entt::null;
                DestroyEntity(child, registry);
                child = next;
            }
        }
        registry.destroy(entity);
    }

    bool HierarchyPanel::IsParentOfEntity(entt::entity entity, entt::entity child, entt::registry& registry)
    {
        LUMOS_PROFILE_FUNCTION();
        auto nodeHierarchyComponent = registry.try_get<Hierarchy>(child);
        if(nodeHierarchyComponent)
        {
            auto parent = nodeHierarchyComponent->Parent();
            while(parent != entt::null)
            {
                if(parent == entity)
                {
                    return true;
                }
                else
                {
                    nodeHierarchyComponent = registry.try_get<Hierarchy>(parent);
                    parent                 = nodeHierarchyComponent ? nodeHierarchyComponent->Parent() : entt::null;
                }
            }
        }

        return false;
    }

    void HierarchyPanel::OnImGui()
    {
        LUMOS_PROFILE_FUNCTION();
        auto flags        = ImGuiWindowFlags_NoCollapse;
        m_CurrentPrevious = entt::null;
        m_SelectUp        = false;
        m_SelectDown      = false;

        m_SelectUp   = Input::Get().GetKeyPressed(Lumos::InputCode::Key::Up);
        m_SelectDown = Input::Get().GetKeyPressed(Lumos::InputCode::Key::Down);

        ImGui::Begin(m_Name.c_str(), &m_Active, flags);
        {
            auto scene = Application::Get().GetSceneManager()->GetCurrentScene();

            if(!scene)
            {
                ImGui::End();
                return;
            }
            auto& registry = scene->GetRegistry();

            auto AddEntity = [scene]()
            {
                if(ImGui::Selectable("Add Empty Entity"))
                {
                    scene->CreateEntity();
                }

                if(ImGui::Selectable("Add Light"))
                {
                    auto entity = scene->CreateEntity("Light");
                    entity.AddComponent<Graphics::Light>();
                    entity.GetOrAddComponent<Maths::Transform>();
                }

                if(ImGui::Selectable("Add Rigid Body"))
                {
                    auto entity = scene->CreateEntity("RigidBody");
                    entity.AddComponent<RigidBody3DComponent>();
                    entity.GetOrAddComponent<Maths::Transform>();
                    entity.AddComponent<AxisConstraintComponent>(entity, Axes::XZ);
                    entity.GetComponent<RigidBody3DComponent>().GetRigidBody()->SetCollisionShape(CollisionShapeType::CollisionCuboid);
                }

                if(ImGui::Selectable("Add Camera"))
                {
                    auto entity = scene->CreateEntity("Camera");
                    entity.AddComponent<Camera>();
                    entity.GetOrAddComponent<Maths::Transform>();
                }

                if(ImGui::Selectable("Add Sprite"))
                {
                    auto entity = scene->CreateEntity("Sprite");
                    entity.AddComponent<Graphics::Sprite>();
                    entity.GetOrAddComponent<Maths::Transform>();
                }

                if(ImGui::Selectable("Add Lua Script"))
                {
                    auto entity = scene->CreateEntity("LuaScript");
                    entity.AddComponent<LuaScriptComponent>();
                }
            };

            ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImGui::GetStyleColorVec4(ImGuiCol_TabActive));

            if(ImGui::Button(ICON_MDI_PLUS))
            {
                // Add Entity Menu
                ImGui::OpenPopup("AddEntity");
            }

            if(ImGui::BeginPopup("AddEntity"))
            {
                AddEntity();
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(ICON_MDI_MAGNIFY);
            ImGui::SameLine();

            {
                ImGuiUtilities::ScopedFont boldFont(ImGui::GetIO().Fonts->Fonts[1]);
                ImGuiUtilities::ScopedStyle frameBorder(ImGuiStyleVar_FrameBorderSize, 0.0f);
                ImGuiUtilities::ScopedColour frameColour(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
                m_HierarchyFilter.Draw("##HierarchyFilter", ImGui::GetContentRegionAvail().x - ImGui::GetStyle().IndentSpacing);
                ImGuiUtilities::DrawItemActivityOutline(2.0f, false);
            }

            if(!m_HierarchyFilter.IsActive())
            {
                ImGui::SameLine();
                ImGuiUtilities::ScopedFont boldFont(ImGui::GetIO().Fonts->Fonts[1]);
                ImGui::SetCursorPosX(ImGui::GetFontSize() * 4.0f);
                ImGuiUtilities::ScopedStyle padding(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
                ImGui::TextUnformatted("Search...");
            }

            ImGui::PopStyleColor();
            ImGui::Unindent();

            ImGui::Separator();

            ImGui::BeginChild("Nodes");

            if(ImGui::BeginPopupContextWindow())
            {
                if(!m_Editor->GetCopiedEntity().empty() && registry.valid(m_Editor->GetCopiedEntity().front()))
                {
                    if(ImGui::Selectable("Paste"))
                    {
                        for(auto entity : m_Editor->GetCopiedEntity())
                        {
                            auto scene          = Application::Get().GetSceneManager()->GetCurrentScene();
                            Entity copiedEntity = { entity, scene };
                            if(!copiedEntity.Valid())
                            {
                                m_Editor->SetCopiedEntity(entt::null);
                            }
                            else
                            {
                                scene->DuplicateEntity(copiedEntity);

                                if(m_Editor->GetCutCopyEntity())
                                {
                                    DestroyEntity(entity, registry);
                                }
                            }
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Paste");
                }

                ImGui::Separator();

                AddEntity();

                ImGui::EndPopup();
            }
            {
                if(ImGui::BeginDragDropTarget())
                {
                    if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Drag_Entity"))
                    {
                        LUMOS_ASSERT(payload->DataSize == sizeof(entt::entity*), "Error ImGUI drag entity");
                        auto entity             = *reinterpret_cast<entt::entity*>(payload->Data);
                        auto hierarchyComponent = registry.try_get<Hierarchy>(entity);
                        if(hierarchyComponent)
                        {
                            Hierarchy::Reparent(entity, entt::null, registry, *hierarchyComponent);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                //                auto draw_list = ImGui::GetWindowDrawList();
                //                auto availSize = ImGui::GetContentRegionAvail();
                //                ImVec2 min = ImGui::GetCursorScreenPos() + ImVec2(ImGui::GetStyle().ItemSpacing.x, ImGui::GetScrollY());
                //                draw_list->AddRectFilled(min, min + availSize, ImGui::ColorConvertFloat4ToU32(ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_FrameBg)) - ImVec4(0.15f, 0.15f, 0.15f, 0.0f)));

                ImGui::Indent();

                // ImGuiUtilities::AlternatingRowsBackground(ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y);

                for(auto [entity] : registry.storage<entt::entity>().each())
                {
                    if(registry.valid(entity))
                    {
                        auto hierarchyComponent = registry.try_get<Hierarchy>(entity);

                        if(!hierarchyComponent || hierarchyComponent->Parent() == entt::null)
                            DrawNode(entity, registry);
                    }
                }

                // Only supports one scene
                ImVec2 min_space = ImGui::GetWindowContentRegionMin();
                ImVec2 max_space = ImGui::GetWindowContentRegionMax();

                float yOffset = Maths::Max(45.0f, ImGui::GetScrollY()); // Dont include search bar
                min_space.x += ImGui::GetWindowPos().x + 1.0f;
                min_space.y += ImGui::GetWindowPos().y + 1.0f + yOffset;
                max_space.x += ImGui::GetWindowPos().x - 1.0f;
                max_space.y += ImGui::GetWindowPos().y - 1.0f + ImGui::GetScrollY();
                ImRect bb { min_space, max_space };

                const ImGuiPayload* payload = ImGui::GetDragDropPayload();
                if(payload != NULL && payload->IsDataType("Drag_Entity"))
                {
                    bool acceptable = false;

                    LUMOS_ASSERT(payload->DataSize == sizeof(entt::entity*), "Error ImGUI drag entity");
                    auto entity             = *reinterpret_cast<entt::entity*>(payload->Data);
                    auto hierarchyComponent = registry.try_get<Hierarchy>(entity);
                    if(hierarchyComponent)
                    {
                        acceptable = hierarchyComponent->Parent() != entt::null;
                    }

                    if(acceptable && ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("Panel Hierarchy")))
                    {
                        if(const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Drag_Entity"))
                        {
                            LUMOS_ASSERT(payload->DataSize == sizeof(entt::entity*), "Error ImGUI drag entity");
                            auto entity             = *reinterpret_cast<entt::entity*>(payload->Data);
                            auto hierarchyComponent = registry.try_get<Hierarchy>(entity);
                            if(hierarchyComponent)
                            {
                                Hierarchy::Reparent(entity, entt::null, registry, *hierarchyComponent);
                                Entity e(entity, scene);
                                e.RemoveComponent<Hierarchy>();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
}
