(() => {
  'use strict';

  const byId = (id) => document.getElementById(id);
  const video = byId('video');
  const canvas = document.createElement('canvas');
  const context = canvas.getContext('2d', { willReadFrequently: true });
  const preview = byId('drop-zone');
  const status = byId('status');
  const startButton = byId('start-camera');
  const fileInput = byId('file-input');
  let stream = null;
  let scanning = false;
  let lastFrameAt = 0;
  let lastResult = '';

  function setStatus(message) { status.textContent = message; }

  function stopCamera() {
    scanning = false;
    if (stream) {
      stream.getTracks().forEach((track) => track.stop());
      stream = null;
    }
    video.srcObject = null;
    preview.classList.remove('live');
    byId('preview-label').textContent = 'CAMERA OFF';
    startButton.innerHTML = '<span class="camera-icon">◉</span> Start camera';
  }

  function showResult(text) {
    if (!text) return;
    lastResult = text;
    byId('empty-result').hidden = true;
    byId('result-content').hidden = false;
    byId('result-text').textContent = text;
    byId('result-tag').textContent = 'FOUND';
    byId('result-tag').classList.add('found');

    let isWebLink = false;
    try {
      const url = new URL(text);
      isWebLink = url.protocol === 'http:' || url.protocol === 'https:';
    } catch { /* Plain text is a valid QR result too. */ }
    byId('open-result').hidden = !isWebLink;
    setStatus('QR code found. Your result is ready below.');
  }

  function decodeFrame(source, width, height, inversionAttempts = 'dontInvert') {
    if (!window.jsQR || !width || !height) return null;
    const maxSide = 1152;
    const scale = Math.min(1, maxSide / Math.max(width, height));
    const targetWidth = Math.max(1, Math.round(width * scale));
    const targetHeight = Math.max(1, Math.round(height * scale));
    canvas.width = targetWidth;
    canvas.height = targetHeight;
    context.drawImage(source, 0, 0, targetWidth, targetHeight);
    const imageData = context.getImageData(0, 0, targetWidth, targetHeight);
    return window.jsQR(imageData.data, targetWidth, targetHeight, { inversionAttempts });
  }

  function scanLoop(timestamp) {
    if (!scanning) return;
    if (timestamp - lastFrameAt >= 130 && video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA) {
      lastFrameAt = timestamp;
      try {
        const code = decodeFrame(video, video.videoWidth, video.videoHeight);
        if (code && code.data) {
          stopCamera();
          showResult(code.data);
          return;
        }
      } catch {
        setStatus('Could not read this camera frame. Try adjusting the lighting.');
      }
    }
    requestAnimationFrame(scanLoop);
  }

  async function startCamera() {
    if (scanning) { stopCamera(); setStatus('Camera stopped.'); return; }
    if (!window.jsQR) { setStatus('Decoder is still loading. Check your internet connection and retry.'); return; }
    if (!navigator.mediaDevices?.getUserMedia) { setStatus('Camera access is not available on this PC. Try Scan image instead.'); return; }
    try {
      setStatus('Requesting camera access…');
      stream = await navigator.mediaDevices.getUserMedia({
        audio: false,
        video: { facingMode: { ideal: 'environment' }, width: { ideal: 1280 }, height: { ideal: 720 } }
      });
      video.srcObject = stream;
      await video.play();
      preview.classList.add('live');
      byId('preview-label').textContent = 'SCANNING';
      startButton.innerHTML = 'Stop camera';
      scanning = true;
      lastFrameAt = 0;
      setStatus('Point your camera at a QR code.');
      requestAnimationFrame(scanLoop);
    } catch (error) {
      stopCamera();
      const message = error?.name === 'NotAllowedError'
        ? 'Camera permission was denied. Allow camera access in Windows settings and retry.'
        : 'Unable to start the camera. Check that it is connected and not in use by another app.';
      setStatus(message);
    }
  }

  async function scanImage(file) {
    if (!file || !file.type.startsWith('image/')) { setStatus('Choose a supported image file.'); return; }
    if (!window.jsQR) { setStatus('Decoder is still loading. Check your internet connection and retry.'); return; }
    stopCamera();
    setStatus('Scanning image…');
    const image = new Image();
    const objectUrl = URL.createObjectURL(file);
    image.onload = () => {
      try {
        const result = decodeFrame(image, image.naturalWidth, image.naturalHeight, 'attemptBoth');
        if (result?.data) showResult(result.data);
        else setStatus('No QR code found in this image. Try a clearer or higher-resolution image.');
      } catch {
        setStatus('This image could not be decoded. Try another image.');
      } finally {
        URL.revokeObjectURL(objectUrl);
      }
    };
    image.onerror = () => { URL.revokeObjectURL(objectUrl); setStatus('Could not open this image.'); };
    image.src = objectUrl;
  }

  startButton.addEventListener('click', startCamera);
  byId('choose-image').addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', () => {
    const [file] = fileInput.files || [];
    if (file) scanImage(file);
    fileInput.value = '';
  });

  byId('copy-result').addEventListener('click', async () => {
    if (!lastResult) return;
    try {
      await navigator.clipboard.writeText(lastResult);
      setStatus('Copied to clipboard.');
    } catch {
      const textArea = document.createElement('textarea');
      textArea.value = lastResult;
      document.body.appendChild(textArea);
      textArea.select();
      document.execCommand('copy');
      textArea.remove();
      setStatus('Copied to clipboard.');
    }
  });

  byId('open-result').addEventListener('click', () => {
    if (!lastResult) return;
    try {
      const url = new URL(lastResult);
      if (url.protocol === 'http:' || url.protocol === 'https:') window.open(url.href, '_blank', 'noopener');
    } catch { /* Ignore non-URL content. */ }
  });

  for (const eventName of ['dragenter', 'dragover']) {
    preview.addEventListener(eventName, (event) => { event.preventDefault(); preview.classList.add('dragging'); });
  }
  for (const eventName of ['dragleave', 'drop']) {
    preview.addEventListener(eventName, (event) => { event.preventDefault(); preview.classList.remove('dragging'); });
  }
  preview.addEventListener('drop', (event) => {
    const file = [...(event.dataTransfer?.files || [])].find((item) => item.type.startsWith('image/'));
    if (file) scanImage(file);
    else setStatus('Drop an image file to scan it.');
  });

  window.addEventListener('paste', (event) => {
    const item = [...(event.clipboardData?.items || [])].find((entry) => entry.type.startsWith('image/'));
    const file = item?.getAsFile();
    if (file) scanImage(file);
  });

  const decoderCheck = window.setInterval(() => {
    if (window.jsQR) {
      window.clearInterval(decoderCheck);
      byId('engine-status').textContent = 'Decoder ready';
      byId('engine-status').classList.add('ready');
      startButton.disabled = false;
      setStatus('Ready. Start your camera or choose an image.');
    }
  }, 100);
  window.setTimeout(() => {
    if (!window.jsQR) {
      window.clearInterval(decoderCheck);
      byId('engine-status').textContent = 'Decoder unavailable';
      byId('engine-status').classList.add('error');
      setStatus('Could not load the QR decoder. Connect to the internet, then restart QuickQR.');
    }
  }, 12000);

  window.addEventListener('beforeunload', stopCamera);
})();
