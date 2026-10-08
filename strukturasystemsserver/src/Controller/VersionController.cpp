//
// Created by Moritz Herzog on 05.03.26.
//

#include "VersionController.h"

#include <boost/algorithm/string.hpp>

namespace StructuraSystems::Server {
    std::shared_ptr<VersionController> VersionController::getInstance() {
        struct versionControllerImplementation : public VersionController {};
        // A function-local static is initialised exactly once, even with concurrent first calls.
        static const std::shared_ptr<VersionController> instance = std::make_shared<versionControllerImplementation>();
        return instance;
    }

    bool VersionController::isOtherVersionLower(std::string version) {
        std::vector<std::string> versionPartsOther;
        boost::algorithm::split(versionPartsOther, version, boost::is_any_of("."));
        std::vector<std::string> VersionParts;
        boost::algorithm::split(VersionParts, VersionString, boost::is_any_of("."));

        return (std::stoi(VersionParts.at(0))>std::stoi(versionPartsOther.at(0))
            || std::stoi(VersionParts.at(1))>std::stoi(versionPartsOther.at(1))
            || std::stoi(VersionParts.at(3))>std::stoi(versionPartsOther.at(3)));
    }

    bool VersionController::areOtherVersionPropertiesLower(Version) {
        return false;
    }

    std::string VersionController::getVersion() const {
        return VersionString;
    }

    std::string VersionController::getCommitHash() const {
        return CommitHash;
    }

    std::string VersionController::getBuildNumber() const {
        return BuildNumber;
    }

    std::string VersionController::getVersionBuildString() {
        return VersionString + "+" + BuildNumber + "+" + CommitHash;
    }

    Version VersionController::getVersionElement() const {
        return Version(VersionString, BuildNumber, CommitHash);
    }
}

