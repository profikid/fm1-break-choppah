# The mail to the Firmware Hub: sent — and it worked

**Sent on 4 November 2026**, from Laurens' own address to `zven@thefirmwarehub.com`.

The text that went out is [`MAIL.txt`](MAIL.txt), naming release **v0.89** and package
`choppah-0.89.fwsc`. That release and its package still exist, so the mail stays valid. Later
releases are picked up by the hub by itself (the hub takes every new release within three hours;
the identity and the asset name stay the same).

**No second mail is needed.** Later releases, a changed card, a changed picture or a changed
manual all go through `fmhub.json` and the GitHub releases — never through another mail.

## It is listed (verified 5 November 2026, 06:17 UTC)

The hub has CHOPPAH since **4 November 2026**, currently at **v0.93**:

```
curl -sL https://thefirmwarehub.com/catalog.json | python3 -c "
import json,sys
d=json.load(sys.stdin)
print(d['updated'], len(d['firmwares']))
c=[f for f in d['firmwares'] if f['id']=='choppah']
print(c[0]['version'], c[0]['added'], c[0]['installs'], c[0]['product'])"
```

- `id: choppah`, `version: 0.93`, `added: 2026-11-04`, `product: FM-1_86009300`
- `pkg: fw/choppah-0.93.fwsc`, `mirror: src/choppah-0.93.tar.gz`, sha256 aanwezig
- `card: fw/cards/choppah-3dfb976b.png`, `talk:` de discussions van de repo
- `beta: true`, `stars: 5`, `installs: 3` — het is een verse listing, de install-teller loopt nog op
- catalogus: 154 entries, `updated 2026-11-05 06:00 UTC`

**De keten volgt de hub zelf.** mutant.cloud, FM1 Manager, FM-SWITCH, FM-1-Swap, altstrument,
2bbb en de emulator nemen de hub-catalogus over — één listing zet CHOPPAH overal neer.

## What to watch

The hub reads `catalog.json` every three hours. Look for an entry with
`repo == "profikid/fm1-break-choppah"` or an `ident` starting with `FM-1_86`:

```
curl -sL https://thefirmwarehub.com/catalog.json | python3 -c "
import json,sys
d=json.load(sys.stdin)
print(d['updated'], len(d['firmwares']))
print([f for f in d['firmwares'] if 'choppah' in json.dumps(f).lower()])"
```

Sven may put it on his test site first, so it can take a while. If it does not appear after a
couple of days, the reply to the mail is the place to ask.

**Let op bij een nieuwe release:** de hub pakt de volgende release vanzelf binnen (drie uur).
De `version` in de catalogus loopt dan mee. De hub zet `beta: true` omdat de versie een 0.x-nummer
heeft — volgens `fmhub.md`: *"A version with beta, alpha, rc or a 0.x number gets a beta tag."*
Dat is de hub's eigen regel, niet iets uit `fmhub.json`.
