//! # blog_registry — SPEL on-chain program entry point
//!
//! This binary is compiled for the Logos Execution Zone (LEZ) using the SPEL
//! toolchain. It wraps the shared logic in `blog_registry_core` and exposes it
//! as a deployable LEZ program.
//!
//! ## Instructions
//!
//! | Instruction | Signer required | Mutates state |
//! |-------------|-----------------|---------------|
//! | `initialize` | author | yes — creates registry account |
//! | `register_post` | author | yes — appends CID (evicts oldest if cap reached) |
//! | `remove_post` | author | yes — removes CID |
//! | `get_posts` | none | no — read-only view |
//!
//! ## CID cap
//!
//! Each author registry is capped at [`blog_registry_core::MAX_CIDS_PER_AUTHOR`]
//! entries (10 000). When full, the oldest CID (index 0) is evicted before the
//! new one is appended.
//!
//! ## SPEL compatibility
//!
//! SPEL is under active development. The proc-macro names, account context types,
//! and PDA derivation API may change.
//!
//! TODO: Audit against SPEL API once a stable release is published.
//! TODO: Verify `#[lez_program]` entry-point convention with SPEL docs.
//! TODO: Confirm PDA seed format for per-author accounts.

use blog_registry_core::{
    GetPostsParams, RegisterPostParams, RemovePostParams, Registry,
};
use spel::{
    account::Account,
    context::Context,
    error::ProgramError,
    instruction,
    lez_program,
    pubkey::Pubkey,
};

// ── Program definition ────────────────────────────────────────────────────────

#[lez_program]
pub mod blog_registry_program {

    use super::*;

    // ── initialize ────────────────────────────────────────────────────────────

    /// Create the registry account for the signing author.
    ///
    /// Must be called once per author before any `register_post` calls.
    /// The account is funded by the caller (author pays rent/storage fees).
    ///
    /// TODO: Confirm SPEL PDA derivation — seed should be b"registry" + author pubkey bytes.
    #[instruction]
    pub fn initialize(ctx: Context<Initialize>) -> Result<(), ProgramError> {
        let registry = &mut ctx.accounts.registry;
        let author_pubkey = ctx.accounts.authority.key().to_string();
        **registry = Registry::new(author_pubkey);
        Ok(())
    }

    // ── register_post ─────────────────────────────────────────────────────────

    /// Append a CID to the author's post list.
    ///
    /// - Only the account authority (author) may call this.
    /// - Duplicate CIDs are silently ignored (idempotent).
    /// - When the list reaches [`MAX_CIDS_PER_AUTHOR`] entries the oldest CID
    ///   (index 0) is evicted before the new one is appended.
    #[instruction]
    pub fn register_post(
        ctx: Context<AuthorAccess>,
        params: RegisterPostParams,
    ) -> Result<(), ProgramError> {
        if params.cid.is_empty() {
            return Err(ProgramError::InvalidArgument);
        }

        let registry = &mut ctx.accounts.registry;
        registry.push_cid(params.cid);
        Ok(())
    }

    // ── remove_post ───────────────────────────────────────────────────────────

    /// Remove a CID from the author's post list.
    ///
    /// Used when an author deletes a published post. No-op if the CID is not
    /// found (safe to call multiple times for the same CID).
    #[instruction]
    pub fn remove_post(
        ctx: Context<AuthorAccess>,
        params: RemovePostParams,
    ) -> Result<(), ProgramError> {
        let registry = &mut ctx.accounts.registry;
        registry.remove_cid(&params.cid);
        Ok(())
    }

    // ── get_posts ─────────────────────────────────────────────────────────────

    /// Return all CIDs registered for a given author (read-only view).
    ///
    /// The client derives the PDA for `params.author_pubkey` and passes that
    /// account in `ctx.accounts.registry`. The on-chain check below validates
    /// that the account belongs to the requested author.
    ///
    /// TODO: Verify the SPEL return-value convention for read-only instructions.
    ///   Some frameworks use a separate "view" function type; update accordingly.
    #[instruction]
    pub fn get_posts(
        ctx: Context<ReadOnly>,
        params: GetPostsParams,
    ) -> Result<Vec<String>, ProgramError> {
        let registry = &ctx.accounts.registry;

        if registry.author_pubkey != params.author_pubkey {
            return Err(ProgramError::InvalidAccountData);
        }

        Ok(registry.cids.clone())
    }

    // ── Account contexts ───────────────────────────────────────────────────────

    /// Accounts needed for `initialize`.
    ///
    /// TODO: Confirm `space` calculation once BorshSerialize is stable.
    #[derive(spel::Accounts)]
    pub struct Initialize<'info> {
        /// The registry account to create (PDA derived from authority pubkey).
        #[account(init, payer = authority, space = Registry::serialised_size())]
        pub registry: Account<'info, Registry>,
        /// The author who is creating and will own the registry.
        #[account(mut, signer)]
        pub authority: Account<'info, Pubkey>,
    }

    /// Accounts needed for mutating instructions (`register_post`, `remove_post`).
    ///
    /// `has_one = authority` enforces that `registry.author_pubkey` matches the
    /// signing authority.
    ///
    /// TODO: Verify `has_one` constraint syntax in SPEL (mirrors Anchor convention).
    #[derive(spel::Accounts)]
    pub struct AuthorAccess<'info> {
        /// The author's existing registry account (must be mutable).
        #[account(mut, has_one = authority)]
        pub registry: Account<'info, Registry>,
        /// Must match the authority stored in `registry`.
        #[account(signer)]
        pub authority: Account<'info, Pubkey>,
    }

    /// Accounts needed for read-only queries (`get_posts`).
    #[derive(spel::Accounts)]
    pub struct ReadOnly<'info> {
        /// The registry account to read from (any caller, no mutation).
        pub registry: Account<'info, Registry>,
    }
}
