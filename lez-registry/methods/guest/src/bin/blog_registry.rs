//! # blog_registry — SPEL on-chain program entry point
//!
//! Compiled for the Logos Execution Zone (LEZ) using the SPEL toolchain.
//! Maintains a per-author `Vec<CID>` registry stored at a PDA derived from
//! the author's account ID.
//!
//! ## Instructions
//!
//! | Instruction      | Accounts                  | Mutates state |
//! |------------------|---------------------------|---------------|
//! | `initialize`     | registry (init+pda), author (signer) | yes — creates registry account |
//! | `register_post`  | registry (mut+pda), author (signer), cid: String | yes — appends CID |
//! | `remove_post`    | registry (mut+pda), author (signer), cid: String | yes — removes CID |
//! | `get_posts`      | registry (pda), author    | no — read-only |
//!
//! ## PDA seed
//!
//! `[b"registry", author_account_id]`
//!
//! ## CID cap
//!
//! Each author registry is capped at 10 000 entries. When full, the oldest
//! CID (index 0) is evicted before the new one is appended.

#![no_main]

use blog_registry_core::Registry;
use borsh::BorshDeserialize;
use lez_framework::prelude::*;
use nssa_core::account::AccountWithMetadata;
use nssa_core::program::AccountPostState;

risc0_zkvm::guest::entry!(main);

#[lez_program]
mod blog_registry {
    #[allow(unused_imports)]
    use super::*;

    // ── initialize ────────────────────────────────────────────────────────────

    /// Create the per-author registry account.
    ///
    /// Must be called once per author before any `register_post` calls.
    #[instruction]
    pub fn initialize(
        #[account(init, pda = [literal("registry"), account("author")])]
        registry: AccountWithMetadata,
        #[account(signer)]
        author: AccountWithMetadata,
    ) -> LezResult {
        let author_pubkey = author
            .account_id
            .value()
            .iter()
            .map(|b| format!("{:02x}", b))
            .collect::<String>();
        let state = Registry::new(author_pubkey);
        let data = borsh::to_vec(&state)
            .map_err(|e| LezError::SerializationError { message: e.to_string() })?;
        let mut new_account = registry.account.clone();
        new_account.data = data.try_into().unwrap();

        Ok(LezOutput::states_only(vec![
            AccountPostState::new_claimed(new_account),
            AccountPostState::new(author.account.clone()),
        ]))
    }

    // ── register_post ─────────────────────────────────────────────────────────

    /// Append a CID to the author's registry.
    ///
    /// Idempotent: duplicate CIDs are silently ignored. When the 10 000-entry
    /// cap is reached the oldest CID (index 0) is evicted.
    #[instruction]
    pub fn register_post(
        #[account(mut, pda = [literal("registry"), account("author")])]
        registry: AccountWithMetadata,
        #[account(signer)]
        author: AccountWithMetadata,
        cid: String,
    ) -> LezResult {
        if cid.is_empty() {
            return Err(LezError::Custom {
                code: 6000,
                message: "cid must not be empty".to_string(),
            });
        }

        let mut state = Registry::try_from_slice(&registry.account.data)
            .map_err(|e| LezError::SerializationError { message: e.to_string() })?;
        state.push_cid(cid);
        let data = borsh::to_vec(&state)
            .map_err(|e| LezError::SerializationError { message: e.to_string() })?;
        let mut updated = registry.account.clone();
        updated.data = data.try_into().unwrap();

        Ok(LezOutput::states_only(vec![
            AccountPostState::new_claimed(updated),
            AccountPostState::new(author.account.clone()),
        ]))
    }

    // ── remove_post ───────────────────────────────────────────────────────────

    /// Remove a CID from the author's registry.
    ///
    /// No-op if the CID is not found (safe to call multiple times).
    #[instruction]
    pub fn remove_post(
        #[account(mut, pda = [literal("registry"), account("author")])]
        registry: AccountWithMetadata,
        #[account(signer)]
        author: AccountWithMetadata,
        cid: String,
    ) -> LezResult {
        let mut state = Registry::try_from_slice(&registry.account.data)
            .map_err(|e| LezError::SerializationError { message: e.to_string() })?;
        state.remove_cid(&cid);
        let data = borsh::to_vec(&state)
            .map_err(|e| LezError::SerializationError { message: e.to_string() })?;
        let mut updated = registry.account.clone();
        updated.data = data.try_into().unwrap();

        Ok(LezOutput::states_only(vec![
            AccountPostState::new_claimed(updated),
            AccountPostState::new(author.account.clone()),
        ]))
    }

    // ── get_posts ─────────────────────────────────────────────────────────────

    /// Return all CIDs in the author's registry (read-only).
    ///
    /// TODO: SPEL does not yet have a stable read-only / view-function
    /// convention. There is no `#[query]` attribute or `LezOutput::return_value`
    /// API yet, so callers cannot receive a `Vec<String>` return value directly.
    /// Until SPEL stabilises one of these mechanisms, callers must read the
    /// registry account data out-of-band (e.g. via `getAccountData`) and
    /// deserialise the `Registry` struct with Borsh themselves.
    /// Tracked upstream: https://github.com/logos-co/spel (no stable issue yet).
    /// Once resolved, update this instruction to use the view-function API and
    /// return `Vec<String>` directly without touching post_states.
    #[instruction]
    pub fn get_posts(
        #[account(pda = [literal("registry"), account("author")])]
        registry: AccountWithMetadata,
        author: AccountWithMetadata,
    ) -> LezResult {
        // Deserialise to validate the account is a well-formed Registry.
        let _state = Registry::try_from_slice(&registry.account.data)
            .map_err(|e| LezError::SerializationError { message: e.to_string() })?;

        // Return accounts unchanged (read-only — no state mutation).
        Ok(LezOutput::states_only(vec![
            AccountPostState::new(registry.account.clone()),
            AccountPostState::new(author.account.clone()),
        ]))
    }
}
