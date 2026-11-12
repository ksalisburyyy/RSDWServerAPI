# Shared map

Import an image and calibrate using three non-collinear world/image landmarks,
or enter known coordinate bounds. Image-only maps do not imply calibrated markers.
Publish through the controller to share the profile across browsers; revisions
prevent stale drafts overwriting another admin's publication. Browser drafts and
import/export remain supported. Preserve server shared-map.json and its previous
copy during maintenance. Map clicks compute X/Y; terrain Z must come from a known
landmark or player position. No map artwork or guessed world bounds are bundled.
