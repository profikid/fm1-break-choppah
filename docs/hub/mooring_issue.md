CHOPPAH is already in the list (the README entry, merged as PR #10), but it is not in `mooring/catalog.json` yet. Here is the entry, with every value read out of the release asset rather than taken from the README, so it should drop straight into the next revision.

```json
{
  "download": {
    "family": "choppah",
    "flashSize": 602112,
    "github": "profikid/fm1-break-choppah",
    "license": "MPL-2.0",
    "loaderSHA256": "87bb282081cb1c40666d51975d9dcf14bc8334b2933ffd5971bc185a04accf0c",
    "notes": [],
    "releaseTag": "v0.85",
    "requiresStockV15": true,
    "size": 611743,
    "sourceRepo": "https://github.com/profikid/fm1-break-choppah",
    "url": "https://github.com/profikid/fm1-break-choppah/releases/download/v0.85/choppah-0.85.fwsc"
  },
  "label": "CHOPPAH 0.85 (choppah-0.85.fwsc)",
  "product": "FM-1_86008500",
  "rawSize": 611743,
  "sha256": "23fc2e71d5e716b17f8c094ab8f99704a00dad7294df3096d85efcac8a039bcc",
  "stock": false
}
```

Where the values come from, and how they were checked:

- **product** `FM-1_86008500`: read out of the package's identity marker bytes (the 20 bytes after each of the first 20 blocks), and confirmed in the build log of that release. The prefix `FM-1_86` is CHOPPAH's own; every release keeps it, so the identity is stable across versions.
- **sha256** / **rawSize**: the whole `choppah-0.85.fwsc`.
- **loaderSHA256**: the SHA-256 of `ota.bin`, the update loader inside the package. I verified the method against a known entry (AMB-1 1.3, whose catalogue value `4800228962602bb1...` is exactly the SHA-256 of its `ota.bin`), so this is the same hash the catalogue uses. The loader is the same in every CHOPPAH release, so this value has not changed since v0.66.
- **requiresStockV15** true: the firmware expects M-VAVE's stock firmware first (the README's flashing page says the same).
- **license** MPL-2.0, the `LICENSE` in the repository root.

About the loader: it is felucca-bsp's, the platform's loader, shipped in every package. The `loaderSHA256` above is the loader as built inside this package. If the catalogue's loader families are keyed by the source (`feluccaG1`, `feluccaG2`, `sloopG3`), this one may want a new family, or an existing one, depending on how the loader compares to those.

One thing worth knowing for the newer units: CHOPPAH accepts both flash parts, the stock Puya P25Q80 and the UC25HQ80 that the units sold since autumn 2026 carry, so it installs and saves on those as well. Firmwares without that fix take those units for unknown and cannot install over them at all.

CHOPPAH is also on the FM-1 Firmware Hub's submission list, and its card's file (`fmhub.json`) is in the repository, so the Hub can pick it up on its own once the submission mail is sent.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
