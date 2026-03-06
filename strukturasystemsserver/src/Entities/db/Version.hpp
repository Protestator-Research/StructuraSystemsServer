//
// Created by Moritz Herzog on 05.03.26.
//

#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace StructuraSystems::Server {
    class Version {
    public:
        Version(std::string jsonString) {
            parseAndDecorate(jsonString);
        }

        Version(std::string versionString, std::string buildNumber, std::string commitHash) {
            VersionString = versionString;
            BuildNumber = buildNumber;
            CommitHash = commitHash;
        }

        virtual ~Version() = default;

        std::string getVersionString() const {
            return VersionString;
        }

        std::string getBuildNumber() const {
            return BuildNumber;
        }

        std::string getCommitHash() const {
            return CommitHash;
        }

        std::string serialiseJson() const {
            nlohmann::json json;
            json["_id"] = CommitHash;
            json["version"] = VersionString;
            json["build"] = BuildNumber;
            return json.dump(2);
        }

    private:
        void parseAndDecorate(std::string jsonString) {
            nlohmann::json json = nlohmann::json::parse(jsonString);
            VersionString = json["version"];
            BuildNumber = json["build"];
            CommitHash = json["_id"];
        }

        std::string VersionString;
        std::string BuildNumber;
        std::string CommitHash;
    };
}
