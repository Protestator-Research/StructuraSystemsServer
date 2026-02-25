//
// Created by Moritz Herzog on 24.02.26.
//

#include "DigitalTwinService.h"

#include <future>

namespace StructuraSystems::Server {
    std::shared_ptr<DigitalTwinService> DigitalTwinService::Instance = nullptr;

    std::shared_ptr<DigitalTwinService> DigitalTwinService::getInstance() {
        struct concreteDigitalTwinService : public DigitalTwinService {};
        if (Instance == nullptr) {
            Instance = std::make_shared<concreteDigitalTwinService>();
        }
        return Instance;
    }

    std::shared_ptr<TwinResponse> DigitalTwinService::createTwin(std::string name, std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit) {
        const auto twin = std::make_shared<TwinResponse>(name, project, commit);

        if (ProjectIdTwinMap.find(project->getId()) != ProjectIdTwinMap.end())
            ProjectIdTwinMap[project->getId()] = std::vector<std::shared_ptr<TwinResponse>>();

        ProjectIdTwinMap[project->getId()].push_back(twin);

        return twin;
    }

    std::vector<std::shared_ptr<TwinResponse>> DigitalTwinService::getAllTwinsForProject(std::shared_ptr<SysMLv2::REST::Project> project) {
        return ProjectIdTwinMap[project->getId()];
    }
}
