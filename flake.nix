{
  description = "Renesas RA8M2 (Cortex-M85) dev shell: ATfE LLVM toolchain + J-Link/pyOCD/OpenOCD/probe-rs";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config = {
          allowUnfree = true;                 # segger-jlink
          segger-jlink.acceptLicense = true;
          # segger-jlink GUI tools link an old Qt4 that nixpkgs flags as insecure; we only use the CLI tools.
          permittedInsecurePackages = [ "segger-jlink-qt4-952" ];
        };
      };

      # Arm Toolchain for Embedded (ATfE) — Arm's official LLVM embedded toolchain.
      # clang + lld + compiler-rt + picolibc/llvm-libc, prebuilt multilibs for
      # all Cortex-M incl. armv8.1-M (Cortex-M85 w/ MVE, hard-float).
      atfe = pkgs.stdenv.mkDerivation rec {
        pname = "arm-toolchain-for-embedded";
        version = "22.1.0";
        src = pkgs.fetchurl {
          url = "https://github.com/arm/arm-toolchain/releases/download/release-${version}-ATfE/ATfE-${version}-Linux-x86_64.tar.xz";
          hash = "sha256-4unmN7ugl7puS65pgog/5wX/1+jDp9yHaWSDXvHHpyQ=";
        };
        nativeBuildInputs = [ pkgs.autoPatchelfHook ];
        buildInputs = with pkgs; [ stdenv.cc.cc.lib zlib zstd libxml2 ncurses ];
        # Don't let fixup touch the arm-none-eabi target archives/objects.
        dontStrip = true;
        dontPatchShebangs = true;
        installPhase = ''
          mkdir -p $out
          cp -r . $out/
        '';
        meta.platforms = [ "x86_64-linux" ];
      };

    in {
      packages.${system} = {
        inherit atfe;
        default = atfe;
      };

      devShells.${system}.default = pkgs.mkShell {
        name = "ra8m2-dev";

        packages = with pkgs; [
          # --- toolchain (LLVM) ---
          atfe                              # clang/clang++/lld/llvm-objcopy/llvm-size… (arm-none-eabi default target)
          llvmPackages_latest.lldb          # lldb -> gdb-remote to JLinkGDBServer / pyocd gdbserver
          cmake ninja                       # build system

          # --- probes / flashing / debug ---
          segger-jlink                      # JLinkExe JLinkGDBServer JLinkRTTClient JFlashLite …
          # pyocd + cmsis-pack-manager (for `pyocd pack`) + pyserial in one interpreter
          (python3.withPackages (ps: with ps; [ pyocd cmsis-pack-manager cffi pyserial ]))
          openocd
          probe-rs-tools                    # probe-rs / cargo-embed (RTT, flashing)

          # --- serial ---
          picocom
        ];

        # Let pyocd's J-Link plugin (pylink) find SEGGER's shared lib.
        JLINK_LIB_DIR = "${pkgs.segger-jlink}/lib";

        shellHook = ''
          # pylink (pyocd J-Link plugin) dlopens libjlinkarm.so.9 by soname.
          export LD_LIBRARY_PATH="$JLINK_LIB_DIR''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
          # Default flags for RA8M2 (Cortex-M85, FPU+MVE, hard-float ABI).
          export RA8M2_CFLAGS="--target=arm-none-eabi -mcpu=cortex-m85 -mfloat-abi=hard -mthumb"
          echo "ra8m2-dev shell"
          echo "  clang   : $(clang --version | head -1)"
          echo "  J-Link  : ${pkgs.segger-jlink.version}   pyocd: ${pkgs.pyocd.version}   openocd: ${pkgs.openocd.version}   probe-rs: ${pkgs.probe-rs-tools.version}"
          echo "  hint    : clang \$RA8M2_CFLAGS -O2 -c main.c"
          echo "  connect : JLinkExe -device R7FA8M2AF -if SWD -speed 4000 -autoconnect 1"
        '';
      };
    };
}
