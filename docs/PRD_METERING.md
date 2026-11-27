# CHOPPAH — The hub's metering: roles, credits, quotas, room, billing and costs

**Feature PRD** · v1.0 · 27 Nov 2026 · Owner: Laurens (Sylverline)  
**Parent PRDs:** [PRD.md](PRD.md) (the instrument) · [PRD_PACKS.md](PRD_PACKS.md) (§10 the hub)  
**Built in:** `choppah-hub` — `1f9623e` (roles, metering, the admin panel), `e7612fd` (billing), `eea9e80`
(costs, room sold by count). Live on choppah.app.  
**Status:** built. **Reconstructed 27 Nov 2026** — the original PRD_METERING.md was not in this repo (never
committed, though every line of the hub's code cites it); this file is the spec as the code implements it, read
back from the hub's migrations, `src/meter.js`, `src/billing.js`, `src/usage.js`, `src/admin.js` and `src/money.js`.

---

## 1. Purpose

The hub gives away compute: a Demucs destem on a T4, an LLM call, file storage, a pack's images. Someone has to
pay for it and someone has to say how much. This PRD is that answer: **what the hub gives away, to whom, at what
price, and what it costs us.**

Three audiences:

1. **A user** — must know what he may do today, what he has left, and what it costs him.
2. **An admin** — must set roles, limits and prices, and see the money.
3. **The owner** — must see what the compute really costs, so the prices can be right.

### 1.1 Principles

1. **One gate.** Every metered thing goes through `src/meter.js`: the pages' routes and the MCP tools alike.
2. **Limits are data, not code.** Roles, meters, costs, prices and limits are rows; the admin panel owns them.
3. **The day is UTC.** A new day is a new `usage_daily` row, so nothing has to be reset.
4. **Charged before the work, refunded when it fails.** Nobody pays for work that did not happen.
5. **Nothing is hidden.** Every charge, refund and cost is in the ledger, and a refusal says the numbers.

---

## 2. Roles (MR-001 – MR-004)

A role is a named set of limits. The role is a row, the limits are rows; the admin panel edits both.

| Role | Rank | What it is |
| --- | --- | --- |
| `suspended` | 0 | The account is off: nothing is allowed, nothing is charged |
| `guest` | 10 | Signed out: read-only, no AI work, no uploads |
| `member` | 20 | **Free**: the default for a signed-up account |
| `creator` | 30 | Made packs and samples: more room, more publishes, the large models |
| `pro` | 40 | The paying plan's role (a Stripe subscription, or set by hand) |
| `admin` | 100 | No limits at all; pays as themselves, never as anyone else |

- **MR-001** — every account has one role; `guest` when signed out, `member` by default, `admin` when `is_admin`.
- **MR-002** — a role set by hand (`users.role_by`) outranks the plan: a higher role set by an admin stays.
- **MR-003** — a suspended account (`role = suspended`) can sign in, but every meter is zero: 403 on a feature, 429
  on a quota, nothing to spend.
- **MR-004** — a user's own view of their role, credits, quotas and room is on the Account page and in the header's
  meter; the admin sees the same numbers for anyone.

---

## 3. A user's usage (§3)

`GET /api/me/usage` (`src/usage.js`), the Account page's *Usage* panel, the header's meter, and the MCP tool
`usage_status` all read the same thing (`src/meter.js` `usage()`):

| Field | What it is |
| --- | --- |
| `role`, `role_name` | The account's role and its name |
| `day`, `resets_at` | The UTC day and when it starts again (00:00 UTC) |
| `credits` | `used`, `limit`, `left`, `day_left`, `granted` (an admin's grants), `balance` (paid credits), every grant with its reason and expiry |
| `costs` | The credit meters and what one unit of each costs, with whether this account may use it |
| `quotas` | Each daily quota: used and limit |
| `storage` | Room: how many samples and packs are kept, and the limit |
| `features` | Each feature gate: on or off |
| `recent` | The last charges and refunds: when, which meter, units, credits, what for, page or MCP, and who acted for whom |

Reading as another user (the switcher, §5) shows their numbers, not yours.

---

## 4. The install history (§4, MI-001 – MI-003)

Every pack put on an FM-1 or into the emulator is recorded, so the user can see what they installed, which
version that was, and whether a newer one is there now.

- **MI-001** — `GET /api/packs/:id/install` starts one and answers with its id (`x-install`); the page finishes it
  with `PATCH /api/me/installs/:id` (the version, the firmware, the proto, or the error).
- **MI-002** — `GET /api/me/installs` lists them: the pack, the version then, the target (FM-1 or emulator), the
  status, when, whether the pack is still shared, and whether a newer one is there.
- **MI-003** — the library's **Installs** tab and the MCP tool `install_history` show the same list.

---

## 5. The admin panel (§5, MA-001 – MA-004)

`/hub/admin` (`src/admin.js`, `/api/admin/*`, admins only — checked on the admin signed in, also while acting as
someone): **overview · users · one user · roles & limits · meters & costs · moderation · billing · money · the
audit log**.

- **MA-001** — the panel lists every user with their role, usage, ledger, installs, grants and own limits; an admin
  can reset the day, give a grant, set a per-user limit, change a role and sign a user out.
- **MA-002** — every admin change is written to the **audit log** (who, what, when).
- **MA-003** — the **user switcher**: view as a user (their reads; every change is refused) or act as them with a
  reason given (the changes carry the admin; the admin pays, never their credits or quota).
- **MA-004** — the switcher never touches the sign-in, passkeys, email, tokens or the account itself, never switches
  onto another admin, and ends after 30 minutes idle.

The roles & limits tab is a matrix (role × meter); the meters and costs tab edits the registry (§10).

---

## 6. The meters: credits, quotas, room and features (MM-001 – MM-009)

One registry (`meters`), one kind per row, the limits per role (`role_limits`), the exceptions per user
(`user_overrides`).

| Kind | What it is | Meters |
| --- | --- | --- |
| `credits` | A wallet the AI work spends | `ai.credits` (the wallet itself), `ai.destem`, `ai.llm.small`, `ai.llm.large` |
| `quota` | A count of one action a day | `quota.sample_upload`, `quota.pack_create`, `quota.publish` |
| `storage` | How many things are kept | `storage.samples`, `storage.packs` |
| `feature` | On or off per role | `feature.llm.small`, `feature.llm.large` |

- **MM-001** — one gate: `charge(env, user, key, units, {what, ref, via})` charges and returns the charge (so it
  can be refunded); `job()` groups the charges of one piece of work.
- **MM-002** — **AI credits**: one wallet a day (`ai.credits`) spent at the meter's own cost. `ai.destem` costs
  **2 credits a minute of audio a stem**; `ai.llm.small` **1 credit a 1k tokens**; `ai.llm.large` **10 credits a
  1k tokens**. Credits are kept in tenths (a tenth of a credit), so nothing is lost to rounding.
- **MM-003** — **charged before, refunded after**: the work is charged first and the charge is taken back when the
  work fails. An MCP tool call is one job: every charge of that call comes back on an error. A destem that fails
  still costs us the GPU seconds, and the ledger says so.
- **MM-004** — **the order of payment**: an admin's grants first (the soonest to expire first), then the day's
  credits, then the paid balance (prepaid top-ups, §9). A grant is tenths of a credit, with a reason and an
  expiry.
- **MM-005** — **quotas a day**: `quota.sample_upload`, `quota.pack_create` (a new version of your own pack does
  not count), `quota.publish`. A missing limit row means no limit; guests and suspended accounts get zero.
- **MM-006** — **room**: `storage.samples` and `storage.packs` count what is kept, are refused **before** anything
  is stored (413), and never reset. The bytes are kept for the costs (§10), not sold.
- **MM-007** — **the refusal says the numbers**: 429 for credits and quotas, 413 for room, 403 for a feature; the
  JSON is `{error, meter, used, limit, left, resets_at}`; every metered response carries `x-credits-left` and
  `x-credits-reset`.
- **MM-008** — **the day's use**: `usage_daily` (user, UTC day, meter, used) and an append-only `usage_ledger`
  (every charge and refund, with what, which ref, page or MCP, who acted for whom, the role, and how it was paid).
- **MM-009** — **the rules that are not negotiable**: an admin has no limits; a missing meter row means no limit
  for a quota or room, and nothing (zero) for credits and features; a meter switched off in the admin panel is
  free; an admin acting as a user pays as themselves.

The seeded limits (the admin panel edits them from here):

| | Member (Free) | Creator | Pro | Guest / Suspended | Admin |
| --- | --- | --- | --- | --- | --- |
| `ai.credits` a day | 50 | 250 | 1000 | 0 | no limit |
| `quota.sample_upload` a day | 50 | 200 | 500 | 0 | — |
| `quota.pack_create` a day | 25 | 100 | 300 | 0 | — |
| `quota.publish` a day | 5 | 25 | 50 | 0 | — |
| `storage.samples` kept | 100 | 500 | 2000 | 0 | — |
| `storage.packs` kept | 20 | 100 | 500 | 0 | — |
| `feature.llm.small` / `.large` | on / off | on / on | on / on | off | — |

---

## 7. Jobs, charges and refunds (the MCP side)

The MCP server is metered like any page (`src/mcp.js`): a tool call is one job, so a tool that fails halfway gives
back everything it took. `usage_status` answers like the Usage page. `install_history` answers like §4. Each
tool's description says what it costs.

---

## 8. What the hub gives away today

| Metered thing | Who | Charged |
| --- | --- | --- |
| Destem (Demucs on the GPU): the browser's `/api/destem` and every MCP tool's splits | signed in | `ai.destem`, 2 credits a minute of audio a stem |
| LLM calls | signed in, the role's feature gate on | `ai.llm.<model>`, 1k tokens a unit |
| Sample upload, new pack, publish | signed in | the day's quotas |
| Storing samples and packs | signed in | the room |

---

## 9. Billing: Free, Pro and pay as you go

`src/billing.js` (Stripe over `fetch`, no SDK), `migrations/0010_billing.sql`, `/hub/pricing`, the Account page's
*Plan and billing*, the admin's Billing tab, the MCP tool `billing`.

- **Free** (`member`): the role's daily credits and room. **Pro** (`plans`): **€8 a month**, a Stripe
  subscription; while it is `active`, `trialing` or `past_due` (Stripe still retrying) the account has the plan's
  role, after that Free again — unless an admin set the role by hand, which outranks the plan.
- **Pay as you go**: top-ups (`topups`: €5 / €20 / €50 = 500 / 2000 / 5000 credits, so €1 = 100 credits) bought
  through Stripe Checkout into a paid balance, spent **only after the day's credits**, refunded to it when the work
  fails, and never expiring.
- **Auto-reload**: a top-up with "save my card" keeps the card; under the line (100 credits by default) the card is
  charged off-session for the chosen pack, at most once a minute; a failed card turns it off.
- **Stripe is the only truth**: credits and roles change only on Stripe's signed webhooks
  (`POST /api/stripe/webhook`, each event taken once) — `checkout.session.completed`,
  `customer.subscription.created|updated|deleted`, `payment_intent.succeeded|payment_failed` (auto-reload),
  `invoice.paid` (revenue), `charge.refunded` (a top-up's unspent credits taken back). A redirect back from
  Checkout gives nothing by itself.
- **Prices are rows**: the admin panel edits them; a plan can point at a Stripe Price (`price_…`), else Checkout
  makes the price from the row. Prices include VAT; `STRIPE_TAX=1` lets Stripe Tax compute it.
- Deleting an account cancels its subscription at Stripe first. An admin acting as someone cannot pay for them.
- Without `STRIPE_SECRET_KEY` and `STRIPE_WEBHOOK_SECRET` the billing endpoints say payments aren't set up and the
  pricing page says "opening soon".

---

## 10. What things cost, and the money dashboard

`migrations/0011_costs.sql`, `src/money.js`, the admin panel's **Money** tab (`GET /api/admin/money?days=`).

- **The registry**: every credit meter knows its provider and its USD rate, and how it is costed —
  `seconds` (measured), `tokens` (1k in and out) or `unit`:

| Meter | Provider | Cost basis | Rate (Nov 2026) |
| --- | --- | --- | --- |
| `ai.destem` | Modal (a T4 + a core + 4 GiB) | measured wall seconds (a cold start and a retry are billed too) | $0.000186 a second |
| `ai.llm.small` | Anthropic (Claude Haiku 5.5) | 1k tokens in / out | $0.10 / $0.50 a million tokens |
| `ai.llm.large` | Anthropic (Claude Sonnet 5.5) | 1k tokens in / out | $2 / $10 a million tokens |

- **The ledger** keeps each charge's measured cost (USD millionths), the role it was charged under, and how it was
  paid (the day's credits, an admin's grant, paid credits) — so the dashboard can split cost and revenue:
  **free / pro / creator** (the day's credits of that role; a subscription's role is where Pro's revenue goes),
  **paid** (prepaid credits: top-ups and auto-reloads are its revenue), **grants**, and **admin**.
- **Modal's idle**: a container stays up (and is billed) for `scaledown_window` after its last job; it is estimated
  from when the jobs ran (the union of [start, end + idle]) and shared out like the busy time; a month's real bill
  (`provider_bills`) sets it straight.
- **Storage**: the bytes kept × R2's rate, by the owner's role. **Stripe's fees** come from the settings.
  USD → EUR at the settings' rate (0.87 by default).
- **Settings** (`PATCH /api/admin/settings`): EUR per USD, Stripe's percentage and fixed fee, Modal's idle seconds
  and second price, R2's GB-month. **Bills** (`PUT` / `DELETE /api/admin/bills`): what a provider really billed a
  month.

---

## 11. Tests

| Test | What it covers |
| --- | --- |
| `test/meter.test.mjs` | The core on SQLite (`test/d1shim.mjs`): charges, refunds, the order of payment, the limits, the refusals |
| `test/metering.mjs` | End to end: sign-up, the metered routes, the quotas, the admin panel, the switcher (run by `scripts/e2e.sh` after `test/e2e.mjs`, each on a fresh D1) |
| `test/billing.test.mjs` | The webhook's signature, the balance in the meter |
| `test/billing.mjs` | End to end against `scripts/fake_stripe.mjs` |

---

## 12. Non-goals and open questions

- **Non-goals**: no metering of the instrument itself (the FM-1 is bought); no per-seat pricing; no annual plan
  beyond what Stripe offers; the bytes of a sample are costed, not sold.
- **Open questions**: the AI credits' price (one credit to what?); what the Pro price should be when the GPU costs
  are known from a real month's bill; whether a suspended account keeps its paid balance; whether the MCP server
  gets its own quotas for heavy tools.
