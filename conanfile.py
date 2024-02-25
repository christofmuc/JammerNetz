from conan import ConanFile
from conan.tools.cmake import CMake


class JammerNetzConan(ConanFile):
    settings = ["os", "compiler", "build_type", "arch"]
    requires = ["pdcurses/3.9", "nlohmann_json/3.11.3"]
    generators = "CMakeDeps", "CMakeToolchain"
