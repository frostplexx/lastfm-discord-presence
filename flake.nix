{
  description = "lastfm-discord-presence build environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            cmake
            gcc14
            curl.dev
            nlohmann_json
          ];

          shellHook = ''
            echo "=== lastfm-discord-presence dev shell ==="
            echo "cmake:  $(cmake --version | head -1)"
            echo "g++:    $(g++ --version | head -1)"
            echo "curl:   $(curl-config --version)"
            echo ""
          '';
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "lastfm-discord-presence";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [ cmake gcc14 ];

          buildInputs = with pkgs; [ curl.dev nlohmann_json ];

          # The Discord Social SDK must be placed in lib/ before building.
          # It isn't in nixpkgs.
          preConfigure = ''
            if [ ! -d lib/discord_social_sdk ]; then
              echo "ERROR: lib/discord_social_sdk/ not found."
              echo "Download the Discord Social SDK C++ archive and unpack it into lib/."
              exit 1
            fi
          '';

          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
        };
      }
    );
}
