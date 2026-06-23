# DAS wxWidgets Host Demo

This directory contains a Visual Studio + wxWidgets prototype for a DAS host application.

Implemented now:

- wxWidgets desktop GUI with parameter controls.
- AOM chirp / DA configuration fields, including 300 MHz sweep bandwidth.
- AOM optical switch pulse settings.
- AD acquisition settings for 4-channel X/Y + BPD style input, 14-bit depth, 1.3 Gsps default.
- Simulated Rayleigh backscatter and vibration event data while the acquisition card is absent.
- Polarization-diversity gauge demodulation.
- Optional CUDA gauge-product acceleration when CUDA is available.
- GUI plots for Rayleigh trace, dynamic strain waterfall, event time trace, and event spectrum.

The simulator uses a compressed/baseband equivalent signal instead of allocating raw 1.3 Gsps DMA captures. This keeps the demo interactive; the `NiDmaDecoder` and acquisition-facing data structures are the intended insertion point for the real board SDK and NI frame format.

## Build

Open a Developer PowerShell for Visual Studio, then run:

```powershell
cd C:\Users\Nico\Documents\Projects\nico\DAS\das_wx_host
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
.\build\Release\das_wx_host.exe
```

If you already have a newer Visual Studio installed, you can also omit `-G` and let CMake pick the newest detected Visual Studio generator automatically.

If you want to open the generated solution:

```powershell
start .\build\DASWxHost.sln
```

The CMake defaults assume:

- wxWidgets root: `C:\wxWidgets-3.3.2`
- wxWidgets library directory: `C:\wxWidgets-3.3.2\lib\vc_x64_lib`
- Static Unicode wxWidgets config: `mswu`

CUDA is detected automatically. If CUDA is not available, the program builds and runs with the CPU demodulator.

## Hardware Integration Notes

Replace the simulation source in `MainFrame::RunSimulationWorker()` with an acquisition worker that:

1. Programs the DA chirp waveform and AOM pulse timing.
2. Starts AD DMA for X/Y + BPD channels.
3. Decodes the NI 14-bit stream into interleaved channels.
4. Feeds compressed/baseband complex traces into `DasDemodulator`.

The current `NiDmaDecoder` contains a conservative 14-bit sign-extension helper and an unpacked 16-bit path. Final packed NI decoding should be adjusted to the exact sample order/packing reported by the acquisition-card SDK.
