# The mail to the Firmware Hub: sent

**Sent on 4 November 2026**, from Laurens' own address to `zven@thefirmwarehub.com`.

The text that went out is [`MAIL.txt`](MAIL.txt), naming release **v0.89** and package
`choppah-0.89.fwsc`. That release and its package still exist, so the mail stays valid. Later
releases are picked up by the hub by itself (the hub takes every new release within three hours;
the identity and the asset name stay the same).

**No second mail is needed.** Later releases, a changed card, a changed picture or a changed
manual all go through `fmhub.json` and the GitHub releases — never through another mail.

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
