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
            makeWrapper # Required to wrap binaries with correct library paths
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

          # Fix for NixOS dynamically loaded OpenGL/EGL drivers and dlopen plugins
          # We wrap the main executables to expose standard NixOS driver paths.
          postInstall = ''
            for prog in interactive-wallpaper shader-desk-run; do
              if [ -f "$out/bin/$prog" ]; then
                wrapProgram "$out/bin/$prog" \
                  --prefix LD_LIBRARY_PATH : "/run/opengl-driver/lib:/run/opengl-driver-32/lib:${pkgs.lib.makeLibraryPath [ pkgs.libGL pkgs.mpv pkgs.wayland ]}"
              fi
            done
          '';

          meta = with pkgs.lib; {
            description = "Interactive Wayland wallpaper engine";
            homepage = "https://github.com/KMartianov/shader-desk";
            license = licenses.mit;
            platforms = platforms.linux;
            mainProgram = "shader-desk-run";
          };
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
