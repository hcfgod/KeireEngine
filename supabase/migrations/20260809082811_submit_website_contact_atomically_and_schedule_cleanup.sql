create extension if not exists pg_cron;

create function public.submit_website_contact(
    p_ip_hash text,
    p_name text,
    p_email text,
    p_category text,
    p_subject text,
    p_message text
)
returns boolean
language plpgsql
security invoker
set search_path = ''
as $function$
begin
    if p_ip_hash !~ '^[0-9a-f]{64}$' then
        raise exception 'invalid contact throttle identity';
    end if;

    perform pg_advisory_xact_lock(hashtextextended(p_ip_hash, 0));

    if (
        select count(*)
        from public.website_contact_rate_limits
        where ip_hash = p_ip_hash
          and attempted_at >= now() - interval '1 hour'
    ) >= 3 then
        return false;
    end if;

    insert into public.website_contact_rate_limits (ip_hash)
    values (p_ip_hash);

    insert into public.website_contact_submissions (name, email, category, subject, message)
    values (p_name, p_email, p_category, p_subject, p_message);

    return true;
end;
$function$;

revoke all on function public.submit_website_contact(text, text, text, text, text, text)
from public, anon, authenticated;
grant execute on function public.submit_website_contact(text, text, text, text, text, text)
to service_role;

comment on function public.submit_website_contact(text, text, text, text, text, text) is
    'Atomically enforces the contact quota and records an accepted website contact submission.';

select cron.schedule(
    'website-contact-rate-limit-cleanup',
    '17 * * * *',
    $cleanup$delete from public.website_contact_rate_limits
        where attempted_at < now() - interval '24 hours';$cleanup$
);
