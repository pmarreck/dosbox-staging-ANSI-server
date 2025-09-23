{
  description = "Dev shell for DOSBox-Staging ANSI server work";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;
        pythonForDocs = pkgs.python312;
        mkdocsGlightbox = pythonForDocs.pkgs.buildPythonPackage {
          pname = "mkdocs-glightbox";
          version = "0.4.0";
          src = pkgs.fetchurl {
            url = "https://files.pythonhosted.org/packages/source/m/mkdocs-glightbox/mkdocs-glightbox-0.4.0.tar.gz";
            hash = "sha256-OSs0IHv5WZEHGhbV+JFtHS8s1dW7Wa4pl0hczXeMcNk=";
          };
          propagatedBuildInputs = [ pythonForDocs.pkgs.mkdocs ];
        };

        mkdocsRedirects = pythonForDocs.pkgs.buildPythonPackage {
          pname = "mkdocs-redirects";
          version = "1.2.0";
          src = pkgs.fetchurl {
            url = "https://files.pythonhosted.org/packages/source/m/mkdocs-redirects/mkdocs-redirects-1.2.0.tar.gz";
            hash = "sha256-3dOCZ9Sf36GfsvJbSu0vtT8ElsgYvzAYAJyOr2Z2oyc=";
          };
          propagatedBuildInputs = [ pythonForDocs.pkgs.mkdocs ];
        };

        mkdocsMdxGhLinks = pythonForDocs.pkgs.buildPythonPackage {
          pname = "mdx-gh-links";
          version = "0.4";
          format = "wheel";
          src = pkgs.fetchurl {
            url = "https://files.pythonhosted.org/packages/py3/m/mdx-gh-links/mdx_gh_links-0.4-py3-none-any.whl";
            hash = "sha256-kFe8ofpSgL8fy/NUOB5GySYcwywtXAQHgB+KkQvl8Jk=";
          };
          propagatedBuildInputs = [ pythonForDocs.pkgs.markdown ];
        };
        pythonWithMkdocs = pythonForDocs.withPackages (ps: [
          ps.mkdocs
          ps.mkdocs-material
          mkdocsGlightbox
          mkdocsRedirects
          mkdocsMdxGhLinks
        ]);
        runtimeLibs = with pkgs; [
          SDL2
          SDL2_net
          libpng
          zlib-ng
          opusfile
          fluidsynth
          libslirp
          speexdsp
          iir1
          libmt32emu
          xorg.libXi
          alsaLib
          libogg
          libvorbis
        ];
      in {
        devShells.default = pkgs.mkShell {
          name = "dosbox-staging-dev";
          packages = with pkgs; [
            meson
            ninja
            pkg-config
            ccache
            cmake
            gdb
            pythonWithMkdocs
            gcc
          ] ++ runtimeLibs;

          shellHook = (
            let
              pkgConfigPath = lib.makeSearchPath "lib/pkgconfig" runtimeLibs;
              ldLibraryPath = lib.makeLibraryPath runtimeLibs;
            in ''
              export PKG_CONFIG_PATH=${pkgConfigPath}:$PKG_CONFIG_PATH
              export LD_LIBRARY_PATH=${ldLibraryPath}:$LD_LIBRARY_PATH
              echo "🔧 DOSBox-Staging dev shell (Meson/Ninja, SDL2, libpng, opusfile, fluidsynth, speexdsp, libslirp, etc.)"
            ''
          );
        };
      }
    );
}
