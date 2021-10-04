from conans import ConanFile, CMake


class JammerNetzConan(ConanFile):
    settings = ["os", "compiler", "build_type", "arch"]
    requires = ["nlohmann_json/3.9.1", "cpprestsdk/2.10.18", "keychain/1.2.0"]
    generators = "cmake", "cmake_find_package"

    def configure(self):
        if self.settings.os == "Windows":
            self.requires.add("pdcurses/3.9")
