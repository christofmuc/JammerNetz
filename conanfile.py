from conan import ConanFile
class JammerNetzConan(ConanFile):
    settings = ["os", "compiler", "build_type", "arch"]
    default_options = {
        "libsodium/*:shared": False,
    }
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("libsodium/1.0.22")
        if str(self.settings.os) == "Windows":
            self.requires("pdcurses/3.9")
