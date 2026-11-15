# TrackWerk 1.1 publication

Publish only the owner-approved bytes recorded by PUBLIC-RELEASE-MANIFEST.json.
The 1.1 application source is common to Android and Linux; do not apply the historical 1.0 overlay.
Keep the signed Android APK unchanged. Keep the matching application and mmiyoo source assets adjacent.

1. Verify the prepared canonical commit against the approved source and website manifest.
2. After owner approval, push that reviewed commit to main without force and tag it v1.1.0.
3. Create the v1.1.0 GitHub Release using the reviewed English release notes; attach exactly the approved assets.
4. Confirm every asset SHA-256 and download, then dispatch the manual publish-trackwerk-website.yml workflow.
5. Check live website, guide, embedded film, audio, device downloads and update instructions.
6. Upload the approved long video and vertical teaser; use the reviewed descriptions, chapters,
   captions, thumbnail and soundtrack credits. No private samples, keys or account data are public assets.

The old v1.0.0 and earlier tags remain historical releases. Historical workflows are not the 1.1 publishing entry point.
