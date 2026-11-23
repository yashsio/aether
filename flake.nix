{
  description = "aether dev env";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system}; in
        {
          default = pkgs.mkShell {
            name = "aether-dev";
            packages = with pkgs; [
              gcc
              cmake
              pkg-config
              wayland
              wayland-scanner
              wayland-protocols
              libxkbcommon
              libffi
              libwebp
              clang-tools
              gdb
            ];
            shellHook = ''
              echo "ready: cmake -B build && cmake --build build"
            '';
          };
        });

      packages = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system}; in
        {
          default = pkgs.stdenv.mkDerivation {
            pname = "aether";
            version = "0.2.0";
            src = ./.;
            nativeBuildInputs = with pkgs; [ cmake pkg-config wayland wayland-scanner wayland-protocols ];
            buildInputs = with pkgs; [ wayland libxkbcommon libffi libwebp ];
            cmakeBuildType = "Release";
            # FetchContent needs the network, which the Nix sandbox lacks.
            cmakeFlags = [ "-DAETHER_WEBP_SYSTEM=ON" ];
          };
        });
    };
}
