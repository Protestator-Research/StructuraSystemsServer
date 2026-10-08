//
// Created by herzog on 15.07.26.
//

#ifndef STRUCTURASYSTEMSSERVER_PROJECT_H
#define STRUCTURASYSTEMSSERVER_PROJECT_H

#include <nlohmann/json.hpp>
#include <mutex>
#include <string>
#include <utility>
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

            if(jsonElements.contains("isLibrary") && jsonElements["isLibrary"].is_boolean())
                IsLibrary = jsonElements["isLibrary"].get<bool>();
        }

        Project(const std::string &projectName, const std::string &projectDescription, const std::string &branchName)
            : SysMLv2::REST::Project(projectName, projectDescription, branchName) {
        }

        Project(const std::string &projectName, const std::string &projectDescription, const std::string &branchName, const std::string& owner, const std::string& owningGroup)
            : SysMLv2::REST::Project(projectName, projectDescription, branchName) {
            Owner = owner;
            OwningGroup = owningGroup;
        }

        // Owner and owning group can change while other threads check access, so they are
        // guarded by a small per-object mutex. It is a leaf lock: nothing else is taken while holding it.
        std::string getOwner() const {
            std::lock_guard lock(OwnershipMutex);
            return Owner;
        }

        std::string getOwningGroup() const {
            std::lock_guard lock(OwnershipMutex);
            return OwningGroup;
        }

        void setOwner(std::string owner) {
            std::lock_guard lock(OwnershipMutex);
            Owner = std::move(owner);
        }

        void setOwningGroup(std::string owningGroup) {
            std::lock_guard lock(OwnershipMutex);
            OwningGroup = std::move(owningGroup);
        }

        /// Sets both values atomically, so readers never see a mix of old and new.
        void setOwnership(std::string owner, std::string owningGroup) {
            std::lock_guard lock(OwnershipMutex);
            Owner = std::move(owner);
            OwningGroup = std::move(owningGroup);
        }

        bool isLibrary() const {
            return IsLibrary;
        }

        void setIsLibrary(bool isLibrary) {
            IsLibrary = isLibrary;
        }

        std::string getDataBaseString() {
            auto json = nlohmann::json::parse(SysMLv2::REST::Project::serializeToJson());
            {
                std::lock_guard lock(OwnershipMutex);
                if(!Owner.empty())
                    json["owner"] = Owner;
                if(!OwningGroup.empty())
                    json["owningGroup"] = OwningGroup;
            }
            if(IsLibrary)
                json["isLibrary"] = true;
            
            return json.dump(JSON_INTENT);;
        }

    private:
        mutable std::mutex OwnershipMutex;
        std::string Owner = "";
        std::string OwningGroup = "";
        bool IsLibrary = false;
    };
} // StructuraSystems::Server

#endif //STRUCTURASYSTEMSSERVER_PROJECT_H
