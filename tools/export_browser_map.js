// Run on the OLD dashboard address in the browser where its map was calibrated.
// Reads that origin's map only; exports no bearer token or other settings.
(async () => {
  const db = await new Promise((resolve, reject) => {
    const request = indexedDB.open('thc-map-profiles', 1);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(Error('Could not open the saved map'));
  });
  let record;
  try {
    record = await new Promise((resolve, reject) => {
      const request = db.transaction('maps', 'readonly').objectStore('maps').get(location.host);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(Error('Could not read the saved map'));
    });
  } finally { db.close(); }
  if (!record || record.schema !== 1 || !record.image || !record.config)
    throw Error('No calibrated map saved at this dashboard address. Open the original address/browser.');
  const blob = record.image;
  if (!(blob instanceof Blob) || blob.size > 10 * 1024 * 1024 || !['image/png','image/jpeg','image/webp'].includes(blob.type)) throw Error('Unsupported saved map image');
  const data = await new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result.split(',')[1]);
    reader.onerror = () => reject(Error('Could not export the map image'));
    reader.readAsDataURL(blob);
  });
  const profile = {schema: 1, config: record.config, anchors: record.anchors || record.config.anchors || [], image: {mime: blob.type, data}};
  const url = URL.createObjectURL(new Blob([JSON.stringify(profile)], {type: 'application/json'}));
  const link = document.createElement('a'); link.href = url; link.download = 'THC-map-profile.json'; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  console.log('THC map image and landmarks exported. Original browser map retained.');
})();
