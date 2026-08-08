create policy website_contact_submissions_browser_deny
on public.website_contact_submissions
for all
to anon, authenticated
using (false)
with check (false);

create policy website_contact_rate_limits_browser_deny
on public.website_contact_rate_limits
for all
to anon, authenticated
using (false)
with check (false);
