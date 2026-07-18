{
  description = "Interactive Wayland wallpaper engine using OpenGL GLESv2 shaders";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
  let
    supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
    
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
  in
  {
    packages = forAllSystems (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        default = pkgs.stdenv.mkDerivation {
          pname = "shader-desk";
          version = "git"; 

          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            wayland-scanner 
            python3         
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            libGL          
            luajit
            nlohmann_json
            glm
            sol2           
            libpulseaudio
            fftw
            libevdev
            mpv
          ];

          cmakeFlags = [
            "-DBUILD_AUDIO_DAEMON=ON"
            "-DBUILD_EVDEV_DAEMON=ON"
            "-DENABLE_PROFILING=OFF"
          ];

        };
      });

    apps = forAllSystems (system: {
      default = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/shader-desk-run";
      };
    });
  };
}
