{
  description = "Logos Blog — decentralized blogging IComponent plugin";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nixpkgs.follows           = "logos-module-builder/nixpkgs";
    logos-cpp-sdk = {
      url    = "github:logos-co/logos-cpp-sdk";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    logos-liblogos = {
      url    = "github:logos-co/logos-liblogos";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    };
  };

  outputs = { self, logos-module-builder, nixpkgs, logos-cpp-sdk, logos-liblogos, ... }:
    let
      moduleOutputs = logos-module-builder.lib.mkLogosModule {
        src        = ./.;
        configFile = ./module.yaml;
      };

      forAllSystems = f: nixpkgs.lib.genAttrs
        [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ]
        (system: f {
          pkgs          = import nixpkgs { inherit system; };
          logosSdk      = logos-cpp-sdk.packages.${system}.default;
          logosLiblogos = logos-liblogos.packages.${system}.default;
        });

    in moduleOutputs // {
      packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos }:
        let
          base = moduleOutputs.packages.${pkgs.system} or {};

          commonCmakeFlags = [
            "-DLOGOS_CPP_SDK_ROOT=${logosSdk}"
            "-DLOGOS_LIBLOGOS_ROOT=${logosLiblogos}"
          ];

          blog-module = pkgs.stdenv.mkDerivation {
            pname   = "blog-module";
            version = "0.1.0";
            src     = ./.;
            nativeBuildInputs = with pkgs; [ cmake ninja ];
            buildInputs = with pkgs.qt6; [ qtbase qtdeclarative qtremoteobjects ] ++ [ pkgs.openssl ];
            cmakeFlags  = commonCmakeFlags ++ [ "-DBUILD_MODULE=ON" ];
            buildPhase  = "cmake --build . --target blog_module_plugin -j$NIX_BUILD_CORES";
            installPhase = ''
              mkdir -p $out/lib
              cp blog_module_plugin.so $out/lib/
              cp ${self}/metadata.json $out/lib/manifest.json
            '';
            dontWrapQtApps = true;
          };

          blog-ui = pkgs.stdenv.mkDerivation {
            pname   = "blog-ui";
            version = "0.1.0";
            src     = ./.;
            nativeBuildInputs = with pkgs; [ cmake ninja pkgs.qt6.wrapQtAppsHook ];
            buildInputs = with pkgs.qt6; [
              qtbase qtdeclarative qtremoteobjects ] ++ [ pkgs.openssl
            ];
            cmakeFlags  = commonCmakeFlags ++ [ "-DBUILD_UI_PLUGIN=ON" ];
            buildPhase  = "cmake --build . --target blog_ui -j$NIX_BUILD_CORES";
            installPhase = ''
              mkdir -p $out/lib
              cp blog_ui.so $out/lib/
              cp ${self}/ui_metadata.json $out/lib/manifest.json
            '';
            dontWrapQtApps = true;
          };

        in base // { inherit blog-module blog-ui; }
      );
    };
}
