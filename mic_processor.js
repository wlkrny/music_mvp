/**
 * MicProcessor — Real-time microphone audio processing
 *
 * Provides:
 *   - RMS volume level (0–100)
 *   - Pitch detection via normalized autocorrelation
 *   - Real-time callback for continuous monitoring
 *
 * Dependencies: none (Web Audio API, available in all modern browsers)
 *
 * Usage:
 *   const mic = new MicProcessor();
 *   mic.onUpdate = (vol, pitch) => console.log(`Vol: ${vol.toFixed(1)}  Pitch: ${pitch?.note ?? '—'}`);
 *   await mic.start();
 *   // … later …
 *   mic.stop();
 */

class MicProcessor {
  constructor() {
    // ── Internal state ──────────────────────────────────────────────────
    /** @type {AudioContext|null} */
    this._audioCtx = null;
    /** @type {MediaStream|null} */
    this._stream = null;
    /** @type {MediaStreamAudioSourceNode|null} */
    this._source = null;
    /** @type {AnalyserNode|null} */
    this._analyser = null;
    /** @type {number|null} requestAnimationFrame handle */
    this._rafId = null;
    /** @type {boolean} */
    this._isActive = false;

    // Cached latest results (updated in the animation loop)
    /** @type {number} */
    this._volume = 0;
    /** @type {{ note: string, freq: number }|null} */
    this._pitch = null;

    // Frame counter used to throttle expensive pitch detection
    this._frameCount = 0;

    // ── Public configuration ────────────────────────────────────────────

    /**
     * FFT size for the AnalyserNode.
     * Larger values improve low-frequency pitch resolution at the cost of latency.
     * Must be a power of 2 in [32, 32768].
     * @type {number}
     */
    this.fftSize = 4096;

    /**
     * Minimum normalized-autocorrelation value to accept a pitch as valid.
     * Range 0–1. Higher → fewer false positives, more false negatives.
     * @type {number}
     */
    this.pitchConfidenceThreshold = 0.8;

    /**
     * Pitch detection is O(N²) on time-domain data.
     * Process pitch once every N animation frames to stay performant.
     * Volume is always computed every frame (it is cheap).
     * @type {number}
     */
    this.pitchUpdateInterval = 2;

    /**
     * Callback invoked after each analysis frame.
     *   (volume: number, pitch: { note: string, freq: number } | null) => void
     * @type {Function|null}
     */
    this.onUpdate = null;
  }

  // ── Public API ───────────────────────────────────────────────────────────

  /**
   * Request microphone permission and start the Web Audio processing pipeline.
   *
   * **Must** be called from a user-gesture context (click / tap) because of
   * browser autoplay policies.
   *
   * @returns {Promise<void>}
   * @throws {Error}  Permission denied, no device found, or setup failure.
   */
  async start() {
    if (this._isActive) return;

    // 1. Request the microphone stream ──────────────────────────────────
    try {
      this._stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false,
        },
      });
    } catch (err) {
      const name = err.name || '';
      if (name === 'NotAllowedError' || name === 'PermissionDeniedError') {
        throw new Error('Microphone permission was denied.');
      }
      if (name === 'NotFoundError' || name === 'DevicesNotFoundError') {
        throw new Error('No microphone device found.');
      }
      if (name === 'NotReadableError') {
        throw new Error('Microphone is in use by another application.');
      }
      throw new Error(`Failed to access microphone: ${err.message}`);
    }

    // 2. Build the Web Audio graph ──────────────────────────────────────
    try {
      this._audioCtx = new (window.AudioContext || window.webkitAudioContext)();

      // Resume if suspended (browser autoplay policy may suspend new contexts)
      if (this._audioCtx.state === 'suspended') {
        await this._audioCtx.resume();
      }

      this._analyser = this._audioCtx.createAnalyser();
      this._analyser.fftSize = this.fftSize;
      this._analyser.smoothingTimeConstant = 0; // raw samples, no time smoothing

      this._source = this._audioCtx.createMediaStreamSource(this._stream);
      this._source.connect(this._analyser);
      // NOTE: deliberately NOT connecting to destination — avoid feedback loop

      this._isActive = true;
      this._frameCount = 0;
      this._processLoop();
    } catch (err) {
      // Roll back the stream if the audio graph setup fails
      this._releaseStream();
      throw new Error(`Failed to initialize audio context: ${err.message}`);
    }
  }

  /**
   * Stop microphone capture and release all resources (stream, nodes, context).
   * Safe to call at any time, even when already stopped.
   */
  stop() {
    this._isActive = false;

    if (this._rafId !== null) {
      cancelAnimationFrame(this._rafId);
      this._rafId = null;
    }

    if (this._source) {
      this._source.disconnect();
      this._source = null;
    }

    if (this._audioCtx) {
      // close() is async but we fire-and-forget; errors are harmless
      this._audioCtx.close().catch(() => {});
      this._audioCtx = null;
      this._analyser = null;
    }

    this._releaseStream();
  }

  /**
   * Latest RMS volume level (0–100).
   * @returns {number}
   */
  getVolume() {
    return this._volume;
  }

  /**
   * Latest detected pitch, or `null` when no clear pitch is present.
   * @returns {{ note: string, freq: number } | null}
   */
  getPitch() {
    return this._pitch;
  }

  /**
   * Whether the microphone is currently streaming and being analysed.
   * @returns {boolean}
   */
  isActive() {
    return this._isActive;
  }

  // ── Private: processing loop ─────────────────────────────────────────────

  /**
   * Main analysis loop (driven by requestAnimationFrame).
   * Reads raw time-domain data and fires the onUpdate callback.
   */
  _processLoop() {
    if (!this._isActive) return;

    this._rafId = requestAnimationFrame(() => this._processLoop());

    // Grab the current time-domain buffer (Float32, range [-1, 1])
    const bufferLen = this._analyser.fftSize;
    const data = new Float32Array(bufferLen);
    this._analyser.getFloatTimeDomainData(data);

    // Volume is O(N) — compute every frame
    this._volume = this._computeVolume(data);

    // Pitch detection is O(N²) — throttle
    this._frameCount++;
    if (this._frameCount % this.pitchUpdateInterval === 0) {
      this._pitch = this._computePitch(data);
    }

    if (typeof this.onUpdate === 'function') {
      this.onUpdate(this._volume, this._pitch);
    }
  }

  // ── Private: volume ──────────────────────────────────────────────────────

  /**
   * Compute RMS (root mean square) volume from time-domain samples.
   * Linear mapping: RMS 1.0 → 100, RMS 0.0 → 0.
   *
   * @param {Float32Array} samples  Time-domain samples in [-1, 1].
   * @returns {number} Volume 0–100.
   */
  _computeVolume(samples) {
    let sumSq = 0;
    for (let i = 0; i < samples.length; i++) {
      sumSq += samples[i] * samples[i];
    }
    const rms = Math.sqrt(sumSq / samples.length);
    // Clamp to [0, 100] — RMS rarely exceeds 1.0 for mic input
    return Math.min(100, Math.max(0, rms * 100));
  }

  // ── Private: pitch detection (autocorrelation) ───────────────────────────

  /**
   * Detect the fundamental frequency using **normalised autocorrelation**
   * on the time-domain buffer.
   *
   * Algorithm outline:
   *   1. For every lag (period) in the search range, compute r(lag).
   *   2. Find the lag with the highest normalised correlation.
   *   3. If peak > pitchConfidenceThreshold → signal is periodic.
   *   4. Parabolic interpolation around the peak for sub-sample precision.
   *   5. Map frequency → nearest equal-temperament note (A4 = 440 Hz).
   *
   * @param {Float32Array} samples  Time-domain samples in [-1, 1].
   * @returns {{ note: string, freq: number } | null}
   */
  _computePitch(samples) {
    const N = samples.length;
    const sampleRate = this._audioCtx.sampleRate;

    // Search range in Hz
    const MIN_FREQ = 50;   // ~G1
    const MAX_FREQ = 2000; // ~B6
    const minLag = Math.max(1, Math.floor(sampleRate / MAX_FREQ));
    const maxLag = Math.min(N - 1, Math.floor(sampleRate / MIN_FREQ));

    if (maxLag <= minLag) return null;

    let bestLag = -1;
    let bestCorr = -1;

    // Scan every integer lag in the valid range
    for (let lag = minLag; lag <= maxLag; lag++) {
      const corr = this._normAutocorr(samples, N, lag);
      if (corr > bestCorr) {
        bestCorr = corr;
        bestLag = lag;
      }
    }

    // Reject if the best correlation is below the confidence threshold
    if (bestCorr < this.pitchConfidenceThreshold) return null;

    // ── Parabolic interpolation ───────────────────────────────────────
    // Given three points (lag-1, y0), (lag, y1), (lag+1, y2) on the
    // correlation curve, the true peak of the parabola lies at:
    //   lag + (y0 - y2) / (2 · (2·y1 - y0 - y2))
    let refinedLag = bestLag;
    if (bestLag > minLag && bestLag < maxLag) {
      const y0 = this._normAutocorr(samples, N, bestLag - 1);
      const y2 = this._normAutocorr(samples, N, bestLag + 1);
      const denom = 2 * (2 * bestCorr - y0 - y2);
      if (Math.abs(denom) > 1e-10) {
        refinedLag = bestLag + (y0 - y2) / denom;
      }
    }

    const freq = sampleRate / refinedLag;
    const note = this._freqToNoteName(freq);

    return { note, freq: Math.round(freq * 100) / 100 };
  }

  /**
   * Compute the normalised autocorrelation at a single lag.
   *
   *   r(lag) = Σ x[i]·x[i+lag] / √(Σ x[i]² · Σ x[i+lag]²)
   *
   * The result lies in [-1, 1]. A value near 1 indicates strong periodicity
   * at that lag.
   *
   * @param {Float32Array} samples
   * @param {number} N       Total number of samples.
   * @param {number} lag     Lag (in samples).
   * @returns {number} Normalised autocorrelation [-1, 1].
   */
  _normAutocorr(samples, N, lag) {
    const winLen = N - lag;
    let sumProd = 0;
    let sumSq1 = 0;
    let sumSq2 = 0;

    for (let i = 0; i < winLen; i++) {
      const a = samples[i];
      const b = samples[i + lag];
      sumProd += a * b;
      sumSq1 += a * a;
      sumSq2 += b * b;
    }

    const denom = Math.sqrt(sumSq1 * sumSq2);
    return denom > 1e-10 ? sumProd / denom : 0;
  }

  // ── Private: frequency → note name ───────────────────────────────────────

  /**
   * Convert a frequency (Hz) to the closest musical note name using
   * equal temperament with A4 = 440 Hz.
   *
   * Examples:  440.0 → "A4"    261.63 → "C4"    830.61 → "G#5"
   *
   * @param {number} freq  Frequency in Hz.
   * @returns {string}     Note name, e.g. "A4", "C#3", "F5".
   */
  _freqToNoteName(freq) {
    const NOTE_NAMES = [
      'C', 'C#', 'D', 'D#', 'E', 'F',
      'F#', 'G', 'G#', 'A', 'A#', 'B',
    ];
    const A4_FREQ = 440;

    // MIDI note 69 = A4 (440 Hz)
    const midi = 12 * Math.log2(freq / A4_FREQ) + 69;
    const rounded = Math.round(midi);

    // Normalise negative MIDI numbers into the 0–11 semitone range
    const idx = ((rounded % 12) + 12) % 12;
    const octave = Math.floor(rounded / 12) - 1;

    return `${NOTE_NAMES[idx]}${octave}`;
  }

  // ── Private: helpers ─────────────────────────────────────────────────────

  /**
   * Stop all tracks on the media stream and release the reference.
   */
  _releaseStream() {
    if (this._stream) {
      this._stream.getTracks().forEach((track) => track.stop());
      this._stream = null;
    }
  }
}

// ── Exports ────────────────────────────────────────────────────────────────
// Works as a plain <script> tag (MicProcessor becomes a global),
// CommonJS require(), or AMD define().
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
  module.exports = { MicProcessor };
} else if (typeof define === 'function' && define.amd) {
  define(() => ({ MicProcessor }));
} else if (typeof window !== 'undefined') {
  window.MicProcessor = MicProcessor;
}
