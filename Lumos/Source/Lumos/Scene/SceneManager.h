#pragma once

namespace Lumos
{
    class Scene;

    class LUMOS_EXPORT SceneManager
    {
    public:
        SceneManager();
        virtual ~SceneManager();

        // Jump to the next scene in the list or first scene if at the end
        void SwitchScene();

        // Jump to scene index (stored in order they were originally added starting at zero)
        void SwitchScene(int idx);

        // Jump to scene name
        void SwitchScene(const std::string& name);

        void ApplySceneSwitch();

        // Get currently active scene (returns NULL if no scenes yet added)
        inline Scene* GetCurrentScene() const
        {
            return m_CurrentScene;
        }

        // Get currently active scene's index (return 0 if no scenes yet added)
        inline uint32_t GetCurrentSceneIndex() const
        {
            return m_SceneIdx;
        }

        // Get total number of enqueued scenes
        inline uint32_t SceneCount() const
        {
            return static_cast<uint32_t>(m_vpAllScenes.size());
        }

        std::vector<std::string> GetSceneNames();
        const std::vector<SharedPtr<Scene>>& GetScenes() const
        {
            return m_vpAllScenes;
        }

        void SetSwitchScene(bool switching)
        {
            m_SwitchingScenes = switching;
        }
        bool GetSwitchingScene() const
        {
            return m_SwitchingScenes;
        }

        int EnqueueSceneFromFile(const std::string& filePath);
        void EnqueueScene(Scene* scene);

        template <class T>
        void EnqueueScene(const std::string& name)
        {
            // T* scene = new T(name);
            m_vpAllScenes.emplace_back(CreateSharedPtr<T>(name));
            LUMOS_LOG_INFO("[SceneManager] - Enqueued scene : {0}", name.c_str());
        }

        const std::vector<std::string>& GetSceneFilePaths();

        void AddFileToLoadList(const std::string& filePath)
        {
            m_SceneFilePathsToLoad.push_back(filePath);
        }

        void LoadCurrentList();

    protected:
        uint32_t m_SceneIdx;
        Scene* m_CurrentScene;
        std::vector<SharedPtr<Scene>> m_vpAllScenes;
        std::vector<std::string> m_SceneFilePaths;
        std::vector<std::string> m_SceneFilePathsToLoad;

    private:
        bool m_SwitchingScenes = false;
        int m_QueuedSceneIndex = -1;
        NONCOPYABLE(SceneManager)
    };
}
