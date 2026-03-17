# Makefile for the lez-registry SPEL workspace
#
# Targets:
#   test        — compile and run unit tests (mock mode, no SPEL runtime needed)
#   build       — build the on-chain binary (requires SPEL toolchain)
#   deploy      — deploy to LEZ devnet (requires SPEL toolchain + credentials)
#   clean       — remove build artifacts

.PHONY: test build deploy clean

# Run unit tests using the in-memory mock implementation (no SPEL runtime needed).
test:
	cargo test --workspace

# Build the on-chain program with the SPEL/on-chain feature enabled.
# Requires the SPEL toolchain to be installed:
#   https://github.com/logos-co/spel
# TODO: Update build command once SPEL publishes an official CLI/toolchain.
build:
	cargo build --release --manifest-path methods/guest/Cargo.toml --features on-chain

# Deploy the compiled program to LEZ devnet.
# TODO: Update target network and program-id path once SPEL devnet is available.
deploy: build
	spel deploy target/release/blog_registry --network devnet

clean:
	cargo clean
