# Pintos Project #1 : User Program

서강대학교 CSE4070 운영체제 / 2026 Fall
개인 프로젝트 저장소입니다.

---

## 1. 프로젝트 개요

| 항목 | 내용 |
| --- | --- |
| 프로젝트명 | Project #1 : User Program |
| 마감 | 2026. 10. 4. 23:59 (3일 지연 제출 가능, 일 10% 감점) |
| 배점 | 개발 80% + 문서 20% |
| 테스트 | 총 76개 (Prj1-1 : 21개 / Prj1-2 : 55개) |
| 추가 점수 | `fibonacci`, `max_of_four_int` 각 2.5점 (총 5점) |
| 제출물 | `os_prj1_[학번].tar.gz` (`src` 디렉터리 + `[학번].docx`) |

---

## 2. 개발 환경

| 구분 | 위치 | 도구 |
| --- | --- | --- |
| 편집 | 로컬 PC | VS Code + C/C++ (ms-vscode.cpptools) |
| 빌드 / 테스트 | cspro9 / cspro10 | 별도 터미널에서 `ssh` 접속 |
| 동기화 | 로컬 → 서버 (단방향) | `rsync` |

### 2.1 동기화

```bash
# 로컬 → 서버 단방향 동기화 (build/, .git/ 제외)
rsync -avz --delete \
  --exclude 'build/' --exclude '.git/' \
  ~/pintos/src/ <ACCOUNT>@cspro9.sogang.ac.kr:~/pintos/src/
```

### 2.2 서버 초기 설정 (최초 1회)

```bash
# ~/.bashrc 마지막 줄에 추가
export PATH=/sogang/under/<ACCOUNT>/pintos/src/utils:$PATH
source ~/.bashrc
```

### 2.3 VS Code 설정 (`.vscode/settings.json`)

```json
{
  "editor.tabSize": 2,
  "editor.insertSpaces": true,
  "editor.detectIndentation": false,
  "files.eol": "\n",
  "files.trimTrailingWhitespace": true,
  "files.exclude": {
    "**/build": true
  }
}
```

---

## 3. 디렉터리 구조

```
pintos/
├── README.md              # 본 문서
├── .gitignore
├── docs/
│   └── verify/            # 단계별 검증 캡처 (1-1-2.png 형식)
└── src/
    ├── userprog/          # 주 작업 디렉터리
    ├── threads/
    ├── lib/
    ├── examples/
    └── tests/userprog/    # 구현 전 반드시 먼저 확인
```

---

## 4. 빌드 및 테스트

```bash
# 빌드
cd ~/pintos/src/userprog && make

# 전체 테스트
make check

# 점수 확인
make grade

# 인자 전달 확인 (1-1-2)
pintos --filesys-size=2 -p ../examples/echo -a echo -- -f -q run 'echo x'

# 추가 시스템 콜 확인 (1-1-8), 기대 출력 : 55 62
pintos --filesys-size=2 -p ../examples/additional -a additional -- -f -q run 'additional 10 20 62 40'
```

---

## 5. 단계 번호 체계

모든 작업은 `1-x-y` 로 번호를 매기고 한 번에 한 단계씩만 진행합니다.

### 5.1 Prj1-1 : User Program 기본

| 단계 | 작업 내용 | 주요 수정 파일 | 상태 |
| --- | --- | --- | --- |
| 1-1-1 | 환경 구축 및 베이스라인 확인 | - | [ ] |
| 1-1-2 | Argument Passing (스택 구성) | userprog/process.c | [ ] |
| 1-1-3 | User Memory Access (주소 유효성 검사) | userprog/syscall.c, threads/vaddr.h | [ ] |
| 1-1-4 | System Call Handler 골격 | userprog/syscall.c, .h | [ ] |
| 1-1-5 | halt, exit (+ Process Termination Message) | userprog/syscall.c, userprog/process.c | [ ] |
| 1-1-6 | write, read (STDOUT / STDIN 만) | userprog/syscall.c | [ ] |
| 1-1-7 | exec, wait | userprog/process.c, threads/thread.c, .h | [ ] |
| 1-1-8 | fibonacci, max_of_four_int + examples/additional.c | lib/syscall-nr.h, lib/user/syscall.c, .h | [ ] |
| 1-1-9 | Prj1-1 전체 회귀 검증 → **git 커밋 1회차** | - | [ ] |

### 5.2 Prj1-2 : File System 관련

| 단계 | 작업 내용 | 주요 수정 파일 | 상태 |
| --- | --- | --- | --- |
| 1-2-1 | File Descriptor 자료구조 설계 및 thread 구조체 확장 | threads/thread.h | [ ] |
| 1-2-2 | create, remove | userprog/syscall.c | [ ] |
| 1-2-3 | open, close, filesize | userprog/syscall.c | [ ] |
| 1-2-4 | read, write 를 fd 기반으로 확장 | userprog/syscall.c | [ ] |
| 1-2-5 | seek, tell | userprog/syscall.c | [ ] |
| 1-2-6 | 실행 파일 쓰기 금지 (file_deny_write / file_allow_write) | userprog/process.c | [ ] |
| 1-2-7 | 파일 시스템 동기화 (lock 적용) | userprog/syscall.c, threads/synch.h | [ ] |
| 1-2-8 | 전체 76개 테스트 검증 → **git 커밋 2회차** | - | [ ] |

각 단계가 길어지면 `1-1-2-a`(파싱), `1-1-2-b`(스택 push), `1-1-2-c`(hex_dump 검증) 형태로 다시 쪼갭니다.

---

## 6. 검증 규칙

한 단계가 끝나면 아래 3단계를 모두 통과한 뒤에만 다음 단계로 넘어갑니다.

1. 빌드 검증 : `make` 가 경고 없이 통과하는지 확인
2. 동작 검증 : 해당 단계의 지정 테스트를 직접 실행해 결과 확인
3. 회귀 검증 : 이전 단계에서 통과했던 테스트가 여전히 PASS 인지 확인

검증 실패 시에는 `src/userprog/build/results` 를 먼저 확인하고, 원인 분석 → 수정 → 재검증 순서를 지킵니다. 원인이 불명확한 상태에서 여러 군데를 동시에 고치지 않습니다.

검증 성공 시 터미널 출력을 `docs/verify/1-1-2.png` 형태로 저장합니다. 보고서의 "시험 및 평가 내용" 항목에 그대로 사용합니다.

---

## 7. 코드 규칙

### 7.1 신규 코드 주석

코드 블록 위쪽에 아래 형식으로 작성합니다.

```c
/* [1-1-2] Argument Passing : 사용자 스택 구성
   목적 : setup_stack() 이 할당한 스택 페이지 위에 80x86 calling convention 에
          맞추어 인자 문자열, word align, argv 포인터 배열, argv, argc,
          fake return address 순으로 데이터를 쌓는다
   입력 : cmdline - process_execute() 에 전달된 원본 명령행 문자열
          esp     - setup_stack() 직후의 스택 포인터 주소
   출력 : *esp 가 최종 스택 top 을 가리키도록 갱신된다
   참고 : Pintos manual 3.5 (80x86 Calling Convention), 조교 슬라이드 35-41
   주의 : 문자열은 역순으로 push 해야 hex_dump 결과가 예시와 일치한다 */
```

### 7.2 기존 코드 수정

기존 주석은 지우거나 고치지 않고, 아래쪽에 수정 이력 블록을 새로 붙입니다.

```c
/* [1-2-4] 수정 : fd 기반 파일 쓰기 지원 추가
   변경 내용 : fd >= 2 인 경우 Fd_table 에서 struct file * 을 찾아
              file_write() 를 호출하도록 분기를 추가
   변경 이유 : Prj1-2 의 write-normal, write-boundary 테스트 대응
   영향 범위 : 기존 fd == 1 경로의 동작은 변경 없음 */
```

### 7.3 스타일

- 주석 첫머리에 단계 태그 `[1-x-y]` 를 붙입니다. 이후 `grep -rn "\[1-2-" src/` 로 단계별 작업 내역을 한 번에 추출할 수 있습니다.
- 주석 문장 끝에는 마침표를 찍지 않습니다.
- 근거가 된 매뉴얼 절 또는 슬라이드 번호를 반드시 남깁니다.
- 들여쓰기는 공백 2칸을 사용합니다.
- 배열 및 자료구조 변수명은 첫 글자를 대문자로 씁니다. (예 : `Fd_table`, `Child_list`)
- 입력이 필요한 변수는 선언 바로 다음 줄에서 값을 읽습니다.
- 나머지는 Pintos 기존 코드 스타일(GNU 스타일, 함수명 다음 줄 개행)을 따릅니다.
- 확인되지 않은 구조체 필드나 함수 시그니처는 추측하지 않고 실제 소스를 먼저 확인합니다.
- 구현 전에 `src/tests/userprog/` 의 해당 테스트 소스를 먼저 읽습니다.

---

## 8. Git 규칙

커밋은 총 2회만 수행합니다.

1. 1회차 : 1-1-9 (Prj1-1 전체 검증 통과) 직후
2. 2회차 : 1-2-8 (전체 76개 테스트 검증 통과) 직후

### 8.1 커밋 메시지 형식

```
Prj1-1: Implement argument passing and basic system calls

- Argument passing (1-1-2)
- User memory access validation (1-1-3)
- System calls: halt, exit, exec, wait, read, write (1-1-4 ~ 1-1-7)
- Additional system calls: fibonacci, max_of_four_int (1-1-8)
- make check: 21/21 PASS
```

### 8.2 커밋과 별개의 백업

커밋은 2회지만, 각 단계 검증 통과 시마다 스냅샷은 따로 남깁니다.

```bash
cd ~/pintos
tar -czf ~/backup/pintos_1-1-4_$(date +%m%d_%H%M).tar.gz src/
```

---

## 9. 제출 전 체크리스트

- [ ] `make check` 76개 테스트 결과 확인 및 캡처
- [ ] `make grade` 로 최종 점수 확인
- [ ] `fibonacci`, `max_of_four_int` 실행 결과 캡처 (`additional 10 20 62 40` → `55 62`)
- [ ] 보고서 `[학번].docx` 작성 완료
- [ ] `[학번].docx` 를 `pintos` 디렉터리에 복사
- [ ] `pintos` 디렉터리에 `학번` 폴더가 없는지 확인
- [ ] `pintos` 디렉터리에서 `submit.sh` 실행
- [ ] `os_prj1_[학번].tar.gz` 생성 확인
- [ ] `tar -zxf` 로 압축 해제해 내용물 확인 (`src` 디렉터리 + `[학번].docx` 만 포함)
- [ ] e-class 업로드

---

## 10. 참고 자료

- Pintos manual (`pintos_manual.pdf`)
- 조교 슬라이드 : `2026fall_pintos_install.pptx`, `2026fall_pintos_proj1.pptx`
- 작업 지침 : `pintos_prj1_guidelines.md`
