#include "Precompiled.h"
#include "B2PhysicsEngine.h"
#include "RigidBody2D.h"

#include "Utilities/TimeStep.h"

#include "Scene/Component/RigidBody2DComponent.h"
#include "Scene/Scene.h"

#include "Maths/Transform.h"
#include "B2DebugDraw.h"

#include <box2d/box2d.h>
#include <box2d/b2_math.h>

#include <imgui/imgui.h>

namespace Lumos
{
    B2PhysicsEngine::B2PhysicsEngine()
        : m_B2DWorld(CreateUniquePtr<b2World>(b2Vec2(0.0f, -9.81f)))
        , m_DebugDraw(CreateUniquePtr<B2DebugDraw>())
        , m_UpdateTimestep(1.0f / 60.f)
        , m_Listener(nullptr)
        , m_Paused(false)
    {
        m_DebugName = "Box2D Physics Engine";
        m_B2DWorld->SetDebugDraw(m_DebugDraw.get());

        uint32 flags = 0;
        // flags += b2Draw::e_shapeBit;
        // flags += b2Draw::e_jointBit;
        // flags += b2Draw::e_aabbBit;
        // flags += b2Draw::e_centerOfMassBit;
        // flags += b2Draw::e_pairBit;

        m_DebugDraw->SetFlags(flags);
    }

    B2PhysicsEngine::~B2PhysicsEngine()
    {
        if(m_Listener)
            delete m_Listener;
    }

    void B2PhysicsEngine::SetDefaults()
    {
        m_UpdateTimestep = 1.0f / 60.f;
    }

    void B2PhysicsEngine::OnUpdate(const TimeStep& timeStep, Scene* scene)
    {
        LUMOS_PROFILE_FUNCTION();

        if(!m_Paused)
        {
            m_B2DWorld->Step((float)timeStep.GetSeconds(), m_VelocityIterations, m_PositionIterations);
        }
    }

    void B2PhysicsEngine::OnImGui()
    {
        ImGui::TextUnformatted("2D Physics Engine");

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
        ImGui::Columns(2);
        ImGui::Separator();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Number Of Collision Pairs");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1);
        ImGui::Text("%5.2i", m_B2DWorld->GetContactCount());
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Number Of Rigid Bodys");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1);
        ImGui::Text("%5.2i", m_B2DWorld->GetBodyCount());
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Paused");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1);
        ImGui::Checkbox("##Paused", &m_Paused);
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Gravity");
        ImGui::NextColumn();
        ImGui::PushItemWidth(-1);
        float grav[2] = { m_B2DWorld->GetGravity().x, m_B2DWorld->GetGravity().y };
        if(ImGui::InputFloat2("##Gravity", grav))
            m_B2DWorld->SetGravity({ grav[0], grav[1] });
        ImGui::PopItemWidth();
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::PopStyleVar();
    }

    b2Body* B2PhysicsEngine::CreateB2Body(b2BodyDef* bodyDef) const
    {
        return m_B2DWorld->CreateBody(bodyDef);
    }

    void B2PhysicsEngine::CreateFixture(b2Body* body, const b2FixtureDef* fixtureDef)
    {
        body->CreateFixture(fixtureDef);
    }

    void B2PhysicsEngine::OnDebugDraw()
    {
        m_B2DWorld->DebugDraw();
    }

    void B2PhysicsEngine::SetDebugDrawFlags(uint32_t flags)
    {
        m_DebugDraw->SetFlags(flags);
    }

    void B2PhysicsEngine::SetGravity(const glm::vec2& gravity)
    {
        m_B2DWorld->SetGravity({ gravity.x, gravity.y });
    }

    uint32_t B2PhysicsEngine::GetDebugDrawFlags()
    {
        return m_DebugDraw->GetFlags();
    }

    void B2PhysicsEngine::SetContactListener(b2ContactListener* listener)
    {
        if(m_Listener)
            delete m_Listener;

        m_Listener = listener;
        m_B2DWorld->SetContactListener(listener);
    }

    void B2PhysicsEngine::SyncTransforms(Scene* scene)
    {
        if(m_Paused)
            return;

        if(!scene)
            return;

        auto& registry = scene->GetRegistry();

        auto group = registry.group<RigidBody2DComponent>(entt::get<Maths::Transform>);

        for(auto entity : group)
        {
            const auto& [phys, trans] = group.get<RigidBody2DComponent, Maths::Transform>(entity);

            // if (!phys.GetRigidBody()->GetB2Body()->IsAwake())
            //     break;

            trans.SetLocalPosition(glm::vec3(phys.GetRigidBody()->GetPosition(), trans.GetLocalPosition().z));
            trans.SetLocalOrientation(glm::quat(glm::vec3(0.0f, 0.0f, phys.GetRigidBody()->GetAngle())));
            trans.SetWorldMatrix(glm::mat4(1.0f)); // TODO: temp
        };
    }
}
