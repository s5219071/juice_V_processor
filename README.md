# JuiceEQ

JuiceEQ는 JUCE/C++ 기반의 VST3 스펙트럴 EQ/레조넌스 억제기입니다.

2048pt FFT로 입력 신호를 분석하고, 주변 대역보다 튀어나온 공진을 `Depth`,
`Sharpness`, `Selectivity` 파라미터에 따라 자동으로 눌러 줍니다. Delta 모드에서는
처리 후 소리가 아니라 제거된 성분만 들을 수 있고, Sidechain 모드에서는 외부 신호의
주파수 윤곽을 기준으로 메인 신호의 같은 대역을 비워 줍니다.

## 주요 기능

- 2048pt FFT 기반 실시간 스펙트럴 분석
- 512 sample hop, 4중 overlap-add 처리
- DAW 지연 보상을 위한 `setLatencySamples(2048)`
- APVTS 기반 `depth`, `sharpness`, `selectivity`, `softHard`, `delta`, `sidechain`, `mix`
- Delta monitoring
- 외부 sidechain 입력 분석
- Mad Lab Spectrum UI와 reduction curve visualizer
- CMake FetchContent 기반 JUCE 자동 다운로드

## 파일 구조

```text
JuiceEQ/
├─ CMakeLists.txt
├─ Source/
│  ├─ PluginProcessor.h
│  ├─ PluginProcessor.cpp
│  ├─ PluginEditor.h
│  └─ PluginEditor.cpp
└─ .github/
   └─ workflows/
      └─ build.yml
```

## 로컬 빌드

```bash
cmake -S . -B build
cmake --build build --config Release --target JuiceEQ_VST3
```

빌드가 끝나면 `JuiceEQ.vst3` 번들이 생성됩니다.

## GitHub Actions 빌드

저장소의 `Actions` 탭에서 `Build JuiceEQ VST3` 워크플로우를 실행하면 다음 산출물이
만들어집니다.

- `JuiceEQ-Windows-VST3.zip`
- `JuiceEQ-macOS-Universal-VST3.zip`

macOS 공개 배포에는 Apple Developer ID 서명과 notarization 설정이 추가로 필요합니다.
