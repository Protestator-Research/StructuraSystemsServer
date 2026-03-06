//
// Created by Moritz Herzog on 05.03.26.
//

#pragma once

#include <version.h>
#include <string>
#include "../Entities/db/Version.hpp"

namespace StructuraSystems::Server {
    class VersionController {
    public:
        static std::shared_ptr<VersionController> getInstance();

        virtual ~VersionController() = default;

        bool isOtherVersionLower(std::string version);
        bool areOtherVersionPropertiesLower(Version properties);

        [[nodiscard]] std::string getVersion() const;
        [[nodiscard]] std::string getCommitHash() const;
        [[nodiscard]] std::string getBuildNumber() const;

        [[nodiscard]] std::string getVersionBuildString();

        Version getVersionElement() const;

    private:
        VersionController() = default;

        const std::string VersionString = VERSION;
        const std::string CommitHash = COMMIT_HASH;
        const std::string BuildNumber = BUILD_NUMBER;

        static std::shared_ptr<VersionController> Instance;
    };
}