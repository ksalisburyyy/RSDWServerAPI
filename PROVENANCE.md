# Source provenance

Target: https://github.com/ksalisburyyy/RSDWServerAPI
Baseline: 994a72092f77e9851797bbd6206c80a3ebb28e5f.
Custom source restored from named-audit-update/source.zip, byte-identical to
source.zip in THC-named-audits-0.5.0-thc.8.zip. Source version: 0.5.0-thc.8.
The source controller, watcher and embedded dashboard match deployed file hashes.
Community artwork was added from the owner's deployed non-secret artwork; the
archive already included the banner. Runtime behavior was not redesigned.

Archive SHA256: `45e6828a55c6f5391bbecf2049e7ae21423425e21bd4603d376b58a56b9be54f`.

Observed deployment manifest (hashes identify installed release artifacts, not a
new build from this checkout):

```json
{
  "version": "0.5.0-thc.8",
  "sha256": "7b5c43dd6c6000461660d3cea811e627f3a0c8af264612932487d1eb0abf71ee",
  "dashboardSha256": "c66dbe0bc9862526734b5f8dbae41f1cbc1013bfc59ad060f67f82daea6a1a63",
  "controllerSha256": "635a628a3543d8fcc3cb853d33089e984c1d9f5709f060896b26ee342be58776",
  "watcherSha256": "07a11d10612cdb52d25f0b564afc7b64ef41e597fb90250b50d1fd6925a1a026"
}
```

Historical release-note claims retain their original context. The obsolete
update_installer_test.py depended on an unshipped /workspace/output package for
update-controls-1; it was removed from this source checkout. Current native/UI/
controller/watcher installer rollback coverage remains in death_installer_test.py,
plus installer_manager_test.py and installer_process_test.py. No production or
old package files were removed. No game installation data or private config is included.
