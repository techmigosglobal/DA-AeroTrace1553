# Workspace cleanup — 28 October 2026

The following generated/machine-specific content was moved out of the project into
`C:/Users/Vinay/Downloads/aero trace/aerotrace-cleanup-20261028/`:

- `build/`, `build-core/`, `build-qwt/`: old generated build trees, including the cache referring to another computer.
- `artifacts/`: prior generated screenshots and logs.
- `.qtcreator/`: machine-specific IDE user settings, including stale/empty run configurations.
- `installers/`: downloaded Qt installer, not application source.
- All three old distribution folders: base, navigation-fix and scroll-fix packages. Recordings and preferences inside them are preserved in the archive.

New builds use `out/build/`, verification output uses `out/validation/`, and the current package uses `dist/AeroTrace1553/`.

No source, requirement documents, `UI_Screens`, concept boards, or required Qwt dependency sources/libraries were removed. No archive contents were permanently erased. Restore a particular folder/file by copying it from the sibling archive; do not copy its CMake cache back into the new build.
