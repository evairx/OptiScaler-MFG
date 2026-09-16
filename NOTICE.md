# Fork notice

**OptiScaler-MFG** is an unofficial fork of **OptiScaler**.

- Upstream project: https://github.com/optiscaler/OptiScaler
- This fork: https://github.com/evairx/OptiScaler-MFG
- Fork maintainer: evairx

This fork is **not** affiliated with, endorsed by, or supported by the upstream
OptiScaler project, its authors (cdozdil and contributors), or NVIDIA. Do not
report fork-specific issues to the upstream project.

## License and modifications

OptiScaler is licensed under the **GNU General Public License v3.0** (see
[LICENSE](LICENSE)). This fork is distributed under the same license and does
not relicense any upstream code.

In accordance with GPLv3 section 5(a), modifications made by this fork are
documented by this repository's version control history. Fork-specific
additions include, non-exhaustively:

- The experimental NVIDIA Multi Frame Generation unlock for Ada (RTX 40) and
  the SM86/SM75 loader for Ampere/Turing (RTX 30/20).
- The unified frame generation menu and its status reporting.
- Streamline DLSS-G presentation work for the OptiFG input path.

## Components and third-party material

- The `dlssg_sm86` runtime (GPLv3-derived, see
  [OptiScaler/dlssg_sm86/THIRD_PARTY_NOTICES.txt](OptiScaler/dlssg_sm86/THIRD_PARTY_NOTICES.txt)).
- The Ada MFG compatibility work derived from MFGAdaUnlock-RenoDx (MIT).
- Model and kernel assets extracted from NVIDIA's `nvngx_dlssg.dll`, which are
  separate third-party material and are **not** relicensed by this project.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details and for the
corresponding source offer of the distributed GPLv3 binaries.
