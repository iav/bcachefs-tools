{
	description = "Userspace tools for bcachefs";

	# Nixpkgs / NixOS version to use.
	inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-21.11";
	inputs.utils.url = "github:numtide/flake-utils";
	inputs.filter.url = "github:numtide/nix-filter";

	outputs = { self, nixpkgs, utils, filter, ... }@inputs:
		let
			# System types to support.
			supportedSystems = [ "x86_64-linux" ];
		in
		{
			version = "${builtins.substring 0 8 self.lastModifiedDate}-${self.shortRev or "dirty"}";

			overlay = import ./nix/overlay.nix inputs;
			nixosModule = self.nixosModules.bcachefs;
			nixosModules.bcachefs = import ./nix/module.nix;
			nixosModules.bcachefs-enable-boot = ({config, pkgs, lib, ... }:{
				# Disable Upstream NixOS Module when this is in use
				disabledModules = [ "tasks/filesystems/bcachefs.nix" ];
				# Import needed packages
				nixpkgs.overlays = [ self.overlay ];

				# Add bcachefs to boot and kernel
				boot.initrd.supportedFilesystems = [ "bcachefs" ];
				boot.supportedFilesystems = [ "bcachefs" ];
			});

			nixosConfigurations.netboot-bcachefs = self.systems.netboot-bcachefs "x86_64-linux";
			systems.netboot-bcachefs = system: (nixpkgs.lib.nixosSystem { 
					inherit system; modules = [
						self.nixosModule 
						self.nixosModules.bcachefs-enable-boot
						("${nixpkgs}/nixos/modules/installer/netboot/netboot-minimal.nix")
						({ lib, pkgs, config, ... }: {
							# installation disk autologin
							services.getty.autologinUser = lib.mkForce "root";
							users.users.root.initialPassword = "toor";
							
							# Symlink everything together
							system.build.netboot = pkgs.symlinkJoin {
								name = "netboot";
								paths = with config.system.build; [
									netbootRamdisk
									kernel
									netbootIpxeScript
								];
								preferLocalBuild = true;
							};
						})
					]; 
				});
		}
		// utils.lib.eachSystem supportedSystems (system: 
		let pkgs = import nixpkgs { 
			inherit system; 
			overlays = [ self.overlay ]; 
		}; 
		in rec {
			
			# A Nixpkgs overlay.

			# Provide some binary packages for selected system types.
			defaultPackage = pkgs.bcachefs.tools;
			packages = {
				inherit (pkgs.bcachefs)
					tools
					toolsValgrind
					toolsDebug
					rbcachefs
					kernel;

				tools-musl = pkgs.pkgsMusl.bcachefs.tools;
			};

			hydraJobs = checks // {
				kernel = pkgs.bcachefs.kernel;
			};

			checks = { 
				kernelSrc = packages.kernel.src;

				inherit (packages) 
					rbcachefs
					toolsValgrind;

				# Build and test initrd with bcachefs and bcachefs.mount installed
				bootStage1Module = self.nixosConfigurations.netboot-bcachefs.config.system.build.bootStage1.extraUtils;
			};

			devShell = devShells.tools;
			devShells.tools = pkgs.bcachefs.tools.override { inShell = true; };
			devShells.rbcachefs = pkgs.bcachefs.rbcachefs.override { inShell = true; };
		});
}
