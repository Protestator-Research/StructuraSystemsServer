//
// Created by Moritz Herzog on 24.02.26.
//

#include "DigitalTwinService.h"

#include <future>
#include <mutex>
#include <shared_mutex>

#include "../Controller/DataBaseController.h"

namespace StructuraSystems::Server {
    std::shared_ptr<DigitalTwinService> DigitalTwinService::getInstance() {
        struct concreteDigitalTwinService : public DigitalTwinService {};
        // A function-local static is initialised exactly once, even with concurrent first calls.
        static const std::shared_ptr<DigitalTwinService> instance = std::make_shared<concreteDigitalTwinService>();
        return instance;
    }

    std::shared_ptr<TwinResponse> DigitalTwinService::createTwin(std::string name, std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit) {
        const auto twin = std::make_shared<TwinResponse>(name, project, commit);

        std::unique_lock lock(Mutex);

        ProjectIdTwinMap[project->getId()].push_back(twin);
        DBController->addTwin(project->getId(),twin);

        return twin;
    }

    std::vector<std::shared_ptr<TwinResponse>> DigitalTwinService::getAllTwinsForProject(std::shared_ptr<SysMLv2::REST::Project> project) {
        std::shared_lock lock(Mutex);
        const auto twins = ProjectIdTwinMap.find(project->getId());
        if (twins == ProjectIdTwinMap.end())
            return {};
        return twins->second;
    }

    DigitalTwinService::DigitalTwinService() {
        DBController = DataBaseController::getInstance();
        ProjectIdTwinMap = DBController->getAllTwins();
    }
}
