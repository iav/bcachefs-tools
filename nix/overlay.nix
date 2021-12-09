{ filter, self, ... }:
final: prev: {
	bcachefs = {
		srcs.tools = filter.lib.filter {
			name = "bcachefs-tools";
			root = ../.;
			exclude = [
				./rust-src
				
				./.git
				./nix
				
				./flake.nix
				./flake.lock
			];
		};

		tools = final.callPackage ../default.nix {
			testWithValgrind = false;
			lastModified = builtins.substring 0 8 self.lastModifiedDate;
			versionString = self.version;
		};
		toolsValgrind = final.bcachefs.tools.override {
			testWithValgrind = true;
		};
		toolsDebug = final.bcachefs.toolsValgrind.override {
			debugMode = true;
		};

		rbcachefs = final.callPackage ../rust-src/rbcachefs {};

		kernelPackages = final.recurseIntoAttrs (final.linuxPackagesFor final.bcachefs.kernel);
		kernel = final.callPackage ./bcachefs-kernel.nix {
			commit = final.bcachefs.tools.bcachefs_revision;
			# This needs to be recalculated for every revision change
			sha256 = builtins.readFile ./bcachefs.rev.sha256;
			kernelPatches = [];
		};
	};
}
