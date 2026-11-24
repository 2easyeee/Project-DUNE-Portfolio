# ERG (에르그)

Unreal Engine 5 기반 1인칭 액션 게임 개인 프로젝트입니다.

런타임 메시 절단, 물리 기반 파괴, 모래 지형 변형, 보스 전투 같은 게임플레이 핵심 시스템을 C++로 구현했습니다.

본 저장소는 **소스 코드를 보여주기 위한 저장소**입니다. 모델, 텍스처, 폰트, 레벨 등 에셋은 포함하지 않으므로 이 저장소만으로는 프로젝트가 빌드되거나 실행되지 않습니다.

## 프로젝트 정보

- **개발 기간:** 2026.10 - 2026.11
- **개발 인원:** 1명
- **개발 환경:** C++, Unreal Engine 5.8
- **AI 개발 도구:** Claude Code

> **ERG(에르그)의 뜻**
> - 사하라 등 사막에서 모래 언덕이 끝없이 이어지는 거대한 모래바다 지형을 뜻하는 말입니다. (아랍어 `عرق`)
> - 물리학에서 일과 에너지를 나타내는 CGS 단위이기도 합니다. 1 erg = 1 dyne·cm = 10⁻⁷ J

## 주요 구현 기능

- **Runtime Mesh Slicing:** 런타임 메시 절단, 절단면 재구성 및 물리 충돌 처리 (`SliceableStructure`, `KrisKnifeComponent`)
- **Physics-Based Destruction:** 총격 지점 기반 구조물 균열, 물리 파편 생성 및 발사 (`DuneImpactManager`, `DuneDebrisLauncher`)
- **Sand Deformation Plugin:** GPU 컴퓨트 셰이더 기반 실시간 모래 높이장 시뮬레이션. 발자국, 바퀴, 충돌 등 여러 상호작용을 하나의 요청 구조로 처리 (`Plugins/SandDeformation`)
- **Boss Fight:** 석판 공격, 받아치기, 모래 폭풍 패턴을 가진 슬랩 보스 (`DuneSlabBoss`)
- **First-Person Combat:** 1인칭 팔 애니메이션 블렌딩, 총기 반동, 모래로 분해되는 처치 연출, 추적형 적

## 저장소 구성

| 폴더 | 내용 |
|---|---|
| `Source/Dune/` | 절단, 파괴, 무기, 모래 지형 연동 등 게임플레이 코드 |
| `Source/Dune/Boss/` | 보스와 보스 투사체 |
| `Source/Dune/Combat/` | 플레이어, 1인칭 팔, 적, 파편 발사기, 모래 연출 |
| `Plugins/SandDeformation/` | 모래 변형 플러그인 (런타임 모듈, 셰이더 모듈, HLSL, README) |

## AI-Assisted Development Workflow

Claude Code를 활용해 코드 분석, C++ 구현, 리팩터링, 오류 해결을 보조하는 워크플로로 개발했습니다.

1. **설계 및 구현 방향 결정:** 기능 요구사항과 핵심 알고리즘을 정의하고 Unreal Engine API와 기존 구조를 검토
2. **AI 기반 구현:** Claude Code로 코드 작성 및 개선
3. **검증 및 디버깅:** 동작 원리와 의존성 검토, 에디터와 PIE 환경에서 실제 테스트, 기존 기능에 미치는 영향 확인

시스템 설계와 기술적 의사결정, 최종 코드 검증은 개발자가 직접 수행하는 것을 원칙으로 합니다.
