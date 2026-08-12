-- Finalize only administrator-approved, validator-bound bytes after an independent Ed25519 signature verifies.
-- Rollback: disable marketplace_enabled, revoke the publication function, and deactivate the trust root. Published
-- rows and immutable release objects are audit evidence and must not be destructively removed during rollback.

create table public.marketplace_signature_keys (
    key_id text primary key,
    algorithm text not null default 'ed25519',
    public_key_base64 text not null,
    fingerprint text not null unique,
    active boolean not null default true,
    valid_from timestamptz not null default now(),
    valid_until timestamptz,
    created_at timestamptz not null default now(),
    constraint marketplace_signature_keys_id check (key_id ~ '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$'),
    constraint marketplace_signature_keys_algorithm check (algorithm = 'ed25519'),
    constraint marketplace_signature_keys_public_key check
        (public_key_base64 ~ '^[A-Za-z0-9+/]{43}=$'),
    constraint marketplace_signature_keys_fingerprint check
        (fingerprint ~ '^sha256:[0-9a-f]{64}$'),
    constraint marketplace_signature_keys_validity check
        (valid_until is null or valid_until > valid_from)
);

insert into public.marketplace_signature_keys
    (key_id, public_key_base64, fingerprint)
values
    ('ed25519-d474a0428cab75db2f4b7c82e2ae1df1',
     'wMaIV66dLJsPm/91qX/d8a8KT6fVRsh/zZT9FlA00JI=',
     'sha256:d474a0428cab75db2f4b7c82e2ae1df1443b532fdd3cb70c48e19d253f636d45');

alter table public.marketplace_signature_keys enable row level security;
alter table public.marketplace_signature_keys force row level security;

revoke all on public.marketplace_signature_keys from public, anon, authenticated;
grant select on public.marketplace_signature_keys to anon, authenticated, service_role;

create policy marketplace_signature_keys_public_read
on public.marketplace_signature_keys for select to anon, authenticated
using (active and valid_from <= now() and (valid_until is null or valid_until > now()));

create or replace function public.service_publish_marketplace_version(
    p_actor_user_id uuid,
    p_version_id uuid,
    p_release_storage_path text,
    p_artifact_sha256 text,
    p_manifest_sha256 text,
    p_signature_key_id text,
    p_signed_manifest text
)
returns table (
    publication_id uuid,
    version_id uuid,
    product_id uuid,
    published_at timestamptz
)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_version public.marketplace_product_versions%rowtype;
    selected_product public.marketplace_products%rowtype;
    selected_submission public.marketplace_submissions%rowtype;
    selected_validation public.marketplace_validation_reports%rowtype;
    selected_upload public.marketplace_uploads%rowtype;
    publication_document jsonb;
    publication_signature jsonb;
    release_size bigint;
    publication_time timestamptz;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not private.service_actor_is_staff(p_actor_user_id, 'administrator') then
        raise exception using errcode = '42501', message = 'staff_administrator_required';
    end if;
    if p_release_storage_path !~ '^[0-9a-f-]{36}/[0-9a-f-]{36}/[0-9a-f]{64}\.keireassetpackage$' or
       p_artifact_sha256 !~ '^[0-9a-f]{64}$' or p_manifest_sha256 !~ '^[0-9a-f]{64}$' or
       char_length(p_signed_manifest) not between 2 and 8388608 then
        raise exception using errcode = '22023', message = 'marketplace_publication_invalid';
    end if;
    if not exists (
        select 1 from public.marketplace_signature_keys signing_key
        where signing_key.key_id = p_signature_key_id and signing_key.algorithm = 'ed25519' and signing_key.active
          and signing_key.valid_from <= now()
          and (signing_key.valid_until is null or signing_key.valid_until > now())
    ) then
        raise exception using errcode = '22023', message = 'marketplace_signature_key_untrusted';
    end if;

    begin
        publication_document := (p_signed_manifest::jsonb ->> 'document')::jsonb;
        publication_signature := p_signed_manifest::jsonb -> 'signature';
    exception when others then
        raise exception using errcode = '22023', message = 'marketplace_signed_manifest_invalid';
    end;
    if (p_signed_manifest::jsonb ->> 'schemaVersion')::integer <> 1 or
       publication_document is null or publication_signature is null or
       publication_document ->> 'versionId' is distinct from p_version_id::text or
       publication_document ->> 'artifactSha256' is distinct from p_artifact_sha256 or
       publication_document ->> 'manifestSha256' is distinct from p_manifest_sha256 or
       publication_document ->> 'releaseStoragePath' is distinct from p_release_storage_path or
       publication_document ->> 'keyId' is distinct from p_signature_key_id or
       publication_signature ->> 'algorithm' is distinct from 'ed25519' or
       publication_signature ->> 'keyId' is distinct from p_signature_key_id or
       publication_signature ->> 'value' !~ '^[A-Za-z0-9+/]{86}==$' then
        raise exception using errcode = '22023', message = 'marketplace_signed_manifest_invalid';
    end if;

    select * into selected_version
    from public.marketplace_product_versions version
    where version.id = p_version_id and version.state = 'approved_pending_signature'
    for update;
    if not found then
        raise exception using errcode = '55000', message = 'marketplace_version_not_publishable';
    end if;
    select * into selected_product from public.marketplace_products product
    where product.id = selected_version.product_id for update;
    select * into selected_submission from public.marketplace_submissions submission
    where submission.version_id = selected_version.id and submission.state = 'approved_pending_signature'
    order by submission.decided_at desc nulls last limit 1 for update;
    if not found then
        raise exception using errcode = '55000', message = 'marketplace_submission_not_publishable';
    end if;
    select * into selected_validation from public.marketplace_validation_reports validation
    where validation.id = selected_submission.validation_report_id and validation.passed for share;
    if not found or selected_validation.package_sha256 is distinct from p_artifact_sha256 or
       selected_validation.manifest_sha256 is distinct from p_manifest_sha256 or
       selected_version.archive_sha256 is distinct from p_artifact_sha256 or
       selected_version.manifest_sha256 is distinct from p_manifest_sha256 then
        raise exception using errcode = '55000', message = 'marketplace_publication_validation_mismatch';
    end if;
    select * into selected_upload from public.marketplace_uploads upload
    where upload.id = selected_validation.upload_id and upload.state = 'validated' for share;
    if not found or not exists (
        select 1 from storage.objects source_object
        where source_object.bucket_id = 'marketplace-quarantine' and source_object.name = selected_upload.storage_path
    ) then
        raise exception using errcode = '55000', message = 'marketplace_publication_source_missing';
    end if;
    select (release_object.metadata ->> 'size')::bigint into release_size
    from storage.objects release_object
    where release_object.bucket_id = 'marketplace-releases' and release_object.name = p_release_storage_path;
    if release_size is null or release_size is distinct from selected_version.archive_size_bytes or
       publication_document ->> 'artifactSizeBytes' is distinct from selected_version.archive_size_bytes::text or
       publication_document ->> 'productId' is distinct from selected_product.id::text then
        raise exception using errcode = '55000', message = 'marketplace_publication_artifact_mismatch';
    end if;

    insert into public.marketplace_publications
        (version_id, artifact_sha256, manifest_sha256, signature_key_id, signed_manifest, approved_by)
    values
        (selected_version.id, p_artifact_sha256, p_manifest_sha256, p_signature_key_id,
         p_signed_manifest, p_actor_user_id)
    returning id, marketplace_publications.published_at into publication_id, publication_time;

    update public.marketplace_product_versions
    set state = 'published', archive_storage_path = p_release_storage_path,
        signature_key_id = p_signature_key_id, published_at = publication_time, updated_at = now()
    where id = selected_version.id;
    update public.marketplace_products
    set state = 'published', published_at = coalesce(marketplace_products.published_at, publication_time),
        updated_at = now()
    where id = selected_product.id;
    update public.marketplace_submissions
    set state = 'approved', decided_at = coalesce(decided_at, now())
    where id = selected_submission.id;
    insert into public.platform_audit_events
        (actor_user_id, action, target_type, target_id, metadata)
    values
        (p_actor_user_id, 'marketplace.version_published', 'marketplace_product_version', selected_version.id::text,
         jsonb_build_object('publicationId', publication_id, 'productId', selected_product.id,
             'artifactSha256', p_artifact_sha256, 'manifestSha256', p_manifest_sha256,
             'signatureKeyId', p_signature_key_id, 'releaseStoragePath', p_release_storage_path));
    version_id := selected_version.id;
    product_id := selected_product.id;
    published_at := publication_time;
    return next;
end;
$$;

revoke all on function public.service_publish_marketplace_version(uuid, uuid, text, text, text, text, text)
    from public, anon, authenticated;
grant execute on function public.service_publish_marketplace_version(uuid, uuid, text, text, text, text, text)
    to service_role;

comment on table public.marketplace_signature_keys is
    'Public Ed25519 marketplace trust roots. Private signing material never enters Supabase, Astro, or Edge Functions.';
comment on function public.service_publish_marketplace_version(uuid, uuid, text, text, text, text, text) is
    'Service-only atomic publication commit after the Edge boundary verifies an offline signature and promotes exact validated bytes.';
