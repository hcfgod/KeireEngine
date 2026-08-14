-- Post-deployment hardening for the upload-once Marketplace publication path.
-- Index foreign-key lookup columns used by review, cancellation, signing, and
-- validator-attestation operations, and make the intended service-only key read
-- policy explicit for the forced-RLS table.

create index if not exists idx_marketplace_publication_jobs_approved_by
    on public.marketplace_publication_jobs (approved_by);
create index if not exists idx_marketplace_publication_jobs_cancelled_by
    on public.marketplace_publication_jobs (cancelled_by)
    where cancelled_by is not null;
create index if not exists idx_marketplace_publication_jobs_signing_key
    on public.marketplace_publication_jobs (signing_key_id)
    where signing_key_id is not null;
create index if not exists idx_marketplace_validation_reports_attestation_key
    on public.marketplace_validation_reports (attestation_key_id)
    where attestation_key_id is not null;

drop policy if exists marketplace_validator_attestation_keys_service_read
    on public.marketplace_validator_attestation_keys;
create policy marketplace_validator_attestation_keys_service_read
on public.marketplace_validator_attestation_keys for select to service_role
using (true);
