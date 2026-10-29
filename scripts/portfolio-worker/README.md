# Portfolio API backend

This optional Cloudflare Worker keeps the owner's OpenAI key in Cloudflare
secret storage. It is separate from the GitHub Pages static site. The frontend
uses the public endpoint configured in `ui/scripts/ai-service.mjs`; it discovers
the enabled models and readiness at `/models`.

The free Workers plan and SQLite-backed Durable Objects support this small demo.
Cloudflare's current free allowances include 100,000 Worker requests/day and
100,000 Durable Object requests/day. OpenAI generation remains billable.

The defaults deliberately allow only GPT-6 Luna, with 100 total upstream
attempts/day, 20 per visitor IP/day, and 6 per IP/minute. Counters persist in one
Durable Object and daily quotas reset at 00:00 UTC. This limits request count,
not exact dollars. Failed upstream attempts also consume quota.

Origin checks restrict browser access to the portfolio's origin. They are not
authentication: non-browser clients can forge an Origin header. The persistent
global quota is the spending safeguard. Shared networks share the per-IP quota.

## Deploy from your own computer

Create a free Cloudflare account, then run from this directory:

```sh
npm ci
npx wrangler login
npm run deploy
npx wrangler secret put OPENAI_API_KEY
```

The login opens Cloudflare in your browser. Paste a **new, unexposed** OpenAI key
at Wrangler's secret prompt; never put it in a source file, GitHub, or chat.
The Worker returns an unavailable response until this secret is configured.

The deployment prints a `workers.dev` URL. That URL is public and safe to share;
set it in `ui/scripts/ai-service.mjs` when moving the service. Public mode sends
no visitor Authorization header or API key. Personal-key mode remains available
and sends its requests directly to OpenAI instead.

Settings are in `wrangler.jsonc`. Increase quotas or enable additional models
only after deciding how much usage to fund. The backend replaces client system
prompts with the verified rules and fixes the schema, token limit, reasoning
level and upstream destination. It accepts structured chess state, not arbitrary
client-selected upstream URLs or credentials. It does not log prompts or keys.

## Validate locally

```sh
npm test
npx wrangler deploy --dry-run
```

These checks do not generate OpenAI requests. Local unit tests simulate the
upstream and persistent storage. Actual Cloudflare deployment and browser
integration require an account and are verified separately.
