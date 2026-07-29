//
// Created by herzog on 15.07.26.
//

#ifndef STRUCTURASYSTEMSSERVER_PROJECT_H
#define STRUCTURASYSTEMSSERVER_PROJECT_H

#include <nlohmann/json.hpp>
#include <sysmlv2/rest/entities/Project.h>

namespace StructuraSystems::Server {
    class Project : public SysMLv2::REST::Project {
    public:
        Project() = default;

        explicit Project(SysMLv2::REST::Project &other)
            : SysMLv2::REST::Project(other) {
        }

        explicit Project(const std::string &JsonString)
            : SysMLv2::REST::Project(JsonString) {
            const auto jsonElements = nlohmann::json::parse(JsonString);
            
            if(jsonElements.contains("owner"))
                Owner = jsonElements["owner"];
            
            if(jsonElements.contains("owningGroup"))
                OwningGroup = jsonElements["owningGroup"];

        }

        Project(const std::string &projectName, const std::string &projectDescription, const std::string &branchName)
            : SysMLv2::REST::Project(projectName, projectDescription, branchName) {
        }

        Project(const std::string &projectName, const std::string &projectDescription, const std::string &branchName, std::string owner, std::string owningGroup)
            : SysMLv2::REST::Project(projectName, projectDescription, branchName) {
            Owner = owner;
            OwningGroup = owningGroup;
        }

        std::string getOwner() {
            return Owner;
        }

        std::string getOwningGroup() {
            return OwningGroup;
        }

        std::string getDataBaseString() {
            auto json = nlohmann::json::parse(SysMLv2::REST::Project::serializeToJson());
            if(!Owner.empty())
                json["owner"] = Owner;
            if(!OwningGroup.empty())
                json["owningGroup"] = OwningGroup;
            
            return json.dump(JSON_INTENT);;
        }

    private:
        std::string Owner = "";
        std::string OwningGroup = "";
    };
} // StructuraSystems::Server

#endif //STRUCTURASYSTEMSSERVER_PROJECT_H
