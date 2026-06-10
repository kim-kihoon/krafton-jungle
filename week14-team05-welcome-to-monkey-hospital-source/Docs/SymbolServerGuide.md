# Symbol Server 사용 가이드

## 한 줄 요약

Symbol Server 흐름은 Release 빌드에서 나온 `.pdb`, `.exe`, `.dll`을 버전명과 함께 심볼 저장소에 올리고, 크래시 덤프 분석 시 같은 버전의 심볼과 소스 정보를 빠르게 찾기 위한 절차입니다.

## 왜 필요한가

크래시 덤프(`.dmp`)만으로는 콜스택을 완전히 읽기 어렵습니다. Visual Studio가 함수명, 파일명, 라인 정보를 복원하려면 해당 실행 파일과 정확히 맞는 PDB가 필요합니다.

그래서 Release를 배포할 때 다음 정보를 같이 관리합니다.

```text
덤프 파일
  -> 실행 중 크래시가 난 순간의 메모리/스레드 상태

PDB 파일
  -> 함수명, 파일명, 라인 정보 등 디버깅 심볼

BuildInfo.txt
  -> 이 덤프가 어떤 빌드/commit/심볼 경로에서 나온 것인지 알려주는 메타데이터

SymbolLogs
  -> 해당 PDB가 어떤 소스 경로와 source indexing 정보를 갖는지 확인한 로그
```

이 흐름의 목적은 덤프를 받은 사람이 다음을 바로 알 수 있게 하는 것입니다.

```text
이 덤프는 어떤 BuildVersion에서 나온 것인가?
어떤 Git commit의 코드인가?
심볼 서버 경로는 어디인가?
PDB 안에 Source Server 정보가 들어 있는가?
```

## 현재 프로젝트의 파일 구성

관련 파일은 다음과 같습니다.

```text
PackageRelease.bat
  Release 빌드, BuildInfo 생성, 패키지 생성, 심볼 업로드를 한 번에 수행합니다.

UploadSymbols.bat
  Release PDB에 source indexing 정보를 넣고, 검증 로그를 만든 뒤, 심볼 서버에 업로드합니다.

Scripts/GenerateSrcSrvStream.ps1
  PDB 안에 들어갈 srcsrv 스트림을 만들고 pdbstr.exe -w로 PDB에 삽입합니다.

KraftonEngine/Source/Engine/Platform/BuildInfo.h
  실행 파일에 포함되는 빌드 메타데이터입니다.

KraftonEngine/Source/Engine/Platform/CrashDump.cpp
  크래시 발생 시 .dmp와 _BuildInfo.txt를 함께 생성합니다.
```

## Release 패키징 방법

릴리즈를 만들 때는 프로젝트 루트에서 다음 명령을 실행합니다.

```bat
.\PackageRelease.bat 20260527_2100
```

`20260527_2100`은 이 Release를 식별하는 버전명입니다. 팀에서 정한 규칙에 맞게 바꾸면 됩니다. 배치 파일을 더블클릭하거나 인자 없이 실행하면 `VersionName`을 물어보고, 그냥 Enter를 누르면 현재 시간 기반 기본값(`yyyyMMdd_HHmm`)을 사용합니다.

추천 형식:

```text
Week12_Release_20260527_2100
20260527_2100
```

중요한 점은 같은 버전명이 다음 위치에 동일하게 사용된다는 것입니다.

```text
BuildInfo.h의 BuildVersion
ReleaseBuild/BuildInfo.txt의 BuildVersion
symstore.exe 업로드 버전
SymbolLogs 파일명
크래시 시 생성되는 _BuildInfo.txt의 BuildVersion
```

## PackageRelease.bat의 전체 흐름

`PackageRelease.bat`는 내부적으로 다음 순서로 동작합니다.

```text
1. BuildInfo.h 생성
2. Visual Studio Release x64 빌드
3. ReleaseBuild 폴더 생성
4. exe/dll/Shaders/Content/Settings 복사
5. ReleaseBuild/BuildInfo.txt 생성
6. UploadSymbols.bat [VersionName] --no-pause 호출
```

즉 일반적으로는 `UploadSymbols.bat`를 따로 실행하지 않고, `PackageRelease.bat` 하나만 실행하면 됩니다.

## UploadSymbols.bat의 역할

`UploadSymbols.bat`는 단순히 PDB를 업로드하는 파일이 아닙니다. 현재는 다음 작업을 함께 합니다.

```text
1. srctool.exe, pdbstr.exe 존재 확인
2. Release PDB에 Source Server 정보 삽입
3. PDB source info 검증 로그 생성
4. 심볼 서버에 .pdb, .exe, .dll 업로드
```

심볼 서버 경로:

```text
\\SYMBOL-SERVER\Symbols\Team7
```

Visual Studio나 WinDbg에서 사용할 심볼 경로:

```text
srv*C:\SymbolCache*\\SYMBOL-SERVER\Symbols\Team7
```

## srctool.exe와 pdbstr.exe의 의미

이 두 도구는 Windows Debugging Tools에 포함되어 있습니다.

일반적인 설치 위치:

```text
C:\Program Files (x86)\Windows Kits\10\Debuggers\x64
C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\srcsrv
```

각 도구의 역할은 다릅니다.

```text
srctool.exe
  PDB가 기억하는 소스 파일 목록과 source indexing 상태를 확인합니다.

pdbstr.exe
  PDB 안의 srcsrv 스트림을 읽거나 씁니다.
```

현재 프로젝트에서는 `pdbstr.exe -w`를 사용해 PDB에 `srcsrv` 스트림을 실제로 삽입합니다. 그 뒤 `pdbstr.exe -r`로 다시 읽어서 정상 삽입 여부를 확인합니다.

## Source Indexing이란

Source Indexing은 PDB 안에 "이 소스 파일은 어떤 Git commit의 어떤 경로에서 가져오면 된다"는 정보를 넣는 작업입니다.

예를 들어 PDB 안에는 이런 정보가 들어갑니다.

```text
GIT_COMMIT=62f457134b54fcdf93d41592418b6062f5e290bf
GIT_REMOTE=https://github.com/shimwoojin/Jungle_Week12_Team7.git
RAW_BASE=https://raw.githubusercontent.com/shimwoojin/Jungle_Week12_Team7/{commit}

C:\development\Jungle_Week12_Team7\KraftonEngine\Source\Engine\Platform\CrashDump.cpp*KraftonEngine/Source/Engine/Platform/CrashDump.cpp
```

시스템 수준 의미는 이렇습니다.

```text
Visual Studio가 덤프 분석 중 소스 파일이 필요함
-> PDB의 srcsrv 스트림을 읽음
-> GitHub raw URL에서 해당 commit의 파일을 다운로드
-> 디버거가 정확한 버전의 소스를 보여줌
```

즉 Source Indexing은 콜스택 복원 자체보다 한 단계 더 나아가, 덤프 분석 PC에 같은 소스 경로가 없어도 정확한 소스를 찾을 수 있게 도와주는 기능입니다.

## 생성되는 SymbolLogs

`UploadSymbols.bat` 실행 후 다음 폴더에 로그가 생성됩니다.

```text
KraftonEngine\Saves\SymbolLogs
```

예:

```text
Release_20260527_2100_KraftonEngine_sources.txt
Release_20260527_2100_KraftonEngine_unindexed.txt
Release_20260527_2100_KraftonEngine_srcsrv.txt
Release_20260527_2100_KraftonEngine.srcsrv
```

각 파일의 의미는 다음과 같습니다.

```text
*_sources.txt
  PDB가 기억하는 전체 소스 경로 목록입니다.

*_unindexed.txt
  Source Server 인덱싱이 안 된 소스 파일 목록입니다.

*_srcsrv.txt
  PDB 안에 실제로 들어간 srcsrv 스트림 내용입니다.

*.srcsrv
  pdbstr.exe -w에 입력으로 사용한 srcsrv 원본 파일입니다.
```

## 정상 결과 예시

`*_srcsrv.txt`에는 다음과 비슷한 내용이 보여야 합니다.

```text
SRCSRV: ini ------------------------------------------------
VERSION=2
INDEXVERSION=2
VERCTRL=Git
GIT_COMMIT=...
GIT_REMOTE=https://github.com/shimwoojin/Jungle_Week12_Team7.git
RAW_BASE=https://raw.githubusercontent.com/shimwoojin/Jungle_Week12_Team7/...
SRCSRV: source files ---------------------------------------
C:\development\Jungle_Week12_Team7\KraftonEngine\main.cpp*KraftonEngine/main.cpp
...
SRCSRV: end ------------------------------------------------
```

이 내용이 보이면 PDB에 Source Server 정보가 들어간 것입니다.

## unindexed.txt에 외부 파일이 남는 경우

`*_unindexed.txt`에 다음처럼 Windows SDK, Visual Studio include, DirectXTK 같은 경로가 남을 수 있습니다.

```text
C:\Program Files (x86)\Windows Kits\10\Include\...
C:\Program Files\Microsoft Visual Studio\2022\...\include\...
C:\__w\1\s\Src\DDSTextureLoader.cpp
```

이것은 보통 정상입니다.

현재 source indexing은 우리 Git repo에 추적되는 프로젝트 파일만 대상으로 합니다. Windows SDK, MSVC STL, 외부 라이브러리 빌드 경로는 우리 GitHub repo에서 가져올 수 없으므로 인덱싱하지 않습니다.

문제로 봐야 하는 경우는 `KraftonEngine/Source/...` 같은 우리 프로젝트 소스가 대량으로 `_unindexed.txt`에 남는 경우입니다. 그 경우 다음을 확인해야 합니다.

```text
PDB가 현재 repo 경로에서 빌드된 것인지
해당 파일이 git ls-files에 포함되는지
GenerateSrcSrvStream.ps1이 실패하지 않았는지
```

## 크래시 발생 시 생성되는 파일

크래시가 발생하면 `CrashDump.cpp`가 다음 파일을 생성합니다.

```text
KraftonEngine\Saves\Dump\Crash_YYYYMMDD_HHMMSS.dmp
KraftonEngine\Saves\Dump\Crash_YYYYMMDD_HHMMSS_BuildInfo.txt
```

`_BuildInfo.txt`에는 다음 정보가 들어갑니다.

```text
Product: KraftonEngine_Team7
Config: Release
BuildVersion: 20260527_2100
GitCommit: 62f45713
SymbolPath: srv*C:\SymbolCache*\\SYMBOL-SERVER\Symbols\Team7
Executable: KraftonEngine.exe
BuildTime: 2026-05-27 21:00:00
DumpType: MiniDumpWithDataSegs
```

이 파일의 목적은 덤프를 분석하는 사람이 "어떤 심볼과 어떤 소스를 써야 하는지" 바로 알 수 있게 하는 것입니다.

## 덤프 분석 방법

덤프를 분석할 때는 먼저 덤프 옆의 `_BuildInfo.txt`를 확인합니다.

확인할 값:

```text
BuildVersion
GitCommit
SymbolPath
Executable
```

Visual Studio에서 `.dmp`를 열고, 심볼 경로에 다음을 추가합니다.

```text
srv*C:\SymbolCache*\\SYMBOL-SERVER\Symbols\Team7
```

그 다음 콜스택을 확인합니다. PDB가 정상적으로 매칭되면 함수명과 라인 정보가 복원됩니다.

Source Server까지 사용하려면 Visual Studio에서 Source Server 지원을 켜야 합니다.

```text
Tools
-> Options
-> Debugging
-> General
-> Enable source server support
```

필요하면 다음 옵션도 같이 확인합니다.

```text
Enable source link support
Require source files to exactly match the original version
```

보안 경고가 뜰 수 있습니다. Source Server는 PDB 안에 들어 있는 명령을 실행해 소스를 가져오는 구조이므로, 신뢰할 수 있는 내부 PDB에 대해서만 사용하는 것이 좋습니다.

## 주의할 점

가장 중요한 순서는 이것입니다.

```text
PDB 생성
-> pdbstr.exe -w로 srcsrv 삽입
-> 검증 로그 생성
-> symstore.exe 업로드
```

이미 심볼 서버에 올린 PDB를 나중에 로컬에서 수정해도, 심볼 서버에 있는 PDB는 바뀌지 않습니다. Source indexing을 추가한 PDB를 사용하려면 수정된 PDB를 다시 업로드해야 합니다.

그래서 같은 버전을 덮어쓰기보다는 새 버전명으로 다시 패키징하는 것을 권장합니다.

```bat
.\PackageRelease.bat 20260527_2100
```

## 문제 해결

`*_srcsrv.txt`에 다음이 보이면 PDB에 srcsrv가 없는 상태입니다.

```text
Could not open stream srcsrv
```

이 경우 확인할 것:

```text
UploadSymbols.bat가 source indexing 단계까지 실행됐는지
Scripts/GenerateSrcSrvStream.ps1이 존재하는지
pdbstr.exe -w가 실패하지 않았는지
```

`*_unindexed.txt`에 다음이 보이면 source indexing 전 상태입니다.

```text
KraftonEngine.pdb is not source indexed.
```

source indexing 이후에도 외부 SDK 파일이 남는 것은 정상입니다. 하지만 프로젝트 소스가 많이 남으면 스크립트 필터링이나 Git 추적 상태를 확인해야 합니다.

`pdbstr.exe`, `srctool.exe`가 없다고 나오면 Windows Debugging Tools 설치를 확인해야 합니다.

```text
C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\srcsrv
```

필요한 도구는 다음 3개입니다.

```text
C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symstore.exe
C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\srcsrv\srctool.exe
C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\srcsrv\pdbstr.exe
```

도구가 없으면 Windows SDK를 설치하거나 수정 설치하면서 다음 항목을 포함해야 합니다.

```text
Debugging Tools for Windows
```

이 도구가 없는 상태에서는 source indexing, PDB 검증, symstore 업로드를 신뢰할 수 없으므로 `UploadSymbols.bat`는 중단됩니다. 설치 후 터미널을 다시 열고 `PackageRelease.bat`를 다시 실행하면 됩니다.

`Symbol store path not accessible`가 나오면 심볼 서버 공유 폴더 접근 권한 또는 네트워크 연결을 확인해야 합니다.

```text
\\SYMBOL-SERVER\Symbols\Team7
```

## 운영 요약

Release를 만들 때:

```bat
.\PackageRelease.bat 20260527_2100
```

릴리즈 후 확인할 것:

```text
ReleaseBuild/BuildInfo.txt
KraftonEngine/Saves/SymbolLogs/*_srcsrv.txt
심볼 서버 업로드 성공 메시지
```

크래시 분석할 때:

```text
1. .dmp 옆의 _BuildInfo.txt 확인
2. BuildVersion과 GitCommit 확인
3. Visual Studio 심볼 경로에 SymbolPath 설정
4. .dmp 열기
5. 콜스택과 소스 위치 확인
```

짧게 말하면, `PackageRelease.bat`는 "배포 파일 생성"뿐 아니라 "나중에 덤프를 분석할 수 있는 근거 자료를 함께 남기는 릴리즈 절차"입니다.
