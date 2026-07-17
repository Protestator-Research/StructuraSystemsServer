from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.env import Environment
from conan.tools.apple import XcodeDeps
import os
from sys import platform


class CppStructuraSystemsRecipe(ConanFile):
    name = "structura-systems-server"
    package_type = "application"

    # Optional metadata
    license = "GPL-3.0"
    author = "Moritz Herzog <moritz.herzog@protestator-research.com>"
    url = "https://github.com/Protestator-Research/StructuraSystemsServer"
    description = "<Description of digitwester package here>"
    topics = ("sysml v2", "rest server", "sysml")

    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": False}

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "CMakeLists.txt", "*"

    def requirements(self):
        self.requires("boost/[>=1.89.0 <2]")
        self.requires("libcurl/[>=8.4.0 <9]")
        self.requires("nlohmann_json/[>=3.11.3 <3.13]")
        self.requires("sysmllib/2607beta")
        self.requires("openssl/3.6.3")
        self.requires("mongo-cxx-driver/4.1.4")
        self.requires("libsodium/1.0.20")
        self.requires("drogon/1.9.13")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
            self.options.shared=True

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
            self.options["boost/*"].shared = True
            self.options["libcurl/*"].shared = True
            self.options["nlohmann_json/*"].shared = True
            self.options["sysmllib/*"].shared=True
            self.options["openssl/*"].shared=True
            self.options["mongo-cxx-driver/*"].shared=True
            self.options["libsodium/*"].shared=False
            self.options["pistache/*"].shared=False
        else:
            self.options["boost/*"].shared = False
            self.options["libcurl/*"].shared = False
            self.options["nlohmann_json/*"].shared = False
            self.options["sysmllib/*"].shared=False
            self.options["openssl/*"].shared=False
            self.options["mongo-cxx-driver/*"].shared=False
            self.options["libsodium/*"].shared=False
            self.options["pistache/*"].shared=False

    def layout(self):
        cmake_layout(self)

    def generate(self):
        build_commit = self.conf.get("user.build:commit", default="none")
        build_number = self.conf.get("user.build:number", default="none")

        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)

        tc.user_presets_path = 'CMakePresets.json'
        tc.variables["BUILD_VERSION"] = self.version
        tc.variables["BUILD_COMMIT"] = build_commit
        tc.variables["BUILD_NUMBER"] = build_number

        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.30.0 <5]")

    def package(self):
        cmake = CMake(self)
        cmake.install()

    

    
