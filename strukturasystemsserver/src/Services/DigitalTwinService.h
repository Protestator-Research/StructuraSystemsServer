//
// Created by Moritz Herzog on 24.02.26.
//

#pragma once

#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Commit.h>

#include <vector>
#include <map>
#include <memory>
#include <boost/uuid/uuid.hpp>

#include "../Entities/json/TwinResponse.h"
#include "../Controller/DataBaseController.h"

namespace StructuraSystems::Server {
    class DigitalTwinService {
    public:
        static std::shared_ptr<DigitalTwinService> getInstance();

        virtual ~DigitalTwinService() = default;

        std::shared_ptr<TwinResponse> createTwin(std::string name, std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit);

        std::vector<std::shared_ptr<TwinResponse>> getAllTwinsForProject(std::shared_ptr<SysMLv2::REST::Project> project);


    private:
        DigitalTwinService();

        static std::shared_ptr<DigitalTwinService> Instance;

        std::map<boost::uuids::uuid, std::vector<std::shared_ptr<TwinResponse>>> ProjectIdTwinMap;
        DataBaseController* DBController;
    };
}
