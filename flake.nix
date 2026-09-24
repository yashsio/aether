{
  description = "Aether dev env";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];

      mkDevShell = system:
        let
          pkgs = import nixpkgs { inherit system; };
          qt = pkgs.qt6;
        in
        pkgs.mkShell {
          name = "aether-dev";

          packages = with pkgs; [
            gcc
            cmake
            ninja
            pkg-config
            qt.qtbase
            clang-tools
            gdb
          ] ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isLinux [ qt.qtwayland ];

          shellHook = ''
            export QT_PLUGIN_PATH="${qt.qtbase}/${qt.qtbase.qtPluginPrefix}"
            ${pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isLinux ''
              export QT_PLUGIN_PATH="${qt.qtwayland}/${qt.qtbase.qtPluginPrefix}:$QT_PLUGIN_PATH"
            ''}
            echo "ready: cmake -B build && cmake --build build"
          '';
        };
    in
    {
      devShells = builtins.listToAttrs (
        map (s: { name = s; value = { default = mkDevShell s; }; }) systems
      );
    };
}
